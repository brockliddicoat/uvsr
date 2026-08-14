# Path-Tracing Resampling and Motion Repair

## Status

- State: completed locally; awaiting optional user product acceptance
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/path-tracing-resampling-controls` at `work/path-tracing-resampling-controls`
- Base commit: `54a57b08a462ad83979ccc8912570f2c6cc7ea03`
- Started: 2026-08-13
- Last updated: 2026-08-13
- Planned archive: `docs/exec-plans/completed/path-tracing-resampling-controls.md`

## Goal and Done Condition

Goal: Improve UVSR's path-tracing solutions from the exact user-observed `54a57b0` candidate by making per-frame sampling and ReSTIR reuse controllable, shortening the ambiguous proposal-reuse label, repairing ineffective behavior where proven, reducing avoidable ReSTIR PT/GI cost, and improving motion-time image reconstruction without hiding quality tradeoffs.

Done when:

- [x] The ReSTIR reuse control has a compact, accurate label and demonstrably changes the intended execution path.
- [x] The user can choose path-tracing samples per pixel per frame within a bounded production range.
- [x] ReSTIR temporal and spatial reuse have independent, meaningful controls with safe defaults and history invalidation.
- [x] Motion no longer exposes avoidable coarse reconstruction artifacts, with the implementation grounded in ZetaRay's confirmed architecture where applicable.
- [x] ReSTIR PT/GI avoid identified redundant work and all changed CPU/HLSL contracts pass focused tests and shader compilation.
- [x] The exact candidate build, source state, settings, and verification evidence are recorded; performance is not claimed without a matched benchmark.

## Scope

In scope:

- Path-tracing settings, UI, dispatch constants, shaders, reconstruction, and focused documentation/tests.
- Static and source-backed comparison with ZetaRay's primary-visibility, ReSTIR, denoising, and motion strategy.
- Safe implementation of high-confidence performance repairs and exposed quality/performance controls.

Non-goals:

- Replacing UVSR's complete renderer, adopting a new third-party denoiser, changing DirectX 12 as the product backend, or editing Donut.
- Publishing, pushing, opening a pull request, merging, or releasing.
- Claiming RTX PT parity without controlled GPU evidence.

Affected subsystems and paths:

- `src/path_tracing_*`, `src/sample_accumulation*`, `src/uvsr.cpp`, and related settings/catalog code.
- `tests/path_tracing_*`, `tests/sample_accumulation_settings_tests.cpp`, and focused UI/source-contract tests.
- `docs/path-tracing-transport.md`, `docs/advanced-settings.md`, and this execution plan.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, CPU/HLSL constant-buffer contracts, `README.md`, global build/shader configuration, documentation, integration, build trees, and renderer/GPU runtime.

## Baseline

- Canonical repository/remote: named user candidate `54a57b0`; local `origin/main` also resolves to that commit at start, but no remote publication is in scope.
- Local versus remote state: the root `main` checkout is independently ahead by two and behind by 51, so this task is isolated from exact `54a57b0` on its own branch/worktree.
- Verified source commit/build: source base is exact `54a57b0`; the task candidate is `b-pt/bin/uvsr.exe` with SHA-256 `BC57793D6C612AC24747B4EAC5E3759C6F4D091A93A65F1075C32D1838C036B7`. It is technically verified but dirty and therefore not Canonical verified.
- GPU, scene, camera, resolution, and settings preset when relevant: to be captured before any matched runtime comparison; no inferred settings from recency.
- Known pre-existing failures: none established; the base worktree contains the preserved untracked `b-ta/` build tree.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| PT-INV-1 UVSR pipeline audit | Evidence for no-op/cost causes and control insertion points | Complete | Coordinator implementation |
| PT-INV-2 ZetaRay research | Confirmed primary visibility, ReSTIR controls, and motion strategy | Complete | Architecture decision |
| PT-INV-3 UVSR UX/test audit | Control schema, reset behavior, and required tests | Complete | Coordinator implementation |
| PT-IMPL | Stable settings and CPU/HLSL contract after investigations | Complete | Verification and review |
| PT-REVIEW | Independent rendering/shader review of integrated diff | Complete; findings repaired and re-reviewed | Completion decision |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Preserve constant-buffer alignment and update both CPU and HLSL definitions together.
- New settings must be clamped, deterministic, reset incompatible temporal history, and be represented in UI command/search metadata when appropriate.
- Temporal and spatial reuse controls must alter actual reservoir paths; an exposed value may not be decorative.
- Samples per pixel per frame must represent real independent path samples and must not silently multiply unrelated raster work.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| PT-INV-1 | `/root/restir_pipeline_audit` | Read-only named-base worktree | `54a57b0` | None | None | Complete |
| PT-INV-2 | `/root/zetaray_research` | Read-only web/source research | `54a57b0` comparison | None | None | Complete |
| PT-INV-3 | `/root/pt_ux_tests_audit` | Read-only named-base worktree | `54a57b0` | None | None | Complete |
| PT-IMPL | `/root` | Task worktree | `54a57b0` | In-scope source, tests, and docs | PT-INV-1 through PT-INV-3 | Complete |
| PT-REVIEW | `/root/path_tracing_correctness_review` | Read-only task worktree | Integrated candidate | None | PT-IMPL | Complete |

## Assignment Contracts

### Cost and Reuse Audit

- Owner/thread: `/root/restir_pipeline_audit`
- Branch/worktree: read-only `work/temporal-accumulation-ui-repair`
- Base commit/state: tracked source clean at `54a57b0`; ignore preserved untracked `b-ta/`.
- Read scope: path-tracing source, shader, settings, UI, tests, and relevant history.
- Write scope: none.
- No-touch scope: all files, builds, processes, Git state, and artifacts.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: exact base only.
- Interface/invariant contract: trace ray/dispatch multipliers, reuse gates, validation, and history behavior from UI to shader.
- Deliverable: evidence-backed cost and no-op diagnosis plus safest high-impact repairs.
- Done when: exact file/line references, risks, and integration order are returned.
- Required verification: static source trace and existing test review.
- Allowed Git and external actions: read-only inspection only.
- Stop and report if: tracked source drifts or relevant paths are dirty.
- Handoff revision/artifact: read-only source audit of exact `54a57b0`.
- Handoff acknowledged by/on: coordinator, 2026-08-13.

### Reference Architecture Research

- Owner/thread: `/root/zetaray_research`
- Branch/worktree: read-only official web/source research.
- Base commit/state: UVSR comparison target `54a57b0`.
- Read scope: ZetaRay official repository and primary linked sources.
- Write scope: none.
- No-touch scope: workspace, builds, Git state, and external writes.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: separate confirmed source facts from inference and record authoritative links/source locations.
- Deliverable: primary-visibility, resampling, sampling, denoising, and motion comparison with implementable recommendations.
- Done when: the user's rasterized-geometry question and relevant ZetaRay behavior are answered from primary evidence.
- Required verification: source inspection only.
- Allowed Git and external actions: read-only web/source access.
- Stop and report if: licensing or source access prevents confirmation.
- Handoff revision/artifact: ZetaRay source audit at `6fd82f1e73360aa196e733e91b548bc95e19968a`.
- Handoff acknowledged by/on: coordinator, 2026-08-13.

### Controls and Motion Reconstruction Audit

- Owner/thread: `/root/pt_ux_tests_audit`
- Branch/worktree: read-only `work/temporal-accumulation-ui-repair`.
- Base commit/state: tracked source clean at `54a57b0`; ignore preserved untracked `b-ta/`.
- Read scope: settings, UI, accumulation, temporal AA, path tracing, tests, and build hooks.
- Write scope: none.
- No-touch scope: all files, builds, processes, Git state, and artifacts.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: exact base only.
- Interface/invariant contract: map every recommended setting to runtime behavior and history invalidation.
- Deliverable: compact labels, ranges/defaults, motion-artifact cause, and focused test plan.
- Done when: source/line evidence and smallest safe implementation scope are returned.
- Required verification: static trace and test discovery.
- Allowed Git and external actions: read-only inspection only.
- Stop and report if: tracked source drifts or relevant paths are dirty.
- Handoff revision/artifact: read-only UI/test audit of exact `54a57b0`.
- Handoff acknowledged by/on: coordinator, 2026-08-13.

## Integration Order

1. Complete and reconcile the three read-only investigations.
2. Freeze the settings and CPU/HLSL contracts, then implement as one coordinated writer because the paths are tightly coupled.
3. Run focused unit/source-contract tests and compile every affected shader permutation in an isolated build tree.
4. Obtain independent shader/rendering review and repair confirmed defects.
5. Build the exact candidate and perform safe runtime correctness inspection; run matched performance measurements only in a user-available quiet window.
6. Reconcile documentation and archive this plan with exact evidence.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Settings are real and bounded | Unit and source-contract tests from UI through constants/shader branches | Focused CTest targets plus static source audit | Passed; complete 43-test Release suite |
| CPU/HLSL contracts agree | Shader compilation and contract tests | Release build of affected shaders and tests | Passed; all 327 shader tasks and the full executable built |
| Motion artifact is improved | Smooth reset reconstruction and runtime control inspection | Source contract plus bounded renderer inspection outside timing window | Passed statically and in shader compilation; ordinary RTX PT and RESTIR PT reported full-frame updates at 1920x1080 |
| ReSTIR avoids redundant work | Static dispatch/ray accounting and matched GPU pass/total frame measurements | Source accounting; benchmark only after full preflight | Primary replay lighting and final-bounce waste removed; Uniform defaults and donor-aware accounting verified; no matched performance claim |
| Exact candidate is launchable | Dirty task diff identity, executable SHA-256, smoke run | Release build and bounded smoke test | Passed; `b-pt/bin/uvsr.exe`, SHA-256 `BC57793D6C612AC24747B4EAC5E3759C6F4D091A93A65F1075C32D1838C036B7` |

For performance work, record:

- baseline and candidate commits plus dirty-diff identities;
- GPU, scene, fixed camera, resolution, and settings preset;
- warmup and sample window/count;
- total frame time plus relevant pass costs;
- correctness/image-quality guardrails;
- before/after captures and raw measurement artifact.

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-13 | Use exact `54a57b0` as base in a new isolated task branch. | The user named that commit and the root `main` checkout has unrelated divergence; selecting by recency would break provenance. | All |
| 2026-08-13 | Preserve the pre-existing untracked `b-ta/` tree and allow read-only source audits around it. | Tracked sources are clean and exact; deleting or repurposing the artifact would be unnecessary and destructive. | PT-INV-1, PT-INV-3 |
| 2026-08-13 | Keep implementation under one writer after parallel research. | Settings, UI, CPU constants, shader constants, dispatches, history, and tests are strongly coupled shared contracts. | PT-IMPL |
| 2026-08-13 | Use `Samples / Pixel` 1 through 8, `Temporal Reuse`, `Spatial Neighbors` 0 through 4, and `Motion Reuse`. | The compact controls map directly to executable work. RESTIR GI remains temporal-only because cross-pixel GI has no valid transform. | PT-IMPL |
| 2026-08-13 | Make Uniform NEE the RESTIR PT/GI preset default and charge only executable donor work. | The old Power preset rescanned every light at each vertex; reset frames were also charged for history that did not exist. | PT-IMPL |
| 2026-08-13 | Replace reset-time nearest-tile replication with presentation-only bilinear reconstruction. | This removes giant blocks without fabricating estimator history. Independent review required traced representatives to remain read-only within the reconstruction dispatch. | PT-IMPL, PT-REVIEW |
| 2026-08-13 | Do not report a performance comparison from the smoke run. | The machine did not enter the required controlled benchmark window, so displayed frame times are correctness observations only. | Completion |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-13 | Coordinator | Complete | Branch at `54a57b0`; tracked diff identity `485e6169943ede2cebb4fc5b1fa82c6655159701` | Full Release build, 327 shader tasks, 43/43 CTest, README counts, and Title Case validation passed | Hand off local candidate for optional user acceptance |
| 2026-08-13 | Investigators | Complete | Read-only UVSR and ZetaRay audits | Source-backed diagnosis and primary-source cross-check | No writes |
| 2026-08-13 | Independent reviewer | Complete | Integrated dirty candidate | Preview UAV race and dispatch-accounting gaps repaired; final targeted review closed all findings | No remaining high-risk finding |
| 2026-08-13 | Runtime smoke | Complete | `b-pt/bin/uvsr.exe` | Sponza Decorated, Position 1, 1920x1080; RTX PT and RESTIR PT rendered; new controls and full-frame work status visible; process closed cleanly | Not a benchmark or product acceptance |

## Risks and Escalation Triggers

- ReSTIR PT/GI quality and cost may require separate algorithms rather than one shared control policy.
- Multiple samples per frame can scale ray cost almost linearly and needs an intentionally low default.
- Motion reconstruction can trade blockiness for blur, lag, or ghosting; visual evidence must accompany changes.
- GPU timing is invalid if the machine is occupied or baseline/candidate identities differ.
- ZetaRay may use a hybrid primary-visibility or denoising architecture that cannot be copied literally without broader scope.

Stop and ask the user if:

- A repair requires choosing between materially different default image quality and performance without a source-backed safe default.
- Required deletion or third-party asset/licensing treatment is uncertain.
- A valid benchmark would require interfering with unrelated user processes or a testing window the user has not made available.

## Completion

- Final integrated commit: the later single publication commit containing this plan; its exact SHA and push result are recorded in the final task handoff.
- Verification summary: Release `ALL_BUILD` passed, all 327 shader tasks compiled, all 43 CTest tests passed, bounded runtime smoke passed, README counts are current, and 131 in-scope headings/lead-ins passed Title Case validation.
- Independent review: complete; preview UAV ordering and complete RTXDI/ReSTIR/SPP dispatch accounting were repaired and re-reviewed with no remaining finding.
- Coming Soon/documentation update: no Coming Soon section exists at this base; README and the durable Path Tracing, Advanced Settings, and Noise documents were updated.
- Pushed/PR/merged, or intentionally local: the user later authorized a direct fast-forward publication to GitHub `main`; the outcome is recorded in the final task handoff.
- Remaining experiments or follow-ups: a matched RTX PT versus RESTIR PT/GI performance benchmark and user visual acceptance remain optional; neither blocks the technically verified local candidate.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/path-tracing-resampling-controls.md`.
