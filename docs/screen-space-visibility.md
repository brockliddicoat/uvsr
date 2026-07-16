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
3. Resolve the CPU-side Reference or curated performance profile, allocate only
   its resources, and trace visibility at full, half, or quarter linear
   resolution.
4. Optionally reproject and accumulate AO/GI history.
5. Optionally joint-bilateral or packed-edge filter to full resolution;
   reduced output always receives at least a minimal guide-aware resolve.
6. Composite approximate sky fallback, AO, screen-space GI, and direct light,
   or use an explicitly selected AO-only fused resolve/application profile.
7. Apply the normal AgX display pipeline.

The normal product view remains unchanged, and **World Materials > Indirect
Diffuse Response** still shows the final material-applied screen-space diffuse
GI contribution. The advanced verification drawer additionally exposes
benchmark-only constant-output, matched depth-read, bitmask-ALU, temporal-copy,
nearest/bilinear resolve, and composition isolation profiles. Their images are
intentionally not product output; their only purpose is to establish timing
floors with the same renderer and timer system.

## Factory Defaults

- Visibility, AO, and GI are enabled at full resolution.
- Medium quality traces 20 fixed samples on one slice per eligible pixel.
- Uniform Solid Angle and Toroidal Blue Noise are selected.
- Adaptive Sparse Sampling is disabled.
- AO strength is 1.0. GI intensity is 4.0 with one bounce, a 0.001 bounce
  contribution cutoff, and emissive sourcing enabled at gain 4.0.
- The Indirect Diffuse Response view is disabled.
- Temporal reconstruction and spatial filtering are disabled. Their dormant
  settings remain a 0.35 temporal current response and Gaussian joint-bilateral
  filtering at radius 4.0.

## Performance Profiles and Controls

**AO Performance Verification** owns the profile selector, profile validity,
active permutation and shader keys, resource/binding/pass masks, plan-derived
exact first-trace and peak-per-pass SRV/UAV counts, active-dispatch count,
optional texture bytes, full-resolution intermediate bytes, logical traffic
avoided, benchmark controls, and the normal AO/GI settings.
Changing a history-affecting profile value changes the displayed history key
and resets temporal/adaptive history. The other three advanced drawers remain
separate:

- **Fast Math & Validation** reports Reference or the conservative FP32 filter-
  algebra experiment and explains which uncompiled controls are unavailable.
- **Precision & Formats** reports actual formats. There is no compiled
  mixed-precision trace, native FP16 trace, R8 raw-AO, or packed R16_UINT raw-AO
  product path.
- **Dispatch, Memory & Cache** reports thread-group, fixed specialization,
  depth mode, minimal bindings, lazy pipeline selection, resource counts, and
  traffic. The fixed shaders compile both direct-depth and hierarchy-aware
  variants; the latter preserves the existing distance-threshold mip mapping
  when the AO-only radius is at least 8.

The displayed resource and pass masks are authoritative plan identities, while
the binding mask is a conceptual effect-wide union rather than a literal
simultaneous binding layout. The byte counters are logical texel arithmetic.
**First-Trace SRVs** and
**First-Trace UAVs** are exact counts for the selected first-trace binding
layout, including the deliberately broad Reference layout and the minimal
candidate or diagnostic layout. **Peak SRVs** and **Peak UAVs** are the maximum
simultaneously bound by any one selected visibility pass, including depth,
later bounce, temporal, reconstruction, fused application, and composition.
They do not sum descriptors across passes or enumerate every layout; use DXIL
reflection or a GPU capture when that whole-effect inventory is required.

Reference is a hard CPU-side lock to the canonical generic shaders, broad
layouts, resources, dispatches, formats, and composition. Selecting Reference
creates no candidate PSO, binding, edge texture, packed FAST texture, or fused
pass. Curated candidates are created lazily and cached by a shader-only key;
workload and history identity remain separate keys so output size or radius do
not duplicate identical pipelines.

The one-click profile status is:

