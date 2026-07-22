# UI Integration Quality Repair

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/aa-ui-merged-experiment` at
  `C:\Users\brock\Documents\Codex\2026-07-19\aa-ui-merged-experiment`
- Base commit: `553e604`
- Started: 2026-07-20
- Last updated: 2026-07-22
- Publication: canonical `main` publication authorized on 2026-07-22
- Archived:
  `docs/exec-plans/completed/ui-integration-quality-repair.md`

## Goal and Done Condition

Goal: repair the integrated Visibility, Buffers, and Statistics UI so it follows
the accepted animated Settings behavior, cannot crash when Statistics opens,
has deliberate sibling-drawer spacing, and replaces the launch progress track
with a one-to-three-dot waiting animation.

Done when:

- [x] Every affected top-level drawer uses the animated body contract and has a
      gap before its next sibling header.
- [x] Newly integrated nested drawers, dropdowns, sliders, and actions use the
      established UVSR-owned control paths without losing visibility behavior.
- [x] Statistics opens and remains responsive in the running Release build.
- [x] The launch screen has no progress track and cycles `please wait.` through
      `please wait...`.
- [x] A durable agent procedure and automated source-contract test prevent the
      same integration omissions.
- [x] Shaders, Release, tests, documentation checks, and a labeled runtime smoke
      pass; the result remains local and unpushed.

## Scope

In scope:

- Repair the merged Settings UI in `src/uvsr.cpp`.
- Preserve every visibility preset, buffer control, statistics and benchmark
  behavior, AA control, shader permutation, and optimization record.
- Add a repository agent procedure, a focused source-contract test, and current
  renderer-baseline documentation.

Non-goals:

- Redesigning accepted colors, ordering, defaults, labels, or renderer behavior.
- Pushing, opening a pull request, merging canonical history, or releasing.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `CMakeLists.txt` and `tests/ui_source_contract_tests.cpp`
- `AGENTS.md`, `README.md`, and
  `docs/ui-integration-agent-procedure.md`

Shared hotspots reserved for the coordinator:

- All affected files in this single-writer worktree
- The worktree-local `build` directory and the UVSR runtime window

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`
- Local versus remote state: local-only merged experiment; publication is
  explicitly withheld
- Verified source commit/build: merged candidate `553e604`; pre-repair Release
  executable reproduced the Statistics crash
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU, current startup scene, 1902 x 1069 user capture
- Known pre-existing failures: opening Statistics raises Windows exception
  `0xc0000005` at executable offset `0x161e83`, inside
  `ImGui::BeginTableEx`, after its current-window pointer becomes null

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Visibility and AA/UI integration | `553e604` | Ready | Whole repair |
| Accepted animated UI helpers | Current merged `src/uvsr.cpp` | Ready | Drawer/control repair |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Keep all renderer, AA, visibility, buffer, benchmark, and statistics data
  contracts unchanged.
- Treat `BeginDrawerBody`/`EndDrawerBody`, `BeginAnimatedTreeNode`/
  `EndAnimatedTreeNode`, `BeginRoundedCombo`, `DrawSliderFloat`,
  `DrawSliderInt`, and full-width sibling spacing as the UI integration
  contract.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Repair | `/root` | `codex/aa-ui-merged-experiment` | `553e604` | Listed files only | None | Completed |

## Assignment Contracts

### Repair: Restore UI Quality and Runtime Safety

- Owner/thread: `/root`
- Branch/worktree: `codex/aa-ui-merged-experiment`
- Base commit/state: clean `553e604`
- Read scope: repository UI source, tests, documentation, and local crash
  evidence
- Write scope: files listed under Affected Subsystems And Paths
- No-touch scope: Donut sources, shader/render contracts, canonical refs,
  remotes, generated build artifacts in Git, and unrelated AA plan files
- Build directory and runtime/GPU/resource lease: destination-local `build`;
  one labeled UVSR process
- Dependencies already integrated: complete visibility and AA/UI merge
- Interface/invariant contract: preserve both feature sets and use the accepted
  animated control composition throughout the new drawers
- Deliverable: local repaired candidate, agent procedure, regression test,
  rebuilt shaders and Release executable
- Done when: every acceptance criterion maps to passing evidence
- Required verification: focused test, full CTest, shader and Release build,
  document self-test/full scan, diff check, labeled UI/runtime smoke
- Allowed Git and external actions: local edits, build, tests, labeled launch,
  and a focused local commit; no push, PR, canonical merge, or release
- Stop and report if: retaining the behavior requires a visible renderer or
  AA/visibility tradeoff
