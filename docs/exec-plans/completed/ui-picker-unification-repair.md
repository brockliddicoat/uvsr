# UI Picker Unification Repair

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-polish-interactions` in
  `work/ui-polish-interactions`
- Base commit: `116fe594779b57ba467faef6352c48e2bc59f1bd`
- Started: 2026-08-12
- Last updated: 2026-08-12
- Planned archive:
  `docs/exec-plans/completed/ui-picker-unification-repair.md`

## Goal and Done Condition

Goal: repair the tooltip and color-picker regressions published in `116fe59`,
then enforce one authored picker design across every current and future UVSR
color control.

Done when:

- [x] Nested and top-level hover tooltips use one uniform size policy.
- [x] Picker controls remain opaque while the interior panel layers are
      transparent and only the outermost few-pixel frame is opaque.
- [x] Wheel, triangle, hue, and alpha markers share the requested intermediate
      circle size.
- [x] Popup-only comparison swatches are removed; the two rows of four values
      and four vertical bars use the full aligned width.
- [x] Advanced Accents is removed and all five color rows use one shared
      first-party invocation path and one identical popup.
- [x] RGB-only pickers retain the four-bar topology with the unused alpha bar
      visibly disabled, and Material pickers have no extra heading text.
- [x] Focused contracts, Release build, and complete tests pass; the user
      explicitly accepts the exact candidate for direct publication while live
      visual inspection remains accurately recorded as not run.

## Scope

In scope:

- Authored tooltip sizing and retained appearance behavior.
- UVSR color-row invocation policy and Interface drawer organization.
- Authored picker popup geometry, frame/layers, vertical bars, markers,
  comparison presentation, and RGB disabled-alpha state.
- Focused runtime/source contracts and maintained UI documentation.
- One focused local commit and a direct fast-forward push to GitHub `main`, as
  explicitly authorized after verification.

Non-goals:

- Renderer, shader, scene, asset, persistence, or color-model changes.
- Direct edits under pinned `donut/`.
- Pull request, merge commit, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`
- Ordered first-party ImGui and Donut override patches under `overrides/`
- `tests/imgui_dropdown_roll_tests.cpp`
- `tests/ui_source_contract_tests.cpp`
- Focused UI documentation and this execution plan

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`
- Ordered override patches and their staged-source composition
- UI tests and maintained UI documentation
- Build directory, renderer process, and Windows UI automation

## Baseline

- Canonical repository/remote: live `origin/main` at `116fe59`.
- Local versus remote state: the dedicated task worktree is clean at the same
  commit; the ordinary `main` worktree is divergent with unrelated untracked
  files and remains untouched.
- Verified source commit/build: `116fe59` was previously reported green but is
  product-rejected by the user; it is only the buggy starting candidate.
- Visual evidence: user screenshot
  `codex-clipboard-0c9317ec-fb74-4ef6-9acf-794d43e5fbb4.png`.
- Known pre-existing failures: inconsistent nested tooltip sizing, opaque picker
  interiors, undersized markers, redundant comparison swatches, inconsistent
  picker routes, and a separate Advanced Accents drawer.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| History archaeology | Exact prior accepted sizing and opacity behavior | Complete | Coordinator |
| Picker architecture audit | Complete five-control route map and shared seam | Complete | Coordinator |
| Geometry review | Measurable layout and alignment invariants | Complete | Coordinator |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- No renderer ABI, shader, resource, persistence, or asset contract changes.
- One UVSR-owned color-control helper must be the only authored route for every
  current Interface and Material color editor.
- The popup always exposes the same four-bar topology; RGB data disables the
  alpha lane without inventing or mutating alpha.
- Comparison colors may remain encoded in existing controls but cannot appear
  as redundant popup swatches.
- ImGui dependency behavior remains supplied through ordered first-party
  overrides rather than edits under `donut/`.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `UI-CP-01` | `/root/history_archaeology` | Shared read-only | `116fe59` | None | None | Complete |
| `UI-CP-02` | `/root/picker_architecture` | Shared read-only | `116fe59` | None | None | Complete |
| `UI-CP-03` | `/root/layout_review` | Shared read-only | `116fe59` | None | None | Complete |
| `UI-CP-04` | `/root/picker_architecture` | Shared read-only | Dirty candidate | None | Integration | Complete |
| Integration | `/root` | Current worktree | `116fe59` | Coordinator hotspots | All audits | Complete |

## Integration Order

1. Freeze shared tooltip, color-control, popup, and geometry contracts from the
   three read-only audits.
2. Implement the shared first-party invocation and Interface drawer cleanup.
3. Implement popup geometry, opacity, marker, and disabled-alpha behavior in the
   ordered ImGui override.
4. Update focused runtime/source contracts and maintained documentation.
5. Recompose overrides, build Release, run focused and complete tests, and audit
   document headings.
6. Launch and visually inspect the exact candidate, repair any mismatch, then
   obtain an independent frozen-diff review.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Uniform tooltips | Equal settled bounds for top-level and nested owners | Focused ImGui runtime contract plus live hover matrix | Runtime contract passed; exact candidate accepted for publication; live inspection not run |
| Transparent picker body | Opaque thin outer frame, translucent interior layers, opaque color assets | Draw-list assertions and live captures | Draw-list contract passed; exact candidate accepted for publication; live inspection not run |
| Shared marker size | Exact midpoint constant and equal interactive marker radii | Geometry contract and live capture | Geometry contract passed; exact candidate accepted for publication; live inspection not run |
| Aligned full-width layout | No comparison swatches; value rows and bars share outer bounds | Runtime geometry assertions | Passed |
| One picker design | Five call sites route through one helper with identical flags/configuration | Source contract and live five-picker matrix | Source contract passed; exact candidate accepted for publication; live inspection not run |
| RGB disabled-alpha state | Fourth bar remains present, visibly disabled, and noninteractive | Runtime input/draw assertions and Material live check | Runtime contract passed; exact candidate accepted for publication; live inspection not run |
| No integration regression | Release build, full CTest, patch equivalence, document checks | Repository verification commands | Release build and 40/40 CTest passed; patch output equivalent; title check passed |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-12 | Continue in the existing dedicated branch/worktree at `116fe59`. | It exactly matches live GitHub `main` and is clean; the ordinary `main` checkout is divergent and contains unrelated files. | Whole task |
| 2026-08-12 | Use read-only parallel audits and one coordinator writer. | Tooltip and picker behavior is coupled through one ordered ImGui override and shared tests. | All assignments |
| 2026-08-12 | Treat `116fe59` as product-rejected despite its historical green test record. | Current user-observed visual defects supersede the previous agent's acceptance claim. | Verification |
| 2026-08-12 | Leave the existing Explorer-launched UVSR process running. | PID 22808 is the user's older `build/bin/uvsr.exe`, not a task-owned candidate; closing it would exceed this task's process authority. | Live inspection |
| 2026-08-12 | Treat the user's direct fast-forward instruction as acceptance of the exact verified candidate for publication. | The exact executable and verification limits were disclosed before the user explicitly requested publication; live inspection remains recorded as not run rather than being inferred. | Publication |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-12 | Coordinator preflight `/root` | Complete | This plan | Live remote, worktrees, branch state, active plans, roadmap, open PRs, visible tasks, old task record, screenshot, and collaboration policy inspected | Complete audits before edits |
| 2026-08-12 | Read-only audits `UI-CP-01` through `UI-CP-03` | Complete | Distilled coordinator handoffs | History, route inventory, geometry, tests, and screenshot reviewed | Contracts frozen and ownership released |
| 2026-08-12 | Integration `/root` | Complete | `build-ui-picker/bin/uvsr.exe`, SHA-256 `21A92720DA565B2492B5FD63521A771B1F2AD6B0AD400F4098F4FA32EF104E16` | Fresh override composition, Release build, strengthened source/runtime contracts, full 40-test suite, patch equivalence, diff check, and document Title Case check passed | Exact-build live inspection is blocked by the user's existing PID 22808 |
| 2026-08-12 | Independent review `UI-CP-04` | Complete | Frozen implementation and test diff | Two independent final reviews passed after false-pass seams were closed; historical opacity comparison matches the accepted pre-regression three-layer stack | Exact-DPI visual judgment was not run and is explicitly accepted as a publication residual |

## Risks and Escalation Triggers

- Ordered patch composition can apply cleanly while producing stale geometry;
  staged output must match the intended source byte-for-byte.
- Removing comparison swatches must not remove Current/Original comparison data
  that the shared picker may still need internally.
- RGB controls must not gain a synthetic persisted alpha channel.
- Popup animation, source-pointer anchoring, lane fitting, and bottom alignment
  can become stale when width or padding changes.
- Exact-build live inspection cannot begin while the user's existing
  Explorer-launched UVSR PID 22808 is open; the coordinator will not close it.

Stop and ask the user only if preserving every selectable color is incompatible
with the requested common topology or if the screenshot's fourth-column
alignment cannot coexist with the available picker lane.

## Completion

- Final integrated commit: the publication commit containing this completed
  plan, with subject `fix: unify tooltips and color pickers`.
- Verification summary: local Release candidate built; 40/40 CTest passed;
  composed ImGui sources match the intended staged sources after newline
  normalization; document Title Case and diff checks passed.
- Independent review: passed by picker architecture, runtime-test, geometry,
  and historical-opacity audits with no actionable finding.
- Coming Soon and documentation update: maintained UI documentation and source
  counts updated; no roadmap entry was required for this focused repair.
- Pushed, pull request, merged, or intentionally local: direct fast-forward of
  the publication commit to GitHub `main` was explicitly authorized; no pull
  request, merge commit, release, or deployment is part of this task.
- Remaining experiments or follow-ups: optional exact-build live inspection
  after the user closes PID 22808; it is not a publication blocker by explicit
  user acceptance.
- Active ownership released: all coordinator and reviewer paths released.
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/ui-picker-unification-repair.md`.
