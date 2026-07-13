# UVSR screen-space visibility and indirect lighting

Primary references:

- Therrien, Levesque, and Gilet, [Screen Space Indirect Lighting with
  Visibility Bitmask](https://arxiv.org/pdf/2301.11376v2)
- Intel GameTechDev, [XeGTAO](https://github.com/GameTechDev/XeGTAO)
- AMD GPUOpen, [FidelityFX Single Pass
  Downsampler](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/single-pass-downsampler/)

## Current implementation

UVSR's deferred PBR path owns one HLSL visibility traversal that produces both
ambient visibility and configurable one-to-four-bounce diffuse irradiance. It
is a first-party DirectX 12/NVRHI compute pipeline; Donut's legacy screen-space
occlusion pass is not instantiated or packaged by UVSR.

The frame order is:

1. opaque PBR G-buffer, with motion vectors only when display AA needs them;
2. deferred direct lighting and a separate configured GI-radiance source;
3. optional single-dispatch hierarchical view-depth generation;
4. full-resolution screen-space visibility-bitmask sampling, repeated once per
   requested GI bounce;
5. indirect-light composition from the current-frame visibility outputs;
6. procedural sky;
7. downstream NRA-RTAA over the complete HDR scene when enabled;
8. exposure, AgX, grading, and output conversion.

The source-radiance UAV is written beside base lighting in the same deferred
dispatch. It contains scene-linear, material-weighted outgoing direct diffuse
radiance plus the enabled/gained emissive source term. Alpha always carries
outgoing sidedness; only the active multi-bounce specialization also packs exact
surface-sidedness and diffuse-activity flags. This lets the stochastic traversal
fetch one HDR texel rather than separate direct, emissive, and feature targets.
Later frontiers preserve the surface flags, so receiver-material early outs need
no repeated G-buffer reads. The source excludes
specular, fallback ambient, screen-space GI, AO, transparency, UI, tone mapping,
and post-processing. For additional finite bounces, the visibility pass reads
only the immediately preceding transport frontier, applies the sampled
surface's diffuse BRDF, and writes the next ping-pong frontier. A separate UAV
accumulates completed frontiers for composition. The pass never
reads the final composite target and never reinjects prior-frame GI. UVSR has
no pre-exposure; source, GI, base lighting, and the composite all
remain unexposed scene-linear values.

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
count rather than render resolution. The UI reports the first-bounce depth-tap
budget. The paper's 16-sample configuration means 16 samples on each horizon
side and therefore maps to 32 Samples Per Pixel in UVSR. Each later GI bounce
halves that budget toward an 8-sample floor without increasing a custom primary
budget that is already below 8.

### Lighting semantics

AO is named ambient visibility and equals the average over slices of:

`1 - popcount(mask) / 32`

Strength and power are applied only after mask evaluation. Ambient
visibility modulates UVSR's sky-gradient fallback indirect diffuse and its
approximate environment-specular response. It does not alter direct light,
emissive radiance, the BSDF, or the new screen-space GI.

The GI output convention is diffuse irradiance. A sample contributes source
radiance times receiver cosine, source-facing cosine, and newly covered angular
fraction. The Algorithm 1 accumulator is multiplied by PI before storage so the
receiver's `baseColor * (1 - metalness) / PI` reproduces the paper-scale result
without an accidental extra 1/PI attenuation. There is no distance attenuation
or division by radial samples/sides. Direct and ordinary emissive sources remain
one-sided; a source texel containing a double-sided emissive card uses the
absolute area cosine. Screen-space AO is not multiplied into GI. Metals receive
no ordinary diffuse GI.

**Bounces** selects one through four finite diffuse-light traversals. Bounce one
uses only the configured direct/emissive source and produces frontier `B1`.
Each later bounce converts only the preceding frontier to outgoing radiance
with the sampled source's `baseColor * (1 - metalness) / PI` and authored
material AO, then evaluates the same transport operator again:

- `C1 = B1`
- `Bn = T(B(n-1))` for `n > 1`
- `Cn = C(n-1) + Bn`

Separating frontier transport from the cumulative result prevents a lower-order
bounce from being transported and counted again at every iteration. GI
Intensity remains a final receiver-side scale and is not fed back per bounce.
Bounce one uses the original AO/GI/debug specialization and full selected sample
count. Later bounces use a fixed GI-only reinjection shader and halve the sample
budget toward 8. At Medium, one through four bounces therefore use 32, 48, 56,
and 64 cumulative taps per pixel instead of 32, 64, 96, and 128. Every bounce
still incurs a full-resolution dispatch, so it is not free, but the dominant
stochastic traversal work grows sublinearly.

### Contribution-aware early outs

`src/lighting_contribution.hlsli` defines the shared lighting contribution gate
used by screen-space GI and forward/deferred direct lighting. Source activity is
represented by composable direct, emissive, environment, indirect-diffuse, and
indirect-specular bits. A scope is skipped only when every relevant source is
known inactive; missing or not-yet-integrated scene data defaults to potentially
active. Debug consumers can force selected sources active. Local hard-rejection
reasons such as zero/non-finite signal, back-facing geometry, zero visibility,
out-of-influence lights, and materials without a contributing lobe remain local
to the operation that proved them. This lets future scene, material, clustering,
visibility, residency, probe, or cache systems contribute tighter facts without
making independent lighting systems incorrectly suppress one another.

The bit positions live in `src/lighting_contribution_shared.h`, shared by C++,
HLSL, and the reference tests. `ScreenSpaceVisibilityInputs` carries a
`knownInactiveLightingSources` mask from CPU scene data into the same gate. UVSR
currently proves direct light inactive for an empty scene-light list and proves
emissive light inactive when that source is disabled or has zero gain. When all
first-bounce sources are proven inactive, production rendering skips the entire
higher-bounce dispatch chain. A zero final GI intensity does the same because no
higher bounce can reach the final composite. Allocation still follows the
configured bounce count, so frame-local activity does not churn resources.
Future clustering, probe, cache, visibility, or residency systems can contribute
additional proven-inactive bits through the same input.

**Bounce Contribution Cutoff** applies only to bounces after the first. For each
eligible source sample, UVSR bounds its peak final-image contribution using the
current GI intensity and exposure, before tone mapping; cosine, angular coverage,
diffuse reflectance, and material AO cannot increase that bound. A source whose
bound cannot exceed the cutoff skips material and normal fetches. A tighter
second test after diffuse-material evaluation can still skip the normal fetch
and remaining shading math. Newly claimed angular sectors are disjoint and
their normalized weights sum to at most one, so the cutoff also bounds the
aggregate rejected final-image energy at one receiver within one higher bounce.
Across several pruned bounces, omitted energy can accumulate up to one such
bound per bounce. A cutoff of `0` retains only exact-zero, non-finite, and hard
material/geometry exits.

The source-sample cutoff runs after depth traversal has found newly claimed mask
sectors. It reduces source material bandwidth and lighting arithmetic, but it
does not remove the bounce's full-resolution dispatch or its visibility
traversal. An independent exact receiver-material gate can skip traversal for a
later-bounce pixel whose diffuse throughput is zero, such as a fully metallic,
black, or material-AO-zero receiver. Indirect Diffuse and GI-light-only
diagnostics force the selected bounce chain into exact-cutoff mode. Diagnostics
that do not consume higher-bounce GI stop after bounce one instead of doing
invisible transport work.

**Emissive Material Light Strength** in the **Lights** drawer multiplies only
the emissive term in the configured source-radiance target. Increasing it makes
the angularly attenuated contribution remain visible over a wider receiver area,
up to the shared world-space sampling radius, without changing the emissive
surface's own displayed radiance or reflected direct-light sources.

### Resources

| Resource | Format | Resolution/lifetime |
|---|---|---|
| base lighting | `RGBA16_FLOAT` | full, frame |
| direct diffuse source | `RGBA16_FLOAT` | full, frame |
| hierarchical view depth | `R16_FLOAT`, 5 mips | full to 1/16, allocated only while the opt-in hierarchy is active |
| raw ambient visibility | `R16_FLOAT` | full, frame |
| raw indirect diffuse frontier A | `RGBA16_FLOAT` | full, frame; also the one-bounce result |
| raw indirect diffuse frontier B | `RGBA16_FLOAT` | full, frame; only for multiple bounces |
| cumulative indirect diffuse irradiance | `RGBA16_FLOAT` | full, frame; only for multiple bounces |
| raw traversal debug | `RGBA8_UNORM` | full, frame |
| final HDR composite | `RGBA16_FLOAT` | full, frame |

Visibility outputs are current-frame only. Composition reads the sampling
textures directly; the pass owns no motion-vector input, output-history pair,
validation surfaces, or denoising intermediates. The deterministic 24-frame
sampling phase still rotates stochastic taps to avoid a fixed spatial pattern,
but it never blends or reprojects a previous result. **Freeze Sampling Phase**
holds that sequence at phase zero for inspection. Exposure is used only to
budget the higher-bounce contribution cutoff; stored lighting remains
scene-linear and unexposed.

NRA-RTAA remains a separate downstream display-reconstruction pass. It receives
the fully composed HDR scene, including visibility AO/GI and the sky, and keeps
its own motion-vector, validation, and color-history resources. Removing the
visibility-specific accumulator does not remove or bypass NRA-RTAA stabilization.

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
The hierarchy texture is not allocated on the default exact-depth path.
Composite, depth-hierarchy, first-bounce, and fixed higher-bounce binding sets
are cached across their finite resource rotations
rather than rebuilt every frame. Because the product pipeline is always full
resolution, shaders use direct pixel addressing and contain no dormant
representative-cell loops.

### Defaults and presets

The default Medium preset is full resolution with one stochastic slice, 32
first-bounce samples per pixel divided between both horizon directions, world radius
3, constant thickness 0.5, linear sample distribution, full radial and angular
jitter, and exact depth by default. AO strength defaults to 1.0 so the visibility
estimate remains clearly visible, while GI intensity and emissive gain default to
4. GI defaults to one bounce, preserving the original traversal cost; the UI
allows up to four. The default higher-bounce contribution cutoff is `0.001`;
setting it to `0` requests exact-zero exits only.

Low/Medium/High/Ultra use 16/32/48/64 total samples per pixel at full resolution.
Editing sampling quality selects Custom without overwriting unrelated AO/GI
controls.
Hierarchical view depth is opt-in: at the default 3 m radius its construction
cost exceeded its saved depth traffic in dense 1080p profiling. It remains
available for AO-only workloads with longer projected rays.
Settings intentionally do not persist between launches. Every process starts
from factory defaults, and **Reset Settings** restores the same renderer, visibility,
tonemapper, LUT, sky, and white-world settings in-session.

GPU timestamp queries report hierarchy, cumulative sampling across all selected
GI bounces, composite, and active complete-effect time without blocking the
render thread. The **All** HUD row shows total time plus grouped Trace and
Composite times. The
main performance row shows resolution, frame time, FPS, current-clock memory
bandwidth, and current-clock FP32 peak GFLOPS separated by pipes. NVIDIA values
come from NVML; Intel values use IGCL plus configured shared-memory data from
SMBIOS when the active adapter is integrated. They describe theoretical
capability at the current clocks, not measured workload utilization;
unsupported adapters show `--`.
Markers label AO-only, GI-only, and combined traversals separately.

## Screen-space limitations

- Off-screen emitters are unavailable without a world-space fallback.
- Surfaces missing behind the single-layer depth buffer cannot contribute.
- Constant thickness cannot match every object.
- Sparse steps can miss very small bright sources.
- Large radii increase bandwidth and cache pressure.
- Every additional GI bounce incurs another full-resolution traversal dispatch,
  although its progressive sample budget makes stochastic work grow sublinearly.
- The higher-bounce cutoff bounds rejected energy per receiver and bounce, not
  across the entire selected chain; nonzero values trade a bounded amount of
  indirect light per pruned bounce for lower material bandwidth and shading cost.
- Visibility noise and phase-to-phase shimmer are controlled only by the
  current-frame sample budget; the visibility pass has no temporal or spatial
  denoiser.
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
conservatively so stale occlusion cannot grow. This would be a new mask-history
pass; the current visibility pipeline has no history.

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
