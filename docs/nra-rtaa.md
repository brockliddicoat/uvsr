# Native-Resolution Analytical Reconstructive Temporal Anti-Aliasing

UVSR's temporal anti-aliasing implementation is officially **Native-Resolution
Analytical Reconstructive Temporal Anti-Aliasing** (NRA-RTAA). The UI uses the
short name **RTAA**. It is a deterministic analytical reconstruction path: it
contains no upscaler, dynamic-resolution mode, model, inference runtime, vendor
SDK, or neural component.

## Resolution and pipeline contract

Render, display, output, immediate history, and persistent history all use the
window's native width and height. UVSR renders one sample per native pixel with
a subpixel projection offset, then runs one or two full-resolution compute
stages according to the static performance tier:

1. scene-linear HDR opaque PBR and screen-space indirect lighting;
2. procedural sky;
3. `RTAA_Prepare` for Balanced and Maximum Quality; Performance fuses these
   calculations into Resolve and allocates no prepare transients;
4. `RTAA_Resolve`;
5. optional, profile-gated conservative RTAA sharpening fused into AgX;
6. AgX white balance, exposure, tone scale, grade/LUT, gamut and display
   transfer;
7. display-space dithering and swap-chain blit;
8. UI.

UVSR currently has no depth of field, motion blur, bloom, film grain, or
transparent-particle pass to reorder. The renderer also has no pre-exposure;
RTAA input and history are unexposed scene-linear HDR, and exposure is applied
only downstream in AgX.

## Projection and motion convention

Production jitter defaults to an R2 sequence. Halton bases 2/3, a selected held
sequence phase, and a frozen exact-zero diagnostic are also available. The
selected finite R2 or Halton cycle is mean-centered over its actual configured
period, then uniformly rescaled only when necessary to stay within half a pixel
per axis. Offsets are deterministic, periodic, expressed in pixels, and have a
zero directional mean over the finite cycle; in particular, the production
8- and 16-sample R2 cycles no longer inherit the bias of a truncated infinite
sequence. Donut's `PlanarView` converts the final offsets to projection
translation.

The PBR G-buffer stores motion in pixels from the current surface to its
previous-frame location:

```text
previousPixelCenter = currentPixelCenter + motion.xy
```

Motion is generated with current and previous jitter removed exactly once by
Donut's writer. Resolved history color, moments, and metadata therefore use the
de-jittered motion directly, while raw depth and surface history retain the
projection jitter from the frame that wrote them:

```text
resolvedPreviousCenter = currentPixelCenter + motion.xy
rawPreviousAuxCenter = resolvedPreviousCenter - currentJitter + previousJitter
```

Persistent reprojection follows the same split. It projects the current world
point without an offset, adds the current jitter for the resolved-color grid,
and uses the stored history view's jittered projection for raw depth and surface
validation. Keeping these coordinates distinct applies the jitter delta exactly
once and prevents a resolved color sample from validating against an adjacent
raw surface. Motion Z is previous minus current device depth. Motion A is a UVSR
validity bit, so a valid zero velocity remains distinguishable from cleared
background or a previous position behind the camera. Reprojection and
world/view-depth comparisons use FP32 even though motion storage is FP16.

The default renderer uses reverse-Z and clears depth to zero; RTAA also handles
forward-Z correctly, including its clear value of one. A valid center surface
always owns its own motion; `RTAA_Prepare` does not replace it with a closer
neighbor on a slope.
If that center motion is invalid, the pass reconstructs camera motion from the
center depth and stored views. The 3x3 nearest-surface search is reserved for an
uncovered background/silhouette center. Resolve may borrow that source's motion,
but it retains the output pixel's own depth, geometric normal, and IDs for
validation and history writes. If the neighborhood also has no surface,
Prepare reconstructs rotational background motion from a distant view ray so
procedural sky can accumulate under camera rotation.

## Surface and shading validation

