# Bent-Normal AO Repair and Performance Report

Date: 2026-08-01  
Status: exact local candidate built, tested, measured in a reduced-evidence
diagnostic matrix, visually checked, and left running for review; not committed
or published

## Decision Summary

The repaired Bent-Normal AO path is very likely functioning correctly, but the
available diagnostic evidence says it is not functioning at the intended
full-resolution performance. The complete
TAA-on product toggle costs 82.40% to 85.10% more than Bent-Off AO, depending
on the IBL consumer. The TAA-off temporal-matched decomposition still costs
56.09% to 57.53% more. Both are materially above the roughly 25% relative
effect-cost increase reported for XeGTAO's different renderer, storage, and
hardware configuration.

The evidence-calibrated diagnostic estimates are:

| Question | Likelihood | Interpretation |
| --- | ---: | --- |
| Is the scalar-plus-directional signal, history, fallback, UI, and IBL integration functioning as designed? | 94% | High functional confidence from deterministic reference tests, compile-time path checks, exact runtime state, and visual review; no offline image oracle or vendor-native capture was collected for every dynamic scene case |
| Does the full-resolution TAA-on product toggle meet the intended at-most-25% AO-pipeline overhead? | 0.14% | The minimum Student-t probability across diffuse, specular, and combined modes after clock normalization and control-drift uncertainty |
| Are Bent normals functioning to the performance they are intended to? | **0.14%** | Conservative diagnostic answer: the minimum of functional confidence, target-performance probability, and the 70% reduced-evidence measurement-confidence ceiling |
| Does the compact 2×2 combined path meet 25% in the temporal-matched decomposition? | 13.80% | It reaches 29.56% measured overhead, close enough that more repeats or packing could plausibly cross the target |
| Does the compact 2×2 combined product toggle meet 25% with TAA on? | 0.57% | It measures 50.49% because Bent-Off does not run scalar visibility temporal reconstruction while renderer TAA is active |

The main remaining cost is not an extra depth march or a doubled IBL
evaluation. It is the full-precision directional temporal/history payload,
followed by application work. The full path moves a 115.433 MiB directional
payload; the compact trace-resolution route moves 28.858 MiB and reduces the
temporal-matched combined overhead from 57.53% to 29.56%. That 75% footprint
reduction is the strongest architectural evidence in this investigation.

## Candidate Identity

| Field | Value |
| --- | --- |
| Git base | `bed36f951407e23d9e6b4a1a9462a96abc059e8a` |
| Benchmark dirty tracked-diff SHA-256 | `D173ED4A836FD68103FF957702BE27BC052DE5D895587CD1DC8C5C4E14D947A4` |
| Exact executable | `build-bent-normal-repair/bin/uvsr.exe` |
| Executable SHA-256 | `AF876EF31B01F651F09843F52B19732ECCC4D52866A6A9E968D18CA72E571FFC` |
| Adapter | NVIDIA GeForce RTX 4090 Laptop GPU |
| Output | 1896×1064, Benchmark Position 1 |
| Renderer priority | High in all 48 diagnostic captures |
| Full matrix | 30 captures: two protocols × three repeats × Off-before, three rotated consumers, Off-after |
| Compact matrix | 18 captures: two protocols × three repeats × Off-before, Combined, Off-after |
| Per-capture window | 600 warm-up frames, then at least 1,000 frames and 15 seconds; the shortest full diagnostic contained 4,855 measured frames and the shortest compact diagnostic contained 6,879 |

The [full manifest](../../outputs/bent-ao-repair/20260801T202500Z-repair-final-full/matrix-manifest.json),
[compact manifest](../../outputs/bent-ao-repair/20260801T204000Z-repair-final-compact2x2/matrix-manifest.json),
[machine-readable summary](../../outputs/bent-ao-repair/20260801-bent-repair-summary.json),
and [optimized DXIL report](../../build-bent-normal-repair/shader_evidence/bent-repair-final/visibility-dxil-report.md)
retain the raw diagnostic evidence.

