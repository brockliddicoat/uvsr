# UI Picker Follow-Up

## Status

- State: complete; authorized for direct publication to `main`
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-polish-interactions` in
  `work/ui-polish-interactions`
- Base commit: `dcecaced5024d29e0dc4363302293e95cbe9b46c` plus the current local UI
  candidate recorded in `docs/exec-plans/completed/ui-tooltip-picker-polish.md`
- Started: 2026-08-11
- Last updated: 2026-08-12
- Planned archive: `docs/exec-plans/completed/ui-picker-followup.md`

## Goal and Done Condition

Goal: repair the rejected color-picker, tooltip, reset-icon, and Material details
without regressing the established Settings interaction work.

Done when:

- [x] Picker outer inset matches the Settings body inset in size and opacity.
- [x] Color swatches use no black outline and instead receive a stronger shadow.
- [x] Rounded-triangle snap affordances are compact and clear of the hue ring.
- [x] Standard, Advanced Interface, and Material color controls share the same
      authored picker path.
- [x] The popup sits flush with the Settings panel bottom and zooms/fades
      reversibly on open and close.
- [x] Tooltips regain their prior pointer-relative placement and typography
      while retaining only reversible zoom/fade.
- [x] Inactive reset icons leave no visible outline.
- [x] The Material normal-scale reset button is removed, and the name after the
      `Material <id>:` prefix uses the shared filename blue.
- [x] Focused tests, the Release build, the full suite, and live visual review
      pass for the exact replacement executable.

## Scope

In scope:

- Authored ImGui color edit/picker popup geometry, appearance, and swatch chrome.
- Authored tooltip appearance animation with stock placement and typography.
- Reset-icon hidden-state drawing and the two named Material presentation fixes.
- Focused runtime/source contracts and maintained UI documentation.

Non-goals:

- Renderer, shader, scene, persistence, or color-model changes.
- Direct edits under pinned `donut/`.
- Release or deployment beyond the user-authorized GitHub `main` publication.

Affected paths:

- `src/uvsr.cpp`
- `overrides/imgui-tooltip-picker.patch`
- `overrides/donut-app-ui-polish.patch`
- `tests/imgui_dropdown_roll_tests.cpp`
- `tests/ui_source_contract_tests.cpp`
- This execution plan and maintained user-facing UI documentation as required.

## Baseline

- Starting lineage: the then-dirty candidate on `codex/ui-polish-interactions`.
- User-rejected executable: prior `build/bin/uvsr.exe`, visually identified by
  the supplied screenshot.
- Starting publication status: local-only; direct `main` publication was
  authorized after verification on 2026-08-12.
- Known report: picker inset mismatch, black swatch outlines, oversized snap
  points, inconsistent nested/Material pickers, short popup, reset outline
  leakage, broken fixed tooltips, missing popup appearance animation, and two
  Material presentation defects.

## Assignment Summary

| Task ID | Owner | Scope | Write Scope | Status |
| --- | --- | --- | --- | --- |
| `latest-picker-audit` | `/root/picker_material` | Picker geometry and shared routes | None | Complete |
| `material-editor` | `/root/picker_material` | Material callback bridge and normal-scale row | Donut override only | Complete |
| `source-contracts` | `/root/shadow_indicators` | Updated tooltip, picker, reset, and Material contracts | `tests/ui_source_contract_tests.cpp` | Complete |
| `runtime-contracts` | `/root/sliders_performance` | Updated ImGui tooltip and picker runtime coverage | `tests/imgui_dropdown_roll_tests.cpp` | Complete |
| `integration` | `/root` | Shared source, override composition, build, runtime, and review | Coordinator hotspots | Complete |

## Integration Order

1. Freeze geometry and lifecycle contracts from the three read-only audits.
2. Repair shared authored ImGui behavior and first-party Material integration.
3. Update focused runtime and source-contract coverage.
4. Recompose overrides, build, and run focused plus full verification.
5. Restart only the task-owned renderer, inspect the affected surfaces, and
   repair any candidate-specific visual mismatch.
6. Freeze for independent review and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence |
| --- | --- |
| Picker inset/frame parity | Runtime geometry assertions and live screenshot |
| Shadow-only swatches | Draw-list color/outline assertions across shared paths |
| Compact exact snap points | Endpoint geometry/input tests and live picker review |
| Shared nested/Material picker | Two Advanced Interface and representative Material color controls exercised |
| Bottom-flush animated popup | Reversible popup lifecycle test and live open/close review |
| Restored tooltips | Stock-placement/typography parity plus reversible appearance test |
| Hidden reset icons | Source/draw-list contract at zero visibility |
| Material presentation | Source contracts and live Material drawer review |
| No regression | Release build, focused tests, full CTest, patch equivalence, diff checks |

## Risks and Escalation Triggers

- Popup close animation requires retained content without accepting input or
  changing ordinary ImGui popup semantics.
- ColorEdit3 has no alpha component; its shared authored layout must preserve
  valid comparison behavior without inventing editable transparency.
- Changing picker padding can invalidate the source pointer, lane-fit, and
  maximum-bottom calculations together.

Stop and ask the user only if matching the Settings inset and bottom edge cannot
coexist within the available viewport or if a shared Material ColorEdit3 path
would require exposing an artificial alpha control.

## Completion

- Final integration record: this completed plan is included in the single
  direct-main publication commit; Git history is the authoritative SHA record.
- Verification summary: Release `uvsr` build passed; focused ImGui runtime and
  UI source-contract tests passed; full CTest passed 40/40. Live review covered
  Interface, Advanced Interface, and Material pickers before the final four
  edge-case repairs. The exact replacement executable was launched afterward;
  the user's 2026-08-12 request to publish that active candidate to GitHub
  `main` records acceptance and publication authority for the replacement.
- Independent review: complete with no remaining actionable finding after the
  Normal Scale label-width repair.
- Published state: included in the direct GitHub `main` publication authorized
  on 2026-08-12.
- Active ownership released: yes.
- Archived path: `docs/exec-plans/completed/ui-picker-followup.md`.
