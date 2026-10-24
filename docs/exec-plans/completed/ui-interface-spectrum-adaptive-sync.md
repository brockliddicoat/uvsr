# UI Interface Spectrum and Adaptive Sync

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/fast-approximate-aa` at
  `C:/Users/brock/OneDrive/Documents/uvsr/work/fast-approximate-aa`
- Base commit: `578496047c4753077e36f39a68df8c2c323cee58`
- Starting dirty-diff identity: `245e873aaa1851b5afe25b8e4e0022031d9f2fba`
- Started: `2026-08-04`
- Last updated: `2026-08-04`
- Planned archive:
  `docs/exec-plans/completed/ui-interface-spectrum-adaptive-sync.md`

## Goal and Done Condition

Goal: simplify the skin set to Amp and a semibold OG, make OG compact and
opaque-headed, add an ordered eight-slot interface spectrum editor, and expose
an honest Adaptive Sync presentation choice below Graphics Adapter.

Done when:

- [x] Only Amp and the semibold OG remain in UI, commands, docs, and tests.
- [x] OG is the narrowest stable width that preserves every visible Settings
      string and longest factory-visible value.
- [x] OG headers are opaque and retain the requested brighter navy baseline.
- [x] Interface exposes one-to-eight repeating ordered style slots whose major
      header, background, text, control, knob, gradient, and alpha roles are
      independently editable and applied consistently by hierarchy order.
- [x] Adaptive Sync exposes Off, G-Sync, and FreeSync with runtime behavior that
      accurately reflects the supported Windows presentation contract.
- [x] Ambient Occlusion Strength accepts the complete `0..8` range through UI
      and commands while retaining its existing factory default.
- [x] Release build, focused contracts, full CTest, and scoped live visual
      exercise pass. Final spectrum/AO polish was contract-tested without
      interrupting the user's active UVSR window.

## Scope

In scope:

- Removing Amp Blue and the old non-semibold OG behavior.
- Renaming OG Bold to OG while preserving existing OG geometry and semibold font.
- Runtime-only interface theme state and factory reset behavior.
- Ordered/repeating theme assignment for Settings title, top-level drawers, and
  nested drawer content, including major control roles.
- Presentation-path integration and user guidance for Adaptive Sync.
- Ambient Occlusion Strength UI/command range expansion from `0..4` to `0..8`.

Non-goals:

- Persisting settings between launches.
- Driver installation, display control-panel mutation, or claiming that DirectX
  can enable monitor/driver features that the application cannot control.
- Editing pinned Donut source under `donut/`.

Affected subsystems and paths:

- `src/ui_skin.h`, `src/uvsr.cpp`, UI command/catalog headers, tests, and UI docs.
- First-party device/presentation code only if Adaptive Sync needs it.
- Dependency override patches only when an existing UVSR-owned rendering hook is
  the narrowest correct integration point.

Shared hotspots reserved for the coordinator:

- `README.md`, `src/uvsr.cpp`, global UI settings, override patches, tests, and
  this execution plan.

## Baseline

- Canonical repository/remote: local feature work; no publication requested.
- Local versus remote state: branch-local dirty candidate on the recorded base.
- Verified source/build: prior functional skin candidate passed Release build
  and 32/32 CTest; a later tooltip-only relink was blocked by the open task-owned
  `uvsr.exe` window.
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090
  Laptop GPU, Sponza Decorated, Benchmark Position 1, 1920x1080, factory settings.
- Known pre-existing failures: none in the last complete CTest run.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Skin and width map | Exhaustive OG/Amp and Settings sizing paths | Complete | Coordinator |
| Theme role map | Minimal ordered spectrum model and color-space contract | Complete | Coordinator |
| Adaptive Sync map | Supported swap-chain/present behavior and limitations | Complete | Coordinator |

Public interface and state contracts:

- Skin tokens remain `amp` and `og`; existing ordinals stay stable where useful.
- Theme slots are runtime factory-default state and repeat by ordered hierarchy
  index modulo active slot count.
- Color editing is performed in displayed color space and converted through the
  existing UVSR transport path before rendering.
- Adaptive Sync must change actual presentation policy or explain and disable an
  unsupported choice; it must not silently pretend to toggle driver settings.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Skin/width map | `/root/skin_architecture` | Shared/read-only | Recorded dirty state | None | None | Complete |
| Theme model map | `/root/blue_color_math` | Shared/read-only | Recorded dirty state | None | None | Complete |
| Adaptive Sync map | `/root/font_architecture` | Shared/read-only | Recorded dirty state | None | None | Complete |
| Integration | `/root` | Current worktree | Recorded dirty state | Task files | All maps | Complete |

## Assignment Contracts

### Skin and Width Map

- Read scope: current skin, Settings layout, label/value, and command paths.
- Write scope: none.
- No-touch scope: all repository files, Git state, build tree, and running window.
- Deliverable: exhaustive removal/rename map and a content-safe minimal-width
  formula with test locations.
- Done when: no exact old-OG or Amp-Blue behavior path is omitted.
- Stop and report if: width cannot be bounded without a material truncation rule.

### Theme Model Map

- Read scope: UI visual tokens, drawer/control render helpers, color conversion,
  gradient override hooks, and tests.
- Write scope: none.
- No-touch scope: all repository files, Git state, build tree, and running window.
- Deliverable: concrete eight-slot state model, ordering rules, role coverage,
  color/gradient/alpha transport, and minimal renderer integration points.
- Done when: the model covers title, drawer, body, text, frames, knobs, borders,
  and hover/active derivation without per-widget duplication.
- Stop and report if: an override contract must change before implementation.

### Adaptive Sync Map

- Read scope: device creation, swap-chain flags, present interval/tearing policy,
  command/catalog routing, and platform constraints.
- Write scope: none.
- No-touch scope: all repository files, Git state, build tree, and running window.
- Deliverable: exact feasible runtime semantics for Off, G-Sync, and FreeSync,
  required restart/recreation behavior, and tests/tooltips that avoid false claims.
- Done when: application-controlled versus driver-controlled behavior is explicit.
- Stop and report if: implementing distinct modes requires new proprietary SDKs.

## Integration Order

1. Resolve the two-skin and Adaptive Sync contracts.
2. Add the theme state model and UI rendering context.
3. Build the Interface drawer and compact OG sizing.
4. Update commands, tests, documentation, and source contracts.
5. Rebuild, run all checks, and perform exact-artifact visual exercise.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Two skins only | Unit/source contracts and live dropdown | Skin/command tests and screenshot | Passed; Amp and OG only |
| Compact OG | No visible clipping at factory settings and longest values | Stable width algebra tests and live exercise | Passed |
| Opaque OG headers | Pixel alpha/token contracts and screenshot | Source contract plus live exercise | Passed |
| Eight-slot spectrum | Deterministic mapping and transport tests | Focused source/UI contracts | Passed |
| Adaptive Sync | Presentation policy test and runtime guidance | Focused renderer/UI contracts plus NVIDIA smoke | Passed |
| AO Strength `0..8` | Boundary tests and slider maximum contract | Visibility and UI source contracts | Passed |
| Integrated candidate | Complete Release build and CTest | Fresh CMake Release build and CTest | Passed, 32/32 |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-04 | Keep Amp and replace OG with the semibold OG behavior | Direct user instruction; Amp Blue and old OG are rejected | All |
| 2026-08-04 | Keep theme state runtime-only | Existing UVSR settings intentionally reset each launch | Theme model |
| 2026-08-04 | Use one-to-eight ordered slots with modulo repetition | Directly models the requested rotating/repeating spectrum | Theme model |
| 2026-08-04 | Use role-based slots instead of per-widget entries | Covers all major visual roles while keeping the Interface drawer navigable | Theme model |
| 2026-08-04 | Map G-Sync and FreeSync to one vendor-gated DXGI tearing-present policy | DirectX cannot enable driver/display VRR; the shared path is the honest application contract | Adaptive Sync |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-04 | Read-only reviewers | Complete | Skin, theme, and Adaptive Sync maps | Three independent reviews | Findings integrated |
| 2026-08-04 | `/root` | Complete | Fresh Release candidate | Build and 32/32 CTest passed | Archive plan and hand off exact executable |

## Risks and Escalation Triggers

- A literal “every UI element” editor can become unusable; role-based editing
  must cover all major elements while keeping the control surface navigable.
- Dynamic width must include long adapter names and status rows without letting
  transient data expand the panel unpredictably.
- G-Sync and FreeSync are normally driver/display capabilities; the application
  may only control a shared variable-refresh-compatible present policy.
- The earlier task-owned executable remained open during final verification, so
  a fresh build tree was used without interrupting the user's active window.

Stop and ask the user if:

- Distinct G-Sync versus FreeSync runtime behavior would require adding and
  shipping proprietary vendor SDKs rather than using the shared DXGI contract.
- A requested “every element” interpretation requires per-widget overrides that
  conflict with the ordered role-based system described above.

## Completion

- Final integrated commit: local dirty candidate unless the user later requests a commit.
- Verification summary: fresh Release build succeeded; focused UI/renderer
  contracts passed; complete CTest passed 32/32; documentation title and line
  count checks passed.
- Independent review: three read-only reviews completed; title-gradient,
  color-preview transport, stable-width, and presentation-wording findings were
  corrected and reverified.
- Coming Soon/documentation update: durable UI docs and README only; no roadmap
  entry unless the work remains active beyond this task.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: optional user visual acceptance of the
  final exact build; no implementation work remains.
- Active ownership released: yes.
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ui-interface-spectrum-adaptive-sync.md`.