## Implemented Repair Order

1. The benchmark path now has two explicit, mutually exclusive protocols and
   rejects ambiguous mode/protocol combinations before device startup. Every
   active stage carries its originating GPU-query frame ID; captures reject
   stage/frame mismatches and report the required upsample and whole-effect
   residual rather than silently omitting them.
2. The 32-sector resolver now uses exact blocked/open mask fast paths. Visible
   interval endpoints reuse their sine/cosine values between scalar mass and
   first-moment antiderivatives. The full-open moment uses its exact closed
   form. The established scalar AO estimator and every depth sample remain
   unchanged.
3. Separate and fused IBL consumers no longer recompute the legacy specular
   environment occlusion after Bent cone/lobe resolution. Diffuse-only omits
   the unused final-visibility binding and texture load. Specular and combined
   reuse one pre-occluded environment evaluation.
4. The enabled-only temporal variant seeds the center once, loads only the four
   axial neighbors for the joint five-tap resolve, and stores the 10×10 Bent
   tile in scalar group-shared planes instead of a `float4` array. The
   Bent-Off shader remains byte-identical.
5. The existing compact 2×2 fused reduced-resolution path was validated with
   exact resource metadata and three matched combined-mode brackets per
   protocol. It omits full-resolution directional finals while retaining the
   full-precision signal at trace resolution.
6. Optimized DXIL, all automated tests, exact runtime UI state, screenshots,
   full and compact GPU matrices, and this probability model were regenerated
   from the same candidate executable.

## Directional Visibility Contract

The producer is independent from IBL and emits one reusable near-field signal:

| Field | Contract |
| --- | --- |
| Established scalar AO | UVSR's existing estimator output; unchanged and retained by diffuse attenuation |
| Physical scalar visibility | Cosine visibility derived from the same final 32-sector mask as the moment; reconstructed jointly and clamped only at the consumer |
| First moment | Unnormalized world-space `float3`; reconstructed spatially and temporally before normalization |
| Confidence | Bounded effective support for accepted current/history evidence; not literal angular coverage or concentration |

The trace performs no second depth march. Reconstruction rejects invalid
history and disocclusion, enforces the geometric-normal hemisphere after the
moment is complete, normalizes only at the consumer, and safely falls back to
the surface normal. Diffuse IBL samples along that finalized direction while
retaining scalar AO. Specular IBL keeps the shading/reflection normal and
cubemap lookup direction, applying only the bounded cone/lobe overlap as
occlusion. Combined mode runs both from the same signal.

No GI control, flag, binding, allocation, history, or shader branch was added.
The unnormalized moment and confidence preserve the information a future
weighted-GI sampler would need without coupling today's producer to that use.

## Static Shader Evidence

The table compares the immediately preceding optimized candidate with this
repair, using the same bundled DXC, shader model 6.5, and `-O3`. DXIL IR is not
native ISA and cannot reveal physical registers, spills, occupancy, or LDS-bank
conflicts.

| Variant | Previous | Repaired | Result |
| --- | ---: | ---: | --- |
| Bent trace static IR | 1,833 | 1,774 | 3.2% lower |
| Bent trace unary operations | 56 | 48 | 14.3% lower |
| Bent trace static `acos` | 8 | 4 | 50% lower |
| Bent temporal static IR | 1,211 | 1,108 | 8.5% lower |
| Bent temporal branches | 163 | 152 | 6.7% lower |
| Diffuse composite texture loads | 12 | 11 | Unused final visibility removed |
| Diffuse composite unary operations | 38 | 34 | 10.5% lower |
| Specular/combined composite static IR | 874 | 868 | Shared work removed; exact partial-overlap solver retained |
| Specular/combined composite unary operations | 68 | 64 | 5.9% lower |

