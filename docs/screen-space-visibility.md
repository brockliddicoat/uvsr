# UVSR Screen-Space Visibility and Indirect Lighting

## Current Pipeline

UVSR owns one deferred screen-space visibility producer for ambient occlusion
(AO) and diffuse global illumination (GI). AO and GI share depth traversal and
one register-local 32-bit directional mask. The production path does not write
that mask to memory.

The frame order is:

1. Fill the PBR G-buffer, including motion only when adaptive or temporal
   visibility requires it.
2. Shade direct light and, when GI can consume it, write source radiance and
   compact diffuse-transport metadata.
3. Trace visibility at full, half, or quarter linear resolution.
4. Optionally reproject and accumulate AO/GI history.
5. Optionally joint-bilateral filter to full resolution; reduced output always
   receives at least a minimal depth/normal-guided 2x2 upsample.
6. Composite approximate sky fallback, AO, screen-space GI, and direct light.
7. Apply the normal AgX display pipeline.

There are no visibility or PBR debug-shader views in the current build. The
sole diagnostic exception is **Show GI-Only Lighting**, a composition switch
that shows the final material-applied screen-space diffuse GI contribution.
Profiling is otherwise limited to GPU stage timings, logical allocation
arithmetic, and external capture tools.

## Estimators

The **Estimator** control exposes three compiled formulations:

- **Uniform Projected Angle** is the default. It follows the finite-thickness
  sector definition in Therrien, Levesque, and Gilet's
  [Screen Space Indirect Lighting with Visibility Bitmask](https://arxiv.org/abs/2301.11376):
  the sector lattice is uniform in projected slice angle.
- **Uniform Solid Angle** maps the receiver hemisphere to equal-solid-angle
  sectors. Receiver cosine remains explicit in GI, and irradiance uses a
  `2*pi` normalization.
- **Cosine-Weighted Solid Angle** uses the complete joint receiver-cosine
  measure. It is no longer gated. Receiver cosine is already represented by
  the CDF and slice mass, so GI must not multiply it a second time; the outer
  irradiance normalization is `pi`.

Uniform Projected Angle remains the default until controlled image comparisons and GPU
measurements justify changing it. Successful compilation and CPU quadrature do
not establish runtime speed, register pressure, occupancy, cache behavior, or
image quality.

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

With **Adaptive Sparse Sampling** disabled, UVSR selects a separately compiled
fixed-work shader. Every eligible pixel receives **Fixed Samples / Pixel** on
one slice. The fixed specialization contains no adaptive depth/normal
neighborhood analysis, motion/reprojection reads, feedback reads or writes, or
stochastic budget rounding. Adaptive feedback textures are not allocated and
adaptive sampling alone does not request motion
vectors. The hash/STBN scheduler remains independent because it determines
where the fixed samples land, not how many samples a pixel receives.

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

## Spatiotemporal Blue-Noise Scheduler

**Hash Baseline** independently hashes stochastic decisions. **Decorrelated
Blue Noise** uses eight independently generated 64x64 toroidal void-and-cluster
rank layers. Slice rotation, CDF sector phase, budget rounding, odd-sample side,
both radial directions, and adaptive-neighbor choice receive separate semantic
layers rather than translated copies of one scalar texture. Dimension-specific
toroidal temporal steps preserve each layer's spatial spectrum, and hashed
cycle offsets prevent exact 64-frame repetition.

The design follows the rejection-safe and toroidal-sequence guidance in
NVIDIA's
[Rendering in Real Time With Spatiotemporal Blue Noise Textures, Part 2](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-2/).
It also follows the filter-aware motivation of
[Importance-Sampled Filter-Adapted Spatiotemporal Sampling](https://jcgt.org/published/0014/01/08/paper.pdf),
but it does not claim to reproduce that paper's offline FAST texture optimizer
or NVIDIA's precomputed 3D STBN volumes. UVSR's rank layers are generated
procedurally and import no external texture asset. They consume exactly 64 KiB
of logical `R16_UNORM` scheduler storage.

The scheduler changes where and when samples appear; it does not change the
nested radial distribution or the requested budget. Profile the hash and blue-
noise modes at identical limits. Human evaluation should look for structured
banding, stationary grain, motion trails, and convergence after disocclusion.

## Reconstruction and Upsampling

**Reconstruction Enabled** is the master switch. When it is off, full
resolution composites raw AO/GI directly. Half/quarter resolution retains the
minimal guide-aware upsampler required to map between source and destination
grids. Temporal reconstruction and **Spatial Filtering** can be toggled
independently while the master is on.

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
zero paths. The source-radiance target is also absent when no scene light is
present and emissive sourcing is disabled or has zero gain. Adaptive feedback, temporal
history, full-resolution filtered outputs, higher-bounce frontiers, and the
depth hierarchy exist only while their consumers require them. Proven
scene-wide first-bounce inactivity terminates the complete higher-bounce
dispatch chain.

## HUD Statistics

The Visibility HUD separates:

- **Outputs:** exact logical AO, GI, filtered output, and active bounce-frontier
  texel payload.
- **Working:** exact blue-noise scheduler, adaptive feedback, temporal
  depth/normal/AO/GI history, and depth-hierarchy texel payload.
- **Mask Cache:** exact persistent directional-mask storage. It is zero in the
  current register-only architecture.
- **Avoided:** exact optional AO/GI resources not allocated because their
  consumer is inactive under the current resolution and reconstruction mode.
- **Shared:** an estimate of one duplicate `R32_UINT` mask payload avoided when
  AO and GI consume the same register-local mask.

Outputs, Working, Mask Cache, and Avoided exclude API alignment, residency, and
driver allocation. Shared is deliberately labeled as an estimate. Avoided does
not count hypothetical recomputation or bandwidth savings.

## Directional-Mask Consumer Contract

Future techniques should consume visibility in one of two ways:

1. Fuse same-frame directional ambient, rough-specular approximation, or
   confidence generation into the visibility dispatch and consume the register-
   local mask. This has no mask write, allocation, or later read.
2. Allocate a compact canonical `R32_UINT` mask cache only for a genuine cross-
   pass, cross-frame, temporal-reprojection, path-guiding, spatial-reuse, or
   world-space-fallback consumer.

All persistent consumers must share one documented cache contract and metadata
layout. A rotating slice is not a canonical directional representation: an
arbitrary new slice direction cannot be recovered by bit rotation. UVSR must
never persist one rotating slice and assume otherwise.

## Validation Boundary

Automated checks cover shared C++/HLSL CDF math, numerical quadrature, AO/GI
fixtures, normalization, homogeneous endpoint clipping, PBR composition, shader
permutations, and Release compilation. They do not replace human review of
thin geometry, motion, temporal trails, reduced-resolution edge stability, or
filter quality. No runtime performance improvement should be claimed without
controlled timings plus register/occupancy and traffic evidence on the target
adapters.
