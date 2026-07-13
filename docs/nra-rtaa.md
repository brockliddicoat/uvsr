# Native-Resolution Analytical Reconstructive Temporal Anti-Aliasing

UVSR's temporal anti-aliasing implementation is officially **Native-Resolution
Analytical Reconstructive Temporal Anti-Aliasing** (NRA-RTAA). The UI uses the
short name **RTAA**. It is a deterministic analytical reconstruction path: it
contains no upscaler, dynamic-resolution mode, model, inference runtime, vendor
SDK, or neural component.

## Resolution and pipeline contract

Render, display, output, immediate history, and persistent history all use the
window's native width and height. UVSR renders one sample per native pixel with
a subpixel projection offset, then runs two full-resolution compute stages:

1. scene-linear HDR opaque PBR and screen-space indirect lighting;
2. procedural sky;
3. `RTAA_Prepare`;
4. `RTAA_Resolve`;
5. conservative RTAA sharpening fused into AgX;
6. AgX white balance, exposure, tone scale, grade/LUT, gamut and display
   transfer;
7. display-space dithering and swap-chain blit;
8. UI.

UVSR currently has no depth of field, motion blur, bloom, film grain, or
transparent-particle pass to reorder. The renderer also has no pre-exposure;
RTAA input and history are unexposed scene-linear HDR, and exposure is applied
only downstream in AgX.

## Projection and motion convention

Production jitter defaults to a centered R2 sequence. Halton bases 2/3, a
selected held sequence phase, and a frozen exact-zero diagnostic are also
available. Offsets are deterministic, periodic, expressed in pixels, and
clamped to half a pixel per axis before Donut's `PlanarView` converts them to
projection translation.

The PBR G-buffer stores motion in pixels from the current surface to its
previous-frame location:

```text
previousPixelCenter = currentPixelCenter + motion.xy
```

Motion is generated with current and previous jitter removed exactly once by
Donut's writer. NRA-RTAA therefore does not add another jitter delta during
history lookup. Motion Z is previous minus current device depth. Motion A is a
UVSR validity bit, so a valid zero velocity remains distinguishable from
cleared background or a previous position behind the camera. Reprojection and
world/view-depth comparisons use FP32 even though motion storage is FP16.

Depth is reverse-Z and clears to zero. `RTAA_Prepare` searches a 3x3 depth
neighborhood and selects the nearest valid surface rather than the largest
velocity. When a surface has invalid object motion, the pass reconstructs its
world position from depth and derives camera motion from the stored views.

## Surface and shading validation

The primary PBR G-buffer writes full 32-bit stable material and instance IDs
alongside shading data. The compact history keeps the exact 32-bit material ID,
an octahedral UNORM8x2 normal, and a stable 16-bit instance token in one
`RG32_UINT` surface. Material validation is collision-free; object validation
is optional because animated or re-instanced geometry can make object identity
overly strict.

Immediate history confidence combines:

- bounds and velocity validity;
- predicted previous view depth versus stored previous depth, using absolute
  plus relative tolerance and grazing-angle compensation;
- current versus previous world-space normal agreement;
- stable material identity and optional instance identity;
- camera/projection/settings history validity.

Geometric invalidity remains separate from shading change. The explicit PBR
reactive target marks alpha coverage, emissive shading and reactive material
features. Resolve combines it with a bounded, exposure-normalized log-luminance
and chroma difference. Reactivity raises current-frame contribution without
automatically declaring a disocclusion.

Thin geometry uses local depth/material/alpha/normal discontinuities,
high-contrast structure, opposing edge evidence, and double-sided/reactive
material features. A compact temporal coverage estimate is carried in history
color alpha. Only validated, locally classified thin geometry receives bounded
clip expansion; severe reactivity or failed geometry removes the relaxation.

## Clipping, accumulation, and fallback