The primary PBR G-buffer writes full 32-bit stable material and instance IDs
alongside distinct geometric and shading normals. RTAA validation uses the
geometric normal, not the normal-mapped shading normal. The compact history
keeps the exact 32-bit material ID, an octahedral UNORM8x2 geometric normal, and
a stable 16-bit instance token in one `RG32_UINT` surface. Material validation
is collision-free; object validation is optional because animated or
re-instanced geometry can make object identity overly strict.

Immediate history confidence combines:

- bounds and velocity validity;
- predicted previous view depth versus stored previous depth, using absolute
  plus relative tolerance and a bounded 1x-to-2x grazing-angle scale derived
  from the geometric normal and view direction;
- current versus previous world-space geometric-normal agreement;
- stable material identity and optional instance identity;
- camera/projection/settings history validity.

Pixels with invalid depth are not discarded before accumulation. Uniform clear
3x3 regions use a dedicated fast path that still reprojects and accumulates the
sky, checks both resolved-color and raw-auxiliary history locations for old
geometry, writes the same production history in debug and non-debug modes, and
skips geometric statistics, thin analysis, and fallback. At depth-changing
silhouettes, bounded coverage confidence can retain color history without
pretending that borrowed foreground depth or identity belongs to the background
pixel. Low-motion coverage transitions require current 8-neighbor support, a
borrowed silhouette source, or a trusted previous thin lock. That lock has a
separate jitter-cycle lifetime from fractional coverage, so a one-of-eight
feature is not squared down or erased during its seven uncovered phases.
Stationary coverage edges then favor the sample-count limit after the confidence
safety gate, so alternating and sparse subpixel coverage converge rather than
following the current jitter phase, including at low frame rates. Motion and
reactivity continuously disable this exception.

Geometric invalidity remains separate from shading change. The explicit target
is reserved for a producer that knows it has transient transport, animated
shading, or composition without reliable depth/motion; the current first-party
depth-writing PBR producer deliberately writes zero. Surviving alpha-tested
coverage and static emissive intensity have trustworthy depth and motion, so
neither is marked reactive merely for being alpha-covered or bright. Automatic
reactivity first clips reprojected history to the broad current 3x3
log-luminance/compressed-chroma range, measures only the unexplained residual,
and requires motion corroboration. This prevents a stationary Nyquist phase
from being misclassified as animation. A uniform stationary lighting change is
still corrected by the tight current neighborhood clip; applications with a
known transient path can author the explicit mask for faster response.
Reactivity raises current-frame contribution without automatically declaring a
disocclusion.

Thin geometry uses local depth/material/alpha/normal discontinuities,
high-contrast structure, opposing edge evidence, and
translucent/refraction/double-sided material features. Fractional coverage is
accumulated in history alpha, while a separately packed lifetime says whether a
sparse feature remains trusted across its jitter cycle. Coverage can modestly
relax confidence only when current hard geometry and previous confidence agree.
Current-neighborhood minimum/maximum bounds remain absolute limits on thin clip
expansion; severe reactivity or failed geometry removes the relaxation.

## Clipping, accumulation, and fallback

Resolve cooperatively loads one shared tile for each 8x8 output group.
Performance and Balanced use a 10x10 tile with a one-pixel halo; Maximum Quality
uses a 12x12 tile with a two-pixel halo. RGB is sanitized and transformed to
compressed YCoCg once per shared-tile sample rather than once for every output
pixel and neighborhood tap. Performance also caches exact Rec.709 luminance
once per tile sample. Every tier keeps the complete 3x3 statistical footprint;
a cardinal-only low-cost clamp was rejected because it cannot see a
diagonal-only silhouette and can collapse temporal history back to the rotating
current sample. The same tile supplies clipping, profile-selected thin
classification, and spatial fallback.
History is reconstructed by the statically selected bilinear path or, in
Maximum Quality, the configured optimized Catmull-Rom path. It is sanitized
against non-finite and negative radiance, then line-clipped toward the current
neighborhood center using variance, minimum, maximum, and bounded thin-geometry
expansion.

Complete rejection skips the history-color fetch, neighborhood reduction, and
clipping when resurrection/debug output cannot consume them. Performance
production also recognizes an exactly matching recurrent RGB sample: because
that sample is the current center and therefore inside every valid box, it can
skip the statistical reduction and color transform without relaxing rejection.
Low-confidence pixels still retain their surface-aware spatial fallback.

