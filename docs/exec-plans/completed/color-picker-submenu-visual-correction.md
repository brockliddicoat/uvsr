# Color Picker Submenu Visual Correction

## Status

- State: complete
- Coordinator: `/root`
- Project branch and worktree: `codex/color-picker-submenu-polish` in
  `work/ui-polish-interactions`
- Base commit: `b9427a875e8187f5f43ebc84777387d8a23a83b3` plus the uncommitted
  first color-picker polish candidate
- Started: 2026-08-12
- Last updated: 2026-08-12
- Completed: 2026-08-12
- Archived:
  `docs/exec-plans/completed/color-picker-submenu-visual-correction.md`
- Prior execution record:
  `docs/exec-plans/completed/color-picker-submenu-polish.md`

## Goal and Done Condition

Goal: correct the first candidate against the user's marked-up runtime capture
without disturbing its unrelated verified behavior.

Done when:

- [x] The popup body is vertically centered on its source swatch whenever it
      fits, then clamps at the established Settings bottom while its pointer
      continues reaching the source row.
- [x] The full popup frame band uses the exact Settings/Performance panel frame
      color and inherited transparency treatment.
- [x] The brightest popup surface extends past every control by 3 to 6 pixels
      on all four sides before the frame begins.
- [x] A clearly visible translucent white one-pixel outline covers both hue-wheel
      circumferences.
- [x] A renderer timing row remains present after first becoming available and
      displays `--` with a zero measurement while temporarily unavailable.
- [x] Focused and full Release verification pass in an isolated continuation
      build, and visual evidence is either captured or accurately reported.

## Scope

In scope:

- Authored picker geometry, layered frame/surface bounds, pointer frame, wheel
  outlines, and their runtime/source contracts.
- Performance renderer-timing row lifetime and unavailable presentation.
- Maintained UI documentation and mechanical README line counts.

Non-goals:

- Renderer algorithms, shader work, scene/material behavior, publication, or
  changes under `donut/`.
- Closing or controlling either user-owned UVSR process already running.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `overrides/imgui-tooltip-picker.patch`
- `tests/imgui_dropdown_roll_tests.cpp`
- `tests/ui_source_contract_tests.cpp`
- `docs/advanced-settings.md`
- `docs/ui-integration-agent-procedure.md`

Shared hotspots reserved for the coordinator:

- All writable paths above, override regeneration, build trees, and runtime
  process control.

## Baseline

- Requested lineage remains exact `b9427a8` plus the current local correction
  candidate; no newer canonical changes are imported.
- The screenshot was captured from the current candidate and demonstrates the
  rejected bottom-clamped body, frame role, bright-surface bounds, and wheel
  outline visibility.
- PID 25712 runs `build-ui-picker/bin/uvsr.exe`; PID 37228 runs the first
  candidate from `build-color-picker-submenu-polish/bin/uvsr.exe`. Both are
  preserved and excluded from build/runtime ownership.
- A new continuation build directory will prevent executable-lock or build-tree
  contention.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Picker visual audit | Exact frame, padding, centering, and wheel roles | Complete | Coordinator |
| Performance row audit | Sticky row lifetime and zero-unavailable semantics | Complete | Coordinator |
| Test audit | Assertions that reject the marked-up defects | Complete | Coordinator |

No public ABI, shader binding, resource layout, serialized setting, or asset
contract may change. ImGui remains an ordered first-party override over the
pinned dependency.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Picker visual audit | `/root/picker_visual_correction_audit` | Shared read-only | Dirty continuation state | None | None | Complete |
| Performance row audit | `/root/performance_row_persistence_audit` | Shared read-only | Dirty continuation state | None | None | Complete |
| Test audit | `/root/picker_correction_test_audit` | Shared read-only | Dirty continuation state | None | None | Complete |
| Integration | `/root` | Current worktree | Dirty continuation state | Coordinator hotspots | Audit decisions | Complete |

## Integration Order

1. Freeze the corrected visual and sticky-row contracts from source plus the
   marked-up capture.
