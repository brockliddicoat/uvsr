# Front-End Functionality Restoration

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/engine-core-cleanup` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\engine-core-cleanup`
- Base commit: `f7c0c87d8cba6880428fbc34400eb2882fb5182e` plus the reviewed local
  engine-cleanup diff identified at intake by tracked patch hash
  `631ec35c500cc7bf4f78cc69b68a1a98cb32eebc`
- Started: 2026-08-02
- Last updated: 2026-08-02
- Planned archive:
  `docs/exec-plans/completed/frontend-functionality-restoration.md`

## Goal and Done Condition

Goal: keep the verified renderer and shader cutdown while restoring the
information, preset ownership, disclosure animation, reset placement,
tooltips, buffer controls, and effect-cost inspection lost in the first compact
front-end pass.

Done when:

- [x] Visibility again shows its originating quality profile, appends
      `(Custom)` after an edit, and provides the established animated reset
      arrow at the correct ownership level.
- [x] Every retained dropdown has a visible label, its current effective value,
      a concise tooltip, and a reset affordance where it inherits a preset.
- [x] Buffers returns as a compact drawer, Statistics selects one effect at a
      time, and the six general statistics values share compact dash-separated
      lines.
- [x] Aliasing, Debug, Visibility, and Advanced groups use animated nested
      disclosure without restoring retired renderer paths.
- [x] The command interface occupies one row, with guidance inside the empty
      input and no separate guidance row.
- [x] Requested terminology and retained options are exact: Aliasing,
      Temporal Reconstructive, Conservative Morphological, Permutated White
      Noise, Full Resolution, and Packed Depth-Normal.
- [x] Packed Depth, ambient-occlusion power, and the shadow edge overlay are
      removed end to end, including commands, shader/resource support, tests,
      and documentation.
- [x] Release build, shader packaging, focused contracts, full tests, document
      validation, runtime UI smoke checks, and independent review pass.

## Scope

In scope:

- Preset/custom/reset presentation and control tooltips.
- Visibility, Buffers, Statistics, Aliasing, Debug, and command-interface
  composition.
- Retained visibility and aliasing settings contracts needed by those controls.
- Effect-specific timing presentation for every retained renderer effect.
- Removal of Packed Depth, ambient-occlusion power, and Edge Overlay support.
- Focused tests and current renderer/UI documentation.
- A ranked recommendation-only list of remaining safe cutdown opportunities.

Non-goals:

- Restoring sparse virtual shadows, diagnostic cascaded shadows, visibility
  temporal accumulation, multiple-bounce diffuse, sample resurrection, forward
  rendering, benchmark planners, or any other backend retired by the cleanup.
- Returning to one mutually exclusive anti-aliasing selector.
- Editing Donut, publishing, pushing, opening a pull request, merging, or
  releasing.
