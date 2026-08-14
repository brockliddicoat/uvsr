# Shared Primary Surface, Temporal Reconstruction, and Spatial GI

## Status

- State: completed locally; awaiting optional user product acceptance
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/path-tracing-resampling-controls` at `work/path-tracing-resampling-controls`
- Base commit: `54a57b08a462ad83979ccc8912570f2c6cc7ea03` plus the technically verified prior-task tracked diff `485e6169943ede2cebb4fc5b1fa82c6655159701`
- Started: 2026-08-13
- Last updated: 2026-08-14
- Planned archive: `docs/exec-plans/completed/shared-primary-temporal-gi.md`

## Goal and Done Condition

Goal: Extend the preserved path-tracing resampling candidate with production defaults, automatic Russian roulette, an optional shared ray-traced primary-surface signal, separated direct and indirect lighting histories, motion-vector temporal reconstruction, and valid spatial ReSTIR GI reconnection.

Done when:

- [x] Every path-tracing preset defaults to Raw presentation, firefly clamp 3, two samples, and four bounces, with a compact `Samples` label.
- [x] Russian roulette is a single toggle whose internal start rule follows the active path depth and cannot terminate primary visibility.
- [x] `Shared Primary Surface` produces a full-resolution ray-traced receiver signal and motion/validation data without reintroducing raster geometry.
- [x] Direct and indirect transport have separate additive signals and histories; newly exposed pixels receive a bounded low-variance starting path instead of black or replicated blocks.
- [x] Motion-vector temporal reconstruction rejects invalid history using current/previous primary-surface data and preserves estimator/presentation separation.
- [x] Spatial GI reconnects a donor path at the current receiver with recomputed geometry, target weight, visibility, and bounded reservoir contribution rather than averaging donor RGB.
- [x] CPU/HLSL contracts, every shader permutation, the full test suite, documentation checks, and a bounded renderer smoke test pass on one exact candidate artifact.
- [x] High-risk shader/resource changes receive an independent review before completion.

## Scope

In scope:

- Path-tracing defaults, settings, UI, commands, accumulation, primary visibility, motion data, direct/indirect signals, temporal reconstruction, ReSTIR GI payloads and reconnection, scheduling, documentation, and focused tests.
- Clean-room adaptation of source-backed architectural ideas from exact ZetaRay commit `6fd82f1e73360aa196e733e91b548bc95e19968a` and primary ReSTIR GI literature.
- A new isolated build tree while preserving `b-pt/bin/uvsr.exe` unchanged as the prior technically verified fallback.

Non-goals:

- Editing Donut, adopting a raster G-buffer beneath path tracing, copying third-party source text, adding another denoiser, publishing, pushing, opening a pull request, merging, or releasing.
- Claiming performance improvement without a matched benchmark window.
- Treating presentation reconstruction as additional unbiased path samples.

Affected subsystems and paths:

- `src/path_tracing_*`, `src/uvsr.cpp`, temporal accumulation/TAA helpers, settings/catalog code, and relevant shader configuration only if a new entry point is proven necessary.
- Focused path-tracing, renderer, UI, accumulation, and source-contract tests.
- `README.md`, `docs/path-tracing-transport.md`, `docs/advanced-settings.md`, `docs/noise.md`, and this plan.

Shared hotspots reserved for the coordinator:

- All writable source, shaders, tests, documentation, CPU/HLSL binding contracts, `README.md`, build trees, Git state, and renderer/GPU runtime.

## Baseline

- Canonical repository/remote: the user explicitly based the active path-tracing lineage on `54a57b0`; remote freshness is not required for this local extension.
- Local versus remote state: root `main` is independently ahead two and behind 51; this work remains isolated in the existing task worktree.
- Verified source commit/build: prior dirty candidate diff `485e6169943ede2cebb4fc5b1fa82c6655159701`; `b-pt/bin/uvsr.exe` SHA-256 `BC57793D6C612AC24747B4EAC5E3759C6F4D091A93A65F1075C32D1838C036B7`; technically verified, not Canonical verified.
- GPU, scene, camera, resolution, and settings preset when relevant: bounded smoke will use Sponza Decorated, Position 1, 1920 by 1080; a performance claim requires a separate matched cold benchmark.
- Known pre-existing failures: none established in the prior candidate; `b-pt/` is preserved generated output.
- Prior plan: `docs/exec-plans/completed/path-tracing-resampling-controls.md` in this worktree.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| PT2-INV-1 | Exact ZetaRay primary-surface, motion, and presentation architecture | Completed | Coordinator design |
| PT2-INV-2 | Valid spatial GI reconnection estimator and payload | Completed | Coordinator design |
| PT2-INV-3 | UVSR insertion map, defaults, resources, and tests | Completed | Coordinator implementation |
| PT2-IMPL | Frozen CPU/HLSL/resource/history contract | Completed | Review and verification |
| PT2-REVIEW | Independent shader, estimator, and lifetime review | Completed | Completion decision |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- `Shared Primary Surface` is optional and ray traced; disabled mode preserves the existing zero-raster transport path.
- Primary-surface outputs include enough current/previous receiver data for motion reprojection and GI donor validation, with explicit invalid/background states.
- Direct and indirect signals remain separately accumulated and add exactly once at presentation.
- Temporal reconstruction never writes fabricated presentation samples into radiance moments, reservoir sample counts, or GI proposal weights.
- Spatial GI stores reconstructable path state and recomputes receiver-dependent quantities; invalid, occluded, or non-finite donors contribute zero.
- Every setting is clamped, deterministic, command-addressable where applicable, included in history signatures, and resets only incompatible state.
- CPU and HLSL layouts remain aligned and every resource transition/UAV ordering is explicit.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| PT2-INV-1 | `/root/zeta_primary_taa_research` | Read-only local/web | Prior candidate plus ZetaRay `6fd82f1` | None | None | Completed |
| PT2-INV-2 | `/root/spatial_gi_reconnection_research` | Read-only local/web | Prior candidate plus primary ReSTIR GI sources | None | None | Completed |
| PT2-INV-3 | `/root/uvsr_pt2_architecture_audit` | Read-only task worktree | Prior candidate diff `485e6169` | None | None | Completed |
| PT2-IMPL | `/root` | Task worktree | Prior candidate diff `485e6169` | All in-scope writable paths | PT2-INV-1 through PT2-INV-3 | Completed |
| PT2-REVIEW | All three independent reviewers | Read-only task worktree | Frozen integrated candidate `92592f22` plus primary shader `6E08ED14` | None | PT2-IMPL | Completed |

## Assignment Contracts

### Primary Surface and Temporal Architecture

- Owner/thread: `/root/zeta_primary_taa_research`
- Branch/worktree: read-only official web sources and task worktree.
- Base commit/state: prior UVSR candidate; exact ZetaRay commit `6fd82f1e73360aa196e733e91b548bc95e19968a`.
- Read scope: ZetaRay G-buffer RT, path-tracer orchestration, motion-vector, temporal/upscaler, and presentation sources; compatible UVSR files.
- Write scope: none.
- No-touch scope: all workspace files, Git, builds, processes, and runtime.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: prior technically verified candidate.
- Interface/invariant contract: distinguish primary visibility, motion, direct, indirect, denoising, and presentation; report exact pass/resource ordering.
- Deliverable: source-linked architecture and smallest coherent UVSR mapping.
- Done when: exact current behavior and mismatches in the requested analogy are documented.
- Required verification: primary-source static inspection only.
- Allowed Git and external actions: read-only inspection only.
- Stop and report if: exact source is unavailable or a public contract would be guessed.
- Handoff revision/artifact: source-linked ZetaRay `6fd82f1` architecture audit and frozen-candidate review.
- Handoff acknowledged by/on: `/root`, 2026-08-14.

### Spatial GI Reconnection Design

- Owner/thread: `/root/spatial_gi_reconnection_research`
- Branch/worktree: read-only official sources and task worktree.
- Base commit/state: prior UVSR candidate and exact cited ZetaRay revision.
- Read scope: ReSTIR GI primary sources/reference source and local path-tracing shaders/resources.
- Write scope: none.
- No-touch scope: all workspace files, Git, builds, processes, and runtime.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: prior candidate exposes bounded spatial-neighbor control but GI currently rejects it.
- Interface/invariant contract: valid receiver reconnection, target/PDF/Jacobian/visibility recomputation, bounded contribution, and no donor-RGB averaging.
- Deliverable: algorithm, payload, binding, scheduling, rejection, and test contracts.
- Done when: the coordinator can freeze an implementable estimator without inventing ZetaRay behavior.
- Required verification: primary-source and static code inspection.
- Allowed Git and external actions: read-only inspection only.
- Stop and report if: source lacks an implementation; report that fact and use cited primary literature only.
- Handoff revision/artifact: clean-room rough-diffuse GI reconnection design and frozen estimator audit.
- Handoff acknowledged by/on: `/root`, 2026-08-14.

### UVSR Extension Architecture Audit

- Owner/thread: `/root/uvsr_pt2_architecture_audit`
- Branch/worktree: read-only task worktree.
- Base commit/state: HEAD `54a57b0` plus prior candidate diff `485e6169`.
- Read scope: settings, UI, path-tracing CPU/HLSL, motion/TAA helpers, renderer, docs, and tests.
- Write scope: none.
- No-touch scope: all files, Git, builds, processes, and runtime.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: prior technically verified candidate.
- Interface/invariant contract: exact insertion points and reusable UVSR mechanisms; no raster primary path and no estimator/presentation contamination.
- Deliverable: line-level implementation map, default/Russian-roulette behavior, resource hazards, and test matrix.
- Done when: writable scope and dependency order are concrete.
- Required verification: read-only static inspection.
- Allowed Git and external actions: none beyond read-only inspection.
- Stop and report if: tracked diff changes or a material product choice is required.
- Handoff revision/artifact: UVSR insertion map plus frozen CPU/HLSL/resource-lifetime review.
- Handoff acknowledged by/on: `/root`, 2026-08-14.

## Integration Order

1. Reconcile all three investigations and freeze primary-surface, signal, motion, GI-payload, and settings contracts.
2. Implement defaults and automatic Russian roulette with focused settings/UI tests.
3. Implement shared primary visibility and separate direct/indirect signal resources, then verify bindings and all shader permutations.
4. Implement motion-vector temporal reconstruction as presentation-only validated history.
5. Extend the GI proposal payload and add bounded spatial receiver reconnection with estimator tests and dispatch accounting.
6. Run targeted tests after each contract, then the full build/test/document suite.
7. Obtain independent review, repair confirmed findings, and run a bounded renderer smoke test on the final artifact.
8. Reconcile user-facing documentation and archive this plan.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Defaults and controls are exact | Settings/catalog/UI unit and source-contract tests | Focused Release CTest targets | Passed |
| CPU/HLSL/resources agree | Constant-layout tests and every affected shader permutation | Isolated Release build `b-pt2` | Passed; 333 shader tasks and Release `ALL_BUILD` succeeded |
| Signals add exactly once | Source contracts plus debug/runtime comparison of direct, indirect, and composite | Focused tests and bounded renderer smoke | Passed for RTX PT, RESTIR PT, and RESTIR GI |
| Temporal reconstruction is valid and presentation-only | Motion/depth/normal/material rejection tests; history-signature tests | Unit/source-contract tests and moving-camera smoke | Passed; focused camera input showed no replicated block preview |
| Spatial GI performs reconnection | Deterministic analytical tests for target/Jacobian/rejection and source/runtime checks | Focused tests, shader compilation, GI debug smoke | Passed; one spatial GI neighbor rendered live |
| Exact candidate is launchable | Build manifest, source-diff identity, executable hash, clean process exit | Full Release build and bounded smoke | Passed; executable SHA-256 `0595560DFA35C93C73ED26464A6F9FF5A7FC830FC734D7C872D03FD7CB81C741` |

For performance work, record:

- baseline and candidate commit plus dirty-diff identities;
- GPU, scene, fixed camera, resolution, settings preset, and all new toggles;
- warmup and sample window/count;
- total frame time plus primary, direct, indirect, temporal, and GI reconnection pass costs;
- correctness/image-quality guardrails;
- before/after captures and raw measurement artifact.

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-13 | Extend the existing task worktree but preserve `b-pt/bin/uvsr.exe` and its exact source identity. | The prior source is an uncommitted, technically verified dependency; a distinct `b-pt2` build preserves the fallback without fabricating commit provenance or copying a large dirty patch into another branch. | All |
| 2026-08-13 | Use one coordinator writer after parallel read-only research. | Primary outputs, lighting histories, motion reconstruction, and GI payloads share bindings and shader contracts; competing writers would create unsafe schema overlap. | PT2-IMPL |
| 2026-08-13 | Treat the ZetaRay links as architectural references, not permission to copy source or assume its disabled GI spatial path works. | UVSR needs a source-backed clean-room design and its own validation; current ZetaRay behavior must be reported exactly. | PT2-INV-1, PT2-INV-2, PT2-IMPL |
| 2026-08-13 | Keep the existing indirect RGB variance as Spatial Path Resolve's confidence signal under Shared Primary Surface. | Raw is the new default and remains correct. Adding a separately sampled direct-light variance would expand the optional resolve ABI late in this high-risk change; the documented limitation is possible direct-light under-filtering, not estimator or TAA corruption. | PT2-IMPL, PT2-REVIEW |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-13 | Coordinator | Completed | Prior candidate diff `485e6169`; fallback executable SHA-256 `BC57793D...036B7` | Preflight, prior plan, worktrees, branches, active plans, and Coming Soon inspected | Preserved as fallback |
| 2026-08-13 | PT2-INV-1 through PT2-INV-3 | Completed | ZetaRay `6fd82f1`, ReSTIR GI paper, and prior UVSR candidate | Static/primary-source inspection | Findings integrated clean-room |
| 2026-08-14 | Coordinator | Completed | Frozen source/test diff `92592f22`; primary shader SHA-256 `6E08ED14...D5F` | Release `ALL_BUILD`, 333 shaders, focused and full CTest, document validation | Candidate technically verified |
| 2026-08-14 | Independent reviewers | Completed | Same frozen source/test and primary-shader identities | No P0/P1 estimator, motion/TAA, binding, history, barrier, capability, or packaging defect | P2 follow-ups recorded below |
| 2026-08-14 | Coordinator | Completed | `b-pt2/bin/uvsr.exe` SHA-256 `0595560D...1C741` | 1920 by 1080 Sponza smoke: all three solvers, GI spatial neighbor one, focused camera input, clean exit | No performance claim |

## Risks and Escalation Triggers

- A full-resolution shared primary ray adds fixed per-frame cost; it must eliminate duplicate primary tracing or materially improve disocclusion quality when enabled.
- Direct/indirect separation can double-count emission or direct lighting unless contribution ownership is explicit.
- Temporal reconstruction can ghost or leak across disocclusions if motion, jitter, depth, normal, material, and bounds validation disagree.
- Spatial GI reconnection can become biased or numerically unstable if donor state is incomplete, receiver-dependent target terms are reused, or Jacobians are unclamped.
- Two default SPP and full-resolution primary work can multiply cost beyond the dispatch-safety bound; every pass must be included.
- Performance comparisons are invalid without a quiet, matched benchmark window.

Stop and ask the user if:

- Source-backed implementation requires choosing between materially different visible defaults or bias/variance tradeoffs that the request does not resolve.
- Required third-party code or asset licensing is uncertain.
- A benchmark would require interfering with unrelated processes or a testing window the user has not made available.

## Completion

- Final integrated commit: the later single publication commit containing this plan; its exact SHA and push result are recorded in the final task handoff.
- Verification summary: Release `ALL_BUILD`, all 333 shader tasks, focused contracts, the complete 43-test suite, line-count and Title Case checks, and the bounded renderer smoke passed.
- Independent review: three read-only reviews found no P0/P1 defect in the frozen estimator, primary/TAA, resource-lifetime, binding, barrier, fallback, capability, or packaging contracts.
- Coming Soon/documentation update: durable renderer and settings documentation updated; no public roadmap entry was needed for this local candidate.
- Pushed/PR/merged, or intentionally local: the user later authorized a direct fast-forward publication to GitHub `main`; the outcome is recorded in the final task handoff.
- Remaining experiments or follow-ups: matched GPU/VRAM benchmarking before any performance claim; direct-light variance for optional Spatial Path Resolve; scale-aware GI endpoint bias at very large coordinates; per-primary-permutation capability gating; fuller numerical GI and private path-TAA regression oracles.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/shared-primary-temporal-gi.md`.
