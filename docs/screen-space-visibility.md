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

### Estimator A/B Contract

The **Estimator** control selects a complete compile-time formulation:

- **Paper Angular** is the current default and regression baseline. It maps
  front/back horizons with two fast-`acos` evaluations, uses linear projected
  angle sectors, explicit receiver/source cosines, and PI normalization.
- **GT Uniform** constructs the projected-normal slice measure, maps directions
  directly to uniform-solid-angle CDF mass without `acos`, quantizes 32
  equal-mass sectors, resolves AO as open mass, retains receiver/source cosines,
  and uses `2 * PI` irradiance normalization.
- **GT Cosine** is represented in the API but disabled in UI. It remains gated
  until a cosine-weighted CDF, sector meaning, GI weight, and normalization are
  derived and pass the same reference and hardware promotion suite.

The estimator is part of shader permutation identity, including AO-only,
GI-only, combined, traversal-debug, first-bounce metadata, and later-bounce
reinjection variants. Runtime branches cannot combine the GT CDF with paper GI
weighting. The executable C++/HLSL equations live in
`src/visibility_estimator_shared.h`; `src/visibility_estimator_cpu.h` supplies
only the CPU dependency adapter. See
[`visibility-estimator-validation.md`](visibility-estimator-validation.md) for
the derivation, 15 deterministic fixtures, numeric baseline, and promotion
gates.

### Radial mask contract

`src/radial_visibility_mask.hlsli` has no texture dependency. Its reusable
contract is:

`VisibilityInterval -> candidate bits -> newly covered bits -> accumulated mask`

Each active slice owns one register-local `uint` with 32 sectors. PaperAngular
interprets them as equal projected-angle intervals; GTUniform interprets them
as equal uniform-hemisphere probability mass. A zero bit is unoccluded; a one
bit is occluded or already claimed by a nearer sample. Samples execute
near-to-far on both sides of a slice. AO consumes the final union, while GI
reads source normal/radiance only when
`candidate & ~occludedBits` is nonzero. Round-to-nearest sector-span
quantization is production default; Ceil and Floor are developer validation
modes. Empty, complete, reversed, clamped, non-finite, and boundary intervals
avoid shift-by-32 and other undefined operations.

Masks are current-frame and register-local. No persistent mask texture is
allocated. Production uses a statically specialized, unrolled one-slice shader
with one randomly rotated slice per pixel and frame, as in the paper
benchmarks. **Developer Slices** values from two through eight select a separate
general loop permutation for validation and multiply traversal work. A
six-phase azimuth sequence and four-phase radial sequence form a 24-frame
low-discrepancy schedule over stable per-pixel spatial noise. Both sides jointly
form the slice; only the final per-slice estimator is averaged.

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
- A desired radial endpoint is transformed to homogeneous clip space once and
  clipped analytically along the receiver-to-endpoint segment against positive
  `w` and the active D3D near plane (`z >= 0` for forward depth or `w - z >= 0`
  for reversed depth). The shortened endpoint is then projected once. This
  replaces iterative projection-and-halving retries while preserving
  perspective, orthographic, forward-depth, and reversed-depth behavior.
- Under perspective, finite thickness extends the sampled front point along
  that point's own camera ray, preserving its projected `xy/w`. Orthographic
  projection uses the camera's constant away direction. Optional distance
  scaling uses the sampled occluder's view depth, not the receiver depth; no
  distance-falloff heuristic is hidden in mask accumulation.

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
radiance times receiver cosine, source-facing cosine, and newly covered sector
measure. PaperAngular multiplies the Algorithm 1 accumulator by PI. GTUniform
sectors represent `p(omega) = 1 / (2 * PI)`, so the matching accumulator uses
`2 * PI`; its receiver cosine is explicit because the uniform CDF has not
already absorbed it. There is no distance attenuation or division by radial
samples/sides. Direct and ordinary emissive sources remain one-sided; a source
texel containing a double-sided emissive card uses the absolute area cosine.
Screen-space AO is not multiplied into GI. Metals receive no ordinary diffuse
GI.

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

Before any later-bounce position reconstruction, normal fetch, or slice setup,
the shader reads the previous frontier's packed receiver metadata and exits
when diffuse throughput is proven inactive. The opt-in **Higher-Bounce Receiver
Statistics** permutation uses one wave-aggregated pair of counters to report
depth-eligible attempts and diffuse-inactive rejections. Its copy/readback uses
the existing delayed sampling-timer ring; production permutations contain no
counter or wave-statistics work.

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
| raw ambient visibility | `R16_FLOAT` | full, frame; allocated only while AO is enabled |
| raw indirect diffuse frontier A | `RGBA16_FLOAT` | full, frame; allocated only while GI is enabled, also the one-bounce result |
| raw indirect diffuse frontier B | `RGBA16_FLOAT` | full, frame; only for multiple bounces |
| cumulative indirect diffuse irradiance | `RGBA16_FLOAT` | full, frame; only for multiple bounces |
| raw traversal debug | `RGBA8_UNORM` | full, frame; only for a traversal-debug view |
| final HDR composite | `RGBA16_FLOAT` | full, frame |

Compiled-out outputs bind three persistent 1x1 format-matching UAV/SRV dummy
textures, so shader binding layouts remain invariant without allocating dormant
full-resolution targets or rebuilding them every frame. The dummies have 14
bytes of logical texel payload in total and are excluded from the table below.