The optimized scalar trace DXIL remains byte-identical at SHA-256
`59252C5F5F676B6435C39F06A71EEEF8C08E64DA63F93718FEA05A10F5F0E440`.
The scalar temporal DXIL remains byte-identical at SHA-256
`D943F8C2F42EF018CCDB8E8460E8C27AC9D3C2302EB16109EC39E6831E9C7281`.
That is direct compile-time evidence that the new repairs add no Bent-Off shader
ALU. Planner tests separately prove no Bent-Off directional allocation,
binding, history, dispatch, or cache/permutation identity.

## Benchmark Method

### TAA-On Product Toggle

Bent-Off and all three Bent modes keep renderer TAA enabled. Explicit legacy
visibility temporal reconstruction remains off because that scalar path still
has its established TAA mutex. Bent-enabled modes pay for their required
jitter-aware directional reconstruction. This is the user-visible optional
feature cost and the only result compared with XeGTAO's reported relative
overhead.

### TAA-Off Temporal-Matched Decomposition

Renderer TAA is deliberately off. Bent-Off enables legacy scalar visibility
temporal reconstruction, while Bent modes enable directional reconstruction.
This isolates the incremental payload/reconstruction cost, but it is not the
product toggle because it changes renderer anti-aliasing.

### Clock Normalization and Controls

The user explicitly waived the cold thermal gate. That waiver did not make an
active loaded limiter benchmark-valid under UVSR's repository policy. Raw
medians remain in every capture, and the advisory normalized value is:

`normalized_ms = raw_ms × median_measurement_clock_MHz ÷ 1815 MHz`

Each candidate is divided by a control linearly interpolated in measurement
time between its Off-before and Off-after runs. Candidate measurement clocks
ranged from 2,055 to 2,115 MHz at full resolution and 2,070 to 2,115 MHz in the
compact matrix. Every candidate telemetry sample reported limiter mask `0x4`;
maximum candidate GPU temperature was 76 C. Therefore none of the 48 captures
is an accepted publication-grade measurement. Clock-normalized control drift
was small: at most 2.95% for full TAA-on effect time, 0.24% for full
temporal-matched effect time, and 1.52% for compact effect time. Those controls
make the large full-resolution miss useful directional evidence, but they do
not repair the active-limiter gate or prove bandwidth-sensitive scaling.

The whole visibility-effect envelope is authoritative. Stage medians diagnose
where the delta went, but overlapping/fused work and the composition/apply
alias in compact mode mean stage medians must not be naively added into a
second headline time. This follows Microsoft's distinction between execution
and end-of-pipe timing in [PIX GPU captures](https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/gpu-captures/pix-gpu-captures).

## Full-Resolution Diagnostic Results

### TAA-On Product Toggle Diagnostic Results

| Mode | Raw Candidate Effect | Clock-Normalized Effect Overhead | Raw Candidate Renderer | Clock-Normalized Renderer Overhead | Probability of Effect Overhead at or Below 25% |
| --- | ---: | ---: | ---: | ---: | ---: |
| Diffuse IBL | 1.3251 ms | 82.40% | 2.9184 ms | 25.62% | 0.145% |
| Specular IBL Occlusion | 1.3578 ms | 83.20% | 2.9778 ms | 26.12% | 0.138% |
| Diffuse and Specular IBL | 1.3455 ms | 85.10% | 2.9512 ms | 25.49% | 0.136% |

Interpolated normalized Bent-Off controls ranged from 0.8261 to 0.8411 ms for
the visibility effect and 2.6664 to 2.7105 ms for the renderer. Candidate
effect values ranged from 1.5124 to 1.5568 ms after normalization.

### TAA-Off Temporal-Matched Diagnostic Results

| Mode | Raw Candidate Effect | Clock-Normalized Effect Overhead | Raw Candidate Renderer | Clock-Normalized Renderer Overhead | Probability of Effect Overhead at or Below 25% |
| --- | ---: | ---: | ---: | ---: | ---: |
| Diffuse IBL | 1.3179 ms | 56.09% | 2.7884 ms | 20.26% | 0.409% |
| Specular IBL Occlusion | 1.3373 ms | 57.49% | 2.8355 ms | 20.72% | 0.364% |
| Diffuse and Specular IBL | 1.3373 ms | 57.53% | 2.8170 ms | 20.63% | 0.365% |