Resolve cooperatively loads one 12x12 shared tile for each 8x8 output group.
The same tile supplies the 3x3 YCoCg neighborhood, thin-geometry diffusion, and
the radius-one or radius-two spatial fallback. History is reconstructed with a
static bilinear or optimized Catmull-Rom variant, sanitized against non-finite
and negative radiance, then line-clipped toward the current neighborhood center
using variance, minimum, maximum, and bounded thin-geometry expansion.

CPU code evaluates the three response exponentials once per frame:

```text
currentWeight = 1 - exp(-deltaTime / responseTime)
```

Resolve interpolates stable and moving response from velocity, then toward the
reactive response. It enforces the per-pixel sample-count limit, increases
current contribution as history confidence falls, and uses current-only output
for clear disocclusion. Medium and Light cap history at 12/5 and 6/2
stable/moving samples respectively; Heavy caps it at 24/10.

When confidence is low, an edge-aware current-frame fallback weights shared
neighbors by depth, normal, material, spatial distance, luminance and thin-
coverage agreement. Valid temporal pixels do not receive this blur.

Heavy can conditionally inspect two older ring slots only after immediate
history fails, clips strongly, or has very low confidence. Each older sample is
projected with the view stored for its own slot, validated independently,
clipped more aggressively, and capped by the resurrection weight. Medium and
Light use a two-slot immediate ring and allocate no persistent slots.

Sharpening uses a luminance unsharp term in the AgX input pass. Local-range and
halo clamps prevent negative radiance and overshoot. History confidence,
motion, reactivity and log-luminance variance suppress the contribution. The
sharpened result is display-only and never becomes temporal history.

## Resources and packing

All values below are bytes per native pixel.

| Resource | Format | Bytes | Packing |
|---|---:|---:|---|
| prepared reprojection | `RGBA16_FLOAT` | 8 | dilated motion XY, owner depth, velocity confidence |
| prepared classification | `RGBA8_UNORM` | 4 | source offset RG, explicit reactive, thin candidate |
| history color, per slot | `RGBA16_FLOAT` | 8 | scene-linear HDR RGB, thin coverage A |
| history moments, per slot | `RG16_FLOAT` | 4 | log-luminance first and second moments |
| history metadata, per slot | `RGBA8_UNORM` | 4 | sample count, confidence, reactive, motion factor |
| history depth, per slot | `R32_FLOAT` | 4 | device depth |
| history surface, per slot | `RG32_UINT` | 8 | exact material32; oct-normal16 plus optional instance-token16 |

Each history slot is 28 B/pixel. Medium and Light allocate two slots (56
B/pixel). Default Heavy allocates four slots (112 B/pixel: two immediate ring
positions plus two older positions). Prepare transients add 12 B/pixel. A
full-resolution `RGBA16_FLOAT` debug output exists only while a debug view is
active; production final output uses the current history-color slot directly.

At 1920x1080, the history allocation is approximately 110.7 MiB for Medium or
Light and 221.5 MiB for Heavy. At 3840x2160 it is approximately 443.0 MiB and
885.9 MiB respectively. Prepare transients are approximately 23.7 MiB at 1080p
and 94.9 MiB at 4K. These figures exclude allocator alignment, the existing
G-buffer, and the optional debug texture.

`RTAA_Prepare` reads a depth/motion neighborhood plus center-pixel material
classification and writes 12 B/pixel. Structural thin-geometry evidence,
clipping, diffusion, and spatial fallback all reuse Resolve's one cooperatively
loaded current-frame tile. `RTAA_Resolve` has content-dependent traffic: its
shared-tile current-frame reads and immediate-history reads are
mandatory; older-history reads occur only on failed/strongly clipped pixels in
Heavy. Catmull-Rom costs more history reads than bilinear. The debug UI reports
an approximately 329 B/pixel logical read footprint and 40 B/pixel write
footprint for the default Catmull-Rom production path. These deliberately
conservative figures are before cache reuse and exclude the conditional older-
history reads; an active full-resolution debug view adds 8 B/pixel of writes.
The debug UI also reports
measured prepare, resolve, fused AgX-plus-sharpen display, and total GPU
intervals. Because sharpening is intentionally fused into AgX, the display
interval is an upper bound that also contains tone mapping, grading, LUT and
dither work; it is not presented as an isolated sharpening cost.
Resurrection is a conditional branch inside resolve, so its overhead is
included in resolve rather than represented by a misleading extra dispatch.

