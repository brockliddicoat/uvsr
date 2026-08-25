# Sample Accumulation Settings Postmortem

## Record Identity and Evidence Labels

This record explains retirement of the generalized sample-accumulation policy
surface from exact pre-cut source
`e29a41245dbd0e6fd7a819d2341646419ab76e72`. Shared measurement and ordering are
in the [Stage-Two Cutdown Decision Record](2026-08-23-stage-two-cutdown-decisions.md),
and the coupled path-tracing histories are covered in the
[ReSTIR Path Tracing Postmortem](2026-08-23-restir-path-tracing.md).

- **Observed** means reproduced from that commit, its history, or a tracked
  contemporaneous report.
- **Estimate** means a bounded calculation without a surviving direct timing or
  capture; confidence is stated.
- **Inference** means a plausible explanation rather than a proven cause;
  confidence is stated.

## Intended User Benefit and Complete Policy Surface

**Observed.** The feature intended to let users trade responsiveness for lower
noise across stochastic ray-marching and path-tracing modes. It generalized a
single progressive mean into configurable averaging, scheduling, history,
warmup, error, and workload policies.

The baseline had ten persisted command/snapshot values:
`noise.accumulate-samples`, `noise.accumulation-mode`,
`noise.accumulation-averaging`, `noise.accumulation-scheduling`,
`noise.accumulation-effective-history`, `noise.accumulation-history-preset`,
`noise.accumulation-minimum-samples`, `noise.accumulation-target-error`,
`noise.accumulation-minimum-update-rate`, and
`noise.accumulation-workload-preset`. `noise.animate-samples` and
`pathing.samples-per-pixel` also changed estimator behavior, although they were
not part of those ten accumulation values.

The cut retires the nine tuning values after `noise.accumulate-samples`.
`noise.accumulate-samples` remains the Ray Marching cumulative-history toggle;
only its former Path Tracing role retires because the fixed tracer always owns
its history.

**Observed.** Every policy and shortcut at the pre-cut commit was:

| Surface | Values and Defaults |
| --- | --- |
| Main preset | `Progressive`: cumulative/every pixel/history 64/minimum 16/error 2%/update 1/16; `Responsive`: exponential/every pixel/32/16/2%/1/16; `Variance Guided`: cumulative/variance guided/64/16/2%/1/16 and the factory policy. |
| Averaging | Cumulative mean or exponential history. |
| Scheduling | Every pixel or variance guided. |
| History shortcut | Quick Preview 8, Responsive 32, Balanced 64, Stable 256, Very Stable 1024. |
| Workload shortcut | Full Quality: minimum 32, error 1%, update 1/4; Balanced: 16, 2%, 1/16; Performance: 8, 4%, 1/32; Maximum Savings: 4, 8%, 1/64. |
| Manual range | Effective history 2–4096, minimum samples 2–256, target error 0.1%–25%, minimum update rate 1/256–1. |
| Derived command state | Unmatched manual history/workload combinations reported get-only `custom`; `custom` was not a settable policy. |
| Sequence state | Frame phase, successful-sample count, and animated-reset behavior. |

The accumulation toggle was factory-off. The many shortcuts changed the same
underlying fields. A saved snapshot could therefore preserve a manual
combination whose label no longer described it.

## Renderer Interactions and State Ownership

**Observed.** One global accumulation toggle covered ray-marching and path-
tracing solutions. In ray marching, a prepare mask identified stochastic
producers and a transactional resolve updated mean, variance, and count. When
accumulation owned long-term history, TAA was disabled as the history owner but
could still provide jitter. With accumulation off, path tracing could use TAA
when shared primary was enabled.

For path tracing, scheduling occurred before traversal. Samples per pixel still
created independent fresh paths; ReSTIR donor counts were additional work; and
shared-primary generation remained full-frame work independent of the update
mask. Thus a displayed accumulation count did not necessarily equal rays,
reservoir candidates, indirect samples, or full-frame work.

The retained tracer now has one accepted-history count contract. Its R32 GPU
texture is the cumulative mean's per-pixel count storage. The UI asynchronously
reads and displays the center pixel from that same texture. No parallel CPU
dispatch counter remains. One dispatch attempts one path per pixel, but invalid
pixel samples do not advance the mean or the accepted count.