Interpolated normalized Bent-Off controls ranged from 0.9628 to 0.9711 ms for
the visibility effect and 2.6519 to 2.7109 ms for the renderer. Candidate
effect values ranged from 1.5019 to 1.5276 ms after normalization.

### Full Combined-Mode Diagnostic Stage Diagnosis

| Protocol | Trace Delta | Temporal Delta | Composition Delta | Whole-Effect Delta | Renderer Delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| TAA on | 0.1087 ms | 0.4239 ms | 0.1668 ms | 0.7073 ms | 0.6795 ms |
| Temporal matched | 0.1049 ms | 0.3171 ms | 0.1342 ms | 0.5566 ms | 0.5495 ms |

The trace repair met its local goal: the former extra 0.16–0.19 ms has fallen
to roughly 0.104–0.109 ms in the matched full matrix. Temporal reconstruction
now accounts for about 60% of the full product-toggle delta. The raw Bent
temporal pass remains about 0.369–0.372 ms, so scalarized LDS and explicit
center seeding reduced static shader work but did not materially reduce this
GPU's pass time. That is consistent with the history/global-memory traffic
remaining dominant.

The consumer results also reject the doubled-IBL hypothesis. Diffuse
composition adds 0.1481 ms, specular 0.1657 ms, and combined 0.1668 ms in the
TAA-on protocol. Combined is effectively specular cost, not diffuse plus
specular cost, because shared intermediates and the pre-occluded environment
path are working.

## Compact 2×2 Diagnostic Results

| Protocol | Mode | Raw Candidate Effect | Clock-Normalized Effect Overhead | Raw Candidate Renderer | Clock-Normalized Renderer Overhead | Target Probability |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| TAA on | Combined | 0.4751 ms | 50.49% | 2.0736 ms | 7.17% | 0.573% |
| Temporal matched | Combined | 0.4649 ms | 29.56% | 1.9487 ms | 5.70% | 13.80% |

The candidate selected `resolution=half`, retained 28.858 MiB of
trace-resolution full-precision directional data, and omitted full-resolution
directional final textures. All 18 captures used the fused path and reported
complete active-stage alignment. The 75% directional-footprint reduction and
27.97 percentage-point drop in temporal-matched effect overhead validate the
architecture. It does not yet prove acceptable angular/temporal quality for
every scene or make the TAA-on product toggle meet the target.

## Correctness and Runtime Verification

- The isolated Release build completed successfully with node reuse disabled.
- All 33 CTest targets passed, including scalar and directional estimator
  oracles, PBR/cone reference cases, temporal AA, performance planning, command
  catalog, UI source contracts, and exact runtime/production shader bundles.
- Reference tests cover open, blocked, one-sided, symmetric, thin/disjoint,
  tangent, partial overlap, nonfinite fallback, geometric-hemisphere projection,
  normal-map orientation, rotated world/HDR behavior, temporal confidence,
  disocclusion/history rejection, screen edges, and bounded specular output.
- Planner/source-contract tests cover settings, presets, cache/permutation and
  history keys, quality modes, fused and separate paths, MSAA, debug views, and
  no Bent-Off resources or bindings.
- The exact candidate was launched after benchmarking. The matched screenshots
  show TAA's **Temporal Reconstructive** method enabled with Bent output both
  off and on. The on screenshot also shows the conditional **Apply To** dropdown
  immediately below **Bent Normal Output**, and only **Strength** remains.

![TAA On and Bent Normal Output Off](../../outputs/bent-ao-review/20260801-repair-final/taa-on-bent-off.jpg)

![TAA On and Bent Normal Output On](../../outputs/bent-ao-review/20260801-repair-final/taa-on-bent-on.jpg)

## Probability Model

