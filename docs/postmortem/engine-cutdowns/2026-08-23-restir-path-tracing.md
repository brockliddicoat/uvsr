# ReSTIR Path Tracing Postmortem

## Record Identity and Evidence Labels

This record explains the ReSTIR and configurable path-tracing retirement from
the exact pre-cut source `e29a41245dbd0e6fd7a819d2341646419ab76e72`. The
ordered decision and shared measurement rules are in the
[Stage-Two Cutdown Decision Record](2026-08-23-stage-two-cutdown-decisions.md).
Integrated deletion and package verification are separate final-gate work; this
record does not turn a source edit into runtime proof.

- **Observed** means reproduced from that commit, its tracked history, or a
  contemporaneous tracked report.
- **Estimate** means a bounded calculation without a preserved direct timing or
  capture; its confidence is stated.
- **Inference** means an explanation consistent with the evidence, not a proven
  root cause; its confidence is stated.

## Intended Improvement and Attempted Implementation

**Observed.** The work intended to make path tracing converge faster at equal
render time, especially under motion, by reusing direct and indirect lighting
samples rather than tracing every contribution afresh. It attempted one DX12
ray-query pipeline with:

- conventional RTX path tracing, ReSTIR path tracing, and ReSTIR GI solvers;
- uniform, power-weighted, and adaptive-tree next-event estimation (NEE);
- optional RTXDI-style direct reservoirs, temporal reuse, zero through four
  spatial neighbors, and proposal reuse during motion;
- shared primary-surface generation, separate primary-surface variants, stable
  planes, signal groups, raw or spatial resolve, firefly filtering, and eleven
  debug views;
- persistent resources for radiance statistics, direct and indirect reservoirs,
  proposal histories, primary-surface data, and resolve histories;
- UI, command, canonical snapshot, reset, source-contract, build, shader, and
  documentation integration.

No external RTXDI or RTXPT library was vendored or compiled. The implementation
used UVSR code over existing Donut/NVRHI DX12 ray queries, the TLAS, material and
light data, environment maps, and retained noise assets. Namesake RTXDI/RTXPT
material was a documentation and license boundary, not a packaged runtime
dependency. Donut, NVRHI, and NRD remain separate NVIDIA dependencies governed
by their own package notices and terms.

## Variants, Settings, and Callers

**Observed.** The main shader had 18 compile tasks: three solvers times two
RTXDI states times three NEE policies. Primary-surface generation added six
tasks, and stable-plane resolve added one, for 25 dedicated tasks.

The persisted surface exposed 19 non-action values, all captured by the
canonical settings snapshot:

- `lighting.solution`;
- `pathing.solver`, `pathing.nee`, `pathing.max-bounces`,
  `pathing.russian-roulette`, `pathing.nee-candidates`,
  `pathing.samples-per-pixel`, `pathing.shared-primary-surface`, `pathing.ser`,
  `pathing.rtxdi`, `pathing.temporal-reuse`, `pathing.spatial-neighbors`, and
  `pathing.reuse-proposals-during-motion`;
- `denoising.path-tracing.signal-groups`,
  `denoising.path-tracing.resolve-strength`,
  `denoising.path-tracing.firefly-filter`,
  `denoising.path-tracing.firefly-threshold`, and
  `denoising.path-tracing.method`;
- `debug.path-tracing.view`.

Only 18 of these 19 values retire. `lighting.solution` remains the retained
Ray Marching/Path Tracing selector; the other 18 configurable transport,
denoising, reuse, and debug values disappear for the fixed tracer.

Their concrete domains were three solvers, three NEE policies, two denoisers
(`Raw` and `SpatialPathResolve`), eleven debug views, one through 96 bounces
(default four), one through 63 NEE candidates (default one), one through eight
samples per pixel (default two), Russian roulette on, SER off, shared primary on,
RTXDI off, temporal reuse off, zero spatial neighbors, motion proposal reuse
off, three stable planes, resolve strength one, and a firefly threshold of three.

Runtime callers included lighting-solution selection, frame reset/invalidation,
path dispatch, primary-surface dispatch, stable resolve, debug presentation,
UI commands, snapshots, and shader staging. The path tracer consumed scene,
camera, TLAS, material, light, environment, noise, and motion data. That breadth
made the feature a renderer-state family rather than an isolated shader.

## Measured Burden

**Observed.** Physical source lines, including blanks, at the pre-cut commit:

