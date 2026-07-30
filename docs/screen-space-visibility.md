# UVSR Screen-Space Visibility and Indirect Lighting

## Current Pipeline

UVSR owns one deferred screen-space visibility producer for ambient occlusion
(AO) and diffuse global illumination (GI). AO and GI share depth traversal and
one register-local 32-bit directional mask. The production path does not write
that mask to memory.

The frame order is:

1. Fill the PBR G-buffer, including motion when temporal visibility requires
   it.
2. Shade direct light and, when GI can consume it, write shadowed direct
   diffuse, directly reflected environment diffuse, emission, and compact
   diffuse-transport metadata.
3. Resolve the CPU-side Reference or curated performance profile, allocate only
   its resources, and trace visibility at full, half, or quarter linear
   resolution.
4. Optionally joint-bilateral or edge-guided reconstruct to full resolution;
   reduced output always receives at least a minimal guide-aware resolve.
5. Composite diffuse and specular IBL from the selected global radiance source,
   AO, screen-space GI, and direct/emissive lighting, or use an explicitly
   selected compatible fused resolve/application profile.
6. Apply the normal AgX display pipeline.

The General drawer provides PBR diagnostics for decoded normals, diffuse IBL,
prefiltered specular radiance, the environment BRDF, final and combined IBL,
specular occlusion, and selected environment mip. **World Materials > Indirect
Diffuse Response** separately shows the final material-applied screen-space
diffuse GI contribution. Visibility stage timings, logical allocation
arithmetic, and external capture tools remain the performance diagnostics.
Diagnostic isolation profiles are no longer packaged or selectable; their
historical measurements remain in the optimization ledger.

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
- High quality traces 20 samples through the compact Runtime loop on one slice
  per eligible pixel.
- Uniform Solid Angle and Toroidal Blue are selected.
- Sampling uses one per-pixel budget shared by AO and every GI bounce.
  Adaptive sparse sampling has been removed.
- AO strength and power are both 1.0. GI intensity is 4.0 with
  **Limit Bounces** enabled at one bounce and a 0.001 contribution cutoff.
  The identity AO Power value selects a compositor with `pow` compiled out.
  The limited one-bounce path dispatches no continuation-control work.
  **Include Emissive Sources** is enabled and **Emissive Source Gain** is 4.0.
  Both remain explicit controls; the unit radiometric reference is 1.0.
- Global diffuse and specular IBL start enabled with **Day - Kloppenheim 03**
  at its calibrated `-2.75 EV` and `1.00` strength. The matching visible
  background also starts enabled.
- The Indirect Diffuse Response view is disabled.
- Visibility temporal accumulation is disabled and has no settings surface;
  renderer TAA owns temporal stability. Spatial filtering is disabled and its
  dormant Gaussian joint-bilateral radius remains 4.0.

## Performance Profiles and Controls

The Visibility panel owns the profile selector and normal AO/GI settings.
Detailed plan, resource, traffic, timing, and benchmark information lives in
the Statistics drawer rather than occupying the main control surface.
The panel is a compact scrollable control surface modeled on the existing AA
panel. Full-width dropdowns expose Sampling Resolution and Profile.
**Shared Visibility Sampling** owns Estimator, Noise Pattern, Samples, Radius,
Thickness, and Distribution. Noise Pattern contains Independent Hash and
Toroidal Blue. Samples always means the 1-64 Runtime implementation; there is
no Sample Count Mode or Visibility Developer Options panel.
**Buffers** is a separate sibling drawer immediately below the visibility
drawer. Its top **Preset** dropdown changes only raw, cumulative, final, and
depth-hierarchy formats. Low, Medium, and High begin with Performance Precision;
Ultra begins with Default Precision. Benchmark controls are in the unified
Statistics drawer. Output and scene locations are opened through folder buttons,
so long filesystem paths do not displace the settings a person is comparing.
Changing a history-affecting profile value changes the displayed history key
and resets temporal history. The other advanced controls remain
separate:

- **Buffers** begins with **Performance Precision**, **Default
  Precision**, **Compact AO**, and **Compact GI** presets, then provides
  vertically labeled **Trace AO**, **Current Bounce GI**, **Accumulated GI**,
  **Output AO**, **Output GI**, and **Long-Range Depth** dropdowns. Actual
  formats and conditional resource cost are reported with the controls. A
  preset or individual format change preserves every non-buffer choice and
  changes the product profile label to its originating preset plus **(Custom)**,
  such as **Medium (Custom)**.
- **Dispatch, Memory & Cache** reports thread-group and sample specialization,
  depth mode, minimal bindings, lazy pipeline selection, resource counts, and
  traffic. Runtime keeps one depth-hierarchy SRV layout and selects direct or
  hierarchy-aware sampling through a workload-coherent uniform branch. The same
  shader family also selects Independent Hash or Toroidal Blue and evaluates
  the runtime Distribution exponent.

The displayed resource and pass masks are authoritative plan identities, while
the binding mask is a conceptual effect-wide union rather than a literal
simultaneous binding layout. The byte counters are logical texel arithmetic.
**First-Trace SRVs** and
**First-Trace UAVs** are exact counts for the selected first-trace binding
layout, including the deliberately broad Reference layout and the minimal
candidate layouts. **Peak SRVs** and **Peak UAVs** are the maximum
simultaneously bound by any one selected visibility pass, including depth,
later bounce, temporal, reconstruction, fused application, and composition.
They do not sum descriptors across passes or enumerate every layout; use DXIL
reflection or a GPU capture when that whole-effect inventory is required.

Reference is a benchmark identity over the guarded Runtime trace and legacy
separate composition. Runtime uses that same 1-64 implementation and selects
the even or odd parity specialization only for its narrow validated AO+GI hot
topology. Neither identity creates an Offline texture or fixed-count shader.
Curated reconstruction and fusion candidates are created lazily. The shader
cache key contains only compiled topology; scheduler, radial exponent, direct
versus hierarchy depth, output size, and radius remain in separate evidence and
history identities without duplicating a PSO.