For each mode, the model uses the three log ratios between clock-normalized
candidate effect time and its interpolated control. It evaluates a Student-t
distribution with two degrees of freedom at `log(1.25)`. Its uncertainty is
never smaller than the larger of 2% or half the observed normalized control
drift. The protocol result is the minimum probability across its modes.

Functional confidence is 94%. It receives full credit for scalar-path identity,
same-mask scalar/directional consistency, finite/fallback behavior, consumer
separation, integration identity, and exact runtime state; it retains a six-point
uncertainty for the absence of an offline rendered-image oracle and native
counter captures across every dynamic case. Measurement confidence is capped at
70% because there are only three brackets per mode, the user waived the cold
gate, every candidate sample carried live limiter mask `0x4`, and no native
register/occupancy/cache/LDS-bank capture was taken. UVSR's benchmark policy
requires an inactive live limiter for an accepted measurement, so these runs
are advisory diagnostics even though their identity, frame alignment, duration,
priority, clocks, and bracket controls passed. The requested singular likelihood
is the conservative diagnostic minimum, 0.14%.

This percentage is an engineering decision aid, not a calibrated physical
probability or a publication-grade benchmark claim. The raw observations and
model are retained so a later limiter-free, counter-instrumented session can
update it rather than replace it subjectively.

## Cross-Renderer Runtime Evidence

The canonical [cross-renderer cost study](../../outputs/bent-normal-cross-renderer-report/report.html)
records exact install identities, runtime proof, architecture, raw observations,
normalization, and limitations for fresh Capsaicin v1.3 and Filament v1.71.5.

- The exact current Capsaicin executable completed full 4,096-frame GI 1.2
  runs for producer-off, consumer-control, and native configurations. Its
  normalized producer point estimate is about 3%, but same-code drift is larger,
  only one variant has two complete repeats, and the missing fourth factorial
  corner prevents a measured total Bent cost. Later `0x141` driver recoveries
  make the campaign diagnostic rather than publication-grade.
- Filament is the usable complete optional-path reference. Three balanced
  1080p brackets put the Bent increment at 14.97% of scalar AO by raw
  variant-median algebra and 30.68% by paired-bracket sensitivity. The same
  increment is 0.56% or 0.88% of whole-renderer GPU-active time. Its confidence
  interval crosses zero, so this is a practical band rather than a precise
  universal constant.
- Filament's roughly 15% to 31% band contains XeGTAO's reported roughly 25%
  effect overhead. It supports a 25% target and 35% diagnostic guardrail for a
  compact optional UVSR path. It does not make Intel, Filament, or Capsaicin
  absolute timings transferable to UVSR, and it does not change the report's
  0.14% likelihood for the current full-resolution candidate.
- Three paired published studies independently put same-evidence direction
  production around 5% to 12%, while one reports about 25% for directional
  filtering. Their hardware and pipelines are not transferable, but the
  producer-versus-reconstruction split strengthens temporal/history bandwidth
  as the leading place to investigate UVSR's excess.

## New Cross-Implementation Findings

- AMD's MIT-licensed [FidelityFX Denoiser temporal resolve](https://github.com/GPUOpen-Effects/FidelityFX-Denoiser/blob/d7dfecbabe7b9523b14e7b067216e06b86e8d189/ffx-reflection-dnsr/ffx_denoiser_reflections_resolve_temporal.h)
  explicitly stages a neighborhood in group-shared memory, loads its center
  once, and separates global loads from shared stores. UVSR independently
  adopted those architectural properties for its 10×10 Bent tile.
- AMD's [RDNA Performance Guide](https://gpuopen.com/learn/rdna-performance-guide/)
  documents 32-bank LDS and recommends structure-of-arrays or padding to reduce
  bank conflicts. This motivated scalar moment/confidence planes. Optimized
  DXIL still exposes the pre-existing `[300 x float]` normal tile, making a
  Bent-only scalar/padded normal layout a plausible next counter-guided test.