| Source | Lines |
| --- | ---: |
| Constant buffer and settings headers | 712 |
| Main shader and material/sampling helpers | 3,777 |
| Main pass implementation and header | 2,705 |
| Primary-surface shader | 610 |
| Stable-plane buffer, shader, pass, and header | 720 |
| **Total** | **8,524** |

The total resolves to the exact files: 71-line constant buffer, 641-line
settings header, 2,020-line main shader, 567-line material helper, 1,190-line
sampling helper, 2,380-line pass implementation, 325-line pass header,
610-line primary shader, and stable-plane files of 14, 415, 227, and 64 lines.

**Observed.** The two dedicated tests were 555 lines of settings tests and
2,250 lines of source-contract tests, 2,805 lines total. The feature created 25
shader tasks. A fixed conventional main path needs one; therefore 24 tasks were
the pre-integration removable estimate, not a final observed reduction.

That 2,805-line test burden is a bounded lower measure: it excludes path-related
assertions in broader UI, renderer, production-bundle, command-catalog, and
snapshot tests. The 2,250-line
`tests/path_tracing_source_contract_tests.cpp` suite is deleted. The 555-line
`tests/path_tracing_settings_tests.cpp` remains narrowed for the fixed tracer,
so only the deleted suite is a direct line reduction.

**Estimate — low confidence.** The two CPU/source tests likely consumed less
than one second together in the historical fast suite. No isolated timing log
survives, so a more precise removed-test-time or shader-build-time claim would
be invented. Final integration must remeasure the actual task and test deltas.

**Observed.** The dedicated transport guide was 760 lines and 6,148 whitespace
tokens. Five ReSTIR-bearing plans added 1,387 lines and 15,195 tokens. Together
they were 2,147 lines and 21,343 tokens, but they also discussed retained
standard path tracing and adjacent work; this is a documentation burden, not a
claim that every word was ReSTIR-only.

The five bounded plan paths were:

- `docs/exec-plans/completed/path-tracing-denoising-convergence-v3.md`;
- `docs/exec-plans/completed/path-tracing-resampling-controls.md`;
- `docs/exec-plans/completed/path-tracing-transport.md`;
- `docs/exec-plans/completed/shared-primary-temporal-gi.md`; and
- `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`.

Broader active UI, PBR, noise, TAA, and settings documentation also carried the
feature but is excluded from the 21,343-token measure. The separately deleted
`docs/exec-plans/completed/path-tracing-ui-cleanup.md` was adjacent UI cleanup
and is not included in that bounded five-plan arithmetic.

**Observed.** The documented 4K history estimates were:

| Enabled State | Bytes per Pixel | 4K Estimate |
| --- | ---: | ---: |
| Base radiance mean/count/variance/display | 44 | 348.0 MiB |
| Direct reuse increment | +72 | +569.5 MiB |
| Shared primary increment | +104 | +822.7 MiB |
| Spatial resolve increment | +28 | +221.5 MiB |
| ReSTIR PT increment | +48 | +379.7 MiB |
| ReSTIR GI increment | +96 | +759.4 MiB |
| Base + shared primary + GI + direct reuse | 316 | 2,499.6 MiB |

These are allocation-model calculations, not captured residency measurements.
Compiled shaders were staged through the existing engine package; there was no
separate ReSTIR executable or third-party runtime DLL.

## What Worked and What Was Never Proven

**Observed.** Contemporaneous reports recorded shader compilation, settings and
source tests, the full CTest suite, bounded 1920x1080 Sponza runtime smokes,
non-black output from all three solvers, and Bistro loading while ReSTIR GI was
selected. Historical plans named output hashes, but those build trees and raw
captures are not tracked in Git. Source history contains no matched ReSTIR image
series or benchmark dataset.

**Observed.** Missing evidence included equal-time image-error or noise
comparisons, high-sample ground-truth comparisons, representative static and
motion sequences, stable frame-time and memory measurements, cross-scene
convergence, and controlled tests of disocclusion, light changes, camera cuts,
scene changes, or every reset boundary. The planned matched
convergence/error/performance matrix was explicitly deferred.

**Observed.** Known failures and repairs included:

- the first runtime candidate omitted the common close/execute path after a
  successful dispatch, so traced work could not present until repaired;
- a two-million-work cap could report high FPS by visiting pixels only once
  every 16 to 128 frames; even a later 384-Mi-work-unit candidate reduced default
  1080p ReSTIR to quarter-frame dispatch;
- same-screen-pixel temporal lookup did not follow moved surfaces and was later
  replaced by reprojection;