**Observed.** Histories were intended to reset on camera, scene, extent,
geometry, material, light, environment, lighting solution, solver, NEE, reuse,
reconstruction, noise, and accumulation-policy changes. Motion reuse was an
exception: revalidated proposal/seed histories could survive while radiance
statistics reset. The generalized pass maintained two RGBA32F means, two R32
counts, two RGBA32F variance images, and one R32 attempt mask.

## Measured Source, Resource, Settings, Test, and Documentation Cost

**Observed.** Physical source lines, including blanks, at the pre-cut commit:

| Source | Lines |
| --- | ---: |
| `src/sample_accumulation_settings.h` | 625 |
| `src/sample_accumulation.hlsli` | 145 |
| Accumulation pass implementation/header | 627 |
| Constant buffer and prepare/resolve shaders | 213 |
| **Total** | **1,610** |

The final two rows resolve to a 485-line pass, 142-line header, 21-line constant
buffer, 100-line prepare shader, and 92-line resolve shader. The dedicated
settings test was 578 lines. The feature added two generic shader tasks, prepare
and resolve; its policy choices did not create a compile-time mode matrix.

**Observed.** The ten values above reached command handling and canonical
snapshots. Their runtime callers included UI presets, scheduling, stochastic
producer masks, path dispatch, lighting accumulation, TAA ownership, debug
counts, invalidation, and settings reset/default handling.

**Observed.** Baseline `CTestCostData.txt` records 0.0216948 seconds for the
dedicated CPU settings test, which is removed. No isolated incremental
shader-build timing survives; both generic shader tasks remain narrowed, so no
shader-task deletion is claimed.

**Observed.** Dedicated accumulation sections occupied 110 lines/937 whitespace
tokens in the path-transport guide and 114 lines/1,035 tokens in the noise guide.
Three accumulation-bearing plans added 636 lines/8,161 tokens:

- `docs/exec-plans/completed/path-tracing-denoising-convergence-v3.md`;
- `docs/exec-plans/completed/temporal-accumulation-ui-repair.md`; and
- `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`.

The minimum documented surface was therefore 860 lines/10,133 tokens, with the
important caveat that those plans also covered retained or adjacent renderer
work.

**Observed calculation.** The seven runtime GPU textures—two RGBA32F means, two
R32 counts, two RGBA32F variance images, and one R32 attempt mask—cost 76 bytes
per pixel: about 150.3 MiB at 1920x1080 and 601.2 MiB at 4K. They were allocated
only while enabled; the disabled dummy was one pixel. Policy modes shared the
same resources. A retained one-policy accumulator still needs mean/count
history, so the full 76-byte figure is burden context, not a claimed final
memory saving.

The retained Path Tracing pass separately owns five full-resolution textures:
an `RGBA32_FLOAT` mean, `R32_UINT` accepted count, `RGBA16_FLOAT` first-hit
motion, `R32_FLOAT` first-hit depth, and `R32_UINT` retry generation. Their
format total is 36 bytes per pixel, about 71.2 MiB at 1920x1080 and 284.8 MiB at
4K. This is source-format arithmetic rather than measured residency. Retry
generation changes a rejected attempt's phase and seed, then clears on
acceptance or history reset; it is neither another product count nor another
radiance history.

Separately, prepare and resolve were two compiled shader artifacts in the
engine package. Those packaged DXIL shaders are not two more runtime history
textures. There was no separate accumulation binary or external dependency.

## Direct Evidence, Gaps, and Ambiguities

**Observed.** Evidence consisted of CPU arithmetic/settings tests, source
contracts, shader compilation, full CTest runs, and bounded Sponza smokes that
exercised resets. Those checks supported formulas, ranges, dispatch wiring, and
the absence of obvious crashes or black output.

The evidence boundary is specific:

| Evidence | What It Supported | What It Did Not Support |
| --- | --- | --- |
| Direct rendered smoke | Dispatch, non-black presentation, and some reset paths in bounded Sponza runs. | Equal-time quality, convergence, or a useful difference between policies. |
| CPU arithmetic/settings tests | Mean/variance formulas, ranges, preset expansion, and schedule decisions. | GPU history lifetime, displayed-count meaning, or image quality. |
| Source contracts | Expected callers, bindings, reset spellings, and package task wiring. | Execution, resource correctness, or rendered output. |
| Shader compilation and CTest | Accepted syntax/interfaces and passing test assertions. | Visual correctness, performance, memory residency, or policy value. |

