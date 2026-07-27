# UVSR Screen-Space Visibility and Indirect Lighting

## Current Pipeline

UVSR owns one deferred screen-space visibility producer for ambient occlusion
(AO) and diffuse global illumination (GI). AO and GI share depth traversal and
one register-local 32-bit directional mask. The production path does not write
that mask to memory.

The frame order is:

1. Fill the PBR G-buffer, including motion only when adaptive or temporal
   visibility requires it.
2. Shade direct light and, when GI can consume it, write shadowed direct
   diffuse, directly reflected environment diffuse, emission, and compact
   diffuse-transport metadata to the source buffers.
3. Trace visibility at full, half, or quarter linear resolution.
4. Optionally reproject and accumulate AO/GI history.
5. Optionally joint-bilateral filter to full resolution; reduced output always
   receives at least a minimal depth/normal-guided 2x2 upsample.
6. Composite diffuse and specular IBL from the selected global radiance
   source, AO, screen-space GI, and direct/emissive lighting.
7. Apply the normal AgX display pipeline.

The General drawer provides PBR diagnostics for decoded normals, diffuse IBL,
prefiltered specular radiance, the environment BRDF, final and combined IBL,
specular occlusion, and selected environment mip. **World Materials > Indirect
Diffuse Response** separately shows the final material-applied screen-space
diffuse GI contribution. Visibility stage timings, logical allocation
arithmetic, and external capture tools remain the performance diagnostics.

## Global Image-Based Lighting Composite Contract

UVSR supplies the visibility composite with the same infinite global probe
used by forward and deferred lighting. Its diffuse cube contains SH9
Lambert-convolved `E / pi`; its specular cube contains the matching
roughness-prefiltered GGX radiance; and a source-independent split-sum BRDF LUT
evaluates the receiver response. Both radiance maps are derived from one
of six imported scene-linear HDR sources. The optional visible environment
background samples that same source and exposure, but is drawn separately and
is not part of AO/GI.

When visibility composition is active, deferred lighting writes direct light,
emission, and unrelated indirect-specular input to the base-lighting target
without either global IBL lobe. The final composite then evaluates both lobes
once:

- Authored material AO modulates diffuse IBL. Adjusted screen-space ambient
  visibility applies the user strength and then attenuates that diffuse result.
- The product of authored material AO and adjusted screen-space ambient
  visibility feeds the view- and roughness-aware specular-occlusion function.
  It does not multiply direct specular lighting.
- Screen-space GI uses material diffuse throughput and authored material AO,
  but not screen-space ambient visibility. Its first-bounce source includes
  environment diffuse as well as direct diffuse and emission; specular IBL is
  deliberately excluded from diffuse transport. **Diffuse Strength** scales
  both the visible diffuse lobe and this environment-diffuse SSGI source.

All IBL controls live in the **Sky** drawer. Common exposure scales diffuse
IBL, specular IBL, and the background together. Independent **Diffuse
Strength** and **Specular Strength** controls range from `0.00` to `2.00`,
default to `1.00`, and do not change the background. A disabled or
zero-strength lobe is exactly zero; the composite never substitutes the legacy
top/bottom hemispheric ambient.

A missing or invalid source clears the active probe and background. It cannot
fall back to procedural illumination or retain a stale previous source, and
the failed request is latched instead of retried every frame. AO alone is not a
consumer when both IBL lobes and diffuse GI are disabled, so that configuration
allocates and dispatches no screen-space visibility work.

### No-Hidden-Ambient Invariant

Before IBL, UVSR always composited the hidden two-color hemispherical term
`lerp(bottom, top, normal.y * 0.5 + 0.5)`. It illuminated every surface from
the normal alone, without asking whether any sky was visible. IBL integration
removed it. With both IBL lobes off or at zero strength, the renderer now shows
shadowed direct lighting plus actual SSGI; pixels reached by neither can become
deep black. The direct BSDF and fixed neutral AgX tonemapper were unchanged.
Future projects must not restore this term or another visibility-free ambient
fill.