- power NEE rescanned the full light list per vertex and defaults returned to
  uniform sampling;
- a preview UAV race and dispatch-accounting gaps required repair;
- direct-light variance was absent under shared primary, which could
  under-filter optional spatial resolve;
- user evaluation reported indistinguishable solver quality/performance, slow
  convergence, ineffective motion reuse, suspect TAA, incomplete denoising,
  and unclear history semantics.

Smoke success proved that a mode could dispatch and show an image. It did not
prove equal-time quality, convergence, performance, or correctness of reuse.

## Drift, Confidence, and Failure Analysis

**Observed.** The family grew in successive commits:

| Commit | Observed Change |
| --- | --- |
| `ae7112f4557365e91ce80169c346c47e0f95a2fc` (August 13) | Added the standard/ReSTIR transport family, matrix, tests, and documentation. |
| `54a57b08a462ad83979ccc8912570f2c6cc7ea03` (August 13) | Added generalized accumulation and expanded temporal behavior. |
| `0224649055f2218dcf1dbab4af4a1ea8a6b894f9` (August 14) | Added transport controls, primary-surface work, and resampling surface. |
| `3bc13fd3c170d746366f01404a7c4b726efdcab9` (August 15; historical branch label `codex/settings-menu-revamp`) | Expanded settings and denoising UI. |
| `275ef2bd8762f49cda581287605ec2f633d8070e` (August 15; historical branch label `codex/ratio-shadow-msaa-cmaa2-integration`) | Reworked temporal stability and path behavior. |
| `63243721fead2578995ea9e0018cf449ee1f4b1d` (August 22) | Later startup work still touched the path pass. |

Within roughly a day, ReSTIR GI moved from temporal-only reuse without a valid
cross-pixel transform to spatial reconnection. Stable resolve moved from a
narrow ReSTIR path to solver-wide signal groups. Defaults moved from eight
bounces, one sample, and no clamp to four bounces, two samples, and filtering.
Historical plans also cited 327 or 333 shader tasks and 50 binaries; exact
pre-cut remeasurement found 311 tasks, 48 runtime bundle items, and 25 path
tasks. Those totals came from different repository states and are not directly
comparable.

**Estimate — medium confidence.** The expected narrow implementation became at
least three major feature slices plus repair work across August 13–22. No
authoritative original schedule or time ledger survives, so calendar overrun
cannot be measured honestly; the repeated follow-on commits are the defensible
drift signal.

**Inference — high confidence.** Drift began in the August 13 initial feature
slice: its “smallest complete” goal already delivered three solvers and a mode
matrix before equal-time proof. Accumulation and control work on August 13–14,
then denoising and temporal changes on August 15, compounded that breadth.

**Inference — high confidence.** UI, command, snapshot, and compatibility
surface preceded equal-time image-quality proof. The first feature commit
already contained three solver choices and the compile matrix.

**Inference — high confidence.** Source-contract and arithmetic tests created
more confidence than the rendered evidence justified: plans foregrounded those
passes while the controlled visual/performance matrix remained absent.

**Inference — high confidence.** Too many variants existed before one stable,
measured baseline. Three solvers, three NEE policies, RTXDI states, history
options, reconstruction options, and debug modes multiplied interpretation
before a single reservoir strategy won.

**Inference — high confidence.** Resource and history complexity obscured
correctness. Reprojection, proposal validity, radiance counts, direct and
indirect reservoirs, stable planes, variance, and reset ownership changed while
visible sample counts remained hard to interpret.

**Inference — high confidence.** Breadth-first scope, inexpensive source-test
success, and the absence of tracked metrics and a fixed retirement gate explain
the persistence of the matrix. **Inference — low-to-medium confidence.** The
algorithm itself could not win in UVSR; no fair benchmark exists to support that
stronger claim.

## Deletion Decision, Replacement, and Risks

**Observed.** Keeping ReSTIR selectable would preserve an unproven user promise,
18 retired snapshot values, 24 estimated excess shader tasks, large optional
histories, and reset/debug branches. `lighting.solution` is the nineteenth
baseline value and remains for the fixed tracer. Deletion is safer because
there is no measured rule for choosing the modes and no evidence that
compatibility cost protects useful output. UVSR retains one conventional
standard path tracer with no configurable transport matrix.

