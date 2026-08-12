# UI Tooltip and Picker Polish

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-polish-interactions` in
  `work/ui-polish-interactions`
- Base commit: `dcecaced5024d29e0dc4363302293e95cbe9b46c` plus the complete local
  candidate recorded in
  `docs/exec-plans/completed/ui-polish-interactions.md`
- Started: 2026-08-11
- Last updated: 2026-08-11
- Planned archive:
  `docs/exec-plans/completed/ui-tooltip-picker-polish.md`

## Goal and Done Condition

Goal: refine tooltip interaction, Material gating, and the authored color picker
without regressing the completed Settings, slider, drawer, or picker work.

Done when:

- [x] Tooltips appear at one predictable non-pointer location, permit scrollbar
      interaction, and zoom/fade continuously on open and close.
- [x] Material names use a deterministic 25-character front-end cutoff followed
      by `...`.
- [x] Disabling Use Normal Texture animates every dependent Material control in
      and out, with a source audit covering other conditional Material controls.
- [x] The rounded triangle has reliable exact-value corner snap zones.
- [x] Current and Original become unlabeled vertical bars beside hue and alpha;
      all four bars match wheel thickness, use hollow-circle indicators where
      interactive, and the bottom actions span their full width.
- [x] The picker frame matches Settings frame thickness/opacity and a rounded
      left pointer identifies the edited source swatch.
- [x] Focused tests, the Release build, the full suite, and a live interaction
      pass succeed for the exact replacement candidate.

## Scope

In scope:

- Authored tooltip placement, scroll/input behavior, and appearance timeline.
- Material name presentation and conditional normal-texture control layout.
- Authored color-picker layout, geometry, selection markers, comparison bars,
  popup frame, source pointer, and bottom action row.
- Focused contracts and maintained UI documentation/counts.

Non-goals:

- Renderer, shader, scene, asset, persistence, or color-model changes.
- Dependency edits under `donut/`; required behavior remains in first-party
  source or ordered overrides.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `overrides/donut-app-ui-polish.patch`
- `overrides/imgui-tooltip-picker.patch`
- Focused UI tests, CMake override composition, and maintained documentation.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`
- `overrides/imgui-ui-polish.patch`
- `tests/imgui_dropdown_roll_tests.cpp`
- `tests/ui_source_contract_tests.cpp`
- Build directory, renderer process, and Windows UI automation.

## Baseline

- Canonical repository/remote: user-pinned `main` merge `dcecace`.
- Local versus remote state: this is a continuation of the intentionally local,
  uncommitted UI candidate; remote freshness is not required.
- Verified source commit/build: prior candidate `build/bin/uvsr.exe`, SHA-256
  `13DA5CBEEBFAB259A057C5F694292D2ADA60E2F426EBFA221E5AEFDBC639E519`,
  passed 40/40 tests and the recorded runtime matrix.
- GPU, scene, camera, resolution, and settings preset when relevant: current
  default runtime state; UI checks exercise long tooltips, Material, Interface,
  and primary/accent color editing.
- Known pre-existing failures: the user reports pointer-following tooltips,
  incorrect Material-name truncation, an unanimated normal-texture gate, and
  picker layout/geometry mismatches in the prior candidate.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior UI candidate | Completed local diff and exact executable identity | Integrated | Whole task |
| Tooltip audit | Placement, ownership, scrolling, and transition seam | Complete | Coordinator |
| Material audit | Donut control topology and first-party animation seam | Complete | Coordinator |
| Picker audit | Four-bar layout and exact-value corner geometry | Complete | Coordinator |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- No renderer ABI, shader, resource, persistence, or asset contract changes.
- Tooltip placement must stay within the active viewport and cannot intercept
  unrelated pointer input while hidden.
- Rounded triangle snap zones must preserve exact white, black, and hue extrema.
- Current and Original comparison bars are displays, not new color inputs.
- All ImGui dependency behavior remains supplied through ordered first-party
  overrides.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `tooltip-audit` | `/root/shadow_indicators` | Shared read-only | Current dirty candidate | None | Prior candidate | Complete |
| `material-audit` | `/root/picker_material` | Shared read-only | Current dirty candidate | None | Prior candidate | Complete |
| `picker-audit` | `/root/sliders_performance` | Shared read-only | Current dirty candidate | None | Prior candidate | Complete |
| `integration` | `/root` | Current worktree | Current dirty candidate | Coordinator hotspots | All audits | Complete |