| One-Click Profile | Status | Exact Scope Or Boundary |
| --- | --- | --- |
| Reference AO 8T | Implemented | Canonical generic bitmask reference at the target workload |
| Exact-Fast AO 8T | Implemented | Fixed-8 bitmask plus packed current FAST; no mixed precision |
| Mixed-Precision AO 8T | Unavailable | No mixed-precision trace permutation is compiled |
| Packed-Edge AO 8T | Implemented | R16F raw AO plus a separate R8_UINT 4x4 packed-edge resolve |
| Activision-Schedule AO 8T | Implemented | UVSR bitmask with Activision spatial/temporal rotation and radial offsets |
| Reference AO+GI 8T | Implemented | Canonical shared AO/GI traversal |
| Exact-Fast AO+GI 8T | Implemented | Fixed-8 shared traversal with packed current FAST and bounce metadata when required |
| Exact-Fast AO+GI 12T | Partial Control | Exact fixed-12 trace; packed FAST exists only for fixed 8 |
| Exact-Fast AO+GI 16T | Partial Control | Exact fixed-16 trace; packed FAST exists only for fixed 8 |
| Exact-Fast Multi-Bounce | Partial Control | Exact fixed-8 first and later traces; no packed FAST or fused multi-bounce application |
| Aggressive Experimental AO 8T | Unavailable | No aggressive mixed-precision or compact-AO permutation is compiled |
| XeGTAO Closest Match | Unavailable | No complete Hilbert/R2, depth-prefilter, XeGTAO denoiser, encoding, and mixed-precision pipeline exists |
| PS4 GTAO Closest Match | Partial Control | Twelve-read analytic horizon plus 4x4-by-6 schedule, but UVSR resolve/history/composition remain |

The custom implementation selector additionally exposes fixed 8/12/16/20 AO,
GI, and AO+GI traces; fixed later-bounce 8; exact 16x8 and 8x16 trace groups;
benchmark-only exact duplicate-rejection-off and full-mask-exit-off controls;
32/64/128-pixel projected-radius clamps; exact fused resolve/application;
depth-only, depth-plus-normal, slope-adjusted, and controlled-leakage packed-
edge resolves; fused packed-edge 2x2 and 4x4; the standalone Activision
scheduler; a constant-scheduler diagnostic; and all other diagnostic floors.
Unsupported combinations fail profile validation and fall back to Reference
rather than silently running a partially matching candidate.
**Generic Fallback** is also available as an exact runtime-count control for
unsupported fixed counts; it is a correctness escape hatch with no forecast
speedup, not a nearby-count substitution.

## Expected Performance Impact

The following values are engineering forecasts, not measurements. They rank
the implemented candidate families by plausible complete-effect impact and
maximum credible upside before controlled testing
for the requested 1920x1080, half-resolution, AO-only, fixed-8 workload on
Intel Xe-LPG. Milliseconds are relative to the user-reported 2.5-2.7 ms
baseline; percentages use its 2.6 ms midpoint. Positive numbers mean a saving,
negative numbers mean a possible regression. Rank therefore reflects potential
impact, not a claim that the midpoint is positive. Ranges overlap, every entry is
non-additive, and target retention requires a controlled Core Ultra 9 185H run
plus image review.