- AMD's MIT-licensed [FidelityFX shadow classifier](https://github.com/GPUOpen-Effects/FidelityFX-Denoiser/blob/d7dfecbabe7b9523b14e7b067216e06b86e8d189/ffx-shadows-dnsr/ffx_denoiser_shadows_tileclassification.h)
  reinforces exact all-open/all-closed mask classification. UVSR's formulas
  and implementation remain independent and specific to its 32-sector cosine
  measure.
- [Screen Space Indirect Lighting with Visibility Bitmask](https://arxiv.org/abs/2301.11376)
  supports retaining one 32-bit visibility mask as the shared AO/directional
  evidence instead of adding another march. Its absolute timings are not used
  as UVSR predictions.
- NVIDIA NRD's [directional-occlusion configuration](https://github.com/NVIDIA-RTX/NRD/blob/9a3fe938a7558fd16b6c91a1c0456305cdcd9f16/Source/Denoisers/Reblur_DiffuseDirectionalOcclusion.hpp)
  is useful only as an architectural comparison for persistent and transient
  directional history. Its license is restrictive, so no NRD code or constants
  were copied.
- NVIDIA's [Peak-Performance-Percentage method](https://developer.nvidia.com/blog/the-peak-performance-analysis-method-for-optimizing-any-gpu-workload/)
  and Microsoft's [PIX timing guidance](https://learn.microsoft.com/en-us/windows/win32/direct3dtools/pix/articles/gpu-captures/pix-gpu-captures)
  show why the next performance session needs native cache, TEX/L2/VRAM, SFU,
  register, occupancy, LDS conflict, and barrier counters. Static DXIL and
  timestamps can localize work, but cannot prove the limiting hardware unit.
- [SVGF](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/)
  motivates reporting temporal stability separately from reference accuracy.
  Any packing trial must therefore report both angular error and temporal
  angular variance, not only bandwidth or frame time.

## Recommended Next Order

1. Capture the existing exact candidate in Nsight Graphics or PIX without
   changing shaders. For trace, collect SFU issue, register count/spills,
   occupancy, TEX/L2/VRAM throughput, and cache hit rates. For temporal, collect
   VRAM/L2 throughput, LDS-bank conflicts, barriers, register pressure, and
   occupancy. For composition, collect SFU and cubemap/TEX metrics. This will
   confirm or reject the bandwidth inference.
2. If temporal is bandwidth-limited, run the required full-precision-versus-
   packed experiment. Start with `RGBA16_FLOAT + R16_FLOAT`, which would halve
   the 115.433 MiB payload to about 57.717 MiB without discarding magnitude.
   Do not adopt it unless range histograms, worst/percentile angular error,
   temporal angular variance/lag, disocclusion recovery, and matched total
   bandwidth/frame time all pass.
3. Independently test a Bent-enabled-only scalar or padded normal LDS layout.
   It can preserve byte-identical Bent-Off DXIL, but native LDS-conflict and
   occupancy counters should decide whether it is useful.
4. Prefer the validated compact fused route for reduced-resolution quality
   modes when its image tests pass. Extend matched diffuse and specular compact
   captures before changing defaults; combined alone is the current measured
   result.
5. Leave the exact partial cone/lobe solver intact unless an independently
   derived cosine/polynomial approximation passes dense overlap tests and an
   offline GGX importance-sampling image comparison. Seven static `acos`
   operations remain because exact partial overlap still needs them; disjoint
   and containment cases already bypass them dynamically.

Packing remains deferred. Full precision is still the candidate's product
contract until the required tests pass.

## Attribution and Licensing

The primary code references remain pinned [XeGTAO](https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli)
and [Capsaicin SSGI](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/src/core/src/render_techniques/ssgi/ssgi.comp),
both under their pinned MIT licenses. GTAO/GTSO, GI-1.0, SVGF, the visibility-
bitmask paper, and Lumen are behavioral/paper references, not code-license
grants. Lumen remains a [behavioral reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine)
only. UVSR's formulas, payload, history, cone bound, tests, and shader structure
were implemented independently; no code was copied from paper-only or
restrictively licensed sources.