CPU code evaluates the three response exponentials once per frame:

```text
currentWeight = 1 - exp(-deltaTime / responseTime)
```

Resolve interpolates stable and moving response from velocity, then toward the
reactive response. It enforces the per-pixel sample-count limit, increases
current contribution as history confidence falls, and uses current-only output
for clear disocclusion. Medium and Light cap history at 12/5 and 6/2
stable/moving samples respectively; Heavy caps it at 24/10.

When confidence is low, the current-frame fallback remains surface aware.
Performance uses a reduced cross gated by depth, geometric normal, and identity;
Balanced and Maximum Quality additionally weight spatial distance, luminance,
and thin-coverage agreement. Fully confident temporal pixels do not receive
this blur.

Maximum Quality can conditionally inspect configured older ring slots only after
immediate history fails, clips strongly, or has very low confidence. Each older
sample uses separate resolved-color and raw-auxiliary projections from the view
stored for its own slot, is validated independently, clipped more aggressively,
and capped by the resurrection weight. The Heavy temporal preset enables two
older histories by default; Medium and Light configure none. Performance and
Balanced compile resurrection out and allocate only the two immediate slots,
even when Heavy temporal response is selected.

Sharpening uses a luminance unsharp term in the AgX input pass for Balanced and
Maximum Quality. Performance statically avoids the extra neighborhood work.
Local-range and halo clamps prevent negative radiance and overshoot. History
confidence, motion, reactivity and log-luminance variance suppress the
contribution. The sharpened result is display-only and never becomes temporal
history.

## Resources and packing

All values below are bytes per native pixel.

| Resource | Format | Bytes | Packing |
|---|---:|---:|---|
| prepared reprojection, split tiers | `RG16_FLOAT` | 4 | de-jittered current-to-previous motion XY |
| prepared classification, split tiers | `RGBA8_UNORM` | 4 | 3x3 source index, velocity confidence, explicit application reactive, authored thin candidate |
| history color, per slot | `RGBA16_FLOAT` | 8 | scene-linear HDR RGB, thin coverage A |
| history moments, Balanced/Maximum per slot | `RG16_FLOAT` | 4 | log-luminance first and second moments for downstream sharpening suppression |
| history metadata, per slot | `RGBA8_UNORM` | 4 | sample count, confidence, R8 thin-lock lifetime, packed reactive4/motion4 |
| history depth, per slot | `R32_FLOAT` | 4 | device depth |
| history surface, per slot | `RG32_UINT` | 8 | exact material32; oct-normal16 plus optional instance-token16 |

Performance strips moments because it also compiles sharpening out, so each of
its two immediate slots is 24 B/pixel (48 B/pixel total). Balanced and Maximum
Quality retain 28 B/pixel slots and two immediate slots (56 B/pixel). Maximum
Quality reserves `persistent count * frame interval` older ring positions when
resurrection is configured, so its supported combinations total three, four,
or six slots (84, 112, or 168 B/pixel); Heavy Temporal plus Maximum Quality
defaults to four total slots. Balanced and Maximum allocate the packed 8
B/pixel prepare pair.
Performance fuses Prepare and allocates neither transient. A full-resolution
`RGBA16_FLOAT` debug output exists only while a debug view is active; production
final output uses the current history-color slot directly and does not bind a
dummy debug UAV.

At 1920x1080, Performance history is approximately 94.9 MiB. Two, three, four,
or six moment-bearing slots are approximately 110.7, 166.1, 221.5, or 332.2
MiB. At 3840x2160 the corresponding Performance figure is 379.7 MiB, while the
moment-bearing totals are approximately 443.0, 664.5, 885.9, or 1,328.9 MiB.
Split-tier prepare transients are approximately 15.8 MiB at 1080p and 63.3 MiB
at 4K. These figures exclude allocator alignment, the existing G-buffer, and
the optional debug texture.