## Assignment Contracts

### Tooltip Audit

- Owner/thread: `/root/shadow_indicators`
- Branch/worktree: shared task worktree, read-only.
- Base commit/state: current dirty candidate atop `dcecace`.
- Read scope: tooltip overrides, popup appearance helpers, UI tests, and ImGui
  tooltip/window lifecycle.
- Write scope: none.
- No-touch scope: all files, Git metadata, build tree, and processes.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: prior complete UI candidate.
- Interface/invariant contract: recommend one stable viewport-safe placement,
  real scrollbar interaction, and reversible zoom/fade without changing ordinary
  popups.
- Deliverable: root cause, exact implementation seam, geometry/timeline choice,
  and focused tests.
- Done when: pointer following and scroll-input ownership are traced end to end.
- Required verification: read-only source and dependency inspection.
- Allowed Git and external actions: none.
- Stop and report if: the only solution changes stock non-tooltip popup behavior.
- Handoff revision/artifact: final tooltip lifecycle and focus-isolation review.
- Handoff acknowledged by/on: `/root`, 2026-08-11.

### Material Audit

- Owner/thread: `/root/picker_material`
- Branch/worktree: shared task worktree, read-only.
- Base commit/state: current dirty candidate atop `dcecace`.
- Read scope: Material drawer, staged ImGui source, Donut MaterialEditor reference,
  animation helpers, and focused contracts.
- Write scope: none.
- No-touch scope: all files, Git metadata, build tree, and processes.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: prior complete UI candidate.
- Interface/invariant contract: 25 visible source characters plus `...`; every
  conditional Material row must use the established reversible animation system.
- Deliverable: exact normal-texture gate cause, complete conditional-control
  inventory, smallest first-party override seam, and tests.
- Done when: the missing animation and all equivalent gates are accounted for.
- Required verification: read-only source and dependency inspection.
- Allowed Git and external actions: none.
- Stop and report if: required behavior cannot be authored without changing
  pinned Donut or expanding the public MaterialEditor contract.
- Handoff revision/artifact: final Material region, truncation, and picker review.
- Handoff acknowledged by/on: `/root`, 2026-08-11.

### Picker Audit

- Owner/thread: `/root/sliders_performance`
- Branch/worktree: shared task worktree, read-only.
- Base commit/state: current dirty candidate atop `dcecace`.
- Read scope: authored picker patch, composed ImGui source, layout tests, and
  source swatch placement state.
- Write scope: none.
- No-touch scope: all files, Git metadata, build tree, and processes.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: prior complete UI candidate.
- Interface/invariant contract: four same-thickness vertical bars, unlabeled
  comparison displays, hollow-circle active markers, full-width actions, opaque
  Settings-weight frame, left rounded source pointer, and exact endpoint snaps.
- Deliverable: feasible geometry, source-arrow anchor strategy, interaction
  mapping, smallest patch seam, and focused tests.
- Done when: every requested picker element has exact bounds and ownership.
- Required verification: read-only source and composed-source inspection.
- Allowed Git and external actions: none.
- Stop and report if: preserving full-gamut selection conflicts with the layout.
- Handoff revision/artifact: final slider, Performance, shadow, and picker review.
- Handoff acknowledged by/on: `/root`, 2026-08-11.

## Integration Order

1. Freeze tooltip placement/timeline, Material conditional-row, and picker
   geometry contracts from the three audits.
2. Implement tooltip and Material behavior in the ordered authored UI layer.
3. Implement picker geometry and interaction changes against the same composed
   ImGui source.
4. Update focused contracts, build, and run the complete automated suite.
5. Restart only the task-owned renderer and exercise hover, scrolling, Material,
   picker snap, comparison bars, and appearance transitions.
