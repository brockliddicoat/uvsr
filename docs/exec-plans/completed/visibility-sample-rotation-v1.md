# Visibility Sample Rotation v1 Experiment

## Status

- State: complete; product rejected and experiment retired
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/sample-rotation-experiment` at `C:\Users\brock\Documents\Codex\2026-07-16\e\work\uvsr`
- Base commit: `a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`
- Started: 2026-07-15
- Last updated: 2026-07-16
- Archived path: `docs/exec-plans/completed/visibility-sample-rotation-v1.md`

## Goal and Done Condition

Goal: Implement Visibility Sample Rotation v1 as a conservative, default-off receiver-rotation experiment for UVSR's supported one-sample-per-2x2 visibility layout without increasing the per-frame sample budget or changing unsupported/native behavior.

Done when:

- [x] The live canonical base is established with recorded verification evidence.
- [x] Half-resolution visibility receiver rotation composes correctly with UVSR's existing stochastic scheduler and block-history reprojection conventions.
- [x] Unsupported layouts, including UI Quarter and the absent half-rate checkerboard representation, fall back unchanged by design.
- [x] Settings transitions reset temporal history and focused tests cover phase resolution and resets.
- [x] Build, tests, shader inspection, renderer smoke checks, and reproducible comparison guidance are complete, with unavailable measurements explicitly qualified.
- [x] The user's negative image-quality evaluation is recorded separately from technical verification, with any convergence benefit marked speculative.
- [x] Documentation and the complete tracked Markdown set pass the title-case validator.

## Scope

In scope:

- Experimental runtime setting and localized CPU frame-state resolution.
- Existing visibility receiver mapping, stochastic scheduler isolation, block-history reprojection correctness, and history invalidation.
- Efficient quarter-rate four-phase behavior and half-rate behavior only if the current layout already supports it at constant cost.
- Focused tests, debug visibility where it fits existing debug-only UI, user documentation, performance/capture reproduction guidance, and an evidence-based recommendation.

Non-goals:

- Full-resolution discard checkerboarding, new render targets, new history resources, mip-bias changes, API-specific sample-position features, or a broad reconstruction rewrite.
- Push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/` renderer settings, frame setup, receiver/guide mapping, reconstruction registration, temporal state, and UI.
- `tests/` focused CPU-reference coverage where the design permits.
- `README.md` and a focused experiment document under `docs/`.

Shared hotspots reserved for the coordinator:

