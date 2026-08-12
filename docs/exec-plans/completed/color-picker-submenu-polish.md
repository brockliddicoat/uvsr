# Color Picker Submenu Polish

## Status

- State: superseded by the visual correction record below
- Coordinator: `/root`
- Project branch and worktree: `codex/color-picker-submenu-polish` in
  `work/ui-polish-interactions`
- Base commit: `b9427a875e8187f5f43ebc84777387d8a23a83b3`
- Started: 2026-08-12
- Completed: 2026-08-12
- Archived:
  `docs/exec-plans/completed/color-picker-submenu-polish.md`
- Superseded by:
  `docs/exec-plans/completed/color-picker-submenu-visual-correction.md`

The user's exact-build screenshot rejected the frame, centering, spacing, and
wheel-edge results recorded here. This file remains as the first candidate's
historical evidence; its visual completion claims are not current acceptance.

## Goal and Done Condition

Goal: polish the authored color-picker submenu without changing the opacity of
its existing translucent surfaces or interior layers.

Done when:

- [x] The outer picker frame stays opaque independently of inherited UI alpha.
- [x] The source pointer targets the Settings content edge while retaining the
      source swatch's vertical coordinate.
- [x] A picker follows a high source row and clamps no lower than the existing
      bottom bound.
- [x] Increased popup padding preserves four-bar/fourth-column alignment.
- [x] Both hue-wheel edges carry the transparent gradient outline.
- [x] Every known authored hover tooltip is at most 120 characters.
- [x] Focused contracts, Release build, full tests, and exact-build visual
      inspection are complete or accurately reported.

The exact candidate was not launched because an older UVSR build remained open
as PID 25712 and was left untouched. Requested geometry and rendering behavior
are covered by the runtime draw-list tests; on-screen product review remains
unclaimed.

## Scope

In scope:

- Authored picker popup geometry, frame alpha isolation, padding, pointer, and
  wheel-edge presentation.
- Production tooltip copy and bounded dynamic tooltip display.
- Focused runtime/source contracts and maintained UI documentation.

Non-goals:

- Renderer, shader, scene, persistence, generic popup, or Ogg behavior.
- Donut source edits, publication, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `overrides/imgui-tooltip-picker.patch`
- `tests/imgui_dropdown_roll_tests.cpp`
- `tests/ui_source_contract_tests.cpp`
- `docs/advanced-settings.md`
- `docs/ui-integration-agent-procedure.md`

## Baseline

- Requested base: clean `b9427a8` in the dedicated UI worktree.
- The ordinary `main` checkout was newer and had unrelated untracked files; it
  remained untouched.
- `b9427a8` was the prior published picker-unification build, but the user
  feedback rejected the affected submenu details.
- Existing tests incorrectly required bottom locking, actual-swatch X targeting,
  and an outer frame whose expected color was also attenuated by style alpha.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Picker geometry audit | Exact current formulas and opacity seam | Complete | Coordinator |
| Tooltip length audit | Exhaustive static and dynamic over-limit inventory | Complete | Coordinator |
| Test audit | Independent contract and visual-verification design | Complete | Coordinator |
| Final UI review | Frozen-diff rendering and contract review | Complete | Coordinator |

No renderer ABI, shader binding, resource layout, persistence, or asset contract
changed. ImGui changes remain first-party ordered overrides; the pinned
dependency stayed pristine.

## Assignment Summary

| Task ID | Owner | Base | Write Scope | Status |
| --- | --- | --- | --- | --- |
| Picker geometry audit | `/root/picker_geometry_audit` | `b9427a8` | None | Complete |
| Tooltip length audit | `/root/tooltip_length_audit` | `b9427a8` | None | Complete |
| UI test audit | `/root/ui_test_audit` | `b9427a8` | None | Complete |
| Final UI review | `/root/final_ui_review` | Frozen local diff | None | Complete |
| Integration | `/root` | `b9427a8` | Coordinator hotspots | Complete |

## Integration Order

1. Repaired picker rendering and placement in composed ImGui source, then
   regenerated the ordered override.
2. Updated production tooltip copy and bounded dynamic tooltip routes.
3. Strengthened runtime/source contracts and maintained UI documentation.
4. Recomposed, built, tested, and independently reviewed the frozen diff.