- Claiming a performance improvement without a matched measurement.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/screen_space_visibility*`
- `src/screen_space_directional_shadows*`
- `src/cmaa2*`
- `src/temporal_aa_options.h`
- `src/ui_settings_command_catalog.h`
- `src/shaders.cfg`
- focused UI, command, visibility, shadow, aliasing, shader-package, and layout
  tests
- current README and renderer/UI documents

Shared hotspots reserved for the coordinator:

- All writable paths above, `README.md`, this execution plan, build trees,
  shader packaging, the renderer window, and the graphics processor.

## Baseline

- Canonical repository/remote: not queried because this is a local continuation
  and no newest-remote, integration, or publication action was requested.
- Local versus remote state: `codex/engine-core-cleanup` is based at `f7c0c87`
  with the complete local cleanup and documentation changes still uncommitted.
- Verified source commit/build: local cleanup candidate
  `build-engine-core-cleanup/bin/uvsr.exe`, SHA-256
  `536D2A6092927E483573144F858F927E8F3B580C2C2FB2ACE83B9FC7CF6B733C`.
- Graphics processor, scene, camera, resolution, and settings preset when
  relevant: retain the cleanup runtime-smoke conditions; record exact state for
  the replacement candidate before visual acceptance.
- Known pre-existing failures: none in the cleanup candidate's recorded 30-test
  suite; the user rejected its front-end functionality and readability.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Backend cleanup | Current dirty cleanup state and singular renderer contracts | Frozen as functional base | Whole restoration |
| Pre-cleanup UI | `f7c0c87` preset, reset, tooltip, Buffer, Statistics, animation, and command behavior | Read-only reference | UI implementation |
| Independent anti-aliasing | Temporal, morphological, and multisample techniques remain independently composable and default off | Fixed product contract | Aliasing UI and renderer |
| Debug composition | World appearance and information filters remain independently composable | Fixed product contract | Debug UI and shaders |

Public interface, shader binding, resource layout, and serialized-setting
contracts:

- The retained renderer topology and 315 first-party shader baseline were the
  starting point. Removing Ambient Occlusion Power reduced the final catalog to
  314 first-party shader tasks.
- UI presentation may hide sentinel inheritance values, but command/reset and
  resolved runtime behavior must remain deterministic.
- Removing a setting removes its commands, constants, shader branches,
  resources, permutations, tests, and documentation when they have no other
  consumer.
- Each effect-cost selector must report a retained live timer or an explicit
  unavailable state; it must not invent timing from unmatched values.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `UI-LEGACY-MAP` | `/root/cleanup_revert_map` | Read-only shared worktree | Intake dirty state | None | Pre-cleanup UI | Completed |
| `UI-AA-VIS-DEBUG-MAP` | `/root/first_cutdown_map` | Read-only shared worktree | Intake dirty state | None | Pre-cleanup UI | Completed |
| `UI-CONTRACT-TEST-MAP` | `/root/independent_review` | Read-only shared worktree | Intake dirty state | None | Current tests/history | Completed |
| `IMPLEMENT` | `/root` | Current worktree | Intake dirty state | All task paths | Audit handoffs | Completed |
| `FINAL-REVIEW` | `/root/independent_review` | Read-only frozen candidate | Final dirty state | None | Integrated candidate | Completed |

## Assignment Contracts

### UI Legacy Map: Restore Presets and Information Surfaces

- Owner/thread: `/root/cleanup_revert_map`
- Branch/worktree: read-only current shared worktree
- Base commit/state: intake dirty state over `f7c0c87`
- Read scope: old and current UI, command catalog, tests, and documentation
- Write scope: none
- No-touch scope: every file, ref, index, build tree, process, and other
  worktree
- Deliverable: exact legacy symbols and smallest coherent restoration design
- Done when: presets, reset placement, labels, tooltips, Buffers, Statistics,
  and command-interface regressions are all mapped
- Required verification: read-only Git/source inspection
- Allowed Git and external actions: read-only only
- Stop and report if: restoration requires a retired backend contract

### UI Aliasing Visibility and Debug Map: Preserve Composition

- Owner/thread: `/root/first_cutdown_map`
- Branch/worktree: read-only current shared worktree
- Base commit/state: intake dirty state over `f7c0c87`
- Read scope: old/current aliasing, visibility, Debug, settings, shaders, tests,
  and documents
- Write scope: none
- No-touch scope: every file, ref, index, build tree, process, and other
  worktree
- Deliverable: exact option, animation, naming, and requested-removal map
- Done when: every named Aliasing, Visibility, and Debug behavior has an
  evidence-backed implementation contract
- Required verification: read-only Git/source inspection
- Allowed Git and external actions: read-only only
- Stop and report if: a requested option conflicts with the retained renderer

### UI Contract Test Map: Prevent Information-Loss Regressions

- Owner/thread: `/root/independent_review`
- Branch/worktree: read-only current shared worktree
- Base commit/state: intake dirty state over `f7c0c87`
- Read scope: current tests, UI source, settings, command catalog, and history
- Write scope: none
- No-touch scope: every file, ref, index, build tree, process, and other
  worktree
- Deliverable: focused regression-test plan and conservative future-cutdown
  candidates
- Done when: every acceptance behavior maps to a durable test and risky brittle
  assertions are identified
- Required verification: read-only source/test/history inspection
- Allowed Git and external actions: read-only only
- Stop and report if: coverage would require a new test framework

## Integration Order

1. Freeze the retained backend and recover exact legacy presentation contracts.
2. Remove the three explicitly rejected runtime/settings paths coherently.
3. Restore preset ownership, Buffers, Statistics, Aliasing, Visibility, Debug,
   and compact command composition.
4. Update commands, tests, and current documents together.
5. Build and package shaders, run the complete automated suite, and inspect the
   integrated diff.
6. Run labeled UI/runtime smoke checks, freeze the candidate, and obtain an
   independent read-only review.
7. Repair task-introduced failures, rerun invalidated evidence, archive this
   plan, and hand off the remaining-cutdown recommendations.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Preset identity and reset | Source contract plus live edited/reset profile | Focused UI tests and labeled runtime | Passed: `High`, `High (Custom)`, profile reset, and per-control reset observed |
| Labels and tooltips | Every retained affected control has visible label and concise explanation | UI source-contract tests and visual smoke | Passed: retained dropdown labels and representative control/drawer explanations observed |
| Animated disclosure | Nested state uses the measured animated tree/toggle helpers | UI tests and live expand/collapse | Passed: Visibility, Aliasing, Debug, and Advanced transitions observed |
| Compact Buffers and command row | Layout contracts plus live height/typing behavior | Focused layout/UI tests and runtime | Passed: compact Buffers plus single input row with disappearing hint observed |
| Effect-specific Statistics | Selector and current retained timing breakdown | UI tests and runtime selection sweep | Passed: compact general line and Screen-Space Visibility breakdown observed |
| Requested removals complete | Whole-tree negative search and exact shader package | `rg`, package tests, Release build | Passed: retired operational symbols absent and exact package contract green |
| No broader regression | Full focused and repository suite | Release build and `ctest` | Passed: Release build and 30 of 30 tests |
| Documentation valid | Title case, links, stale labels, and diff hygiene | repository document checker and `git diff --check` | Passed after final accounting update |
| Independent review | No unresolved priority-zero through priority-two findings | read-only frozen-candidate review | Passed: no unresolved priority-zero through priority-two source findings |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-02 | Restore the front end on the existing cleanup lineage. | The user explicitly accepted the backend cutdown and rejected only the over-simplified front end; reverting the full cleanup would restore unwanted renderer bloat. | Whole task |
| 2026-08-02 | Use the pre-cleanup UI as the behavioral reference, not as a file-level revert. | Its preset/reset/animation/tooltip behavior is desired, but it also owns retired shadows, planners, benchmarks, and renderer modes. | Implementation |
| 2026-08-02 | Keep one coordinator as the only writer and build/runtime operator. | The affected settings, UI, command, shader, and statistics contracts are tightly coupled shared hotspots. | Coordination |
| 2026-08-02 | Treat the latest user wording as the placement authority. | Temporal Cost moves to the main temporal section and Previous-Depth Validation moves into Advanced, reversing the earlier placement request. | Aliasing UI |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-02 | `/root` | Active | Intake dirty state and this plan | Repository/worktree/overlap audit complete | Await read-only maps, then implement |
| 2026-08-02 | Read-only mapping agents | Completed | Legacy UI, cutdown, and contract maps | Source/history audit | Findings integrated without worker writes |
| 2026-08-02 | `/root` | Completed | `build-frontend-restoration/bin/uvsr.exe`, SHA-256 `58B58FBD643859D12CFB9A746BD5FA563048CB1F569B56449E31FBE6A5F82458` | Release build, 268 core tasks, 46 shadow tasks, exact 39-file package, 30 of 30 tests | Run exact-artifact UI smoke |
| 2026-08-02 | `/root` | Completed | Exact replacement executable at 1920 by 1080, Sponza Decorated, Benchmark Position 1 | Full labeled UI/runtime smoke; factory settings restored; task process closed | Freeze source and finalize documents |
| 2026-08-02 | `/root/independent_review` | Completed | Final dirty source candidate | No unresolved priority-zero through priority-two source findings; `git diff --check` clean | Documentation accounting repaired by coordinator |

## Risks and Escalation Triggers

- The legacy UI cannot be copied wholesale because its controls reference
  removed shadow, planner, benchmark, temporal, and forward-rendering state.
- Removing Packed Depth compacts a CPU-to-shader numeric mode contract and
  requires exact constant/shader/test updates.
- Removing Edge Overlay changes screen-space shadow debug resources and shader
  outputs; isolation views must remain intact.
- Restoring per-effect timing must not recreate benchmark orchestration or
  developer-only permutation axes.
- Visual acceptance is invalidated by any later UI or artifact-changing repair.

Stop and ask the user if:

- a requested debug output requires choosing a materially new visualization
  rather than exposing retained data;
- restoration would require reviving a backend feature the user accepted as
  removed;
- publication, integration, or destructive cleanup beyond this task is
  requested without an unambiguous destination.

## Completion

- Final integrated commit: none; no commit was requested
- Verification summary: Release build passed; 268 core and 46 shadow shader
  tasks compiled; exact 39-file runtime package passed; 30 of 30 tests passed;
  labeled live UI/runtime smoke passed
- Independent review: complete with no unresolved priority-zero through
  priority-two source findings
- Coming Soon/documentation update: current README, renderer/UI guides, dated
  restoration report, and accounting reconciled
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: recommendation-only cutdown list in the
  dated engine-core report and final handoff; no additional cut was made
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/frontend-functionality-restoration.md`