## Presets

| Setting | Heavy Temporal | Medium Temporal | Light Temporal |
|---|---:|---:|---:|
| jitter | R2 / 16 | R2 / 8 | R2 / 8 |
| history filter | Catmull-Rom | Catmull-Rom | Catmull-Rom |
| stable response | 250 ms | 125 ms | 55 ms |
| moving response | 100 ms | 50 ms | 22 ms |
| reactive response | 12 ms | 8 ms | 4 ms |
| maximum history | 24 | 12 | 6 |
| maximum moving history | 10 | 5 | 2 |
| variance sigma | 1.60 | 1.25 | 0.95 |
| automatic reactive strength | 0.60 | 0.80 | 1.00 |
| thin maximum relaxation | 0.040 | 0.025 | 0.012 |
| spatial radius | 2 | 1 | 1 |
| older histories | 2 | 0 | 0 |
| sharpen strength | 0.08 | 0.18 | 0.30 |

Light is clarity-biased without a fixed global blend. At 60 FPS its 22 ms
moving response produces about 0.531 current-frame weight, while stable pixels
use about 0.261 and strongly reactive pixels about 0.984.

## History reset and diagnostics

History resets on first use, re-enable, resize/resource reallocation, scene or
camera-mode change, shader recreation, force reset, missing previous view,
camera teleport/large rotation, non-jittered projection change, or a change to
an interpretation-setting signature. Exposure edits do not reset history
because exposure is downstream and history remains in the same scene-linear
convention.

The **Aliasing** drawer exposes all projection, reprojection, validation,
reactive, thin-geometry, clipping, accumulation, fallback, resurrection,
sharpening and debug settings. Manual quality edits switch the preset label to
Custom. Debug views cover every input and decision requested by the NRA-RTAA
specification, including distinct rejection-reason colors. Debug surfaces do
not replace or contaminate temporal history.

## Current limitations

- NRA-RTAA is active for deferred UVSR PBR, where depth, normals, identity and
  object motion are all trustworthy. Forward and Donut-legacy comparison paths
  bypass it rather than silently accepting incomplete validation data.
- UVSR currently renders opaque and alpha-tested geometry only. The explicit
  reactive input is ready for a future transparent/particle composite, but no
  such renderer pass exists today.
- Material history is exact. Optional object validation uses a stable 16-bit
  token to retain a 64-bit packed surface; exceptionally large scenes can have
  an object-token collision, while depth, normal, and exact material validation
  still apply.
- Persistent resurrection reprojects the current world point through each
  slot's stored camera view. It is therefore intended for camera/static-surface
  occlusion recovery; independently moving geometry normally fails the older
  depth test instead of being resurrected because no per-object motion chain is
  stored for older slots.
- Sharpening is fused with AgX. Its timer is therefore the fused display-pass
  interval, and resurrection overhead is part of Resolve; isolating either
  without a controlled A/B capture would require the extra passes this design
  intentionally avoids.
- Runtime validation currently consists of one 1920x1080 Bistro Exterior
  Medium-preset smoke/profile. A dedicated synthetic validation scene, Heavy /
  Medium / Light / no-AA capture matrix, 30/60/120 FPS visual playback, and
  offline supersampled references have not yet been produced.
- Automated reference tests cover presets, jitter, response-time math and
  sanitization; the shader target separately compiles every static RTAA
  permutation. Image-quality judgments and GPU timings remain adapter-, scene-,
  resolution-, and driver-specific.