On a valid center surface, split-tier `RTAA_Prepare` reads depth, motion,
diffuse, and specular data for a 20 B/pixel baseline, plus 1 B/pixel when the
explicit mask is consumed, and writes the packed 8 B/pixel pair above. Only
uncovered pixels conditionally inspect the other eight depths for dilation.
Performance performs the same selection inside Resolve, reuses shared depth and
center specular, and removes the 8 B/pixel transient write/read round trip,
dispatch, and inter-pass dependency. Its conservative steady-state footprint is
approximately 104 B/pixel of logical reads without an explicit mask (105 with
one) and 24 B/pixel of writes. The default Medium Temporal plus Balanced profile
reports approximately 133 B/pixel of logical reads and 36 B/pixel of writes.
Maximum Quality with Catmull-Rom reports approximately 248 B/pixel of logical
reads and the same 36 B/pixel writes. These estimates are before cache reuse;
they exclude conditional dilation and persistent-history reads, and rejected
disocclusions skip filtered color history and neighborhood clipping. An active
full-resolution debug view adds 8 B/pixel of writes.

The UI reports coherent latest, 120-sample median, and P95 RTAA measurements.
Prepare plus Resolve samples carry matching history-validity, resolution,
profile, debug, and history-reset epochs; stale timestamp results are discarded.
When RTAA sharpening runs, the UI also shows a separate Display+sharpen
interval. It is not included in total RTAA and contains AgX tone mapping,
grading, LUT and dither work rather than isolated sharpening cost. Performance
does not run sharpening and therefore does not show a misleading zero-valued
AgX row.
In Maximum Quality, resurrection is a conditional branch inside resolve, so
its overhead is included in resolve rather than represented by a misleading
extra dispatch.

## Temporal presets and performance profiles

Temporal presets select analytical response and reconstruction parameters. The
independent **Performance Profile** dropdown selects statically compiled GPU
cost. Choosing Heavy, Medium, or Light does not change the performance profile,
so stability/clarity can be tuned separately from throughput. Factory defaults
are Medium Temporal and Balanced.

| Setting | Heavy Temporal | Medium Temporal | Light Temporal |
|---|---:|---:|---:|
| jitter | R2 / 16 | R2 / 8 | R2 / 8 |
| configured Maximum Quality history filter | Catmull-Rom | Catmull-Rom | Catmull-Rom |
| stable response | 250 ms | 125 ms | 55 ms |
| moving response | 100 ms | 50 ms | 22 ms |
| reactive response | 12 ms | 8 ms | 4 ms |
| maximum history | 24 | 12 | 6 |
| maximum moving history | 10 | 5 | 2 |
| variance sigma | 1.60 | 1.25 | 0.95 |
| automatic reactive strength | 0.60 | 0.80 | 1.00 |
| thin maximum relaxation | 0.040 | 0.025 | 0.012 |
| spatial radius | 2 | 1 | 1 |
| configured older histories | 2 | 0 | 0 |
| sharpen strength | 0.08 | 0.18 | 0.30 |

Light is clarity-biased without a fixed global blend. At 60 FPS its 22 ms
moving response produces about 0.531 current-frame weight, while stable pixels
use about 0.261 and strongly reactive pixels about 0.984.

The resolve tiers are separate static shader variants:

| Behavior | Performance | Balanced | Maximum Quality |
|---|---|---|---|
| history reconstruction | bilinear | bilinear | configured bilinear or Catmull-Rom |
| shared tile per 8x8 group | 10x10 | 10x10 | 12x12 |
| neighborhood statistics | full 3x3 | full 3x3 | full 3x3 |
| shared memory per group | 4,400 B | 4,400 B | 6,912 B |
| thin evidence | authored plus cheap silhouette coverage | authored plus center structural evidence | authored plus structural evidence and optional gated 3x3 diffusion |
| spatial fallback | low-cost 5-tap cross | fixed 3x3 bilateral | configured direction-aware 3x3 or 5x5 |
| prepare scheduling | fused into Resolve | separate pass | separate pass |
| history moments | compiled out | retained | retained |
| native FP16 color/statistics | capability-gated A/B with FP32 fallback | FP32 | FP32 |
| persistent resurrection | compiled out | compiled out | optional and conditionally sampled |
| fused sharpening neighborhood | disabled | enabled when configured | enabled when configured |