## Factory Defaults

- Visibility, AO, and GI are enabled at full resolution.
- Medium quality traces 20 fixed samples on one slice per eligible pixel.
- Uniform Solid Angle and Toroidal Blue Noise are selected.
- Adaptive Sparse Sampling is disabled.
- AO strength is 1.0. GI intensity is 1.0 with one bounce, a 0.001 bounce
  contribution cutoff, and emissive sourcing enabled at gain 1.0. Higher gains
  remain explicit artistic controls rather than reference defaults.
- Global diffuse and specular IBL start enabled with **Day - Kloppenheim 03**
  at its calibrated `-2.75 EV` and `1.00` strength. The matching visible
  background also starts enabled.
- The Indirect Diffuse Response view is disabled.
- Temporal reconstruction and spatial filtering are disabled. Their dormant
  settings remain a 0.35 temporal current response and Gaussian joint-bilateral
  filtering at radius 4.0.

## Estimators

The **Estimator** control exposes three compiled formulations:

- **Uniform Projected Angle** follows the finite-thickness
  sector definition in Therrien, Levesque, and Gilet's
  [Screen Space Indirect Lighting with Visibility Bitmask](https://arxiv.org/abs/2301.11376):
  the sector lattice is uniform in projected slice angle.
- **Uniform Solid Angle** is the default. It maps the receiver hemisphere to
  equal-solid-angle sectors. Receiver cosine remains explicit in GI, and
  irradiance uses a `2*pi` normalization.
- **Cosine-Weighted Solid Angle** uses the complete joint receiver-cosine
  measure. It is no longer gated. Receiver cosine is already represented by
  the CDF and slice mass, so GI must not multiply it a second time; the outer
  irradiance normalization is `pi`.

Uniform Solid Angle is the current factory default. Successful compilation and
CPU quadrature do not establish runtime speed, register pressure, occupancy,
cache behavior, or image quality.

### Complete Joint-Cosine CDF

For receiver-to-camera direction `V`, positive slice tangent `S`, and receiver
normal projected into the slice, define

```text
Nproj = p * (cos(gamma) * V - sin(gamma) * S)
```

where `p` is the projected-normal length. A slice direction at signed angle
`alpha` has joint density

```text
cos(alpha + gamma) * abs(sin(alpha))
```

over the receiver-facing support

```text
[-pi/2 - gamma, pi/2 - gamma].
```

The piecewise antiderivative is

```text
alpha >= 0:
  0.5*cos(gamma)*sin(alpha)^2
  - 0.5*sin(gamma)*alpha
  + 0.25*sin(gamma)*sin(2*alpha)

alpha < 0:
  -0.5*cos(gamma)*sin(alpha)^2
  + 0.5*sin(gamma)*alpha
  - 0.25*sin(gamma)*sin(2*alpha)
```

The complete projected slice mass is

```text
p * (cos(gamma) + gamma*sin(gamma)).
```

Front and finite-thickness back directions are mapped through that CDF and
sorted into one interval. Each newly claimed sector therefore represents an
equal conditional fraction of the slice's joint cosine mass. AO multiplies the
open-sector fraction by the complete slice mass before the uniform azimuth
average. GI multiplies newly claimed sector fraction by slice mass and source-
facing cosine only.

A single uniformly selected slice is an unbiased outer Monte Carlo sample and
can exceed one before azimuthal or temporal averaging for a tilted receiver.
UVSR preserves that value through temporal and spatial reconstruction and
applies the physical `[0,1]` visibility bound only during final composition.
Clamping the raw sample would bias the completed cosine measure.

## Finite Thickness

Thickness is one conservative world-space estimate. For perspective cameras,
the back point extends along each sampled point's own away-from-camera ray; the
orthographic path uses the camera's constant away direction. Analytic
homogeneous clipping handles near-plane and camera-plane crossings before the
single endpoint divide.

The CDF cannot infer a better thickness. It maps angular measure after front
and back geometry have been chosen; it contains no information about unseen
back faces or object thickness. Automatic thickness would require a separate
heuristic or geometric source, such as depth-layer evidence, material metadata,
or a second depth representation. UVSR intentionally exposes one thickness
instead of hiding a view-distance heuristic in estimator comparisons.

## Current Sample Distribution

With **Adaptive Sparse Sampling** enabled, **Minimum Samples / Pixel** and
**Maximum Samples / Pixel** are scheduled radial sample budgets on one
stochastic slice, not budgets per radial direction.
Full-mask early termination, invalid projection, and duplicate screen coordinates
can make the executed depth-read count lower than the selected budget.

Every eligible pixel receives one slice and at least the minimum sample count.
The selected total is divided between the two near-to-far radial directions.
Later diffuse bounces halve both limits toward an eight-sample floor, without
raising a first-bounce limit that was already below eight.

Each radial direction owns a 32-stratum bit-reversal sequence. The complete
selected prefix receives a stochastic toroidal stratum rotation plus an
independent within-stratum offset every pixel and frame. Increasing a sample
limit with the same phase appends strata without moving its lower-budget prefix,
while changing phase prevents the same global radius shells from accumulating
into rings. The rotated set is consumed in ascending physical-stratum order.
Nesting therefore controls set membership without letting a farther GI sample
claim a sector before a nearer selected source on the same radial direction. The
**Radial Distribution Exponent** transforms each normalized stratum by
`x^exponent`; the default `x^2` concentrates depth taps near the receiver. This
means:

- lower the minimum first to measure the guaranteed base cost;
- raise the maximum to give difficult pixels more possible evidence;
- tune the exponent separately, because it redistributes distance rather than
  changing tap count; and
- compare scheduler modes with identical sample limits.

With **Adaptive Sparse Sampling** disabled, as it is by default, UVSR selects a
separately compiled fixed-work shader. Every eligible pixel receives **Fixed
Samples / Pixel** on one slice; the default is 20. The fixed specialization
contains no adaptive depth/normal neighborhood analysis, motion/reprojection
reads, feedback reads or writes, or stochastic budget rounding. Adaptive
feedback textures are not allocated and adaptive sampling alone does not
request motion vectors. The sample scheduler remains independent because it
determines where the fixed samples land, not how many samples a pixel receives.

The quality presets set the following first-bounce budgets. With adaptive
sampling off, only the fixed/max value is executed.

| Preset | Adaptive minimum | Fixed/adaptive maximum |
|---|---:|---:|
| Low | 4 | 10 |
| Medium (default) | 8 | 20 |
| High | 12 | 48 |
| Ultra | 16 | 64 |

Activision's
[Practical Realtime Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf)
informs the horizon-slice traversal, quadratic radial concentration, and the
decision to distribute work across spatial and temporal reconstruction. It is
not the source of UVSR's finite-thickness bitmask sector definition; calling
the default estimator "GTAO" would conflate two related but different methods.

## Adaptive Sparse Sampling

UVSR transfers the stochastic allocation principles from
[Forget Superresolution, Sample Adaptively (when Path Tracing)](https://arxiv.org/html/2602.08642v1)
without claiming to reproduce its unpublished neural sampler. The renderer has
no network or learned density. It builds a local error importance from:

- four-neighbor depth and normal discontinuity;
- invalid motion or reprojected depth mismatch;
- reprojected instability; and
- the current pixel plus one stochastically selected, depth-compatible
  eight-neighbor contribution seed.

The last term makes pixels around previously contributing GI samples
stochastically more likely to receive extra work without a fixed cross stencil.
A contribution discovered only because of a neighboring seed is tagged as
ineligible to seed another outward step; center or independently discovered
signals remain persistent. This prevents the old one-texel-per-frame positive-
feedback dilation that appeared as expanding crosses and rings. Stored
instability decays when no current evidence sustains it. The sample budget uses
a one-eighth uniform component plus seven-eighths adaptive importance so flat
regions cannot become permanent starvation zones. Fractional desired sample
counts use stochastic rounding. Importance changes only radial tap count; every
pixel retains one stochastic slice.

This is a probability framework, not a hard classification table. Features
raise the chance of extra work; they do not deterministically force one quality
tier. **Adaptive Error Strength** scales those probabilities. Zero, or an empty
refinement range where minimum equals maximum, selects the fixed-work
specialization. The explicit **Adaptive Sparse
Sampling** checkbox is the clearer A/B control: off fixes the budget to the
maximum/fixed count and removes all adaptive instructions and resources.

## Sample Schedulers

**Independent Hash Noise** independently hashes stochastic decisions and
consumes no rank-field texture.

**Toroidal Blue Noise** uses eight independently generated 64x64
toroidal void-and-cluster rank layers. Slice rotation, CDF sector phase, budget
rounding, odd-sample side, both radial directions, and adaptive-neighbor choice
receive separate semantic layers rather than translated copies of one scalar
texture. Dimension-specific toroidal temporal steps preserve each layer's
spatial spectrum, and hashed cycle offsets prevent exact 64-frame repetition.
It is spatiotemporal as a runtime sequence, but its eight 2D layers were not
jointly optimized as one 3D space-time volume.
This is the default scheduler.

**Filter-Adapted Spatiotemporal Noise** uses a 64x64x32 scalar-uniform
volume generated offline by Electronic Arts' FastNoise optimizer. Its fixed
objective is a Gaussian spatial filter with sigma 1.0 and exponential temporal
history with alpha 0.35. R2-separated spatial reads provide different semantic
random values without adding texture layers, and a coprime 4096-position offset
advances after each 32-frame volume cycle. This mode is the genuinely 3D,
jointly filter-adapted option. Its objective remains statistically valid when
reconstruction settings change, but is no longer an exact match for a different
spatial kernel or temporal response.

The design follows the rejection-safe and toroidal-sequence guidance in
NVIDIA's
[Rendering in Real Time With Spatiotemporal Blue Noise Textures, Part 2](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-2/).
The filter-adapted mode directly follows the offline optimization described by
[Importance-Sampled Filter-Adapted Spatiotemporal Sampling](https://jcgt.org/published/0014/01/08/paper.pdf),
using the authors' FastNoise implementation. The toroidal mode remains UVSR's
procedurally generated alternative; neither mode claims to reproduce NVIDIA's
precomputed 3D STBN volumes. The two resident rank fields consume exactly 192
KiB of logical scheduler storage: 64 KiB of `R16_UNORM` toroidal layers and 128
KiB of `R8_UNORM` filter-adapted volume data.

The scheduler changes where and when samples appear; it does not change the
nested radial distribution or the requested budget. Profile all modes at
identical limits. Human evaluation should look for structured banding,
stationary grain, motion trails, and convergence after disocclusion.

## Reconstruction and Upsampling

**Temporal Reconstruction** and **Spatial Filtering** are independent and both
are disabled by default. Their **Reconstruction and Upsampling** drawer starts
collapsed. At full resolution with both disabled, UVSR composites the current
AO/GI output directly. Temporal reconstruction can accumulate history with or
without spatial filtering, and spatial filtering can process the current frame
with or without temporal accumulation.

Half and quarter resolution always require a guide-aware mapping from the trace
grid to the full-resolution destination. When spatial filtering is disabled,
UVSR performs only the minimal depth/normal-guided 2x2 upsample. Enabling spatial
filtering routes reconstruction through the selected filter: the compact path
uses its guided 2x2 gather, while the Gaussian path uses its parity-varied
four-tap reduced-resolution subset.

Temporal reconstruction follows SSRT3's core contract:

- reproject with current-to-previous pixel motion;
- validate motion, device-depth delta, and normal agreement;
- find current bounds from four diagonal neighbors;
- move previous history 25% toward those bounds; and
- blend with **Temporal Current Response**, whose reference default is `0.35`.

Invalid history selects the current frame without reading uninitialized values
into arithmetic. Camera topology, render-target changes, scene unload, shader
recreation, White World, estimator changes, traversal-budget changes,
radius/thickness changes, and GI source/bounce-contract changes invalidate
history.

The compact **Joint Bilateral** filter uses a 3x3 kernel at full resolution and
a guided 2x2 gather for reduced-resolution upsampling. **Gaussian Joint
Bilateral** follows the structure of SSRT3's HDRP diffuse denoiser: 16 disk taps
at full resolution or one parity-varied four-tap subset when reduced,
`sigma = 0.9 * radius`, receiver tangent-plane placement projected back into
screen space, and depth/normal bilateral rejection. Background pixels resolve
to open AO and zero GI rather than bleeding foreground values into the sky.
Disabling **Spatial Filtering** skips the dispatch and full-resolution AO/GI
target allocation at full resolution. At half/quarter resolution it selects
only the compact four-tap joint upsampler. Direct nearest expansion is not used:
it exposes isolated, high-energy GI samples as coherent horizontal or vertical
streaks and is not a valid reconstruction of the lower-resolution signal.

Source: [cdrinmatane/SSRT3](https://github.com/cdrinmatane/SSRT3), MIT license.

## Resolution and Cost

Full, half, and quarter are linear resolution scales, so the nominal sampling
pixel counts are approximately `1`, `1/4`, and `1/16` of full resolution before
edge rounding. This is not a predicted end-to-end speedup: the full-resolution
G-buffer, temporal guides, upsampling/filtering, and composition remain, while
actual trace cost depends on adaptive budgets, bounce count, divergence, and
hardware occupancy.

For AO-only radii of at least eight world units on perspective cameras, UVSR
automatically builds a five-level XeGTAO-style smart-average view-depth
hierarchy and uses coarser depth for distant taps. GI stays on exact depth so a
coarse geometry sample cannot be paired with unrelated full-resolution source
radiance.

Source activity and output allocation are consumer driven. AO-only does not
allocate GI outputs or the full-resolution source-radiance target; GI-only does
not allocate AO outputs. AO strength zero and GI intensity zero make their
respective effects inactive consumers rather than dispatching mathematically
zero paths. The source-radiance target is also absent when direct-light,
diffuse-environment, and emissive sources are all inactive. Adaptive feedback,
temporal history, full-resolution filtered outputs, higher-bounce frontiers,
and the depth hierarchy exist only while their consumers require them. Proven
scene-wide first-bounce inactivity terminates the complete higher-bounce
dispatch chain.

## HUD Statistics

The collapsed **Statistics** drawer starts with one timing row:

- **All:** the complete visibility effect time.
- **Trace:** depth hierarchy plus all active visibility traversal.
- **Filter:** spatial filtering or required reduced-resolution upsampling.
- **Other:** temporal reconstruction plus final visibility composition.

Two following memory rows separate:

- **Outputs:** exact logical AO, GI, filtered output, and active bounce-frontier
  texel payload.
- **Working:** exact resident scheduler textures, adaptive feedback, temporal
  depth/normal/AO/GI history, and depth-hierarchy texel payload.
- **Mask Cache:** exact persistent directional-mask storage. It is zero in the
  current register-only architecture.
- **Avoided:** exact optional AO/GI resources not allocated because their
  consumer is inactive under the current resolution, temporal, and spatial-
  filter state.
- **Shared:** an estimate of one duplicate `R32_UINT` mask payload avoided when
  AO and GI consume the same register-local mask.

Outputs, Working, Mask Cache, and Avoided exclude API alignment, residency, and
driver allocation. Shared is deliberately labeled as an estimate. Avoided does
not count hypothetical recomputation or bandwidth savings.

## Directional-Mask Consumer Contract

Future techniques should consume visibility in one of two ways:

1. Fuse a future rough-specular approximation or confidence generation into
   the visibility dispatch and consume the register-local mask. This has no
   mask write, allocation, or later read.
2. Allocate a compact canonical `R32_UINT` mask cache only for a genuine cross-
   pass, cross-frame, temporal-reprojection, path-guiding, spatial-reuse, or
   world-space-fallback consumer.

All persistent consumers must share one documented cache contract and metadata
layout. A rotating slice is not a canonical directional representation: an
arbitrary new slice direction cannot be recovered by bit rotation. UVSR must
never persist one rotating slice and assume otherwise.

## Future Sky Visibility Recommendations

This section is a future design direction, not implemented behavior. The
current renderer has scalar screen-space ambient visibility and material AO;
it does not yet evaluate source-directional environment visibility, output a
bent normal, represent offscreen occluders, consume baked sky occlusion, or
offer environment rotation.

1. **Define Source-Side Environment Visibility.** Treat sky visibility as
   visibility of incoming environment radiance,
   `V(x, omega) * L_environment(omega)`, before receiver integration. Diffuse
   and specular consumers may filter that directional field differently, but
   they must share one source-side visibility contract. A post-lighting
   brightness scalar is only an approximation and must not become a hidden
   ambient replacement.
2. **Build the Near-Term Screen-Space Solution at Multiple Scales.** Extend
   screen-space traversal across near, medium, and long horizons, retain
   directional information in a canonical representation, and derive both a
   sky-visibility factor and bent normal. Use those outputs to sample or
   integrate the environment directionally. This is the nearest practical
   improvement for visible occluders, but disocclusion and screen-edge failures
   must remain explicit because screen space cannot prove offscreen visibility.
3. **Add an Offscreen World-Space Source.** For occluders absent from the
   primary depth buffer, evaluate a world-space voxel or SDF representation, a
   scene-space or multiview HZB, or ray-traced hemisphere visibility. The
   representation must cover the environment hemisphere rather than only the
   primary light direction, and its quality/performance tier must be measured
   independently from the screen-space pass.
4. **Keep Static Baked Sky Occlusion Optional.** Static scenes may provide
   baked directional sky occlusion or a bent normal as a low-cost, stable
   source. Store it separately from authored material AO, define behavior for
   movable geometry and changed environments, and never require it for dynamic
   or asset-agnostic rendering.
5. **Compose a Hybrid Conservatively.** Prefer detailed screen-space evidence
   where valid, use world-space or baked data for missing directions, and blend
   with confidence and temporal stability rather than taking an unexplained
   global minimum. Apply sufficient environment visibility before environment
   diffuse is written into the SSGI source, or carry the visibility contract
   with that source; otherwise under-occluded IBL can be rebroadcast as
   screen-space GI and refill the enclosure that visibility was meant to
   darken.
6. **Calibrate with Independent References.** Add controlled environment
   rotation so asymmetric HDR features can be checked against the bent normal
   and visibility field. Preserve a direct-only reference with both IBL lobes
   and SSGI off, then compare diffuse-only, specular-only, and SSGI-enabled
   states at `1.00` strength before artistic gain. Do not reuse the directional
   sun shadow as sky visibility: one hard-light direction cannot represent the
   broad environment hemisphere. Exposure and lobe strength are calibration
   controls, not occlusion fixes.

## Validation Boundary

Automated checks cover shared C++/HLSL CDF math, numerical quadrature, AO/GI
fixtures, normalization, homogeneous endpoint clipping, PBR composition,
common-exposure and lobe-strength contracts, shader permutations, and Release
compilation. They do not replace human review of thin geometry, motion,
temporal trails, reduced-resolution edge stability, environment failure, or
filter quality. The future recommendations above require their own reference
images and tests when implemented. No runtime performance improvement should
be claimed without controlled timings plus register/occupancy and traffic
evidence on the target adapters.