| Rank | Implemented Candidate | Forecast Saving | Baseline Share | Why This Range Is Plausible | Quality Classification |
| ---: | --- | ---: | ---: | --- | --- |
| 1 | Exact fused 2x2 resolve and AO application | 0.15-0.45 ms | 6-17% | Removes one 1080p R16F round trip and dispatch; DXIL drops the separate pair's 50 static loads/four stores to 20 loads/one store | Exact with explicit R16F round-trip emulation |
| 2 | Fused packed-edge 2x2 | 0.12-0.40 ms | 5-15% | Combines fusion with cheaper edge connectivity, but pays the trace-resolution R8 edge write/read | Algorithmic reconstruction |
| 3 | Packed depth-edge 2x2 resolve | 0.08-0.30 ms | 3-12% | Replaces repeated full-resolution depth/normal bilateral work with four 2-bit connectivities | Algorithmic reconstruction |
| 4 | Projected-radius clamp 32 | -0.50 to 0.30 ms | -19 to 12% | Same eight reads may stay closer in screen space, but the comprehensive two-frame smokes regressed by 0.070-0.466 ms versus Fixed 8; useful only when the radius actually exceeds the clamp often enough | Algorithmic; can omit distant occlusion |
| 5 | Packed depth-plus-normal 2x2 resolve | 0.06-0.27 ms | 2-10% | Better discontinuity coverage than depth-only edges with extra normal work in the trace | Algorithmic reconstruction |
| 6 | Packed slope-adjusted 2x2 resolve | 0.05-0.25 ms | 2-10% | Can reduce false edge rejection on sloped surfaces, with additional derivative arithmetic | Algorithmic reconstruction |
| 7 | Packed controlled-leakage 2x2 resolve | 0.04-0.24 ms | 2-9% | Adds a small denoise rule to the cheap packed-edge resolve; corner leakage requires visual acceptance | Algorithmic reconstruction |
| 8 | Projected-radius clamp 64 | -0.20 to 0.22 ms | -8 to 8% | Weaker locality cap with lower quality risk than 32 pixels; the comprehensive smokes regressed by 0.085-0.119 ms versus Fixed 8 | Algorithmic; can omit distant occlusion |
| 9 | Exact fixed-8 trace specialization | 0.04-0.20 ms | 2-8% | Removes runtime count, exponent, adaptive/feedback, and loop control, but produces 29,104 bytes of DXIL and still needs physical register/occupancy evidence | Exact at the same eight samples |
| 10 | Conservative FP32 filter algebra and precomputed weights | -0.30 to 0.18 ms | -12 to 7% | Compact-resolve DXIL has 6.4% fewer static IR instructions, 10.5% fewer branches, and 34.0% fewer unary sites, but the zero-warm-up two-frame smoke regressed by 0.223 ms complete-effect; controlled timing is required | Numerical FP32 reassociation/constant rounding |
| 11 | Exact fixed-12 trace specialization | 0.03-0.16 ms | 1-6% | Same control-flow removal as fixed 8, compared only with generic 12 at equal sample count | Exact at the same twelve samples |
| 12 | Exact fixed-16 trace specialization | 0.02-0.13 ms | 1-5% | Compile-time control savings are diluted by the larger equal-quality trace workload | Exact at the same sixteen samples |
| 13 | Packed current FAST delivery | 0.01-0.14 ms | <1-5% | Texture-load call sites fall from 14 to 11, or 21.4%, versus fixed-eight while retaining the current FAST values | Exact delivery for the fixed-8 candidate |
| 14 | Projected-radius clamp 128 | -0.12 to 0.14 ms | -5 to 5% | Conservative locality cap may rarely engage at radius 3; the comprehensive smokes regressed by 0.067-0.125 ms versus Fixed 8 | Algorithmic; can omit distant occlusion |
| 15 | Exact fixed-20 trace specialization | 0.01-0.11 ms | <1-4% | Compile-time control savings are a smaller fraction of twenty physical reads | Exact at the same twenty samples |
| 16 | Minimal AO/GI bindings and dummy-resource removal | 0.00-0.10 ms | 0-4% | Reduces descriptor and state surface but does not reduce eight physical depth reads; driver sensitivity is high | Exact candidate layouts |
| 17 | Exact 16x8 trace group | -0.08 to 0.14 ms | -3 to 5% | Occupancy and horizontal locality can improve or regress depending on Xe-LPG register allocation | Exact dispatch-shape experiment |
| 18 | Exact 8x16 trace group | -0.08 to 0.12 ms | -3 to 5% | Vertical footprint may interact differently with depth-cache lines; no work is removed | Exact dispatch-shape experiment |
| 19 | Separate packed-edge 4x4 resolve | -0.10 to 0.20 ms | -4 to 8% | Wider support performs sixteen source evaluations per full-resolution pixel; current DXIL emits 209 static loads and no gather | Algorithmic reconstruction |
| 20 | Fused packed-edge 4x4 | -0.18 to 0.25 ms | -7 to 10% | Avoids the intermediate yet performs the 4x4 work at full resolution, so one-pass structure can still lose | Algorithmic reconstruction/application |
| 21 | Activision 4x4-by-6 bitmask schedule | -0.04 to 0.05 ms | -2 to 2% | Changes deterministic schedule arithmetic and quality, not the number of depth reads | Algorithmic sampling schedule |
| 22 | Hierarchy-aware fixed shader variant | 0.00 ms at radius 3; 0.01-0.10 ms when hierarchy is already profitable | 0% at target radius | The requested radius 3 selects direct depth; at radius 8 or above the fixed shader preserves hierarchy use without a generic branch | Exact for the selected hierarchy mapping |
| 23 | Fixed later-bounce 8 and fixed all-bounce 8 | 0.00 ms for AO only; 0.02-0.25 ms forecast for active GI | 0% for target AO | AO-only has no later bounce; the specialization removes dynamic work only in GI/multi-bounce runs | Exact GI ownership order |
| 24 | Lazy PSO caching and conditional candidate resources | 0.00-0.03 ms steady state | 0-1% | Primarily protects startup, memory, and the zero-cost-off contract; it is not a substitute for GPU work reduction | Exact orchestration |
| 25 | Exact duplicate-pixel rejection Off control | 0.00 ms product saving; forecast 0.00-0.04 ms Off regression | Not Applicable | Measures the already-enabled duplicate skip; duplicate frequency is camera/radius dependent | Exact benchmark-only control |
| 26 | Exact full-mask early-exit Off control | 0.00 ms product saving; forecast 0.00-0.15 ms Off regression | Not Applicable | Measures data-dependent iterations avoided by the already-enabled safe exit | Exact benchmark-only control |
| 27 | Constant scheduler diagnostic | 0.00 ms product saving; forecast 0.01-0.12 ms diagnostic trace reduction | Not Applicable | Removes scheduler fetch/address work but produces deliberately structured samples | Diagnostic only |
| 28 | Eight- and twelve-read analytic-horizon controls | 0.00 ms product saving; trace delta forecast 0.05-0.40 ms | Not Applicable | Measures estimator overhead with a different visibility model and incomplete downstream match | Algorithmic benchmark-only controls |
| 29 | Constant/depth/bitmask trace, temporal copy, nearest/bilinear resolve, and composition floors | 0.00 ms product saving | Not Applicable | Isolates dispatch, texture, ALU, history, resolve, and application scopes rather than proposing product output | Diagnostic only |