The shared-memory figures are useful footprint accounting, not a claimed fixed
occupancy threshold. The older Xe-LP guidance's approximate 4,672 B step does
not transfer directly to Meteor Lake Xe-LPG, whose occupancy depends on the
compiler-selected SIMD width and GRF mode. Intel GPU Analyzer/Profiler evidence
is required before trading additional unpack/conversion ALU for a smaller tile.

Static specialization removes disabled high-cost paths instead of selecting
them through per-pixel dynamic branches. Production permutations compile the
32-way diagnostic switch and debug UAV completely out. The package contains 14
resolve blobs: production/debug FP32/FP16 Performance, production/debug
Balanced, and production/debug for the four Maximum Quality combinations of
bilinear/Catmull-Rom with resurrection off/on. Runtime creates the native FP16
Performance PSOs only when D3D12 reports native 16-bit shader operations;
geometry, depth, motion, matrices, shared storage, and reprojection stay FP32.
Changing the profile or FP16 mode resets history so the new interpretation
begins from current data.

## History reset and diagnostics

History resets on first use, re-enable, resize/resource reallocation, scene or
camera-mode change, shader recreation, force reset, missing previous view,
camera teleport/large rotation, non-jittered projection change, performance-
profile change, native-FP16 mode change, procedural-sky or scene-light edit, or
another change to the interpretation-setting signature.
Exposure edits do not reset history because exposure is downstream and history
remains in the same scene-linear convention.

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
- Maximum Quality persistent resurrection reprojects the current world point
  through each slot's stored camera view. It is therefore intended for
  camera/static-surface occlusion recovery. Before any older lookup, immediate
  motion must agree with camera-only reprojection within 0.125 pixel and with
  projected depth confidence above 0.5. Independently moving geometry therefore
  cannot resurrect stale world-static color merely because an older material
  and depth happen to match; no per-object motion chain is stored for old slots.
- Sharpening is fused with AgX. Its timer is therefore the fused display-pass
  interval, and resurrection overhead is part of Resolve; isolating either
  without a controlled A/B capture would require the extra passes this design
  intentionally avoids.
- Latest-published-main Intel validation at 1920x1080 covers Medium
  Temporal/Balanced in distant and close geometry and Performance in close
  geometry. The close views were moved interactively, so they are smoke and
  workload-range measurements rather than a controlled static-camera FP16/FP32
  comparison. A dedicated synthetic validation scene, Heavy / Medium / Light /
  no-AA capture matrix, controlled native-FP16 A/B, all-tier playback at
  30/60/120 FPS, and offline supersampled references have not yet been produced.
- Pure clear-depth background uses a deliberately cheap temporal path. User
  edits to the procedural sky and scene lights reset history, but a future
  continuously animated sky must provide explicit reactivity or an equivalent
  invalidation signal.
- Automated reference tests cover temporal presets; performance-profile ABI,
  defaults, names, independence, and sanitization; finite-cycle jitter bounds
  and strict 8/16-sample R2 centering; resolved-color versus raw-auxiliary
  coordinate algebra for immediate and persistent history; motion ownership;
  reverse- and forward-Z clear sentinels; depth/normal/material validation and
  bounded grazing tolerance; clipping; frame-rate response and sample caps;
  asymmetric alternating coverage plus one-of-eight/two-of-eight sparse-gap
  convergence; exact/fractional weighted lock reprojection; 2..64-period lock
  expiry and hard-disocclusion cleanup; reactive/motion metadata packing;
  stationary shading clipping; fallback/thin gates; camera-only resurrection
  gating; and odd-resolution dispatch coverage. The shader target separately
  compiles all 14 production/debug, tier, filtering, resurrection, and
  capability permutations used by the runtime. Image-quality judgments and GPU
  timings remain adapter-, scene-, resolution-, and driver-specific.
