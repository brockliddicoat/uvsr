# Temporal Accumulation and UI Repair: Uniform Drawers, Stable Flashlight, and Linear-Hdr History

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/temporal-accumulation-ui-repair` at `C:/Users/brock/OneDrive/Documents/uvsr/work/temporal-accumulation-ui-repair`
- Base commit: `ae7112f4557365e91ce80169c346c47e0f95a2fc`
- Started: 2026-08-13
- Last updated: 2026-08-13
- Planned archive: `docs/exec-plans/completed/temporal-accumulation-ui-repair.md`

## Goal and Done Condition

Goal: Repair the nonuniform collapsed-drawer gaps visible in the supplied 1920x1080 capture, remove bright geometry-edge outlines and contrast loss from temporal AA, replace the fragile static sample accumulator with complete-pixel linear-HDR accumulation modes and configurable presets, and let the flashlight become exactly stationary whenever the active camera is idle.

Done when:

- [x] Every adjacent top-level Settings drawer uses the same Tight spacing token in the reported UI state.
- [x] TAA depth-validates stationary silhouettes in both shader routes and defaults to a scene-linear RGB blend domain.
- [x] Static sample accumulation covers every eligible pixel, exposes useful presets plus a Custom mode, resets on movement or estimator changes, and never preserves history through movement.
- [x] A flashlight motion toggle makes the submitted flashlight transform bit-stable while the active camera is idle and resets temporal histories when its state changes.
- [x] Focused tests, the Release renderer build, the relevant full suite, source-contract audits, and a bounded runtime visual check pass for the exact candidate executable.

## Scope

In scope:

- Top-level Settings drawer vertical spacing and its source-contract coverage.
- TAA sampling, reprojection, validation, rectification, blend-domain, and history-weight behavior required to fix edge halos and contrast loss.
- Lighting/path-tracing sample accumulation policy, complete-pixel scheduling, linear-HDR averaging, presets, Custom controls, reset behavior, UI, and tests.
- Flashlight idle-motion policy, UI toggle, history invalidation, documentation, and tests.

Non-goals:

- Preserving sample-accumulation history after camera, flashlight, scene, or estimator movement. TAA retains its ordinary validated motion reprojection when it is the active history owner.
- A general temporal upscaler, adaptive denoiser, unrelated UI redesign, or changes under `donut/`.
- Push, pull request, merge, release, deployment, or destructive cleanup.

Affected subsystems and paths:

- `src/uvsr.cpp`, `src/uvsr.h`, temporal-AA sources and shaders, lighting-accumulation sources and shaders, flashlight settings/shared contracts, focused tests, and user-facing advanced/temporal documentation.
- `overrides/` only if the defect is proven to originate in the pinned ImGui build-time override rather than UVSR layout submission.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/shaders.cfg`, settings serialization/defaults, shared CPU/HLSL contracts, build trees, packaged shader output, the UVSR process, and this execution plan.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`, live `refs/heads/main` queried on 2026-08-13.
- Local versus remote state: isolated feature worktree is equal to live GitHub `main` at `ae7112f`; the original checkout is intentionally untouched and diverged (`main` ahead 2, behind 50) with unrelated untracked files.
- Verified source commit/build: the user identifies GitHub main build `ae7112f` as the failing artifact; its prior Canonical verification record is accepted as provenance, but this task will independently build and verify its candidate.
- GPU, scene, camera, resolution, and settings preset when relevant: supplied failure capture is 1920x1080 at approximately 2.8 ms, scene/camera unspecified; exact runtime check settings will be recorded with the candidate.
- Known pre-existing failures: nonuniform drawer gaps; bright TAA geometry-edge outlines; temporal contrast/color washout; incomplete/noisy sample convergence; flashlight continues subpixel motion while the view is idle.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| UI spacing audit | Identify the exact submission/layout cause and one-token invariant | Complete | Coordinator implementation |
| Temporal/history audit and primary-source research | Establish the faulty math/domain/scheduling and the preset contract | Complete; final review clean | Coordinator implementation |
| Flashlight motion and test audit | Identify transform update inputs, idle signal, invalidation path, and focused tests | Complete; final review clean | Coordinator implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Sample accumulation modes share one scene-linear HDR accumulation core. Every Pixel attempts each eligible pixel; Variance Guided may skip after complete warmup but guarantees a bounded deterministic revisit.
- Custom controls must resolve to the same runtime parameter contract as presets, and preset selection must report Custom when any exposed value diverges.
- Movement always invalidates all sample-accumulation history; no preset or custom option may override that invariant.
- Flashlight idle lock freezes its submitted position, direction, and roll only after the complete active-camera pose stops changing; camera translation/rotation and an explicit setting change invalidate history and update the frozen transform.
- TAA history stores and blends scene-linear HDR radiance before display mapping and must never sample invalid/disoccluded foreground/background history across depth or bounds failures.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| UI-1 | `/root/ui_spacing_audit` | Shared read-only checkout | `ae7112f` | None | None | Complete; final review clean |
| TEMP-1 | `/root/temporal_audit` | Shared read-only checkout | `ae7112f` | None | None | Complete; final review clean |
| LIGHT-1 | `/root/flashlight_audit` | Shared read-only checkout | `ae7112f` | None | None | Complete; final review clean |
| INT-1 | `/root` | Feature worktree | `ae7112f` | All task-owned implementation, tests, docs, plan, and integration | UI-1, TEMP-1, LIGHT-1 | Complete |

## Assignment Contracts

### UI-1: Diagnose Nonuniform Top-Level Drawer Gaps

- Owner/thread: `/root/ui_spacing_audit`
- Branch/worktree: shared read-only access to the feature worktree
- Base commit/state: `ae7112f` plus only the coordinator-authored execution plan
- Read scope: `src/uvsr.cpp`, UI helpers/contracts/tests, relevant ImGui overrides, advanced-settings and UI-integration documentation, and the supplied screenshot description.
- Write scope: none.
- No-touch scope: entire filesystem and Git/index state.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: every adjacent visible top-level drawer gap is exactly `g_UiSpacingTokens.tight`, independent of conditional drawer visibility, animated collapse, child bodies, and scroll position.
- Deliverable: exact cause with file/line evidence, smallest safe repair, and focused test additions.
- Done when: the three larger gaps in the screenshot are explained by source behavior rather than appearance alone.
- Required verification: read-only source/test inspection.
- Allowed Git and external actions: read-only commands only; no Git metadata changes.
- Stop and report if: source state differs from `ae7112f` beyond the plan or a competing writer touches the inspected files.
- Handoff revision/artifact: source-level diagnosis and final UI review against the integrated diff.
- Handoff acknowledged by/on: coordinator, 2026-08-13.

### Temp-1: Audit TAA and Sample Accumulation

- Owner/thread: `/root/temporal_audit`
- Branch/worktree: shared read-only access to the feature worktree
- Base commit/state: `ae7112f` plus only the coordinator-authored execution plan
- Read scope: temporal-AA and lighting-accumulation C++/HLSL, path-tracing/noise integration, tests, documentation, and primary technical sources for efficient static accumulation.
- Write scope: none.
- No-touch scope: entire filesystem and Git/index state.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: scene-linear HDR history, complete-pixel stationary sampling, movement reset, bounded finite math, and preset/custom equivalence.
- Deliverable: ranked root causes, concrete math/API recommendation, primary-source URLs, and focused tests.
- Done when: bright edge halos, contrast washout, and incomplete convergence each have source-backed explanations or clearly bounded hypotheses.
- Required verification: read-only source/test inspection and primary-source research.
- Allowed Git and external actions: read-only commands and web research only; no Git metadata changes.
- Stop and report if: source state differs from `ae7112f` beyond the plan or the required fix would edit `donut/`.
- Handoff revision/artifact: primary-source accumulation research, ranked temporal findings, and final rendering-risk review against the integrated diff.
- Handoff acknowledged by/on: coordinator, 2026-08-13.

### Light-1: Audit Flashlight Idle Motion and Invalidation

- Owner/thread: `/root/flashlight_audit`
- Branch/worktree: shared read-only access to the feature worktree
- Base commit/state: `ae7112f` plus only the coordinator-authored execution plan
- Read scope: flashlight settings/transform/update code, camera input state, temporal invalidation, UI controls, and flashlight tests/docs.
- Write scope: none.
- No-touch scope: entire filesystem and Git/index state.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: an opt-in setting makes the submitted flashlight transform exactly unchanged on idle frames, but camera/mouse motion and setting changes update it and invalidate histories.
- Deliverable: exact update chain, usable idle/mouse-look signal, recommended state machine and defaults, and focused tests.
- Done when: the source of idle drift and all consumers needing invalidation are enumerated.
- Required verification: read-only source/test inspection.
- Allowed Git and external actions: read-only commands only; no Git metadata changes.
- Stop and report if: source state differs from `ae7112f` beyond the plan or behavior depends on uncertain user-input semantics.
- Handoff revision/artifact: flashlight update-chain audit and final state/reset/UI review against the integrated diff.
- Handoff acknowledged by/on: coordinator, 2026-08-13.

### Int-1: Implement, Integrate, and Verify the Coupled Repair

- Owner/thread: `/root`
- Branch/worktree: `codex/temporal-accumulation-ui-repair` feature worktree
- Base commit/state: `ae7112f` plus this execution plan
- Read scope: whole repository excluding generated build outputs and forbidden dependency edits.
- Write scope: task-owned first-party sources, focused tests, documentation, and this plan.
- No-touch scope: `donut/`, unrelated active plans/worktrees, user files, remote branches, and generated outputs as commits.
- Build directory and runtime/GPU/resource lease: coordinator-only isolated `build-temporal-accumulation-ui-repair`; one task-owned UVSR process at a time.
- Dependencies already integrated: none.
- Interface/invariant contract: contracts listed above; no history through movement; no edits under `donut/`.
- Deliverable: local verified candidate executable, focused diff, test evidence, runtime capture, and archived completed plan.
- Done when: all acceptance criteria map to passing evidence and an independent rendering-risk review is complete.
- Required verification: focused source-contract/unit tests, Release build, relevant/full CTest, shader bundle checks, heading validation, diff audit, and bounded runtime visual inspection.
- Allowed Git and external actions: local edits/builds/tests/launch only; no commit, push, PR, merge, release, or deployment without later authorization.
- Stop and report if: an unrelated diff appears, the design would preserve history through movement, uncertain deletion is required, or runtime testing would conflict with user activity.
- Handoff revision/artifact: `b-ta/bin/uvsr.exe`, SHA-256 `FDFBB1059AB82C1AD42B04F01BDE444808D74AFE48B421912C1843EB40B3373F`.
- Handoff acknowledged by/on: local handoff prepared 2026-08-13; no commit or publication authorized.

## Integration Order

1. Complete and reconcile the three read-only audits into one stable runtime/settings contract.
2. Add or tighten failing focused tests for spacing, accumulation math/scheduling, TAA rejection/color preservation, and flashlight idle state.
3. Implement the shared CPU/UI contract and shader/runtime repairs, then build and run focused checks.
4. Run independent rendering-risk review, repair findings, and run composed Release/full verification.
5. Perform a bounded runtime visual check, record the exact executable identity/settings, update documentation, and archive this plan.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Uniform top-level spacing | Source invariant plus layout/source-contract regression coverage and candidate screenshot | Focused UI tests and 1920x1080 runtime capture | Passed; all collapsed gaps were visually uniform and source contracts cover the six former duplicate-spacing sites |
| TAA edge/contrast correctness | Deterministic reference tests plus still/motion visual comparison | Temporal-AA tests, shader build, bounded runtime matrix | Passed; stationary silhouettes are depth-owned, scene-linear blend is default, FP16 current/history uncertainty and sloped planes have regressions, and no bright rim was visible in the clean runtime scene |
| Complete, color-preserving sample accumulation | CPU reference math tests, shader/source contract, every-pixel mask assertion, stationary convergence capture | Focused accumulation/path-tracing tests and runtime convergence check | Passed; finite scene-linear mean/variance, complete deterministic scheduling, per-success RNG phases, movement reset, and local nonfinite-history recovery are covered |
| Presets and Custom controls | Default/reset/round-trip/custom-detection tests and UI inspection | Focused settings/UI tests | Passed; Progressive Mean, Responsive Mean, Variance Guided, and Custom were visible; deferred dependent controls animated correctly |
| Flashlight exact idle transform | State-machine unit/source-contract tests and repeated-frame transform/hash evidence | Flashlight tests and bounded runtime check | Passed automated exact-pose/publication/reset contracts; runtime control presence and flashlight activation verified, while live user input prevented a final automated toggle interaction |
| Integrated candidate | Clean task diff, Release target, relevant/full CTest, executable SHA-256 | Isolated Release build and final audit | Passed; 327 DXIL tasks, Release link, 43/43 CTest, Title Case 173/173, diff check, three independent reviews, and exact final launch/load |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-13 | Base the repair on exact live GitHub `main` commit `ae7112f` in a new isolated worktree. | The original checkout is diverged with unrelated untracked files; editing it or the existing path-tracing worktree would risk user/peer work. Live `refs/heads/main` exactly matches the reported build. | All |
| 2026-08-13 | Keep one coordinator as the only writer and use parallel read-only specialists. | TAA, accumulation, UI settings, and invalidation share `src/uvsr.cpp` and CPU/HLSL contracts, so competing writers would create unsafe interface overlap. | All |
| 2026-08-13 | Treat movement reset as a hard invariant for every accumulation preset. | The user explicitly excludes history preservation after movement; this also bounds ghosting and stale-light risk while the correctness repair is evaluated. | TEMP-1, LIGHT-1, INT-1 |
| 2026-08-13 | Make the sample accumulator the sole temporal owner in Ray Marching and derive stochastic phase from each pixel's successful sample count. | Averaging TAA/denoiser output compounds bias and correlation; a global cosmetic noise phase plus random harmonic retry skips sequence coverage. Every retained sample now belongs to one scene-linear estimator and movement resets its epoch. | TEMP-1, INT-1 |
| 2026-08-13 | Use filtered-depth ownership plus complete-footprint coherence for default TAA. | Unconditional stationary history caused bright silhouettes; comparing both depth extrema to one expected depth rejected valid slopes. The selected rule rejects mixed geometry/background while retaining ordinary planar slope and budgeting the actual FP16 storage paths. | TEMP-1, INT-1 |
| 2026-08-13 | Freeze the last submitted flashlight pose, not an idealized zero-sway pose. | Pausing clocks alone still dirtied the scene graph; snapping would create a lurch. Exact camera/config changes update once, while exact submitted-transform equality keeps idle history stable. | LIGHT-1, INT-1 |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-13 | `/root` | Active | Worktree at `ae7112f`; plan added | Live remote/main identity, original status, worktree inventory, open PR list, and screenshot inspected | Await audits, then stabilize contract before edits |
| 2026-08-13 | `/root` | Active | `b-ta/bin/uvsr.exe` | 327 DXIL variants and Release renderer target built; 12 focused tests passed before final naming/docs pass | Run final focused/full CTest, independent reviews, and bounded runtime inspection |
| 2026-08-13 | `/root/ui_spacing_audit` | Complete | Integrated working diff | Final read-only UI review found no actionable defect | Ownership released |
| 2026-08-13 | `/root/flashlight_audit` | Complete | Integrated working diff | Final read-only flashlight review found no actionable defect | Ownership released |
| 2026-08-13 | `/root/temporal_audit` | Complete | Integrated working diff | Final read-only temporal/rendering review found no P0/P1 defect | Ownership released; optional performance profiling retained below |
| 2026-08-13 | `/root` | Complete | `b-ta/bin/uvsr.exe`, SHA-256 `FDFBB1059AB82C1AD42B04F01BDE444808D74AFE48B421912C1843EB40B3373F` | 327/327 shader tasks; Release link; 43/43 CTest; 173/173 document headings; diff check; exact executable launch and Sponza load; clean UI/temporal visual smoke on the immediately preceding build | Archive plan and hand off local candidate |

## Risks and Escalation Triggers

- TAA and accumulation may share symptoms while having different causes; do not mask either with indiscriminate sharpening or exposure compensation.
- A conditional drawer may omit or double-submit spacing during animation; test visible adjacency rather than only counting literal `ImGui::Spacing()` calls.
- Running-average precision and sample-count saturation must remain finite at the maximum configured history.
- Flashlight idle lock must not reinterpret ordinary camera translation as idle or conceal a history reset.

Stop and ask the user if:

- A required choice materially trades color fidelity for persistent noise or performance after the correctness-preserving presets are exhausted.
- Verification requires stopping or reconfiguring an unrelated process, or the machine is not available for a bounded runtime check.
- Completion would require publishing, merging, deleting uncertain work, or restoring history through movement.

## Completion

- Final integrated commit: none; the user authorized local implementation but not a commit.
- Verification summary: Release `uvsr` built with all 327 DXIL tasks; 43 of 43 CTest tests passed; the complete in-scope Title Case scan checked 173 headings with zero violations; `git diff --check` reported no whitespace errors; SHA-256 is `FDFBB1059AB82C1AD42B04F01BDE444808D74AFE48B421912C1843EB40B3373F`.
- Independent review: UI, flashlight, and temporal/rendering reviewers all returned clean final verdicts with no P0/P1 defects.
- Coming Soon/documentation update: the README has no Coming Soon section; durable behavior is documented in `docs/advanced-settings.md`, `docs/noise.md`, `docs/path-tracing-transport.md`, `docs/pbr-foundation.md`, and `docs/temporal-aa-options.md`.
- Pushed/PR/merged, or intentionally local: intentionally local; no external action was authorized.
- Remaining experiments or follow-ups: profile variance-surface bandwidth and Four Texel's Gather plus `SampleLevel` before optimizing; add an optional chromatic CPU/HLSL scheduler-parity test. Neither is a correctness blocker.
- Active ownership released: yes; all reviewers released ownership and coordinator work is complete.
- Archived to completed/abandoned path: `docs/exec-plans/completed/temporal-accumulation-ui-repair.md` on 2026-08-13.