**Observed.** No tracked matched image-error series compared cumulative,
exponential, every-pixel, or variance-guided policies at equal time. No raw
capture set demonstrates a user-visible win for any preset. There is no
preserved per-mode frame-time, update-density, memory-residency, or isolated
test-time series. Source and arithmetic evidence therefore exceeded rendered
evidence.

No individual preset or policy had tracked direct rendered proof of a useful
tradeoff. The three main presets, two averaging policies, two scheduling
policies, five history shortcuts, and four workload shortcuts were supported
mainly by source-contract and arithmetic evidence; the smokes exercised the
system rather than comparing those choices.

The reset surface left these practical ambiguities:

- **Camera and scene.** Resets were wired, but bounded smokes did not prove every
  camera cut, scene replacement, geometry/material mutation, or motion case.
- **Resolution.** Extent changes reset histories, but no tracked resize sequence
  proves count, variance, mask, and presentation changed atomically.
- **Exposure.** Environment radiance/exposure changes alter the estimator and
  require reset; downstream presentation-only exposure need not. The UI and
  evidence did not make that boundary easy to observe.
- **Denoising.** Method/topology changes should invalidate dependent histories,
  while display strength or signal grouping might preserve radiance means. The
  combined controls did not present a simple, proven rule.
- **Mode and policy.** Lighting solution, solver, NEE, averaging, scheduling,
  reuse, and reconstruction changes crossed several counters and histories.
  Reset wiring existed, but displayed sample count could still mean direct
  frames, indirect SPP work, reservoir donors, or successful masked updates.
- **Motion.** Preserving revalidated proposal histories while resetting
  radiance state was intentional, but made “reset” non-atomic to a user or test.

## Drift, Confidence, and Lessons

**Observed.** Generalized accumulation landed in
`54a57b08a462ad83979ccc8912570f2c6cc7ea03`. Resampling and controls expanded in
`0224649055f2218dcf1dbab4af4a1ea8a6b894f9`; settings/denoising grew in
`3bc13fd3c170d746366f01404a7c4b726efdcab9`; and temporal behavior changed again
in `275ef2bd8762f49cda581287605ec2f633d8070e`. The complete pre-cut state is
`e29a41245dbd0e6fd7a819d2341646419ab76e72`.

**Inference — medium confidence.** Development began to drift in the August 13
generalization commit, when a single cumulative accumulator became multiple
averaging, scheduling, history, error, and workload choices without comparative
rendered proof. Controls expanded on August 14 and settings/temporal behavior
changed again on August 15. No authoritative schedule survives, so calendar
overrun is not measurable; surface growth is the defensible drift measure.

**Inference — high confidence.** Settings were exposed before their user value
was proven. Snapshot and command compatibility existed without a matched
rendered comparison demonstrating when users should choose each policy.

**Inference — high confidence.** Multiple means, variances, counts, phases,
masks, reservoir donor counts, SPP counts, and proposal histories made reset and
display semantics difficult to reason about. A source test could confirm one
formula while missing a cross-history visual error.

**Inference — high confidence.** Preset proliferation attempted to explain an
unsettled contract rather than proving distinct durable needs. Snapshot presence
then made removal appear expensive even though compatibility did not establish
value. **Inference — low confidence.** Any one discarded adaptive policy could
never help UVSR; the missing fair comparison cannot support that claim.

The durable lesson is to define ownership, work counting, and invalidation
before exposing policy. Tests must follow visible claims, not substitute for
them, and a failed choice should be retired before persistence multiplies cost.

## Deletion Decision, One Contract, and Required Validation

**Observed.** Ray Marching retains `noise.accumulate-samples`, factory off. When
enabled, its prepare/resolve transaction attempts every eligible pixel, accepts
only finite scene-linear samples, advances that pixel's cumulative mean and
internal count, and prevents TAA from owning a competing long-term history.
Image-defining changes reset the history. Path Tracing ignores that toggle and
always owns its fixed cumulative history. Its one product count is the
asynchronously read center-pixel accepted-sample value from the GPU history
that backs the mean.