6. Freeze for independent review and archive this plan after repair/reverification.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Stable scrollable tooltips | Fixed position, wheel/drag scroll, reversible appearance | Focused lifecycle tests and live long-tooltip interaction | Fixed top-right placement, wheel and scrollbar drag, popup/focus isolation, drag/drop replacement, and reversible zoom/fade pass |
| Deterministic Material label | 25 source characters plus `...` | Source contract and live long-name drawer | UTF-8 24/25/26 boundaries and all nine blue basename routes pass; live drawer shows front-cut ellipsis |
| Animated normal gate | Both hidden controls roll/fade; equivalent gates audited | Source/animation contracts and live toggle | Seven balanced per-material conditional regions pass contracts and runtime lifecycle coverage |
| Exact triangle endpoints | Forgiving snap zones select exact extrema | Geometry interaction tests and live clicks | Exact hue, white, and black endpoint snaps pass |
| Four-bar picker layout | Equal thickness, hollow markers, full-width footer | Draw-geometry tests and live capture | Four equal bars, hollow markers, display-only comparisons, and full-width controls pass |
| Picker framing and pointer | Settings-weight frame and source-linked left arrow | Geometry contract and live accent picker | Carved frame/pointer parity and source-swatch targeting pass |
| No integration regression | Complete build and suite pass | Release build, CTest, document and diff checks | Release build, 40/40 CTest, patch byte-equivalence, line-count and Title Case checks, and diff check pass |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-11 | Continue in the existing task worktree but start a fresh plan linked to the archived prior pass. | The new request refines the exact uncommitted candidate; copying or rebasing its broad dirty diff would weaken provenance. The lifecycle requires a fresh plan for resumed work. | Whole task |
| 2026-08-11 | Use read-only parallel audits and one coordinator writer. | Tooltip, Material, and picker investigation can proceed independently, while production and override files remain coupled hotspots. | All assignments |
| 2026-08-11 | Retain authored tooltips at the viewport work area's top-right with a transit grace period. | The fixed slot avoids pointer chasing and permits real scrollbar input; scoped focus and drag/drop exclusions keep it from behaving like an ordinary popup. | Tooltip polish |
| 2026-08-11 | Bridge seven mutable Material groups through first-party callbacks. | The selected material owns each reversible region ID; pinned Donut remains untouched and unsafe resource-presence branches still snap. | Material animation |
| 2026-08-11 | Use four equal ring-width vertical bars and display-only Current/Original comparisons. | This preserves the rounded full-gamut triangle while giving hue, alpha, current, and original one consistent visual grammar. | Picker polish |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-11 | Coordinator preflight `/root` | Complete | This plan | Branch, worktrees, dirty state, active plans, roadmap, prior candidate, and collaboration policy inspected | Complete read-only audits before shared-file edits |
| 2026-08-11 | Tooltip audit and repair `/root/shadow_indicators`, `/root` | Complete | `overrides/imgui-tooltip-picker.patch` and focused lifecycle tests | Fixed placement, scrolling, popup preservation, drag/drop suppression, empty-click and automatic-focus fallback pass | No remaining tooltip finding |
| 2026-08-11 | Material audit and repair `/root/picker_material`, `/root` | Complete | `overrides/donut-app-ui-polish.patch`, `src/uvsr.cpp` | Seven regions, per-material isolation, and nine filename routes pass | No remaining Material finding |
| 2026-08-11 | Picker audit and repair `/root/sliders_performance`, `/root` | Complete | Four-bar picker and exact endpoint geometry | Focused draw/input tests pass; live capture inspected | No remaining picker finding |
| 2026-08-11 | Final integrated candidate `/root` | Complete | `build/bin/uvsr.exe` | Release build and 40/40 CTest pass; ordered patches match staged sources byte-for-byte | Intentionally local pending user review |

## Risks and Escalation Triggers

- A tooltip that no longer follows the pointer can still be unusable if it
  overlaps its hovered source or loses wheel/drag input on the first frame.
- MaterialEditor belongs to pinned Donut; animation must be injected through a
  narrow first-party override rather than dependency edits.
- Picker layout changes can make endpoint coordinates, popup auto-fit, or source
  pointer anchoring stale even when isolated geometry tests pass.

Stop and ask the user if:

- the only viable fixed tooltip location necessarily hides the hovered control;
- Current or Original must become editable rather than display-only;
- preserving exact color extrema requires a visibly different picker topology.

## Completion

- Historical phase record: final acceptance and publication are superseded by
  `docs/exec-plans/completed/ui-picker-followup.md`.
- Final integrated commit: intentionally none unless separately authorized.
- Verification summary: Release build, all 40 CTest cases, focused tooltip and
  picker lifecycle tests, source contracts, ordered-patch byte equivalence,
  README line counts, document Title Case, diff validation, and runtime smoke
  passed.
- Independent review: complete; final focus-fallback and directional-slider
  bypass findings were repaired and reverified.
- Coming Soon and documentation update: complete for this local candidate.
- Pushed, pull request, merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: user visual acceptance of the final
  candidate.
- Active ownership released: all worker file ownership released.
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/ui-tooltip-picker-polish.md`.