2. Update composed ImGui source and UVSR state, then regenerate the ordered
   override.
3. Replace false-positive tests and reconcile maintained documentation.
4. Build and test in a new isolated continuation directory.
5. Independently review the frozen diff and capture exact-candidate visual
   evidence only if runtime ownership becomes available.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Centering and bottom clamp | Runtime popup/source/pointer geometry | `uvsr_imgui_dropdown_roll_lifecycle` | Passed, including centered and bottom-clamped source fixtures |
| Frame and bright margin | Draw-list colors, band coverage, and four-sided distance | `uvsr_imgui_dropdown_roll_lifecycle` | Passed; measured approximately 3.5/3.5/3.5/3.0 pixels |
| Wheel outlines | High-contrast white-gradient coverage at both radii | `uvsr_imgui_dropdown_roll_lifecycle` | Passed with angular coverage and visible alpha-gradient checks |
| Sticky timing row | Executable available/unavailable lifecycle plus source integration contract | Focused UI tests | Passed; omission before first sample, session retention, view isolation, zero unavailable backing, and `--` are enforced |
| Ordered override integrity | Exact patch reconstruction | Patch application and isolated CMake composition | Passed |
| Integrated candidate | Release build and complete CTest | New continuation build tree | Passed, 40 of 40 tests |
| Visible behavior | Exact candidate with representative Material and Interface swatches | Local launch/screenshots | Not run; two pre-existing UVSR processes were preserved |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-12 | Treat the green rectangle as desired body placement and the red rectangle as the full current frame perimeter. | Their equal footprint and source-centered green position resolve the user's explicit markup. | Picker geometry/frame |
| 2026-08-12 | Preserve both running UVSR processes and use a new build directory. | Neither process belongs to this continuation's runtime lease. | Build/runtime |

## Progress and Handoffs

| Date | Task and Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-12 | Coordinator preflight | Complete | Screenshot and dirty worktree | Processes, worktrees, plans, and Coming Soon inspected | Await audit handoffs and implement |
| 2026-08-12 | Read-only audits | Complete | Agent handoffs | Geometry, frame, spacing, wheel, timing-row, and test contracts inspected | Implemented by coordinator |
| 2026-08-12 | Integration and verification | Complete | `build-color-picker-submenu-polish-v2` | Release build, focused tests, Title Case check, and 40/40 CTest passed | Independent review found no production defect; both P3 coverage/documentation gaps corrected |

## Risks and Escalation Triggers

- The panel frame treatment spans both a filled band and carved outlines; a
  two-pixel stroke alone is known insufficient.
- Bright-surface bounds and control padding are separate roles and must not be
  conflated again.
- The sticky performance state must survive temporary unavailability without
  inventing a nonzero timing or changing metric order.

Stop and ask the user only if exact visual verification requires control of a
currently active renderer window and neither process becomes available.

## Completion

- Final integrated commit: the commit containing this completion record.
- Verification summary: Release build and all 40 CTest cases passed; the focused
  executable UI test verifies centering, clamp/pointer reconciliation, full
  frame coverage, four-sided margins, wheel outlines, and tooltip bounds.
- Independent review: complete; no P0-P2 production defect found. Its two P3
  closeout findings were corrected with executable timing-row lifecycle coverage
  and current UI reference metadata.
- Coming Soon/documentation update: no roadmap entry expected; maintained UI
  documentation will be reconciled.
- Publication authority: user approved a direct fast-forward of the containing
  commit to `origin/main` after final checks.
- Remaining experiments or follow-ups: optional exact-candidate visual review
  after the two user-owned renderer processes are no longer active.
- Active ownership released: yes after independent review handoff.
- Archived to completed/abandoned path: completed path above.

Candidate artifact:

- Path: `build-color-picker-submenu-polish-v2/bin/uvsr.exe`
- SHA-256: `446971A6B6B248101EB391E6C7366F6C4DADA2063C2AF8C38F5605A47A23DFD3`
- Size: 2,759,680 bytes
- Build time: 2026-08-12 13:01:56 UTC
