# UVSR screen-space visibility and indirect lighting

Primary references:

- Therrien, Levesque, and Gilet, [Screen Space Indirect Lighting with
  Visibility Bitmask](https://arxiv.org/pdf/2301.11376v2)
- Intel GameTechDev, [XeGTAO](https://github.com/GameTechDev/XeGTAO)
- AMD GPUOpen, [FidelityFX Single Pass
  Downsampler](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/single-pass-downsampler/)

## Current implementation

UVSR's deferred PBR path owns one HLSL visibility traversal that produces both
ambient visibility and single-bounce diffuse irradiance. It is a first-party
DirectX 12/NVRHI compute pipeline; Donut's legacy screen-space occlusion pass
is not instantiated or packaged by UVSR.

The frame order is:

1. opaque PBR G-buffer, including motion vectors;
2. deferred direct lighting and a separate configured GI-radiance source;
3. optional single-dispatch hierarchical view-depth generation;
4. full-resolution screen-space visibility-bitmask sampling;
5. temporal AO/GI output accumulation;
6. depth/normal-aware spatial filtering;
7. indirect-light composition;
8. procedural sky;
9. exposure, AgX, grading, and output conversion.

The source-radiance UAV is written beside base lighting in the same deferred
dispatch. It contains scene-linear, material-weighted outgoing direct diffuse
radiance plus the enabled/gained emissive source term. Alpha flags a two-sided
emissive card, allowing the stochastic traversal to fetch one HDR texel rather
than separate direct, emissive, and feature targets. The source excludes
specular, fallback ambient,
screen-space GI, AO, transparency, UI, tone mapping, and post-processing. The
visibility pass therefore never reads the target that its composite modifies
and cannot feed current-frame GI back into itself. UVSR has no pre-exposure;
source, histories, GI, base lighting, and the composite all remain unexposed
scene-linear values.

### Radial mask contract

`src/radial_visibility_mask.hlsli` has no texture dependency. Its reusable
contract is:

`VisibilityInterval -> candidate bits -> newly covered bits -> accumulated mask`

Each active slice owns one register-local `uint` with 32 uniform sectors. A
zero bit is unoccluded; a one bit is occluded or already claimed by a nearer
sample. Samples execute near-to-far on both sides of a slice. AO consumes the
final union, while GI reads source normal/radiance only when
`candidate & ~occludedBits` is nonzero. Round-to-nearest sector-span
quantization is production default; Ceil and Floor are developer validation
modes. Empty, complete, reversed, clamped, non-finite, and boundary intervals
avoid shift-by-32 and other undefined operations.

Masks are current-frame and register-local. No persistent mask texture is
allocated. Production uses one randomly rotated slice per pixel and frame, as
in the paper benchmarks. A six-phase azimuth sequence and four-phase radial
sequence form a 24-frame low-discrepancy schedule over stable per-pixel spatial
noise. Both sides jointly form the slice; only the final per-slice estimator is
averaged.

### Coordinates, depth, and thickness

- Depth is the renderer's reversed-Z D3D device depth; zero is background.
- Positions are reconstructed analytically from device or positive linear view
  depth and `matViewToClip`; per-sample 4x4 inverse-projection multiplies are
  deliberately avoided.
- G-buffer shading normals are world-space `RGBA16_SNORM` values and are
  transformed to view space for traversal.
- Perspective `V` points from the receiver toward the camera. Orthographic
  views use the camera direction stored by `PlanarViewConstants`.
- Slice azimuths rotate around `V`, not around the surface normal.
- Projection through `matViewToClip` converts the world radius to pixels, so
  perspective and orthographic projection share one path.
- Finite thickness offsets the reconstructed front delta by `-V * thickness`.
  Optional distance scaling uses the sampled occluder's view depth, not the
  receiver depth; no distance-falloff heuristic is hidden in mask accumulation.

Sampling is always full resolution. Quality is controlled by stochastic sample
count rather than render resolution. In the paper's terminology, Sample Count
is the number of depth fetches on one horizon side, so a one-slice value of 16
performs at most 32 depth taps per shaded pixel.

### Lighting semantics

Raw AO is named ambient visibility and equals the average over slices of:

`1 - popcount(mask) / 32`

Strength and power are applied only after mask evaluation/filtering. Ambient
visibility modulates only UVSR's sky-gradient fallback indirect diffuse. It
does not alter direct light, emissive radiance, the BSDF, specular, or the new
screen-space GI.

The GI output convention is diffuse irradiance. A sample contributes source
radiance times receiver cosine, source-facing cosine, and newly covered angular
fraction. The Algorithm 1 accumulator is multiplied by PI before storage so the
receiver's `baseColor * (1 - metalness) / PI` reproduces the paper-scale result
without an accidental extra 1/PI attenuation. There is no distance attenuation
or division by radial samples/sides. Direct and ordinary emissive sources remain
one-sided; a source texel containing a double-sided emissive card uses the
absolute area cosine. Screen-space AO is not multiplied into GI. Metals receive
no ordinary diffuse GI.

### Resources

| Resource | Format | Resolution/lifetime |
|---|---|---|
| base lighting | `RGBA16_FLOAT` | full, frame |
| direct diffuse source | `RGBA16_FLOAT` | full, frame |
| hierarchical view depth | `R16_FLOAT`, 5 mips | full to 1/16, frame |
| motion + depth delta | `RGBA16_FLOAT` | full, only while temporal filtering is active |
| raw ambient visibility | `R16_FLOAT` | full, frame |
| raw indirect diffuse irradiance | `RGBA16_FLOAT` | full, frame |
| raw traversal debug | `RGBA8_UNORM` | full, frame |
| AO history pair | `R16_FLOAT` | full, persistent output history |
| GI history pair | `RGBA16_FLOAT` | full, persistent output history |
| history depth pair | `R32_FLOAT` | full, persistent validation |
| history normal pair | `RGBA8_UNORM` | full, persistent validation |
| history validity | `R8_UNORM` | full, frame |
| denoised AO/GI/debug | `R16_FLOAT` / `RGBA16_FLOAT` / `RGBA8_UNORM` | full, frame |
| final HDR composite | `RGBA16_FLOAT` | full, frame |

Temporal accumulation reprojects AO/GI results, not radial masks. Motion XY is
previous-minus-current pixels and Z is the matching device-depth delta. It uses
that motion, UV bounds, point-validated depth/normal, projection,
resolution, settings-signature, and camera-cut validation. GI is clamped to a
current center-plus-cardinal-neighbors component range and uses a stronger
default response than AO.
History is discarded on disocclusion, sharp normal changes, projection/size or
relevant-setting changes, large camera jumps, explicit reset, and pass
recreation.

The optional five-level depth hierarchy follows Intel XeGTAO's AO-specific design rather
than a generic nearest-depth HZB. An 8x8 group covers a 16x16 tile, writes
full-resolution positive view depth, and reduces four additional mips through
group-shared memory in one dispatch. A far-depth-anchored smart average
preserves slopes while avoiding foreground dilation. Distant AO-only traversal
samples select LOD from projected pixel offset with a 3.3 bias and use point
loads. Combined GI uses exact depth because coarse depth cannot safely identify
a full-resolution normal/radiance source; this prevents cross-surface color
bleeding until aligned attribute pyramids or representative-pixel provenance
are added. Orthographic traversal also stays on exact depth.

### Defaults and presets

The default Medium preset is full resolution with one stochastic slice, 16
total samples per pixel divided between both horizon directions, world radius
3, constant thickness 0.2, distribution
exponent 2, full jitter, exact depth by default, temporal responses 0.90 AO /
0.94 GI, bilateral radius 1, depth rejection 0.02, and normal dot threshold
0.85. AO and GI start enabled; AO strength defaults to 1.0 so the visibility
estimate remains clearly visible, while GI intensity and emissive gain default
to 4.

Low/Medium/High/Ultra use 8/16/24/32 total samples per pixel at full resolution.
Editing sampling or filter quality selects Custom without overwriting unrelated
AO/GI controls.
Hierarchical view depth is opt-in: at the default 3 m radius its construction
cost exceeded its saved depth traffic in dense 1080p profiling. It remains
available for AO-only workloads with longer projected rays.
Settings intentionally do not persist between launches. Every process starts
from factory defaults, and **Reset All** restores the same renderer, visibility,
tonemapper, LUT, sky, and white-world settings in-session.

GPU timestamp queries report hierarchy, sampling, temporal, spatial, composite,
and active complete-effect time without blocking the render thread. The UI
keeps this to a compact `VBIL x.xx ms` line. Markers
label AO-only, GI-only, and combined traversals separately.

## Screen-space limitations

- Off-screen emitters are unavailable without a world-space fallback.
- Surfaces missing behind the single-layer depth buffer cannot contribute.
- Constant thickness cannot match every object.
- Sparse steps can miss very small bright sources.
- Large radii increase bandwidth and cache pressure.
- Temporal accumulation can ghost despite rejection and clamping.
- Visibility is incomplete at screen edges; out-of-bounds samples fail open.
- No checked-in image-regression scene suite exists; the local Bistro scenes
  are the current GPU smoke-test content.

## Upgrade path to unified visibility

The following is design intent, not behavior claimed by the current passes.

### Persistent radial masks

Store one 32-bit integer per pixel and active slice in an integer texture or
texture array. Store canonical slice azimuth/phase plus validity, confidence,
and age in companion channels. Canonical orientation must be independent of a
frame's random rotation. Extend the binary representation only if known-visible
must be distinguished from unresolved; today a zero bit means either.

### Temporal mask reprojection

Reproject masks with motion vectors, rotate sectors from the previous slice
basis into the current canonical basis, then validate depth, normal, material,
projection, and disocclusion. Reset invalid masks. Decay confidence and merge
conservatively so stale occlusion cannot grow. This will be a distinct mask
history pass, not the current AO/GI output accumulator.

### Spatial mask reuse

Gather neighboring persistent masks only after depth, normal, material, and
surface-identity validation. Reorient each neighbor between surface/slice
bases before merging. Reject across silhouettes and thin depth layers to avoid
visibility dilation. Confidence must reflect angular resampling loss.

### World-space fallback

Query rays, probes, surfels, or a radiance cache only for unresolved/unclaimed
sectors and submit their angular intervals through the same accumulator. A
future tri-state representation should distinguish known visible, known
occluded, and unresolved sectors before fallback results are fused.

### Cyclic reinjection

Preserve off-screen fallback visibility/radiance in explicit temporal storage
with confidence, age, exposure convention, and energy clamps. Reinjection must
be temporally bounded and disabled by default until furnace/energy tests prove
that radiance cannot feed itself without limit.

### Future consumers and exact next steps

1. Allocate/capture canonical integer slice masks plus phase/confidence.
2. Add validated temporal reprojection and sector-basis rotation.
3. Add validated spatial reorientation/reuse without silhouette dilation.
4. Add unresolved-sector world-space visibility/radiance queries.
5. Add confidence-decayed cyclic reinjection with explicit energy bounds.
6. Consume sector directions for directional ambient sampling.
7. Derive conservative rough-specular visibility from directional masks,
   validated separately from diffuse AO.
8. Expose the same contract to path guiding, radiance-cache visibility, and
   screen/world transport handoff.