## Verification Evidence

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Picker geometry and opacity | Runtime draw-list geometry and alpha assertions | `uvsr_imgui_dropdown_roll_lifecycle` | Passed, including moved-source close, inset pointer, RGB/RGBA alignment, opacity split, padding, and both wheel edges |
| Source and tooltip contract | Exact override/source assertions and 120-character audit | `uvsr_ui_source_contract` | Passed |
| Tooltip runtime boundary | Rendered exact-120, 121-plus, UTF-8, formatted dynamic, and stock cases | `uvsr_imgui_dropdown_roll_lifecycle` | Passed with exact text/count and no scrollbar |
| UI animation safety | Existing animation reference | `uvsr_ui_animation_reference` | Passed |
| Ordered override integrity | Reapply final patch to the preceding composed ImGui state | Normalized seven-file comparison | All files matched |
| Integrated candidate | Complete Release build and CTest | CMake build plus CTest | Passed, 40 of 40 tests |
| Independent review | Frozen-diff review and repair re-review | `/root/final_ui_review` | Both findings resolved; no remaining concrete issue |
| Visible behavior | Exact candidate at high, middle, low, and inset color rows | Local launch | Not run; older UVSR PID 25712 remained open and was not disturbed |

Candidate artifact:

- Path:
  `build-color-picker-submenu-polish/bin/uvsr.exe`
- SHA-256:
  `D8006D275825637DE5AC310C6FF5F144CA4E636A658A67F12BA134BD4E8BC764`
- Size: 2,757,632 bytes
- Build time: 2026-08-12 11:19:12 UTC

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-12 | Branch locally from exact `b9427a8`. | It is the requested clean base; newer `main` was outside scope. | Whole task |
| 2026-08-12 | Keep one coordinator writer and parallel read-only audits. | The override and its tests are coupled shared hotspots. | Whole task |
| 2026-08-12 | Make only the outer frame ignore inherited style alpha. | The frame role is alpha 1; popup surfaces and interior layers must retain their opacity. | Picker rendering |
| 2026-08-12 | Restore source-anchor Y with a lower clamp at the current bottom position. | This follows higher swatches and never moves lower than the rejected bottom-locked result. | Picker placement |
| 2026-08-12 | Refresh one shared source rectangle during retained close. | Popup placement and pointer drawing must not split when Settings moves on the close frame. | Picker transition |
| 2026-08-12 | Use a Unicode-safe 120-code-point cap and seven-line fixed tooltip height. | Rendered exact-boundary, UTF-8, formatted, and stock cases must remain scrollbar-free. | Tooltips |

## Progress and Handoffs

| Date | Task and Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-12 | Read-only audits | Complete | Agent handoffs | Source, test, geometry, opacity, and tooltip inventory inspected | Frozen contracts implemented |
| 2026-08-12 | Integration by `/root` | Complete | Local task diff | Ordered override reconstruction and focused tests passed | Frozen for review |
| 2026-08-12 | Final UI review | Complete | Read-only findings | Found closing-anchor mismatch and tooltip coverage gap | Both repaired |
| 2026-08-12 | Repair re-review | Complete | Frozen final diff | Original findings resolved, no remaining concrete issue | Full verification passed |

## Risks and Follow-Up

- Exact on-screen product review remains useful after the pre-existing renderer
  window is closed, but it is not claimed by this handoff.
- The local candidate is technically verified, not Canonical verified, because
  it remains uncommitted, unintegrated, and not product accepted.

## Completion

- Final integrated commit: intentionally uncommitted because none was requested.
- Verification summary: Release build and all 40 CTest cases passed; the final
  ordered patch exactly reconstructs the intended composed ImGui sources.
- Independent review: complete; both findings repaired and re-reviewed with no
  remaining concrete issue.
- Coming Soon/documentation update: no roadmap entry was needed for this focused
  polish; maintained UI documentation was reconciled.
- Pushed, pull request, merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: optional exact-candidate on-screen review
  after the older renderer process is no longer open.
- Active ownership released: yes.
- Archived to completed/abandoned path: completed path above.