The ranges are deliberately broader than the expected signal of several small
changes. A measured ordering may differ, especially for group shape and 4x4
reconstruction. The optimization ledger records every duplicated source-level
candidate and its individual disposition; this table ranks the distinct
implemented runtime families rather than counting the same fusion saving once
per supporting code change.

### Deferred High-Upside Forecast

These unimplemented candidates may exceed individual retained experiments, but
their ranges are lower-confidence design forecasts and are not included in the
implemented ranking. They remain non-additive and must not be read as promised
savings.

| Rank | Deferred Candidate | Forecast Saving | Percent Of 2.6 ms | Current Blocker |
| ---: | --- | ---: | ---: | --- |
| 1 | Apply AO inside deferred lighting and remove AO-only `BaseLighting` | 0.25-0.80 ms | 10-31% | Exact composition-order proof, AO-aware PBR PSO, and conditional target allocation are not implemented |
| 2 | Half-resolution spatial resolve followed by cheap full-resolution apply | 0.15-0.55 ms | 6-21% | Receiver identity, filter-quality acceptance, and a separate half-grid resolve resource/path are pending |
| 3 | Combined spatial plus temporal resolve | 0.00 ms for the primary temporal-off profile; 0.20-0.65 ms when temporal is enabled | 0% primary; 8-25% temporal workload | Exact support/bounds reuse and history-validation equivalence are not implemented |
| 4 | Gather/LDS/thread-coarsened reconstruction | -0.12 to 0.35 ms | -5 to 13% | Component ordering, edge dispatch, register/occupancy evidence, and target cache timing are pending |
| 5 | Conservative mixed-precision trace/filter blocks | -0.12 to 0.30 ms | -5 to 12% | Intel physical-width, conversion, sector-boundary, ownership, and image evidence are pending |

Full XeGTAO and complete Activision PS4 controls are omitted from this product-
saving table: they are diagnostic comparison pipelines with different estimators,
not candidate replacements for UVSR's visibility-bitmask product path.

The compiler figures above come from the reproducible
[visibility DXIL evidence](visibility-dxil-evidence.md). They are optimized
static IR call-site counts, not dynamic instructions, target timings, or Intel
physical register/occupancy data.

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

**Packed Current FAST** is an exact fixed-8 delivery permutation, not a new
noise objective. It pre-packs the four stochastic dimensions needed by the
even-count shader into one RGBA8 texel so the trace does not issue separate
scalar semantic reads. The packed texture is allocated and uploaded only while
that profile is active. Multi-bounce metadata remains available for the AO+GI
fixed-8 profile.

