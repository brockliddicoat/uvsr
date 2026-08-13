# Path Tracing Denoising and Convergence V3

## Status

- State: complete locally and technically verified; controlled performance benchmarking remains a follow-up
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/temporal-accumulation-ui-repair` at `C:/Users/brock/OneDrive/Documents/uvsr/work/temporal-accumulation-ui-repair`
- Base commit: `ae7112f4557365e91ce80169c346c47e0f95a2fc` plus the complete dirty temporal candidate recorded in `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`
- Starting tracked-diff identity: `321ad8580f5218671fe09f76eaa4172fbe3e5e6d`
- Starting executable: `b-ta/bin/uvsr.exe`, SHA-256 `F54659CD3F01FDBDDF99CA7099A995473CF8893E99F5620BF53B7D4A8C9F98D8`
- Started: 2026-08-13
- Last updated: 2026-08-13
- Planned archive: `docs/exec-plans/completed/path-tracing-denoising-convergence-v3.md`

## Goal and Done Condition

Goal: make accumulation history easier to tune, give RTX PT, RESTIR PT, and RESTIR GI truthful solver-compatible denoising, validate motion behavior across accumulation/TAA/RESTIR ownership, and repair avoidable convergence or work-scheduling defects found by comparing UVSR with ZetaRay, NVIDIA RTXPT, and other primary path-tracing references.

Done when:

- [x] History controls offer clear goal-oriented presets without hiding their underlying values or breaking the existing accumulation-preset provenance model.
- [x] Each executable path solver exposes an effective denoising path with explicit signal ownership and no invalid solver/denoiser combination.
- [x] Camera-motion tests distinguish TAA, radiance accumulation, direct proposal reuse, RESTIR PT seed reuse, and RESTIR GI radiance history; retained state is proven to contribute or the UI says it cannot.
- [x] RTX PT, RESTIR PT, and RESTIR GI have explained quality/work differences and converge without a known avoidable starvation, duplicated-work, or stale-history defect.
- [x] External findings are grounded in current ZetaRay, NVIDIA RTXPT, NRD/RTXDI, and other primary source code or official documentation, then translated only into UVSR features with a demonstrated present need.
- [x] Focused tests, 327 shaders, Release renderer, full CTest, independent review, and bounded runtime image checks pass for the replacement executable.
- [ ] A cold matched performance/convergence matrix quantifies the solver tradeoffs. This was intentionally deferred because benchmark preflight was not established; no quantitative performance claim is made.

## Scope

In scope:

- Goal-oriented history slider presets and explanatory UI/help text.
- Solver/denoiser capability resolution for RTX PT, RESTIR PT, and RESTIR GI.
- Spatial Path Resolve validation and repairs; NRD integration only where existing buffers and supported contracts make it truthful.
- TAA/sample-accumulation motion gating, selective RESTIR proposal/seed reuse, convergence rate, progressive lattice, sample budget, and pass timing.
- Primary-source comparison against ZetaRay, NVIDIA RTXPT, RTXDI/NRD, and at most one additional production path tracer when it materially answers a gap.
- Focused documentation and regression coverage.

Non-goals:

- Copying another renderer wholesale, adding speculative buffers with no active consumer, changing Donut sources, or claiming performance improvement without a matched benchmark.
- Retaining non-reprojected radiance through motion, introducing ghosting as a quality tradeoff, or conflating TAA history with the path tracer's scene-linear estimator.
- Commit, push, pull request, merge, release, or deployment without later authorization.

Affected subsystems and paths:

- `src/sample_accumulation_settings.h`, `src/path_tracing_settings.h`, `src/path_tracing_pass.*`, `src/path_tracing_cs.hlsl`, denoiser integration in `src/uvsr.cpp`, temporal AA and motion reset paths, UI command catalog, focused tests, and durable renderer documentation.

Shared hotspots reserved for the coordinator:

- All writable files, especially `src/uvsr.cpp`, shared CPU/HLSL contracts, `CMakeLists.txt`, `README.md`, build tree `b-ta`, packaged shaders, the active UVSR process, and this plan.

## Baseline

- Canonical repository/remote: publication and latest-main comparison are not requested; this is a local continuation of the exact `ae7112f` feature lineage.
- Local versus remote state: dirty local feature branch; no commit or publication authority; unrelated worktrees and active plans remain untouched.
- Verified source/build: the starting candidate passed 327 shaders, Release link, 43/43 CTest, heading/diff checks, and Sponza UI/debug/motion smoke.
- GPU, scene, camera, resolution, and settings preset: NVIDIA GeForce RTX 4090 Laptop GPU, Sponza Decorated, Position 1, 1920x1080. A fresh cold/matched benchmark gate is required before quantitative performance claims.
- Known pre-existing failures: user reports indistinguishable solver quality/performance, slow path convergence, ineffective RESTIR motion reuse, suspect TAA behavior during motion, incomplete solver denoising, and unclear history values.
- Overlap: `legal-and-licensing.md` and `ray-traced-sky-visibility.md` remain externally owned active plans; this task does not edit them.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| External Path Tracer Audit | Exact ZetaRay/RTXPT/NRD/RTXDI signal, reuse, denoise, and scheduling patterns | Complete | Coordinator design |
| UVSR Temporal Audit | Motion-owner matrix and convergence root causes | Complete | Coordinator implementation |
| UVSR Denoising and UI Audit | Solver-compatible signals, existing denoiser gaps, and preset precedent | Complete | Coordinator implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- History presets are shortcuts over visible settings, not a second hidden state machine; a manual edit produces the established `<Preset> (Custom)` presentation.
- A denoiser option is selectable only when its required signal decomposition and solver semantics exist. Fallbacks are explicit and deterministic.
- TAA remains disabled in Path Tracing unless evidence justifies a separate presentation-only reconstruction; path accumulation owns scene-linear history.
- Motion may retain only proposal or replay seed state that is re-evaluated at the current surface. Raw means, stable signals, and RESTIR GI radiance checkpoints remain invalid after motion unless real reprojection/reconnection is implemented and validated.
- Performance comparisons use matched SPP, bounce count, solver features, denoiser, accumulation policy, scene, camera, resolution, warmup, and measurement window.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| EXT-3 | `/root/flashlight_audit` | Shared checkout plus public sources | Current dirty candidate | Spatial resolve pass after audit | None | Complete |
| TEMP-3 | `/root/temporal_audit` | Shared checkout | Current dirty candidate | Path pass/shaders and motion contracts after audit | None | Complete |
| DENOISE-3 | `/root/ui_spacing_audit` | Shared checkout | Current dirty candidate | History preset settings/tests after audit | None | Complete |
| INT-3 | `/root` | Feature worktree | Current dirty candidate | All task-owned files | All audits | Complete |

## Assignment Contracts

### `EXT-3`: Compare Production Path Tracer Designs

- Owner/thread: `/root/flashlight_audit`
- Base commit/state: current dirty UVSR candidate.
- Read scope: current public ZetaRay repository, NVIDIA RTXPT sample/application, official NRD and RTXDI source/docs, and one additional primary renderer only if needed.
- Write scope: none.
- No-touch scope: all local files, Git/index state, third-party dependency state, and external write actions.
- Interface/invariant contract: distinguish transferable algorithmic contracts from renderer-specific architecture; cite exact URLs, commits/branches, files, and line regions.
- Deliverable: gap table for sampling, reservoirs, reconstruction/denoising, motion, stable planes, scheduling, and convergence; prioritized UVSR corrections with licensing/dependency caveats.
- Done when: every recommended change has primary-source evidence and maps to an observed UVSR deficiency.
- Required verification: public source inspection only.
- Allowed Git/external actions: read-only browsing/fetching; no clone into the UVSR workspace, no comments or repository writes.
- Stop and report if: a comparison source is unavailable or a recommendation depends on copying incompatible licensed code.
- Handoff revision/artifact: primary-source comparison and solver-denoiser contract, followed by a clean final review.
- Handoff acknowledged by/on: `/root`, 2026-08-13.

### `TEMP-3`: Audit Motion Ownership and Convergence

- Owner/thread: `/root/temporal_audit`
- Base commit/state: current dirty UVSR candidate.
- Read scope: TAA resolution, path accumulation, RESTIR direct/PT/GI histories, motion signatures, sample lattice, sample counts, retry state, timing/tests/docs.
- Write scope: none.
- No-touch scope: all files and runtime/build resources.
- Interface/invariant contract: prove whether each state resets, reprojects, revalidates, or persists during camera motion; identify why higher FPS yields slower convergence under matched work.
- Deliverable: exact root causes, motion-owner matrix, smallest safe fixes, focused tests, and a matched benchmark matrix.
- Done when: the user-reported TAA/motion/RESTIR/convergence symptoms are either reproduced from source or falsified with concrete evidence.
- Required verification: read-only source and test inspection.
- Allowed Git/external actions: read-only only.
- Stop and report if: fixing motion reuse would require preserving unvalidated radiance history.
- Handoff revision/artifact: motion-owner/convergence audit, implementation review, and clean final temporal review with no P0/P1 findings.
- Handoff acknowledged by/on: `/root`, 2026-08-13.

### `DENOISE-3`: Audit Solver Denoising and History Preset User Experience

- Owner/thread: `/root/ui_spacing_audit`
- Base commit/state: current dirty UVSR candidate.
- Read scope: path solver outputs, signal groups, denoiser inputs/capabilities, Spatial Path Resolve, NRD modes, accumulation UI/preset precedents, commands/tests/docs.
- Write scope: none.
- No-touch scope: all files and runtime/build resources.
- Interface/invariant contract: expose only denoisers whose inputs are truthful for the active solver; history presets remain transparent shortcuts over visible fields.
- Deliverable: solver-by-denoiser compatibility matrix, Spatial Path Resolve diagnosis, minimal UI design for history presets, exact files/tests, and stop conditions.
- Done when: every solver has a concrete denoising recommendation and every new preset has an intended quality/responsiveness use case.
- Required verification: read-only source/test inspection.
- Allowed Git/external actions: read-only only.
- Stop and report if: a proposed denoiser requires a new material/signal decomposition not present in a solver.
- Handoff revision/artifact: history-preset implementation, solver UI audit, and clean final UI/product review with no P0/P1 findings.
- Handoff acknowledged by/on: `/root`, 2026-08-13.

## Integration Order

1. Complete and reconcile external, temporal, and denoising audits into one signal-ownership and capability contract.
2. Add focused failing tests for history preset semantics, solver-denoiser resolution, motion state reuse/reset, and convergence scheduling.
3. Implement settings/UI first, then CPU/HLSL denoiser and convergence repairs.
4. Run focused checks and an independent shader/temporal review.
5. Rebuild all shaders and Release, run full CTest, inspect all solver/denoiser combinations, and perform a matched convergence/performance matrix when the host passes benchmark preflight.
6. Record the replacement artifact and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| History preset usability | Unit/source contracts and runtime UI inspection | Settings/UI tests plus dropdown exercise | Passed; all five shortcuts appeared and Balanced remained selected |
| Solver-specific denoising | Capability tests, signal-route contracts, and image inspection | RTX PT/RESTIR PT/RESTIR GI x available denoisers | Passed; Raw and Spatial Path Resolve rendered for all three solvers with compatible group limits |
| Motion ownership | Source contracts plus static/moving captures and history counters | TAA/path/reservoir motion matrix | Passed contracts plus bounded RESTIR PT camera-motion smoke; no quantitative ghosting metric claimed |
| Convergence | Accepted-sample counts and image-error or stable proxy over time | Matched solver curves at fixed SPP/bounces | Structural starvation and sparse ordinary-dispatch defects repaired; matched error curves deferred |
| Performance | Cold matched position-1 measurements with total and pass timings | Repository benchmark hygiene protocol | Not run; runtime was functional smoke only and is not performance evidence |
| External cross-reference | Primary-source URLs and exact file/contract comparison | GitHub/web source inspection | Complete against ZetaRay, RTXPT, NRD, and RTXDI |
| Integrated candidate | 327 shaders, Release build, full CTest, heading/diff checks, executable hash | `b-ta` final verification | Passed; executable SHA-256 `13240CF7B6DABD50CFC905A9D97099D2B3A157569431B6DC0795C0CBDFF38FAF` |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-13 | Continue on the exact dirty temporal candidate and start a new plan. | The requested work depends on the just-verified accumulation and path transport changes; reopening the archived plan would corrupt its historical record. | All |
| 2026-08-13 | Use one coordinator as the sole writer with three read-only audits. | Denoising, path solver state, UI, and motion ownership overlap in shared CPU/HLSL contracts and `src/uvsr.cpp`. | All |
| 2026-08-13 | Treat ZetaRay and RTXPT as evidence, not code donors. | UVSR must preserve its focused architecture and licenses while importing only demonstrated contracts it currently lacks. | EXT-3, INT-3 |
| 2026-08-13 | Expose Raw and Spatial Path Resolve for every solver; remove dormant path NRD/PSR choices. | UVSR lacks the split demodulated radiance, in-lobe hit distance, and path motion-guide contract required for truthful NRD. One or two signal groups are valid for every solver; the third first-lobe split is RTX PT-only. | DENOISE-3, INT-3 |
| 2026-08-13 | Replace ordinary sparse path dispatch with full-frame work and retain the lattice only as a one-Gi-work-unit safety bound. | The old two-million-unit cap produced high FPS by visiting each pixel only once per 16–128 frames. A 384-Mi candidate still reduced default 1080p Sponza RESTIR modes to one-quarter-frame dispatches, so the final bound covers that ordinary scene while retaining a pathological-settings guard. | TEMP-3, INT-3 |
| 2026-08-13 | Preserve only reprojected and re-evaluated direct proposals and RESTIR PT seeds during camera-only motion. | Same-screen-pixel lookup did not track the moved surface. Radiance means, variances, counts, signal groups, and RESTIR GI checkpoints remain unsafe and continue to reset. | TEMP-3, INT-3 |
| 2026-08-13 | Base spatial resolve confidence on standard error and initialize deterministic center-pixel guides once per transport history. | Sample deviation left permanent spatial bias, while overwriting guides from each jittered ray made silhouette classification flicker. Resolve-only presentation edits now preserve accumulated transport. | DENOISE-3, INT-3 |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-13 | `/root` preflight | Complete | `ae7112f` + tracked diff `321ad858...`; starting executable `F54659CD...` | Branch/status/worktrees/active plans/process/skill/protocol checked | Dispatch read-only audits |
| 2026-08-13 | `EXT-3` and `RESOLVE-3` | Complete | Public source audit plus spatial-resolve implementation | Direct DXC pass; diff check | Coordinator wiring and full build pending |
| 2026-08-13 | `DENOISE-3` and `HISTORY-3` | Complete | Five stateless history shortcuts and solver/denoiser matrix | Focused settings test passed | Coordinator UI/command integration pending |
| 2026-08-13 | `TEMP-3` convergence implementation | Complete | Full-rate ordinary dispatch, full-frame reset preview, conditional candidate mean | 327/327 shaders and selected compile passed | Motion reprojection and integrated verification pending |
| 2026-08-13 | `TEMP-3` motion implementation | Complete | Previous-view proposal/seed reprojection with current-surface validation and selective history clearing | 327/327 shaders, focused contracts, selected compile | Integrated verification |
| 2026-08-13 | `/root` integration | Complete | Replacement executable SHA-256 `13240CF...` | 327/327 shaders; Release link; 43/43 CTest; runtime/production-bundle tests; title-case and diff checks | Independent review and bounded runtime smoke |
| 2026-08-13 | `/root` runtime smoke | Complete | 1920x1080 Sponza on RTX 4090 Laptop GPU | RTX PT, RESTIR PT, and RESTIR GI rendered full-frame; presets/resolve controls inspected; one RESTIR PT camera motion remained coherent | No controlled performance claim |
| 2026-08-13 | Independent reviewers | Complete | Final source snapshot | Three clean reviews; no P0/P1 findings | Archive locally |

## Risks and Escalation Triggers

- A denoiser that receives semantically invalid signals can look smoother while silently biasing transport; compatibility must be proven per solver.
- RESTIR GI stores radiance, unlike a proposal reservoir or replay seed, and cannot safely survive motion without a real reconnection/reprojection design.
- Higher frame rate is not faster convergence when each frame traces fewer pixels or samples; compare accepted rays/samples and wall-clock error, not FPS alone.
- Runtime performance evidence is invalid unless the host passes the repository's cold/matched preflight; source-only fixes may proceed while benchmarking waits.
- ZetaRay and RTXPT may use dependencies, licenses, or renderer architecture that UVSR should not adopt.

Stop and ask the user if:

- A complete solver denoising solution requires a new third-party dependency, redistributed binary, or material signal contract with a material licensing/product tradeoff.
- The only way to retain a requested history through motion is to accept ghosting or stale radiance.
- Benchmarking requires stopping or reconfiguring unrelated applications, or publication/destructive authority becomes necessary.

## Completion

- Final integrated commit: none; local implementation only unless later authorized.
- Verification summary: 327/327 shader permutations, Release renderer link, 43/43 CTest, runtime and production shader-bundle tests, 32-case heading-validator self-test, 247-heading in-scope scan, and `git diff --check` passed. Bounded Sponza runtime smoke covered all three solvers, Spatial Path Resolve, history presets, and one RESTIR PT camera-motion transition.
- Independent review: three clean final reviews found no P0/P1 issue in temporal ownership, path reconstruction, commands/UI, work scheduling, or runtime resource contracts.
- Coming Soon/documentation update: README has no Coming Soon section; durable docs and this plan will be reconciled.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: run a cold matched convergence/performance matrix before making quantitative speed or error claims; richer full RTXDI-style PT/GI reconnection and truthful NRD signal decomposition remain future work rather than hidden compatibility claims.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/path-tracing-denoising-convergence-v3.md`.