The HUD's **Targets** and **Avoided** values are exact logical texel payloads,
not API-aligned residency, heap commitment, or measured memory traffic. The
former one-bounce policy always allocated AO, GI, and traversal debug for 14
bytes per pixel. With no traversal debug active:

| One-bounce configuration | Active bytes/pixel | 1920x1080 active | 1920x1080 avoided | 3840x2160 active | 3840x2160 avoided |
|---|---:|---:|---:|---:|---:|
| Former always-allocate baseline | 14 | 27.69 MiB | 0 MiB | 110.74 MiB | 0 MiB |
| AO + GI | 10 | 19.78 MiB | 7.91 MiB | 79.10 MiB | 31.64 MiB |
| AO only | 2 | 3.96 MiB | 23.73 MiB | 15.82 MiB | 94.92 MiB |
| GI only | 8 | 15.82 MiB | 11.87 MiB | 63.28 MiB | 47.46 MiB |

An active traversal-debug target adds 4 bytes per pixel (7.91 MiB at 1080p or
31.64 MiB at 4K). Selecting multiple GI bounces adds two `RGBA16_FLOAT`
targets, or 16 bytes per pixel (31.64 MiB at 1080p or 126.56 MiB at 4K), under
both the former and current policies.

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

The default estimator remains PaperAngular pending the documented GTUniform
hardware promotion gates. The default Medium preset is full resolution with one
stochastic slice, 32 first-bounce samples per pixel divided between both horizon
directions, world radius 3, constant thickness 0.5, linear sample distribution,
full radial and angular jitter, and exact depth by default. AO strength defaults
to 1.0 so the visibility estimate remains clearly visible, while GI intensity
and emissive gain default to 4. GI defaults to one bounce, preserving the
original traversal cost; the UI allows up to four. The default higher-bounce
contribution cutoff is `0.001`; setting it to `0` requests exact-zero exits only.

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
The HUD also reports the active and avoided logical target payload described in
the resource table. Higher-bounce receiver counts appear only when their
diagnostic permutation is explicitly enabled.

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
- The checked-in deterministic estimator suite is CPU/slice-space rather than a
  renderer image-regression corpus; local Bistro scenes remain the current GPU
  smoke-test content.
- The checked-in visibility-projection suite covers forward/reversed depth,
  perspective/orthographic paths, positive-`w` and near-plane crossings,
  near-plane receivers, very large finite radii, side symmetry, and invalid or
  non-finite inputs.

## Upgrade path to unified visibility

The following is design intent, not behavior claimed by the current passes.

### Persistent radial masks

Store one 32-bit integer per pixel and active slice in an integer texture or
texture array. Store canonical slice azimuth/phase plus validity, confidence,
and age in companion channels. Canonical orientation must be independent of a
frame's random rotation. Extend the binary representation only if known-visible
must be distinguished from unresolved; today a zero bit means either.

### Shared-mask consumers and conditional lifetime

Keep the current register-local mask whenever every consumer can execute in the
visibility dispatch. Directional ambient, conservative rough-specular
visibility, and transport-confidence production should consume that mask in
place where practical, avoiding a texture write, later read, and persistent
allocation.

Temporal reprojection, spatial reuse, unresolved-sector world fallback, path
guiding, radiance-cache visibility, and other cross-pass or cross-frame users
require a persistent canonical mask texture or texture array. Allocate that
cache only while at least one such consumer is active, and share one validated
representation instead of letting each consumer allocate or retrace its own.
When no persistent consumer is active, retain the zero-allocation register-only
path. A single randomly rotating slice cannot be transformed into an arbitrary
new slice by bit rotation; persistent reuse requires a canonical directional
basis and explicit basis conversion.

### Future payload telemetry

When persistent masks are implemented, split the visibility HUD payload into:

- **Outputs:** exact logical AO, GI, traversal-debug, and bounce payload.
- **Mask Cache:** exact logical persistent directional-mask and metadata
  payload.
- **Avoided:** exact logical allocation omitted because its consumers are
  inactive, preserving the current arithmetic meaning.
- **Shared:** a separately labeled estimate of duplicate payload or traversal
  avoided by several consumers sharing one mask contract.

Do not fold hypothetical traffic, recomputation, API-aligned residency, or the
estimated **Shared** value into **Avoided**. The latter must remain auditable
from active resource formats, dimensions, and lifetimes.

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

1. Define each consumer as same-dispatch or persistent and fuse same-dispatch
   consumers into the register-local path where practical.
2. Allocate/capture canonical integer slice masks plus phase/confidence only
   for active persistent consumers.
3. Extend payload telemetry with separate exact output/mask-cache/avoided
   values and a clearly estimated shared-work value.
4. Add validated temporal reprojection and sector-basis rotation.
5. Add validated spatial reorientation/reuse without silhouette dilation.
6. Add unresolved-sector world-space visibility/radiance queries.
7. Add confidence-decayed cyclic reinjection with explicit energy bounds.
8. Consume sector directions for directional ambient sampling.
9. Derive conservative rough-specular visibility from directional masks,
   validated separately from diffuse AO.
10. Expose the same contract to path guiding, radiance-cache visibility, and
   screen/world transport handoff.