**Activision 4x4-by-6 Schedule** is an optional algorithmic bitmask profile. It
uses the source deck's 4x4 spatial direction index, six temporal directions,
four spatial radial offsets, and four temporal radial offsets. UVSR's slice is
pi-periodic, so the documented degree schedule is mapped into that domain. The
profile changes sample locations but retains the 32-sector finite-thickness
estimator; the twelve-read PS4 control changes to the separate analytic-horizon
estimator and is labeled partial because its downstream pipeline remains UVSR.

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

Candidate AO-only reconstruction profiles keep the raw AO in R16F and write
one separate R8_UINT edge byte at trace resolution. The byte stores left,
right, top, and bottom connectivity in four 2-bit fields. Depth-only,
depth-plus-normal, and slope-adjusted producers generate it in the trace;
reconstruction enforces symmetric neighbor connectivity. The controlled-
leakage mode deliberately relaxes isolated connections and therefore requires
separate visual acceptance. The 2x2 and 4x4 filters are algorithmic
replacements for continuous bilateral weights, not exact filter rewrites.

**Exact Fused Resolve And Apply** performs the existing compact 2x2 reduced-
resolution resolve, explicitly reproduces the R16F intermediate round trip,
then applies the existing AO composition equations in the same full-resolution
dispatch. It does not allocate `FinalAmbientVisibility`, dispatch the separate
filter, or read the intermediate during composition. Fused packed-edge 2x2 and
4x4 profiles use the same resource omission with their algorithmic weights.
Every fused mode currently requires AO-only, reduced resolution, and spatial
filtering disabled; incompatible selections fail validation rather than
silently dropping an enabled filter.

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

Fixed 8/12/16/20 shaders have direct-device-depth and hierarchy-aware compile-
time variants. The hierarchy variant retains the reference distance thresholds
and receiver-surface contract; it does not convert jittered sample distances
into an inexact precomputed LOD table. At the requested radius 3 target the
hierarchy, its bindings, and its preparation dispatch remain absent, so this
variant has no primary-profile performance effect.

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

The collapsed **Statistics** drawer reports:

- **Envelope:** one outer timestamp spanning the complete visibility effect.
- **Summed:** the sum of nonoverlapping named stages for the same originating
  frame.
- **Signed Residual:** envelope minus summed stages. A small negative value can
  occur from independent timestamp quantization; it is not clamped or hidden.
- **Trace:** depth preparation plus first and aggregate later traversal.
- **Later Bounce 2/3/4:** nested breakdowns that are excluded from Summed so
  later traversal is not counted twice.
- **Filter, Apply, and Composite:** spatial/required resolve, full-resolution
  application, and general lighting composition as independent scopes.

The benchmark collector correlates delayed query results by originating frame,
discards incomplete frames from distributions, warms for 120 frames by
default, and records medians and p95 values from at least 240 complete frames
for a controlled run. **Producer Subtotal** is depth, first/later trace,
temporal, and spatial resolve; a fused run also attributes its inseparable
full-resolution apply to the producer mask. Complete effect is the outer
envelope rather than a sum of stage medians.

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

## Benchmark Export and Command Line

**Benchmark Current Profile** locks Benchmark Position 1, resets history,
collects one isolated profile, and automatically exports a schema-v2 JSON
summary, raw per-frame CSV, and the last measured frame. The JSON includes
profile and permutation identity, shader-only key, adapter, available clock
state, scene, camera preset, API, build identity, output dimensions, run window,
stage masks, ingestion diagnostics, every stage distribution, producer
subtotal, summed stages, signed residual, complete envelope, and controlled/
smoke classification. Clock telemetry is retained as an explicit unavailable
string when the renderer cannot query it; it is never invented.

The run-settings object is both human-readable and canonically FNV-hashed. It
serializes output and trace dimensions; AO/GI enablement, strengths, bounce
cutoff, intensity, and emissive controls; estimator, min/max counts, adaptive
state/strength, radius, thickness, exponent, and scheduler; implementation,
specializations, noise, math, precision, temporal, reconstruction, application,
depth, and binding modes; temporal/spatial settings; formats; group size; exact
first-trace and peak-per-pass descriptors; dispatch count; and logical resource
bytes. The opaque pipeline/history keys remain separate compiled and history
identities rather than substitutes for those values.