The internal benchmark profile status is:

| Benchmark Profile | Status | Exact Scope Or Boundary |
| --- | --- | --- |
| Reference AO 8T | Implemented | Guarded Runtime bitmask reference at the target workload |
| Runtime AO 8T | Implemented | Runtime bitmask with the same eight-sample workload |
| Packed-Edge AO 8T | Implemented | R16F raw AO plus a separate R8_UINT edge-guided resolve |
| Reference AO+GI 8T | Implemented | Guarded shared Runtime AO/GI traversal |
| Runtime AO+GI 8T | Implemented | Trusted-even first-bounce contract |
| Runtime AO+GI 12T | Implemented | Trusted-even first-bounce contract |
| Runtime AO+GI 16T | Implemented | Trusted-even first-bounce contract |
| Runtime Multi-Bounce | Implemented | Guarded later-bounce Runtime traversal |

The Samples slider controls the total shared by the first trace and every GI
bounce; there is no separate later-bounce count. Runtime exposes the complete
1-64 range. Distribution remains a runtime value in **Shared Visibility
Sampling**, immediately after Thickness.
The custom implementation
selector additionally exposes exact fused resolve/application;
**Depth-Guided Reconstruction**, **Depth-Normal Reconstruction**,
**Slope-Aware Reconstruction**, **Leakage-Limited Reconstruction**, and fused
depth-normal reconstruction/application. Neutral or regressing thread groups,
radius clamps, conservative filter algebra, feature-off controls, algorithm
ceilings, unavailable verification presets, and diagnostic floors were removed
from the UI, planner, shader package, and tests. The safe duplicate-pixel and
full-mask shortcuts remain always enabled in retained shaders without adding
investigation controls.
Unsupported combinations fail profile validation rather than silently running a
partially matching candidate. Composable UVSR optimizations preserve compatible
noise, estimator, AO/GI, spatial, format, and math choices instead of
being cleared by unrelated edits. Labels explicitly identify the remaining
`(Mutex GI)` fused-application constraint.
The unified **Profile** dropdown beneath **Sampling Resolution** exposes four
product presets:

| Preset | Resolution | Runtime Samples | Precision | GI Bounces | Reconstruction |
| --- | --- | --- | --- | ---: | --- |
| Low | Quarter | 8 | Performance | 1 | Compact joint-bilateral upsampling |
| Medium | Half | 8 | Performance | 1 | Compact joint-bilateral upsampling |
| High | Full | 20 | Performance | 1 | Unreconstructed full-resolution input |
| Ultra | Full | 48 | Default | 2 | Unreconstructed full-resolution input |

High is the factory default and matches the current launch/reference state.
Low uses Uniform Projected Angle; Medium, High, and Ultra use Uniform Solid
Angle. All four use Toroidal Blue, radius 3, thickness 0.5,
radial exponent 2, AO strength/power 1, and GI intensity 4. Low and Medium use
Performance Precision buffers; High also uses Performance Precision, while
Ultra uses full-precision Default Precision buffers. Any later advanced
visibility or buffer edit remains active
and changes the profile label to its originating preset plus **(Custom)**. The Profile dropdown
contains no implementation-profile presets. The remaining `(Mutex GI)`
constraint belongs only to fused final-application shaders: those shaders
directly write the AO-modulated lighting target and do not preserve the
separate GI reconstruction/composition ordering.
Runtime retains a dynamic count from the constant buffer. It uses a guarded
loop for the general topology and CPU-validated even or odd compact
specializations for the supported High-style AO+GI topology, retaining every
1-64 slider value. Scheduler, depth-hierarchy selection, and Distribution are
workload-coherent uniform branches within those shaders. No count-specific
shader or nearest-count substitution remains.
Ordinary quality, sampling resolution, estimator, AO, GI, denoiser, or buffer-
format edits preserve compatible composable optimization identities. No
removed source-port or diagnostic label can be selected.

### Source-Port Comparison Pipelines

The former **Activision PS4 GTAO Approximation** was a source-informed
experiment, not an exact port: Activision did not publish the shipping source,
all constants, or a complete reconstruction contract. The user subsequently
removed its 4x4-by-6 scheduler, scalar and packed-gather reconstruction,
prepared-depth surface, temporal pass, profiles, shaders, and test fixtures.
Historical measurements and source boundaries remain in the optimization
ledger so the negative result is still auditable.

The independent **Activision Horizon Control** was also removed. It was useful
as an attribution ceiling, but it changes the visibility estimator and was not
a production UVSR optimization.

The public Activision record has two distinct scopes. The
[SIGGRAPH 2016 slide deck](https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf)
discloses the eight-tap PS4 workload, 4x4 spatial schedule, six temporal phases,
and coupled spatial/temporal reconstruction that informed the removed experiment.
The expanded
[2019 technical memo `ATVI-TR-19-01`](https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf)
provides the fuller analytical derivation, discusses one, 16, and 96 effective
sample directions after spatial and temporal reuse, and reports baseline GTAO
plus GI at 0.5 ms on PS4 at 1080p with a standard half-resolution occlusion
buffer. It does not publish a replacement shipping shader, exact tap positions,
or every reconstruction constant. UVSR therefore did not present the removed
2016 eight-tap approximation as a direct port of the 2019 report.

#### Removed XeGTAO Historical Evidence