For both retained accumulators, `UINT32_MAX` is terminal overflow safety, not a
selectable history cap; mean/count stop advancing there. An invalid candidate
leaves a valid prior history unchanged. Path Tracing additionally repairs a
non-finite stored mean to empty history and publishes that repair even when the
new candidate is rejected. Ray Marching validates the new candidate but does
not repair a non-finite stored mean.

Deletion is safer than leaving generalized policies selectable because no
rendered evidence defines a correct choice, saved combinations can outlive
their labels, and partial resets can present stale confidence as convergence.
The replacement removes policy branches while preserving required AO/GI
histories and the minimum fixed history owned by each lighting solution.

The retired contract is nine tuning values; three main presets; exponential and
variance-guided policies; history/workload shortcuts; variance histories and
policy counters; their UI, commands, snapshots, tests, and active documentation;
and only the Path Tracing role of `noise.accumulate-samples`. The exact deleted
paths are:

- `src/sample_accumulation_settings.h`;
- `tests/sample_accumulation_settings_tests.cpp`;
- `docs/exec-plans/completed/path-tracing-denoising-convergence-v3.md`;
- `docs/exec-plans/completed/temporal-accumulation-ui-repair.md`; and
- `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`.

The toggle, `src/sample_accumulation.hlsli`, both lighting-accumulation shaders,
pass, constant buffer, renderer/catalog/snapshot owners, and two shader tasks
remain narrowed in place. The 1,610-line baseline is burden context, not
deleted LOC.

Risks are an incomplete reset list, accidental removal of protected AO/GI
history, stale snapshot/command references, altered TAA ownership, or confusing
attempted dispatches with accepted samples. Camera, scene, resize, material,
lighting, environment, mode, noise, and explicit invalidation reset the accepted
history to zero. Readback is asynchronous and intentionally reports only the
viewport center, not a whole-image minimum or average. Integrated validation
must cover camera motion/cuts, scene load, resolution, exposure/radiance,
material, lighting, settings and mode changes; verify the accepted counter;
exercise TAA and path-tracing interactions; preserve every AO/GI option; search
for retired policy names; run focused and full developer gates; and smoke the
exact production package.

**Final validation status: pending.** This record still needs the integrated
retired-name/catalog check, focused and full developer gates, exact production-
package smoke, and the accepted-count/reset/TAA/Path-Tracing/AO/GI runtime
matrix. Source edits and focused contracts are not runtime or package proof.

## Exact Recovery Boundaries

- Restore the generalized accumulation implementation from
  `54a57b08a462ad83979ccc8912570f2c6cc7ea03`.
- Restore its resampling/control integration from
  `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`.
- Restore later settings/denoising and temporal changes from
  `3bc13fd3c170d746366f01404a7c4b726efdcab9` and
  `275ef2bd8762f49cda581287605ec2f633d8070e`.
- Restore the complete, mutually consistent pre-cut source from
  `e29a41245dbd0e6fd7a819d2341646419ab76e72`; this is authoritative.

Recovery must include settings, pass and shaders, resources, producer masks,
path/TAA ownership, invalidation, UI, commands, snapshots, build tasks, tests,
and documentation. Copying only `sample_accumulation_settings.h` cannot restore
the contract.

Restore a removed policy only after a controlled equal-time rendered comparison
shows a repeatable benefit over the retained cumulative baseline, with explicit
accepted-GPU-count meaning, reset ownership, memory and frame-time budgets, and
a retirement gate. Restore one winning policy cleanly; do not restore the
former preset matrix or snapshot compatibility as a fallback.

## Future-Agent Advice

1. For any replacement, **begin with one counter and one history**.
2. Specify the reset contract before adding any controls.
3. Test camera, scene, resolution, exposure, material, lighting, settings, and
   mode changes with tracked rendered output.
4. Verify accepted-work advance and every reset directly; never substitute CPU
   dispatch attempts for accepted GPU work.
5. Expose a setting only after a repeatable user-visible tradeoff is proven.
6. Do not retain a failed policy merely because old snapshots contain it.
7. Retire failed variants promptly rather than adding compatibility, before
   commands, presets, and histories make them look permanent.