The command-line surface is:

```text
--visibility-profile <display-name-or-hyphenated-name>
--visibility-benchmark
--benchmark-sequence <reference-versus-current|fixed-sample|noise|reconstruction|math|all|precision>
--benchmark-warmup <0..100000>
--benchmark-frames <1..100000>
--benchmark-output <directory>
--benchmark-auto-close
```

Invalid or unavailable profiles and invalid counts print a command-line error
to standard error and return a nonzero exit code. They do not create modal
message boxes. The Reference-versus-current, fixed-count, noise,
reconstruction, and math actions are sequential one-at-a-time runners: each
entry applies explicit settings, resets history, captures and exports its own
artifact set, and restores the exact starting settings after completion,
cancellation, or failure. The Precision action is intentionally unavailable
because the renderer has no honest non-reference mixed-precision trace; it does
not relabel the conservative FP32 filter as precision evidence.

## Current Smoke Evidence

The pre-commit Release smoke used the Intel Arc adapter on the target Core Ultra
9 185H system, 1920x1080 output, Benchmark Position 1, zero warm-up frames, and
two measured frames per isolated entry. All exports correctly label themselves
`smoke`, set `controlled_protocol_valid` to false, and report unavailable clock
telemetry. The binary still embedded checkpoint `3339505` while the task tree was
dirty, so these results establish runnable feature coverage and directional
sanity only; they are not controlled performance claims for the final commit.

| Sequence | Coverage | Directional Observation |
| --- | ---: | --- |
| Fixed Sample | 5/5 entries; 10/10 complete frames | Fixed 8 was below generic 8 in both smoke runs, but the observed complete-effect delta varied from about 2% to 28%; specialization is also bundled with minimal bindings, so attribution is not isolated |
| Noise | 6/6 entries; 12/12 complete frames | Packed FAST was about 5% lower in trace than scalar FAST in the successful retry but had regressed in the first incomplete run; no stable win is claimed |
| Reconstruction | 12/12 entries; 24/24 complete frames | Exact fusion saved 0.140 ms and fused packed-edge 2x2 saved 0.213 ms versus the 2.711 ms compact reference; separate/fused 4x4 rose to about 4.70 ms and are likely slower on this workload |
| Math | 2/2 entries; 4/4 complete frames | Conservative FP32 rose from 2.714 to 2.937 ms, a 0.223 ms smoke regression; retain as an experiment pending the controlled run |
| All Implemented | 53/53 entries; 106/106 complete frames | Every selectable implemented or partial-control profile ran, including generic Reference GI-only/AO+GI/multi-bounce, packed-FAST AO+GI, fixed 8/12/16/20 AO/GI-only/AO+GI, later-bounce/multi-bounce, three temporal responses, diagnostics, group shapes, radius clamps, horizon controls, and fused debug output |

Every successful sequence exported matching JSON, CSV, and final-frame BMP sets
with empty stderr and zero incomplete frames. Reference versus Fixed 8 final-frame
comparison found no gross corruption (35.0 dB PSNR and 2.35/255 RGB MAE), but the
captures used different stochastic frame phases; exact GPU-image equivalence
therefore remains unproven. The 589,824-case CPU suite supplies the stronger
fixed-order/mask/AO/GI ownership proof without replacing final image review.

The comprehensive smokes also keep the 16x8/8x16 group shapes and all three
radius clamps in the possible-regression category. The PS4-schedule and UVSR
horizon controls showed conspicuous structured noise/checkerboarding and darker
sampled means than Reference, so they remain diagnostic and quality-gated rather
than comparable-quality performance candidates.

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

For the performance profiles, focused checks cover plan validity, reference
zero-cost-off masks, fixed sample/order ownership, packed FAST delivery, packed
edge bytes and receiver mapping, delayed frame correlation, envelope/sum/
residual accounting, export structure, and reproducible optimized-DXIL
comparison. Final aggregate Release results, one-at-a-time runtime smoke
results, and gross final-frame comparisons are now recorded above. Exact same-
phase GPU image equivalence, Intel native ISA/register/occupancy counters, and
the controlled target timing table remain pending; documentation does not
substitute forecasts, two-frame smoke, or static IR for those fields.