The retired contract is the ReSTIR PT/GI and RTXDI solver surface, temporal and
spatial reservoir reuse, motion-proposal reuse, stable-plane and optional
spatial-resolve machinery, shared-primary configuration, their settings and
debug modes, the dedicated source-contract suite, configurable assertions in
the retained settings test, compile variants, resources, commands, snapshots,
and active documentation. Some narrowed `path_tracing_*` filenames may remain
as owners of the fixed tracer; the measured historical files are recovery paths,
not a claim that every filename must disappear.

The exact deleted paths are:

- `src/path_tracing_primary_surface_cs.hlsl`;
- `src/path_tracing_stable_plane_resolve_cb.h`;
- `src/path_tracing_stable_plane_resolve_cs.hlsl`;
- `src/path_tracing_stable_plane_resolve_pass.cpp`;
- `src/path_tracing_stable_plane_resolve_pass.h`;
- `tests/path_tracing_source_contract_tests.cpp`;
- `docs/exec-plans/completed/path-tracing-denoising-convergence-v3.md`;
- `docs/exec-plans/completed/path-tracing-resampling-controls.md`;
- `docs/exec-plans/completed/path-tracing-transport.md`;
- `docs/exec-plans/completed/shared-primary-temporal-gi.md`;
- `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`; and
- `docs/exec-plans/completed/path-tracing-ui-cleanup.md`, excluded from the
  five-plan burden arithmetic as noted above.

Main path settings, pass, shader, helpers,
`tests/path_tracing_settings_tests.cpp`, `src/shaders.cfg`, UI, snapshot, and
CMake owners remain narrowed in place rather than deleted. The source-contract
suite is deleted; only the singular settings test remains narrowed.

This report and the commits below are the restoration mechanism. The integrated
product should retain no dormant ReSTIR setting, hook, alias, or compatibility
path.

The main risk is deleting an implementation that might improve under a fair
future redesign. Other risks are stale references, incomplete history release,
changed snapshots, and regression of the retained tracer while shared code is
removed. Required validation is zero active ReSTIR/RTXDI/stable-plane references,
one fixed path task, clean settings/snapshot/command catalogs, representative
static and motion output, every reset boundary, high-sample comparison, measured
frame time and memory, full developer gates, and smoke of the exact production
package. These checks belong to the integrated candidate, not this report.

**Final validation status: pending.** This record still needs the integrated
zero-reference/one-task/catalog check, full developer gate, exact production-
package smoke, static/motion/reset rendered matrix, high-sample comparison, and
measured frame-time and memory results. Source deletion and historical smokes do
not close those gates.

## Exact Recovery Boundaries

- Recover the initial transport family from
  `ae7112f4557365e91ce80169c346c47e0f95a2fc`.
- Recover generalized accumulation from
  `54a57b08a462ad83979ccc8912570f2c6cc7ea03`.
- Recover the later resampling, primary-surface, and control surface from
  `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`.
- Recover denoising/settings and later temporal changes from
  `3bc13fd3c170d746366f01404a7c4b726efdcab9` and
  `275ef2bd8762f49cda581287605ec2f633d8070e`.
- Recover the complete, internally consistent pre-cut family from
  `e29a41245dbd0e6fd7a819d2341646419ab76e72`; this is the authoritative
  restoration point. The full commit IDs above are the recovery evidence.

Cherry-picking only a shader is unsafe. Recovery must include settings,
constant buffers, bindings, resources, passes, invalidation, UI, commands,
snapshots, build tasks, tests, documentation, and license review.

## Clean-Room Retry Gate and Future-Agent Advice

A retry should be a clean-room experiment only after the fixed conventional
tracer has a tracked high-sample reference, reproducible equal-time harness,
explicit frame-time and memory budgets, representative scenes/motion, and a
named retirement date. It must not inherit removed schemas or compatibility
surface merely because history exists.

1. Establish and freeze the standard-tracer baseline first.
2. Define equal-time image-quality, error, and noise targets before coding.
3. Work as an isolated experiment so failure can be deleted without migration.
4. Implement one reservoir strategy, not a solver/NEE/reuse matrix.
5. Add no UI, commands, snapshots, persistence, or compatibility until it wins.
6. Test static views, motion, disocclusion, lighting changes, camera cuts, scene
   changes, and every reset transition.
7. Compare against tracked high-sample ground truth, not another low-sample
   mode or a non-black smoke.
8. Set explicit memory and frame-time budgets and measure both continuously.
9. Fix a retirement gate: if the candidate misses the target by the review
   date, delete it rather than broadening its controls.
10. Use source-spelling tests only for narrow wiring contracts; never treat
    them as primary rendered-quality or performance evidence.