- `README.md`, root build files, global settings, CPU/HLSL contracts, documentation, branch/index/build/runtime resources.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`, default branch `main`.
- Local versus remote state: Equal at `a7e51b7d3a09e18cc4e5da085b511623a87cc0ac` when the task branch was created and again in the final live `origin/main` recheck on 2026-07-16.
- Verified source commit/build: `a7e51b7`; the canonical publication record reports a Release renderer build, all 11 tests, the document validator, and a live `main-a7e51b7` smoke check before the fast-forward push. The matching clean build at `C:\Users\brock\Documents\Codex\2026-07-15\work\work\uvsr-sponza-camera-benchmark` has executable SHA-256 `4914DA5211698B489AA8917C482901B1F02617FC7587EECAC93B6097ACC1F8C0`, and its CTest log records 11/11 passing.
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090 Laptop GPU, PBR Sponza Decorated, locked Benchmark Position 1, 1920x1080, deferred PBR, and identical visibility settings for baseline/candidate runs.
- Known pre-existing failures: None observed in the recorded canonical verification.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical base verification | Clean `a7e51b7` build, tests, and smoke evidence | Ready | All implementation and comparison work |
| Jitter architecture audit | Units, signs, sequence, and reprojection convention | Ready | Sample-rotation resolver and integration |
| Render-layout audit | UI Half is one sample per 2x2; UI Quarter and diagonal checkerboard are unsupported | Ready | Mode support and fallback behavior |
| Cosine-estimator task | Write lease released; keep this verified-base experiment isolated, consume no scheduler dimension, and preserve its reserved dimension 7 | Released in separate task | Write scheduling and future integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Prefer an existing runtime setting and CPU-resolved receiver/reconstruction offsets; do not change resource layouts unless architecture inspection proves it unavoidable and cost-neutral.
- Disabled, native, and unsupported paths retain existing behavior and bindings.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Prompt analysis | `/root/prompt_analysis` | Shared read-only | Attached prompt | None | None | Complete |
| Repository discovery | `/root/repo_discovery` | Shared read-only | Workspace/GitHub | None | None | Complete |
| Verification research | `/root/verification_research` | Shared read-only | `a7e51b7` | None | Clone availability | Complete |
| Jitter architecture | `/root/jitter_architecture` | Shared read-only | `a7e51b7` | None | Clone availability | Complete |
| Partial rendering review | `/root/partial_review` | Task-local dirty candidate | `a7e51b7` | None | Initial implementation | Complete |
| Runtime matrix review | `/root/runtime_matrix_review` | Shared read-only | Task-local candidate | None | Initial implementation | Complete |
| Second static review | `/root/second_static_review` | Shared read-only | Task-local candidate | None | Partial-review remediation | Complete |
| Final diff review | `/root/final_diff_review` | Shared read-only | Final task-local candidate | None | Runtime and final composition | Complete |
| Final documentation audit | `/root/final_docs_audit` | Shared read-only | Final task-local candidate | None | Final evidence draft | Complete |
| Telemetry extraction | `/root/telemetry_evidence` | Shared read-only | Saved runtime PNGs | None | Runtime capture | Complete |
| Implementation and integration | `/root` | Task branch/shared checkout | `a7e51b7` | All task-owned files | Architecture decisions | Complete |

## Assignment Contracts

### Read-Only Architecture and Verification Research

- Owners/threads: `/root/repo_discovery`, `/root/verification_research`, and `/root/jitter_architecture`
- Base commit/state: `a7e51b7`; read-only findings may also cover the remote canonical state.
- Read scope: Repository, GitHub metadata, attached prompt, renderer architecture, tests, and build guidance.
- Write scope: None.
- No-touch scope: Git state, source, documentation, build tree, runtime, and external GitHub state.
- Interface/invariant contract: Report exact evidence; do not invent half-rate support or jitter conventions.
- Deliverable: Distilled findings, risks, and recommended checks with file/line references where available.
- Done when: The coordinator can make a localized implementation decision and verification plan.
- Allowed Git and external actions: Read-only inspection only; no publication.
- Stop and report if: Repository identity, base, ownership, or architecture is ambiguous.

### Implementation and Integration

- Owner/thread: `/root`
- Branch/worktree: `codex/sample-rotation-experiment` in the canonical local clone.
- Base commit/state: `a7e51b7`, initially clean.
- Read scope: Entire repository and task evidence.
- Write scope: Task-owned first-party source, tests, and documentation; never `donut/`.
- No-touch scope: Unrelated active roadmap work, remote branches/PRs, dependency revisions, and published history.
- Build directory and runtime/GPU/resource lease: Coordinator-only `build/` and one `uvsr` experiment window.
- Interface/invariant contract: Default-off; no per-pixel disabled branch, new scene resources, passes, or bandwidth; one documented receiver/reconstruction registration; unchanged projection and motion-vector convention; reset on convention changes.
- Deliverable: Integrated local patch, test evidence, comparison instructions/artifacts, and completed plan.
- Done when: Every feasible acceptance criterion is mapped to evidence and limitations are explicit.
- Required verification: Baseline/candidate builds, `uvsr_pbr_tests`, CTest, title-case self-test/check, `git diff --check`, shader/resource inspection, renderer smoke checks, and reproducible visual/performance comparison.
- Allowed Git and external actions: Local branch, edits, builds, tests, launch, and focused local commit if requested later; no push/PR/merge/release.
- Stop and report if: A correct implementation requires new resources/API features, a material product tradeoff, unrelated deletion, or external publication authority.

## Integration Order

1. Establish and record the clean canonical baseline.
2. Complete jitter/render-layout/history architecture decisions.
3. Implement the smallest CPU/settings/receiver-registration change and focused tests.
4. Add documentation and reproduction guidance.
5. Build, inspect, smoke-test, compare, independently review, and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Default and unsupported behavior unchanged | Resolver tests and disabled-path diff/shader inspection | Focused tests plus generated shader/resource inspection | Passed: exact legacy constants; odd, Full, and Quarter fallback; all 42 final primary permutations compile |
| Four phases are deterministic and zero-centered | Exact sequence/unit tests | Focused sample-rotation tests | Passed in Release/Debug tests and four frozen-phase captures |
| Reprojection convention remains correct | Receiver/guide/history audit and transition smoke test | Source inspection plus renderer capture | Passed for the block-history contract and locked-pose transitions; arbitrary motion remains unmeasured |
| History resets on convention changes | Transition tests and runtime smoke test | Focused reset tests plus toggle/resolution/freeze checks | Passed: Off-On, temporal, Half-Full-Half, Half-Quarter-Half, and frozen reset |
| No resource or sample-budget increase | Render-target/resource/binding diff and frame dimensions | Source/generated shader inspection plus UI memory metrics | Passed for layouts, bindings, operations, and identical logical bytes; actual traffic remains unmeasured |
| No meaningful disabled overhead | Matched baseline/off measurements | Fixed-scene repeated frame-time capture | Inconclusive: one baseline and five candidate UI samples under variable shared-GPU load; static growth is 3-10 DXIL instructions |
| Visible image-quality benefit | User comparison plus synchronized convergence capture | Current-candidate qualitative A/B | Not demonstrated: the user observed no improvement and worse, noisier output even with temporal history gathered; any convergence benefit remains speculative |
| Documentation is compliant | Full Markdown checker | `tools/check_document_title_case.cmd --self-test` and default run | Passed: self-test and full scan report zero violations |

For performance work, record the baseline and candidate revisions, adapter, scene, fixed camera, output/render resolution, all relevant settings, warmup, sample window, total frame time, UVSR/scene cost where exposed, correctness observations, and before/after captures.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-15 | Use `brockliddicoat/uvsr` `main` at `a7e51b7` as the canonical lineage under audit. | It is the owned exact-name repository, its default branch is `main`, live/local heads matched, and a later workspace publication record confirmed canonical verification. | All |
| 2026-07-15 | Keep the work local on `codex/sample-rotation-experiment`. | Implementation authority includes local edits and checks but not GitHub publication. | Implementation and integration |
| 2026-07-15 | Do not add a Coming Soon entry yet. | This is explicitly a removable private experiment; the repository says short-lived private experiments need no roadmap entry. | Documentation |
| 2026-07-15 | Accept `a7e51b7` as the latest Canonical verified checkpoint. | A later workspace publication record supersedes older `e3cc4c7` evidence and ties `a7e51b7` to the Release build, 11/11 tests, document validation, live smoke, and canonical fast-forward publication. | All |
| 2026-07-15 | Pause overlapping visibility-file writes while the separate cosine-estimator task is active. | Both tasks start at `a7e51b7` and reserve the same visibility CPU/HLSL/test/document paths; the repository requires cross-task coordination before overlapping writes. A coordination message was sent to task `019f6925-2287-76a3-a1aa-00610c5082bd`. | Implementation and integration |
| 2026-07-15 | Rotate only UI Half visibility receivers; do not jitter the camera. | UVSR renders the scene/G-buffer at output resolution and forces projection jitter to zero. UI Half alone matches one receiver per output 2x2; camera jitter would move unrelated full-resolution scene rendering. | Implementation, motion, and captures |
| 2026-07-15 | Preserve a logical block-history lattice. | AO/GI and depth/normal history are sampling-resolution resources. The experiment can average receiver phases into one block estimate without new resources, but cannot retain four independent output pixels. | Reprojection, filtering, and limitations |
| 2026-07-15 | Use a CPU-resolved integer receiver offset in repurposed constant-buffer padding. | This preserves buffer size, bindings, passes, resources, dispatch dimensions, and pipeline count while avoiding a per-pixel feature branch. Disabled/unsupported states resolve the exact legacy `scale / 2` receiver. | CPU/HLSL contract and disabled path |
| 2026-07-15 | Advance a rendered-sample clock independent of the stochastic scheduler. | Rotation resets with history and advances only after an active visibility render, while existing 32/64-frame ray/noise sequences remain unchanged and dimension 7 stays available to the cosine-estimator task. | Phase resolution and scheduler compatibility |
| 2026-07-16 | Register the reconstructed value at the current phase's represented output-pixel center. | A block-only guide shift rotated tracing but left the upsampler at one fixed block-center lattice. Subtracting the CPU-resolved centered output offset satisfies the output-space mapping requirement without changing allocation, dispatch, history addresses, or motion vectors. | Filtering and reconstruction |
| 2026-07-16 | Pack the integer receiver coordinate and carry two centered offsets in the three existing padding words. | This preserves the 896-byte constant buffer and all bindings while reducing representative disabled-path DXIL growth to three IR instructions in temporal reconstruction and ten in filtering, with unchanged texture operations and branches. | CPU/HLSL contract and disabled-path cost |
| 2026-07-16 | Treat node-pick reframing as a camera cut. | The framing helper teleports the camera. Resetting the previous view and visibility history prevents old motion/history from being interpreted under the new pose and restarts rotation safely. | Camera cuts and history |
| 2026-07-16 | Fall back to the legacy receiver when either output dimension is odd. | A partial 2x2 edge block clamps some phases, so one frame-global centered offset would misregister that edge relative to complete blocks. The conservative fallback preserves exact reconstruction without edge-specific shader logic. | Mode resolution, resize safety, and tests |
| 2026-07-16 | Preserve isolation from the released cosine-estimator task. | Its write lease was explicitly released before composition. This task remains based on canonical `a7e51b7`, consumes no scheduler semantic dimension, and preserves dimension 7 for future integration. | Source composition and scheduler compatibility |
| 2026-07-16 | Keep the result local and default-off. | Static and transition evidence is clean, but the short sequential telemetry cannot establish performance equivalence and synchronized motion/convergence evidence is still absent; the subsequent user evaluation was negative. | Recommendation and completion |
| 2026-07-16 | Reject production promotion on image-quality grounds. | The user's current-candidate comparison found no visible improvement and worse, noisier output even after temporal reconstruction gathered. This rejects current product value; a convergence advantage remains speculative. | Recommendation, postmortem, and completion |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-15 | Prompt analysis | Complete | Attached prompt analysis | Read-only review | Resolve actual renderer architecture before promising half-rate support. |
| 2026-07-15 | Coordinator preflight | Complete | `a7e51b7` and this plan | Remote equality, verification record/build hash/11-test log, clean status, branches/worktrees/PRs/roadmap/tasks reviewed | Finish architecture audit while overlapping writes remain paused. |
| 2026-07-15 | Architecture explorers | Complete | Read-only handoffs | Resolution/layout, projection/motion, scheduler, history, filter, and reset paths inspected | Implement the limited block-receiver experiment and document the architectural mismatch. |
| 2026-07-15 | Coordinator implementation | Complete | Task-local dirty candidate | Resolver, CPU/HLSL integration, UI, tests, README, and experiment document complete | Keep local; no publication requested. |
| 2026-07-16 | Independent partial review | Complete | Read-only handoff | Diff and source audit; `git diff --check` clean | Primary-helper, phase-registration, camera-cut, and debug-wording findings were remediated. |
| 2026-07-16 | Coordinator static verification | Complete | Task-local dirty candidate | Release renderer, all 53 visibility shader tasks, focused Release/Debug tests, complete 12-test Release suite, document self-test/full scan, and `git diff --check` pass | Static evidence cannot prove execution cost or image-quality value. |
| 2026-07-16 | Second independent static review | Complete | Read-only handoff plus coordinator remediation | Confirmed registration, history, ABI, debug guards, and node-pick reset | Odd-dimension edge finding resolved by exact legacy fallback and focused coverage. |
| 2026-07-16 | Runtime matrix review | Complete | Read-only handoff | Defined minimum matched performance, capture, transition, and measurement-boundary matrix | Used to scope the runtime smoke checks and report unmeasured profiler metrics. |
| 2026-07-16 | Coordinator runtime validation | Complete | Saved baseline/off telemetry, clean rotating image, and four fixed-phase captures | Four phases; frozen reset; rotation/temporal/Full/Quarter transitions; logical bytes; Release debug-string absence | Short sequential telemetry under shared load is not performance-equivalence evidence; enabled Release row excluded after interactive adapter restart. |
| 2026-07-16 | Final diff review | Complete | Read-only final handoff | No P0/P1/P2 blockers; coordinate, history, ABI, fallback, UI, and scheduler contracts confirmed | Residual risks are performance and motion/convergence acceptance only. |
| 2026-07-16 | Final documentation audit | Complete | Read-only final handoff plus coordinator remediation | Heading validator clean; stale architecture/evidence language corrected | Archive after final verification. |
| 2026-07-16 | User qualitative evaluation | Complete | Current local candidate | No visible improvement; worse, noisier result with rotation even after temporal gathering | Retire the experiment and do not promote it. |

## Risks and Escalation Triggers

- One block-history value cannot retain four phase-specific details. The user's test observed worse, noisier output even with temporal gathering, and no convergence benefit was observed.
- The disabled common mapping adds 3-10 representative DXIL instructions and 1-2 constant loads; the available UI telemetry cannot prove that cost is within noise.
- Synchronized slow/fast motion, disocclusion, and convergence captures remain outstanding; until they show otherwise, any convergence advantage is speculative.
- Future integration with the separate cosine-estimator work must preserve its scheduler dimension 7; this experiment consumes no scheduler dimension.

Stop and ask the user if:

- Correctness requires a new resource/layout/API feature or a material quality/performance choice outside the conservative experiment.
- Publication, merge, release, destructive cleanup, or a different repository destination becomes necessary.

## Completion

- Final integrated commit: No implementation commit; renderer source remains uncommitted on local branch `codex/sample-rotation-experiment`. Documentation-only pull request [#16](https://github.com/brockliddicoat/uvsr/pull/16) merged into Canonical `main` at `869e2241a72faf59f11604b5199a96b3c0218788`.
- Verification summary: Release renderer and shaders build; complete Release CTest passes 12/12; focused Debug test passes; document self-test/full scan and `git diff --check` pass. Runtime transitions completed without stale-history corruption; saved captures document state only and do not establish image-quality benefit.
- Independent review: Final high-risk read-only review found no P0/P1/P2 source blocker; final document audit found no remaining heading violation.
- Coming Soon/documentation update: No roadmap entry; durable design evidence remains at `docs/visibility-sample-rotation-v1.md`, and the negative product result and revival triggers are archived at `docs/postmortem/visibility-sample-rotation-v1.md`.
- Pushed/PR/merged, or intentionally local: Documentation-only pull request #16 was merged. The rejected implementation remains local; no source push, source PR, source merge, release, or deployment was authorized.
- Remaining experiments or follow-ups: Optional research only. A redesigned history model or synchronized convergence testing would be needed to overturn the negative qualitative result; external GPU profiling remains unmeasured.
- Active ownership released: Yes; task-owned renderer windows are closed. User-owned renderer sessions, including the process the user restarted during final testing, were not controlled.
- Archived to completed/abandoned path: `docs/exec-plans/completed/visibility-sample-rotation-v1.md`.