The removed **Intel XeGTAO 1.30 High Source Port** was pinned to MIT-licensed upstream
commit [`a5b1686c7ea37788eeb3576b5be47f7c03db532c`](https://github.com/GameTechDev/XeGTAO/tree/a5b1686c7ea37788eeb3576b5be47f7c03db532c).
For finite perspective inputs it retained Intel's five-mip smart depth prefilter,
High preset of three slices by three steps by two sides, Hilbert/R2 sequence,
horizon integral and falloffs, packed edges, and sharp denoiser tap order. The
LUT/mixed-precision profile was the closest practical port; inline Hilbert
isolated LUT traffic/ALU, and LUT/FP32 isolated precision.

UVSR integration prevented bit-identical output. The adapter transformed UVSR's
world-space float normals to view space instead of consuming packed view-space
normals; preserves Intel's 96-byte constants prefix inside a 176-byte UVSR
buffer; clamps padded 16x16 depth-hierarchy edges while logical reconstruction
uses the unpadded size; uses R16F depth; stores AO in R16F while explicitly
emulating Intel's R8 rounding; binds the LUT at UVSR's slot; uses a static noise
phase because this path does not feed matching TAA history; omits bent normals
and depth-derived normal generation; and adds finite/bounds/degenerate guards.
The removed XeGTAO path required a perspective camera,
viewport origin `(0, 0)`, and viewport dimensions exactly equal to the depth-
texture extent. An orthographic, offset, cropped, or mismatched view reports a
clear reason and runs Reference rather than attempting a partial source port.

Intel's pinned README reports the complete XeGTAO effect at about 0.56 ms for
1080p High on RTX 2060, 1.4 ms for 4K High on RTX 3070, and 2.39 ms for 1080p
High on an i7-1195G7 integrated GPU; its same-system 1080p RTX 2060 comparison
lists ASSAO Medium at about 0.72 ms. It also reports 1080p GTX 1050 High and
Medium at about 2.2 and 1.5 ms, Low at roughly two-thirds of Medium, and a
5-20% gain from optional FP16 math on hardware where it helps; the same source
warns that FP16 can regress on some GPU/driver combinations. These are
[upstream measurements](https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/README.md),
not current UVSR timings, not Xe-LPG forecasts, and not directly comparable with UVSR's
different normal input, R16 AO adapter, composition scope, or target workload.

All controlled Intel XeGTAO variants were slower than canonical Reference.
Consequently, the XeGTAO profiles, shaders, resources, UI, benchmark sequences,
and fixtures were removed. This section remains only as rejection evidence.

## Historical Performance Evidence

This section preserves the forecasts and measurements that informed earlier
candidate work. Fixed-count and Offline rows are historical evidence, not
current profiles or restoration recommendations; the continuous shader-path
retirement postmortem owns the current keep/remove decision. The following
values were engineering forecasts, not measurements. They ranked the then-
implemented candidate families by plausible complete-effect impact and
maximum credible upside before controlled testing
for the requested 1920x1080, half-resolution, AO-only, fixed-8 workload on
Intel Xe-LPG. Milliseconds are relative to the user-reported 2.5-2.7 ms
baseline; percentages use its 2.6 ms midpoint. Positive numbers mean a saving,
negative numbers mean a possible regression. Rank therefore reflects potential
impact, not a claim that the midpoint is positive. Ranges overlap, every entry is
non-additive, and target retention requires a controlled Core Ultra 9 185H run
plus image review. The completed RTX 4090 Laptop measurement below does not
rewrite these Xe-LPG forecasts; it supplies adapter-scoped keep/drop evidence.

### Historical Forecast Finalist Ranking

This short list ranks the largest expected complete-effect deltas among the
implemented finalists and the new direct comparisons. A positive saving is the
named baseline minus the candidate; overlapping rows must not be added.

| Rank | Implemented Finalist Or Comparison | Forecast Saving | Percent Of 2.6 ms | Interpretation |
| ---: | --- | ---: | ---: | --- |
| 1 | Exact Fixed 8 plus fused resolve/apply versus canonical Reference | 0.20-0.60 ms | 8-23% | Strongest same-estimator finalist; controlled Intel measurements saved 20.6-22.4% |
| 2 | Exact fused 2x2 resolve/apply versus separate resolve/composition | 0.15-0.45 ms | 6-17% | Removes one full-resolution R16F round trip and dispatch; controlled Intel measurements saved 17.7-18.8% |
| 3 | Fused packed-edge 2x2 versus separate compact resolve/composition | 0.12-0.40 ms | 5-15% | Similar traffic win with algorithmic edge weights and a required image-quality gate |
| 4 | Exact fixed-8 trace versus generic eight-sample trace | 0.04-0.20 ms | 2-8% | Controlled Intel measurements saved 1.9-4.7%; the fused combination remains more conclusive |

### Historical Detailed Bitmask Candidate Ranking

| Rank | Implemented Candidate | Forecast Saving | Baseline Share | Why This Range Is Plausible | Quality Classification |
| ---: | --- | ---: | ---: | --- | --- |
| 1 | Exact fused 2x2 resolve and AO application | 0.15-0.45 ms | 6-17% | Removes one 1080p R16F round trip and dispatch; DXIL drops the separate pair's 50 static loads/four stores to 20 loads/one store | Exact with explicit R16F round-trip emulation |
| 2 | Fused packed-edge 2x2 | 0.12-0.40 ms | 5-15% | Combines fusion with cheaper edge connectivity, but pays the trace-resolution R8 edge write/read | Algorithmic reconstruction |
| 3 | Packed depth-edge 2x2 resolve | 0.08-0.30 ms | 3-12% | Replaces repeated full-resolution depth/normal bilateral work with four 2-bit connectivities | Algorithmic reconstruction |
| 4 | Packed depth-plus-normal 2x2 resolve | 0.06-0.27 ms | 2-10% | Better discontinuity coverage than depth-only edges with extra normal work in the trace | Algorithmic reconstruction |
| 5 | Packed slope-adjusted 2x2 resolve | 0.05-0.25 ms | 2-10% | Can reduce false edge rejection on sloped surfaces, with additional derivative arithmetic | Algorithmic reconstruction |
| 6 | Packed controlled-leakage 2x2 resolve | 0.04-0.24 ms | 2-9% | Adds a small denoise rule to the cheap packed-edge resolve; corner leakage requires visual acceptance | Algorithmic reconstruction |
| 7 | Exact fixed-8 trace specialization | 0.04-0.20 ms | 2-8% forecast | Controlled local trace medians were both 0.101376 ms and complete median regressed 0.926%; retain only as a benchmark/component of the fused finalist | Exact at the same eight samples |
| 8 | Exact fixed-12 trace specialization | 0.03-0.16 ms | 1-6% | Same control-flow removal as fixed 8, compared only with generic 12 at equal sample count | Exact at the same twelve samples |
| 9 | Exact fixed-16 trace specialization | 0.02-0.13 ms | 1-5% | Compile-time control savings are diluted by the larger equal-quality trace workload | Exact at the same sixteen samples |
| 10 | Packed current FAST delivery | 0.01-0.14 ms | <1-5% | Texture-load call sites fall from 14 to 11, or 21.4%, versus fixed-eight while retaining the current FAST values | Exact delivery for the fixed-8 candidate |
| 11 | Exact fixed-20 trace specialization | 0.01-0.11 ms | <1-4% | Compile-time control savings are a smaller fraction of twenty physical reads | Exact at the same twenty samples |
| 12 | Minimal AO/GI bindings and dummy-resource removal | 0.00-0.10 ms | 0-4% | Reduces descriptor and state surface but does not reduce eight physical depth reads; driver sensitivity is high | Exact candidate layouts |
| 13 | Hierarchy-aware fixed shader variant | 0.00 ms at radius 3; 0.01-0.10 ms when hierarchy is already profitable | 0% at target radius | The requested radius 3 selects direct depth; at radius 8 or above the fixed shader preserves hierarchy use without a generic branch | Exact for the selected hierarchy mapping |
| 14 | Fixed later-bounce 8 and fixed all-bounce 8 | 0.00 ms for AO only; 0.02-0.25 ms forecast for active GI | 0% for target AO | AO-only has no later bounce; the specialization removes dynamic work only in GI/multi-bounce runs | Exact GI ownership order |
| 15 | Lazy PSO caching and conditional candidate resources | 0.00-0.03 ms steady state | 0-1% | Primarily protects startup, memory, and the zero-cost-off contract; it is not a substitute for GPU work reduction | Exact orchestration |

The ranges are deliberately broader than the expected signal of several small
changes. A measured ordering may differ, especially for reconstruction
variants. The optimization ledger records every duplicated source-level
candidate and its individual disposition; this table ranks the distinct
retained runtime families rather than counting the same fusion saving once per
supporting code change. Removed experiments remain only in the ledger's
historical evidence and disposition table.

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

The removed XeGTAO source port and removed Activision PS4 approximation are omitted from this
bitmask product-saving table because they are algorithmic comparison pipelines with different
estimators, not drop-in exact replacements for UVSR's visibility-bitmask product
path. XeGTAO rejection evidence remains in the optimization ledger.

### Current Production Guidance

Keep the fused resolve/application candidates for manual image validation. On
Intel, fusion saved 17.7-18.8% across controlled repeats. The combined
Fixed-8-plus-fusion result saved 20.6-22.4%, but the isolated Fixed-8 result was
small and inconsistent; the count-specific family was therefore retired while
Runtime kept every 1-64 sample count.

Keep 16-bit storage defaults. Final GI `RGBA16_FLOAT` saved 8.39% versus
`RGBA32_FLOAT`, and the `R16_FLOAT` depth hierarchy saved 3.88% versus
`R32_FLOAT`. Raw/cumulative GI and final AO had smaller wins. Treat
adaptive-feedback, temporal-GI, and temporal-depth differences as noise because
their median and p95 directions were inconsistent.

Do not promote approximate math modes: the 24-profile controlled matrix kept
nearly every exact/standard/approximation result within +/-1%. The per-function
controls and their benchmark/shader surface were removed; Reference and curated
shaders compile static default math with no runtime mode branch.

The Activision PS4 GTAO approximation was removed after its scalar/packed
ordering changed across repeats and both remained slower than Reference. The
UVSR horizon attribution control was also removed because it changes the
estimator rather than optimizing the retained UVSR result. Remove XeGTAO because
every profile was slower than Reference on Intel.
Packed-edge 4x4 was also removed: its DXIL contained 209 static loads and no
gather, and runtime was substantially slower. Diagnostic floors, feature-off
controls, neutral group shapes, regressing radius clamps, and conservative
filter algebra are historical attribution evidence, not current settings.

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

**Samples** is the scheduled radial sample budget on one
stochastic slice, not a budget per radial direction. Full-mask early
termination, invalid projection, and duplicate screen coordinates can make the
executed depth-read count lower than the selected budget. The selected total is
divided between the two near-to-far radial directions. AO and every diffuse GI
bounce use the same selected total. **Limit Bounces** keeps the explicit
one-through-eight count active and is on by default. With the limit disabled,
the renderer records up to 16 possible bounces as a fault guard, raises the
contribution threshold by four times per bounce, and uses a GPU-written
indirect argument to turn all full-screen passes after convergence into
zero-group dispatches without a CPU readback.

Each radial direction owns a 32-stratum bit-reversal sequence. The complete
selected prefix receives a stochastic toroidal stratum rotation plus an
independent within-stratum offset every pixel and frame. Increasing a sample
count with the same phase appends strata without moving its lower-budget prefix,
while changing phase prevents the same global radius shells from accumulating
into rings. The rotated set is consumed in ascending physical-stratum order.
Nesting therefore controls set membership without letting a farther GI sample
claim a sector before a nearer selected source on the same radial direction. The
**Distribution** transforms each normalized stratum by
`x^exponent`; the default `x^2` concentrates depth taps near the receiver. This
means:

- lower the sample count to measure the trace-cost response;
- raise the sample count to give every eligible pixel more evidence;
- tune the exponent separately, because it redistributes distance rather than
  changing tap count; and
- compare scheduler modes with identical sample counts.

Every eligible pixel receives the selected **Samples** budget on one slice; the
default is Runtime 20. Runtime uses one guarded dynamic shader over the complete
1-64 range, plus CPU-selected even and odd compact variants for the supported
High-style AO+GI topology. The shaders contain no adaptive depth/normal neighborhood
analysis, adaptive motion/reprojection reads, feedback reads or writes, or
stochastic budget rounding. The sample scheduler remains independent because
it determines where samples land, not how many samples a pixel receives.
Distribution is directly available with the other Shared Visibility Sampling
controls.

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
odd-sample side, both radial directions, and auxiliary stochastic decisions
receive separate semantic layers rather than translated copies of one scalar
texture. Dimension-specific toroidal temporal steps preserve each layer's
spatial spectrum, and hashed cycle offsets prevent exact 64-frame repetition.
It is spatiotemporal as a runtime sequence, but its eight 2D layers were not
jointly optimized as one 3D space-time volume.
This is the default scheduler.

The removed unpacked and packed Offline modes no longer have assets, upload
state, resources, bindings, UI choices, or shader families. They coupled a
fixed filter objective and, for packed delivery, a fixed-eight topology to the
sampling implementation. The continuous shader-path retirement postmortem
preserves the evidence and restoration boundary.

The removed Activision 4x4-by-6 schedule and analytic-horizon attribution
control likewise have no profiles or compiled permutations. Both retained
schedulers drive the standard UVSR bitmask estimator through a frame-coherent
uniform branch.

The design follows the rejection-safe and toroidal-sequence guidance in
NVIDIA's
[Rendering in Real Time With Spatiotemporal Blue Noise Textures, Part 2](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-2/).
Toroidal remains UVSR's procedurally generated option and does not claim to
reproduce NVIDIA's precomputed 3D STBN volumes. Its eight `R16_UNORM` layers
consume 64 KiB of logical scheduler storage.

The scheduler changes where and when samples appear; it does not change the
nested radial distribution or the requested sample count. Profile all modes at
identical sample counts. Human evaluation should look for structured banding,
stationary grain, motion trails, and convergence after disocclusion.

## Spatial Reconstruction

Visibility-owned temporal accumulation is disabled and its settings drawer,
diagnostic profile, and source-specific temporal permutation have been removed.
Renderer TAA owns temporal stability in the current build. The
**Reconstruction Method** dropdown explicitly identifies this bypass as
**Unreconstructed Full Resolution Input**. In that mode UVSR composites the
current AO/GI output directly, because no upsampling is required and filtering
is optional.

Half and quarter resolution always require a guide-aware mapping from the trace
grid to the full-resolution destination. **Guide-Aware Upsampling** selects the
minimal depth/normal-guided reconstruction without optional denoising. The
joint-bilateral choices denoise while reconstructing: the compact path
uses its guided 2x2 gather, while the Gaussian path uses its parity-varied
four-tap reduced-resolution subset.

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

Intel edge-guided reconstruction profiles keep the raw AO in R16F and write
one separate R8_UINT edge byte at trace resolution. The byte stores left,
right, top, and bottom connectivity in four 2-bit fields. Depth-only,
depth-plus-normal, and slope-adjusted producers generate it in the trace;
reconstruction enforces symmetric neighbor connectivity. The controlled-
leakage mode deliberately relaxes isolated connections and therefore requires
separate visual acceptance. The retained 2x2 filter is an algorithmic
replacement for continuous bilateral weights, not an exact filter rewrite.

**Exact Fused Resolve And Apply** performs the existing compact 2x2 reduced-
resolution resolve, explicitly reproduces the R16F intermediate round trip,
then applies the existing AO composition equations in the same full-resolution
dispatch. It does not allocate `FinalAmbientVisibility`, dispatch the separate
filter, or read the intermediate during composition. The fused packed-edge 2x2
profile uses the same resource omission with its algorithmic weights.
Every fused mode currently requires AO-only, reduced resolution, and spatial
filtering disabled; incompatible selections fail validation rather than
silently dropping an enabled filter.

The spatial filter structure was informed by
[cdrinmatane/SSRT3](https://github.com/cdrinmatane/SSRT3), MIT license.

## Resolution and Cost

Full, half, and quarter are linear resolution scales, so the nominal sampling
pixel counts are approximately `1`, `1/4`, and `1/16` of full resolution before
edge rounding. This is not a predicted end-to-end speedup: the full-resolution
G-buffer, temporal guides, upsampling/filtering, and composition remain, while
actual trace cost depends on the sample budget, bounce count, divergence, and
hardware occupancy.

For AO-only radii of at least eight world units on perspective cameras, UVSR
automatically builds a five-level XeGTAO-style smart-average view-depth
hierarchy and uses coarser depth for distant taps. GI stays on exact depth so a
coarse geometry sample cannot be paired with unrelated full-resolution source
radiance.

Runtime declares one hierarchy SRV layout and uses a workload-uniform branch
between direct device depth and the hierarchy. The hierarchy path retains the
reference distance thresholds and receiver-surface contract; it does not
convert jittered sample distances into an inexact precomputed LOD table. At the
requested radius 3 target, the hierarchy resource and preparation dispatch
remain absent, so this branch has no primary-profile preparation cost.

Source activity and output allocation are consumer driven. AO-only does not
allocate GI outputs or the full-resolution source-radiance target; GI-only does
not allocate AO outputs. AO strength zero and GI intensity zero make their
respective effects inactive consumers rather than dispatching mathematically
zero paths. Active GI retains its source-radiance target only while direct-light,
diffuse-environment, or enabled positive-gain authored emissive sources are
active. Temporal history, full-resolution filtered outputs, higher-bounce
frontiers, and the depth hierarchy exist only while their consumers require
them. Proven
scene-wide first-bounce inactivity terminates the complete higher-bounce
dispatch chain.

## HUD Statistics

The collapsed **Statistics** drawer starts with a selector for **Complete
Renderer**, **Geometry**, **Direct Lighting**, **Screen-Space
Visibility**, **Material Picking**, **Environment Background**, **Tone
Mapping**, and **Output Blit**. Complete Renderer shows the outer frame and
every named renderer pass. Selecting one non-visibility effect shows that pass beside the
complete frame. Selecting Screen-Space Visibility exposes:

- **Complete Visibility Pipeline:** one outer timestamp spanning the complete
  visibility effect.
- **Named-Stage Total:** the sum of nonoverlapping named stages for the same
  originating frame.
- **Unattributed Timer Difference:** the envelope minus named stages. A small
  negative value can occur from independent timestamp quantization; it is not
  clamped or hidden.
- **Depth Preparation:** generic hierarchy or Activision prepared depth,
  depending on the selected profile.
- **First-Bounce Visibility Trace** and **Later Bounces:** distinct traversal
  scopes. Bounces two and later are intentionally combined in the human-facing
  Statistics table; the benchmark collector retains internal bounce-two,
  bounce-three, and bounce-four queries for historical export compatibility.
- **Spatial Denoise**, **Temporal Reconstruction**, **Fused Spatial Denoise &
  Upsample**, **Required Full-Resolution Upsample**, **Fused Apply**,
  and **Indirect-Lighting Composition:** separate rows with no catch-all
  **Other** label and no unrelated stages combined.

The benchmark collector correlates delayed query results by originating frame,
discards incomplete frames from distributions, warms for 120 frames by
default, and records medians and p95 values from at least 240 complete frames
for a controlled run. **Producer Subtotal** is depth, first/later trace,
dedicated spatial denoise, temporal, fused spatial denoise/upsample, and required
upsample; a fused resolve/apply run also attributes its inseparable full-
resolution application to the producer mask. Complete effect is the outer
envelope rather than a sum of stage medians.

Two following memory rows separate:

- **Outputs:** exact logical AO, GI, filtered output, and active bounce-frontier
  texel payload.
- **Working:** exact resident scheduler textures, temporal
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

## Benchmark and Command Line

Windows requests `HIGH_PRIORITY_CLASS` at startup. Instrumented
`UVSR_PERF_*` captures can explicitly request Normal or High priority with
`UVSR_PERF_PRIORITY`; exported metadata records the live process priority so
priority-mismatched runs are not treated as equivalent.

**Run Current** locks Benchmark Position 1, resets history, and collects one
isolated visibility profile. The Statistics drawer retains the latest
per-stage median and p95 distributions in memory; it does not serialize JSON,
CSV, or a measured-frame image.

The same drawer contains the warm-up/measured-frame controls, **Run Current**,
**Run Current With Motion**, **Cancel**, benchmark progress, and the latest
results. Run Current benchmarks the effective rendered configuration rather
than rejecting a modified preset label. Interactive runs automatically lock
Benchmark Position 1, resize to 1920x1080, wait for the matching rendered
workload, and restore the previous window size afterward. The former Reference
comparison, all other multi-run actions, result export, and benchmark-folder
action are removed.
Readiness follows the renderer-reported workload and active permutation rather
than the selected preset label. Modified presets therefore remain runnable once
their settings reach the GPU. A run is blocked only for another active run,
non-deferred rendering, no active AO/GI consumer, an invalid execution plan, a
permutation that has not reached the GPU yet, or a scene without the controlled
camera. Only PBR Sponza Decorated and PBR Sponza Plain currently provide that
camera. Command-line runs queued before asynchronous scene loading finishes now
wait for `SceneLoaded` instead of being misclassified as an unsupported scene.

The command-line surface is:

```text
--visibility-profile <display-name-or-hyphenated-name>
--visibility-contribution-terminated-bounces
--visibility-benchmark
--benchmark-warmup <0..100000>
--benchmark-frames <1..100000>
--benchmark-auto-close
```

Invalid or unavailable profiles and invalid counts print a command-line error
to standard error and return a nonzero exit code. They do not create modal
message boxes. The contribution-terminated override enables GI, turns
**Limit Bounces** off, and clears the fixed one-click verification identity so
the run measures the effective implementation profile and 16-entry
GPU-terminated command stream. The former fixed-count, noise, reconstruction, math, buffer,
compatibility, new-candidate, and all-profile test-matrix runners and the
`--benchmark-sequence` option have been removed. The former **Compare
Reference** action has also been removed; select Reference or another current
configuration explicitly and use **Run Current** for each isolated measurement.

An independent top-right overlay remains visible while a benchmark is queued,
warming up, or collecting data even when the settings UI is hidden. It cycles
between `Benchmarking.`, `Benchmarking..`, and `Benchmarking...`, and displays
completed measured frames over the requested total, such as
`Benchmarking... (67/420)`. Warm-up frames deliberately leave the numerator at
zero.

## Runtime Evidence

### Runtime 20 Versus Fixed 20

The decisive Intel comparison used Benchmark Position 1 with anti-aliasing
disabled. Fully unrolling Fixed 20 backfired in the Intel driver: its static
shader IR contained 7,989 instructions, compared with 1,221 for the compact
Runtime 20 loop. Runtime 20 increased total frame rate by 7.32% and reduced
visibility trace time by 16.08% in that matched comparison.

Runtime is therefore not a smaller Fixed specialization and is not merely the
old guarded shader under a new label. It keeps the sample count in the constant
buffer, while the CPU selects an even- or odd-count contract that removes the
redundant clamp and parity decision without unrolling all 20 iterations. The
Runtime guarded contract covers other compositions. Fixed and Generic remain
historical comparison labels only.

The earlier pre-source-port Release smoke used the Intel Arc adapter on the
target Core Ultra 9 185H system, 1920x1080 output, Benchmark Position 1, zero warm-up frames, and
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
| All Implemented | 53/53 entries; 106/106 complete frames | Every then-selectable implemented or partial-control profile ran, including generic Reference GI-only/AO+GI/multi-bounce, packed-FAST AO+GI, fixed 8/12/16/20 AO/GI-only/AO+GI, later-bounce/multi-bounce, three temporal responses, diagnostics, group shapes, radius clamps, horizon controls, and fused debug output; this run predates the repaired PS4 and XeGTAO profiles |

The final all-implemented Release smoke expanded that coverage to 58/58 entries,
116/116 complete frames, and zero incomplete frames. Every entry produced
matching JSON, CSV, and BMP artifacts. Its two measured frames per profile and
`controlled_protocol_valid=false` correctly classify it as runtime coverage,
not performance evidence; the controlled finalist results follow below.

### Controlled Intel 600-Frame Evidence

The current controlled runs used the Intel Arc integrated GPU (`DEV_7D55`,
driver `32.0.101.8724`), PBR Sponza Decorated, 1920x1080, Benchmark Position 1,
120 warm-up frames, and 600 measured frames per profile. Every reported entry
completed 600/600 frames with zero incomplete frames.

| Comparison | Complete Median | Saving |
| --- | ---: | ---: |
| Reference | 3.0240 ms | Baseline |
| Exact Fused Resolve And Apply | 2.4889 ms | 17.70% |
| Exact Fixed 8 + Fused Resolve And Apply | 2.3999 ms | 20.64% |
| Reference versus Fixed 8 across repeats | 3.0240 versus 2.8826 ms in the current-source run | 1.9-4.7% range |
| Final GI RGBA32 versus RGBA16 | 4.2102 versus 3.8841 ms | 8.39% |
| Depth Hierarchy R32 versus R16 | 3.3549 versus 3.2297 ms | 3.88% |
| Reference versus Horizon Control 8 | 2.9252 versus 2.4747 ms | 15.40%, algorithmic |

The repeated candidate runs put fusion at 17.7-18.8% and Fixed 8 plus fusion at
20.6-22.4%. In the fixed-sample matrix, the horizon control reduced trace from
1.0406 to 0.5861 ms, quantifying bitmask-estimator overhead without claiming
image equivalence. The 24-profile math matrix kept nearly every exact/standard/
approximation result within +/-1%, so the per-function math family was removed. The
paired 20-profile buffer matrix supports 16-bit final-GI and hierarchy storage;
smaller inconsistent temporal/adaptive deltas are treated as noise.

The historical compatibility smoke completed 12/12 profiles at 2/2 measured
frames. Reference and curated profiles now compile their default math
statically; per-function runtime math selection has been removed.

### Historical Source-Port Smoke

The current local `new-candidates` smoke used the NVIDIA GeForce RTX 4090
Laptop GPU at 1920x1080, one warm-up frame, and two measured frames per entry.
It completed 9/9 entries and 18/18 measured frames with zero incomplete frames,
exporting matching JSON, CSV, and BMP artifacts. The exports embed build identity
`e3f1908`, label the run `smoke`, and set `controlled_protocol_valid` to false.
The task tree was still dirty, so these numbers establish runtime coverage and
very short directional observations only.

| Profile | Complete Median / p95 | Directional Smoke Observation |
| --- | ---: | --- |
| Reference | 2.206208 / 2.651802 ms | First entry was visibly cold/noisy; do not use it to quantify candidate savings |
| Exact Fixed 8 | 0.258560 / 0.260864 ms | Runtime path completed; superseded by the controlled run below |
| Exact Fused Resolve and Apply | 0.233472 / 0.234394 ms | Runtime path completed; superseded by the controlled run below |
| Exact Fixed 8 + Fused Resolve and Apply | 0.220160 / 0.224768 ms | Lowest same-estimator smoke median; superseded by the controlled run below |
| Activision PS4 Approximation, Scalar 4x4 | 0.323584 / 0.323584 ms | All six named passes completed; superseded by the controlled run below |
| Activision PS4 Approximation, Packed 4x4 Gather | 0.320000 / 0.329677 ms | Spatial stage was 0.024576 ms versus 0.029696 ms scalar; complete delta was only 0.003584 ms |
| XeGTAO High, LUT + Mixed Precision | 0.439808 / 0.440269 ms | Pinned source-port path completed |
| XeGTAO High, Inline Hilbert + Mixed Precision | 0.445952 / 0.449178 ms | LUT was directionally 0.006144 ms lower in this smoke |
| XeGTAO High, LUT + FP32 | 0.367104 / 0.368486 ms | FP32 was directionally 0.072704 ms lower than mixed precision, consistent with Intel's warning that FP16 can regress on some GPU/driver combinations |

The Xe mixed-to-FP32 transition therefore ran successfully in the bounded
candidate sequence. Its direction was later confirmed by the controlled main
run and an independent controlled precision repeat.

### Controlled Local 600-Frame Evidence

The final local run used PBR Sponza Decorated at 1920x1080 on the NVIDIA GeForce
RTX 4090 Laptop GPU, 120 warm-up frames, and 600 measured frames per profile.
Every profile exported 600 complete and zero incomplete frames with
`controlled_protocol_valid=true`. Final captures were clean, including both PS4
profiles; no recurrence of the large black AO artifacts was visible. These are
controlled local results, but they remain NVIDIA-specific and do not predict the
target Core Ultra 9 185H/Xe-LPG ordering.

| Profile | Complete Median / p95 | Delta And Local Disposition |
| --- | ---: | --- |
| Reference | 0.221184 / 0.263168 ms | Product baseline |
| Exact Fixed 8 | 0.223232 / 0.230400 ms | 0.926% median regression; drop as a standalone production candidate |
| Exact Fused Resolve and Apply | 0.207360 / 0.217088 ms | 6.250% median and 17.510% p95 saving; keep for product validation |
| Exact Fixed 8 + Fused Resolve and Apply | 0.203776 / 0.212992 ms | 7.870% median and 19.066% p95 saving; keep for product validation |
| Activision PS4 Approximation, Scalar 4x4 | 0.306176 / 0.340019 ms | 38.4% slower than Reference; faithful algorithm comparison, not a local performance profile |
| Activision PS4 Approximation, Packed 4x4 Gather | 0.302080 / 0.313344 ms | 1.338% median and 7.845% p95 faster than scalar, but 36.6% slower than Reference; prefer packed for this comparison |
| XeGTAO High, LUT + Mixed Precision | 0.413696 / 0.454656 ms | Removed; slower than canonical Reference |
| XeGTAO High, Inline Hilbert + Mixed Precision | 0.419840 / 0.454656 ms | Removed; LUT mixed was 1.463% faster internally but the family missed the replacement goal |
| XeGTAO High, LUT + FP32 | 0.349184 / 0.384000 ms | Removed; fastest local Xe variant still did not justify a UVSR performance profile |

Named-stage medians explain the complete ordering without a catch-all bucket:

| Profile | Depth | Trace | Spatial | Temporal | Upsample Or Fused Apply | Composition | Residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Reference | — | 0.101376 | — | — | 0.076800 | 0.028672 | 0.014336 |
| Exact Fixed 8 | — | 0.101376 | — | — | 0.077824 | 0.028672 | 0.014336 |
| Exact Fused | — | 0.101376 | — | — | 0.092160 | — | 0.009216 |
| Exact Fixed 8 + Fused | — | 0.101376 | — | — | 0.093184 | — | 0.009216 |
| PS4 Scalar | 0.008192 | 0.060416 | 0.031744 | 0.036864 | 0.092160 | 0.048128 | 0.028672 |
| PS4 Packed | 0.008192 | 0.059392 | 0.023552 | 0.037888 | 0.091136 | 0.048128 | 0.028672 |
| Xe LUT Mixed | 0.020480 | 0.300032 | 0.029696 | — | — | 0.043008 | 0.019456 |
| Xe Inline Mixed | 0.020480 | 0.309248 | 0.028672 | — | — | 0.040960 | 0.019456 |
| Xe LUT FP32 | 0.019456 | 0.244736 | 0.023552 | — | — | 0.041984 | 0.019456 |

All stage values are milliseconds. Reference and Fixed 8 had identical trace
medians, so the standalone specialization removed no measurable trace time in
this run. Fusion reduced complete work by combining the required full-resolution
resolve/application path; its stage is not directly comparable to the separate
upsample and composition medians.

The independent controlled precision sequence also completed 600/600 frames per
profile with zero incomplete frames: Xe LUT mixed measured 0.394240/0.410726 ms
and Xe LUT FP32 measured 0.350208/0.371712 ms. FP32 was 11.169% faster by median
there and 15.594% faster in the main controlled pass. The observed local range
is therefore 11.2-15.6% in favor of FP32, consistent with upstream's warning
that minimum precision can regress on some hardware/driver combinations.

Every successful sequence exported matching JSON, CSV, and final-frame BMP sets
with empty stderr and zero incomplete frames. Reference versus Fixed 8 final-frame
comparison found no gross corruption (35.0 dB PSNR and 2.35/255 RGB MAE), but the
captures used different stochastic frame phases; exact GPU-image equivalence
therefore remains unproven. The 589,824-case CPU suite supplies the stronger
fixed-order/mask/AO/GI ownership proof without replacing final image review.

The earlier comprehensive smokes also keep the 16x8/8x16 group shapes and all
three radius clamps in the possible-regression category. The PS4-schedule and UVSR
horizon controls showed conspicuous structured noise/checkerboarding and darker
sampled means than Reference. Those observations apply to the older partial
controls, not the repaired PS4 approximation. The dedicated PS4 profiles now
have controlled local coverage and clean final captures. Historical XeGTAO
measurements remain only as rejection evidence. Dynamic motion/
disocclusion stress and controlled target timing remain user validation tasks;
Release compilation, controlled local timing, and target evidence remain
distinct layers.

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
common-exposure and IBL lobe-strength contracts, shader permutations, and
Release compilation. They do not replace human review of thin geometry, motion,
temporal trails, reduced-resolution edge stability, environment failure, or
filter quality. Controlled target timings can rank candidates, but final
promotion still requires manual image review; native register/occupancy and
traffic counters should be added when available.

For the performance profiles, focused checks cover plan validity, reference
zero-cost-off masks, fixed sample/order ownership, offline-computed packed-noise
delivery, edge bytes and receiver mapping, delayed frame correlation,
envelope/sum/residual accounting, export structure, full-texture viewport
fallback, UI source-profile invalidation, and all-or-nothing re-export. The
current Release build, all CTest targets, the retained Intel controlled runs,
and the final compatibility smoke pass cover the retained surface. Exact same-phase
GPU image equivalence, dynamic
motion/disocclusion stress, and Intel native ISA/register/occupancy counters
remain pending. The documented Intel timing is valid performance evidence for
this adapter and driver; it does not replace those image and physical-counter
gates.

The future recommendations above require their own reference images and tests
when implemented. No runtime performance improvement should be claimed without
controlled timings plus register/occupancy and traffic evidence on the target
adapters.
