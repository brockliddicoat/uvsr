# UVSR Screen-Space Visibility and Indirect Lighting

## Current Pipeline

UVSR owns one deferred screen-space visibility producer for ambient occlusion
(AO) and diffuse global illumination (GI). AO and GI share depth traversal and
one register-local 32-bit directional mask. The production path does not write
that mask to memory.

The frame order is:

1. Fill the PBR G-buffer, including motion only when temporal visibility
   requires it.
2. Shade direct light and, when GI can consume it, write source radiance and
   compact diffuse-transport metadata.
3. Trace visibility at full, half, or quarter linear resolution.
4. Optionally reproject and accumulate AO/GI history.
5. Optionally joint-bilateral filter to full resolution; reduced output always
   receives at least a minimal depth/normal-guided 2x2 upsample.
6. Composite approximate sky fallback, AO, screen-space GI, and direct light.
7. Apply the normal AgX display pipeline.

There are no visibility or PBR debug-shader views in the current build. The
sole diagnostic exception is **World Materials > Indirect Diffuse Response**,
a composition mode that shows the final material-applied screen-space diffuse
GI contribution. Profiling is otherwise limited to GPU stage timings, logical
allocation arithmetic, and external capture tools.

## Factory Defaults

- Visibility, AO, and GI are enabled at full resolution.
- Medium quality traces 20 fixed samples on one slice per eligible pixel.
- Uniform Solid Angle and Toroidal Blue Noise are selected.
- AO strength is 1.0. GI intensity is 4.0 with one bounce, a 0.001 bounce
  contribution cutoff, and emissive sourcing enabled at gain 4.0.
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

**Samples / Pixel** is one fixed radial budget on one stochastic slice, not a
budget per radial direction. Every eligible pixel receives one slice and the
selected total is divided between its two near-to-far radial directions.
Full-mask early termination, invalid projection, and duplicate screen
coordinates can make the executed depth-read count lower than the configured
budget. Later diffuse bounces halve the fixed budget toward an eight-sample
floor without raising a first-bounce budget that was already below eight.

Each radial direction owns a 32-stratum bit-reversal sequence. The complete
selected prefix receives a stochastic toroidal stratum rotation plus an
independent within-stratum offset every pixel and frame. Increasing a sample
count with the same phase appends strata without moving its lower-budget prefix,
while changing phase prevents the same global radius shells from accumulating
into rings. The rotated set is consumed in ascending physical-stratum order.
Nesting therefore controls set membership without letting a farther GI sample
claim a sector before a nearer selected source on the same radial direction. The
**Radial Distribution Exponent** transforms each normalized stratum by
`x^exponent`; the default `x^2` concentrates depth taps near the receiver. This
means:

- lower **Samples / Pixel** to measure a smaller fixed traversal cost;
- raise it when every eligible pixel needs more radial evidence;
- tune the exponent separately, because it redistributes distance rather than
  changing tap count; and
- compare scheduler modes with identical sample counts.

The quality presets set the following fixed first-bounce budgets:

| Preset | Samples / Pixel |
| --- | ---: |
| Low | 10 |
| Medium (default) | 20 |
| High | 48 |
| Ultra | 64 |

The sampling shader contains no importance neighborhood, sampling
reprojection, feedback read or write, or stochastic budget-selection path.
Sampling therefore allocates no feedback textures and does not request motion
vectors. The sample scheduler remains independent because it determines where
the fixed samples land, not how many samples a pixel receives.

Activision's
[Practical Realtime Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf)
informs the horizon-slice traversal, quadratic radial concentration, and the
decision to distribute work across spatial and temporal reconstruction. It is
not the source of UVSR's finite-thickness bitmask sector definition; calling
the default estimator "GTAO" would conflate two related but different methods.

## Sample Schedulers

**Independent Hash Noise** independently hashes stochastic decisions and
consumes no rank-field texture.

**Toroidal Blue Noise** uses eight independently generated 64x64
toroidal void-and-cluster rank layers. Slice rotation, CDF sector phase,
odd-sample side, and both radial directions receive separate semantic layers
rather than translated copies of one scalar texture. Dimension-specific
toroidal temporal steps preserve each layer's spatial spectrum, and hashed
cycle offsets prevent exact 64-frame repetition. It is spatiotemporal as a
runtime sequence, but its eight 2D layers were not jointly optimized as one 3D
space-time volume.
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
nested radial distribution or the requested sample count. Profile all modes at
identical sample counts. Human evaluation should look for structured banding,
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
recreation, White World, estimator changes, sample-count changes,
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
actual trace cost depends on the fixed sample count, bounce count, divergence, and
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
present and emissive sourcing is disabled or has zero gain. Temporal history,
full-resolution filtered outputs, higher-bounce frontiers, and the depth
hierarchy exist only while their consumers require them. Proven
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
- **Working:** exact resident scheduler textures, temporal depth/normal/AO/GI
  history, and depth-hierarchy texel payload.
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