- Handoff revision/artifact: local uncommitted working tree based on `553e604`;
  `build/bin/uvsr.exe`, SHA-256
  `067B5C28F4F445ED6A5FD4E398ACB4212BF147C2194117D2D57C487ED264B708`
- Handoff acknowledged by/on: accepted through subsequent user review;
  canonical publication authorized on 2026-07-22

## Integration Order

1. Reproduce and localize the crash and audit the merged UI against accepted
   helper usage.
2. Repair layout, animation composition, Statistics lifetime, and launch text.
3. Add the agent procedure and its automated source contract.
4. Rebuild shaders and Release, run all checks, and exercise the labeled build.
5. Archive this plan and keep the repaired candidate local and uncommitted for
   user approval.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Drawer spacing and animation | Source contract plus visual interaction | Focused CTest and labeled launch | Passed; affected drawers use measured animated bodies, and the live panel shows deliberate gaps between Buffers and Statistics |
| Statistics no longer crashes | Responsive process after opening and using drawer | Labeled launch and live click test | Passed; `uifinal-553e604-0331` rendered the GPU-stage table and remained responsive with no new Windows crash event |
| Loading bar removed | Source contract and observed startup text cycle | Focused CTest and labeled launch | Passed; one-dot and two-dot states were observed with no loading track, and the source contract enforces all three states |
| Visibility and AA behavior retained | Full build and CTest | Release build and full CTest | Passed; all shader artifacts rebuilt, Release linked, and 15 of 15 CTest cases passed |
| Agent procedure durable | Title-case-valid procedure linked from root policy | Document checker and link audit | Passed; self-test passed and 514 headings/lead-ins produced zero violations |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-20 | Repair the exact local merge at `553e604` | It is the user-named newest AA/UI plus visibility destination and the failing artifact was reproduced from it | Whole repair |
| 2026-07-20 | Add a mechanical source-contract test beside the written procedure | Written guidance alone would not catch missing drawer bodies or sibling spacing before another build is handed off | Tests and procedure |
| 2026-07-20 | Stage every ImGui translation unit beside the patched headers | The override changed `ImGuiContext`, but unstaged `imgui_tables.cpp` compiled against the original sibling header and read the wrong `CurrentWindow` offset | Build override and Statistics |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-20 | Crash reproduction `/root` | Completed | Release `553e604` | Statistics click produced repeatable `0xc0000005`; Windows event and disassembly identify `ImGui::BeginTableEx` current-window dereference | Repair UI composition |
| 2026-07-20 | Root-cause trace `/root` | Completed | Instrumented Release trace | Caller had a valid current window immediately before `BeginTable`; staged and original internal headers place `CurrentWindow` at different offsets because the original table translation unit was not replaced | Unify ImGui translation-unit headers |
| 2026-07-20 | UI and build repair `/root` | Completed | Working tree based on `553e604` | Added animated Buffers/Statistics bodies, owned controls, spacing, loading-dot text, complete ImGui source replacement, procedure, and source contract | Rebuild and verify |
| 2026-07-20 | Final verification `/root` | Completed | `uifinal-553e604-0331` | Clean full shader build, Release build, 15 of 15 CTest, document self-test/full scan, diff check, responsive live Statistics table, no new crash event | Await user approval |

## Risks and Escalation Triggers

- Statistics contains multiple live tables and benchmark controls inside a
  deeply animated, auto-sizing Settings hierarchy; every Begin/End and
  disabled-scope balance must remain exact.
- Existing visibility and AA settings must not be simplified while adapting
  their presentation.

Stop and ask the user if:

- A fix would require removing or changing required visibility, statistics,
  benchmark, AA, or renderer behavior.
- Completion would require publication or canonical-history changes.

## Completion

- Final integrated commit at this repair checkpoint: intentionally not created;
  the repair remained a local working-tree candidate based on `553e604`
- Verification summary: all shader artifacts rebuilt; Release executable
  rebuilt; focused UI contract and all 15 CTest cases passed; document checker
  self-test and 514-item full scan passed; live loading text, drawer gaps, and
  Statistics rendering verified in `uifinal-553e604-0331`
- Independent review: coordinator post-change audit; no subagent is authorized
  by current execution constraints
- Coming Soon/documentation update: renderer baseline and agent procedure only;
  this corrective task does not add a roadmap item
- Pushed/PR/merged at this repair checkpoint: intentionally local
- Remaining experiments or follow-ups at this repair checkpoint: user
  acceptance, then a focused local commit if requested
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ui-integration-quality-repair.md`

## Canonical Publication

The user subsequently accepted the repaired UI and its later refinements. On
2026-07-22, this work was accepted for inclusion in the combined candidate
authorized for canonical `main` publication. The earlier local-only statements
above describe the original repair checkpoint rather than the final disposition.
