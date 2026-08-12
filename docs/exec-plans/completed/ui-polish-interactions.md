# UI Polish Interactions

## Status

- State: complete local candidate
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-polish-interactions` in
  `work/ui-polish-interactions`
- Base commit: `dcecaced5024d29e0dc4363302293e95cbe9b46c`
- Started: 2026-08-11
- Last updated: 2026-08-11
- Archive:
  `docs/exec-plans/completed/ui-polish-interactions.md`

## Goal and Done Condition

Goal: repair the intermittent Settings top-margin shadow and complete the
requested interaction, geometry, range, and depth polish without changing the
renderer feature set.

Done when:

- [x] The Settings top-margin shadow remains present while General and other
      drawers are opened, closed, and animated in any order.
- [x] Performance rows roll in and out, drawer indicators rotate, and both
      animations remain deterministic under rapid toggles.
- [x] Slider travel matches the practical value range without numeric overlap.
- [x] The color selector preserves the full selectable gamut with rounded
      geometry, the opacity control has four rounded corners, and the picker
      uses the requested nested translucent depth layers.
- [x] The Material drawer has no unintended white background.
- [x] The picker uses the same outer margin language as Settings/Performance,
      retains a translucent popup surface with opaque color controls, and uses
      a smooth rounded triangle without losing any selectable color.
- [x] Interface exposes an opt-in beyond-visual-range slider-input override
      within safe logical limits; long material names elide; Representation and
      denoising status copy matches the requested placement and wording.
- [x] Advanced temporal options show algorithm controls directly and fold Cost
      last, and
      Performance omits timing rows whose values are entirely unavailable.
- [x] Focused contracts, the Release build, the complete test suite, and a
      runtime interaction/visual pass succeed for the exact candidate.

## Scope

In scope:

- Settings and Performance panel draw order, clipping, animation state, and
  drawer indicators.
- Performance-table expansion/collapse animation.
- Existing slider bounds and authored slider geometry.
- Material and color-picker geometry, background, translucency, and clipping.
- Focused tests and repository-maintained UI documentation required by the
  implementation.

Non-goals:

- Renderer algorithm, shader, scene, asset, persistence, or skin redesign.
- New controls or compatibility layers.
- Changes under `donut/`.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/ui_animation.h`
- `overrides/imgui-ui.patch`
- `overrides/imgui-slider-controls.patch`
- `overrides/imgui-combo-roll.patch`
- Focused UI tests and UI documentation selected after source inspection.

Shared hotspots reserved for the coordinator:

- `README.md`
- `src/uvsr.cpp`
- ImGui override ordering and generated patch inputs
- Build directory, renderer process, and UI automation

## Baseline

- Canonical repository/remote: user-specified GitHub `main` merge
  `dcecaced5024d29e0dc4363302293e95cbe9b46c`
- Local versus remote state: isolated branch created directly at the specified
  commit; remote freshness is not needed because the user pinned the base.
- Verified source commit/build: canonical verification worktree build at
  `dcecace`, previously recorded as 40/40 tests and responsive at High priority.
- GPU, scene, camera, resolution, and settings preset when relevant: default
  scene and settings; UI verification exercises Settings, Performance,
  General, Material, and authored color-picker states.
- Known pre-existing failures: intermittent top-margin shadow disappearance
  reported by the user; establish a deterministic state/draw-order model.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Exact UI base | `dcecace` | Integrated | Whole task |
| Shadow and indicator audit | Current draw order and animation ownership | Complete | Coordinator implementation |
| Picker and material audit | Current geometry and layer ownership | Complete | Coordinator implementation |
| Slider and Performance audit | Practical range map and row lifecycle | Complete | Coordinator implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- No renderer ABI, shader, resource, persistence, or asset contract changes.
- Full color gamut remains selectable; visual rounding must not clip away
  reachable colors.
- Animation state must reverse continuously when a control is toggled before
  its prior transition finishes.
- ImGui behavior remains supplied through ordered first-party override patches,
  never by editing `donut/`.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `shadow-indicators` | `/root/shadow_indicators` | Read-only task worktree | `dcecace` | None | Exact base | Complete |
| `picker-material` | `/root/picker_material` | Read-only task worktree | `dcecace` | None | Exact base | Complete |
| `sliders-performance` | `/root/sliders_performance` | Read-only task worktree | `dcecace` | None | Exact base | Complete |
| `integrate` | `/root` | `work/ui-polish-interactions` | `dcecace` | Coordinator/shared task paths | Audit findings | Complete |
| `independent-review` | `/root/shadow_indicators` | Read-only frozen candidate | Candidate | None | Integrated diff | Complete |
| `shadow-picker-refinement` | `/root/shadow_indicators` | Shared task worktree | `dcecace` plus current diff | `overrides/imgui-ui-polish.patch` only | User screenshot and current picker override | Complete |
| `settings-copy-refinement` | `/root/picker_material` | Shared task worktree | `dcecace` plus current diff | Command catalog and its focused test only | Current Settings drawers | Complete |
| `temporal-performance-refinement` | `/root/sliders_performance` | Shared task worktree | `dcecace` plus current diff | `tests/ui_source_contract_tests.cpp` only | Current integrated source | Complete |

## Assignment Contracts

### Shadow and Indicator Audit

- Owner/thread: `/root/shadow_indicators`
- Branch/worktree: read-only task worktree
- Base commit/state: clean `dcecace`
- Read scope: first-party UI source, animation helpers, ImGui UI override, and
  focused tests
- Write scope: none
- No-touch scope: all files, Git metadata, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: exact base
- Interface/invariant contract: distinguish window draw-list lifetime, clip
  state, and animation state; no speculative rewrite
- Deliverable: root cause, smallest repair seam, indicator design, and focused
  test recommendations
- Done when: the intermittent shadow behavior and both drawer levels are mapped
  to exact functions and state
- Required verification: read-only source and test inspection
- Allowed Git and external actions: read-only only
- Stop and report if: the required repair crosses a renderer or dependency
  contract
- Handoff revision/artifact: read-only topology and indicator audit against
  `dcecace`
- Handoff acknowledged by/on: `/root`, 2026-08-11

### Picker and Material Audit

- Owner/thread: `/root/picker_material`
- Branch/worktree: read-only task worktree
- Base commit/state: clean `dcecace`
- Read scope: color-picker, Material drawer, authored framing, UI skin, and
  relevant tests
- Write scope: none
- No-touch scope: all files, Git metadata, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: exact base
- Interface/invariant contract: preserve every selectable color and bottom
  button layout while changing only visual geometry/layers
- Deliverable: exact draw sites, proposed rounded full-gamut geometry, layer
  stack, white-background cause, opacity-corner cause, and tests
- Done when: each requested picker/material change maps to a minimal source seam
- Required verification: read-only source and test inspection
- Allowed Git and external actions: read-only only
- Stop and report if: full-gamut selection would require a behavior tradeoff
- Handoff revision/artifact: read-only picker and Material audit against
  `dcecace`
- Handoff acknowledged by/on: `/root`, 2026-08-11

### Sliders and Performance Audit

- Owner/thread: `/root/sliders_performance`
- Branch/worktree: read-only task worktree
- Base commit/state: clean `dcecace`
- Read scope: slider declarations, slider override, Performance panel/table,
  animation helpers, and focused tests
- Write scope: none
- No-touch scope: all files, Git metadata, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: exact base
- Interface/invariant contract: preserve actual accepted values and exact-input
  editing while reducing only excessive mouse-travel ranges
- Deliverable: current range inventory, practical range recommendations,
  Performance row lifecycle, reversible animation seam, and tests
- Done when: every slider and expandable Performance row has an exact mapping
- Required verification: read-only source and test inspection
- Allowed Git and external actions: read-only only
- Stop and report if: a reduced range would make a currently valid necessary
  value inaccessible
- Handoff revision/artifact: read-only range and table-lifecycle audit against
  `dcecace`
- Handoff acknowledged by/on: `/root`, 2026-08-11

## Integration Order

1. Reproduce and model the shadow and current animation/draw lifecycle.
2. Freeze the smallest shared animation and geometry contracts.
3. Implement the shadow and indicator/Performance animation changes.
4. Implement slider range and picker/material visual changes.
5. Update focused contracts and regenerate ordered ImGui patches if needed.
6. Build and run focused then complete automated checks.
7. Run the exact candidate through the drawer, Performance, slider, Material,
   and color-picker interaction matrix.
8. Freeze for independent review, repair findings, reverify, and archive this
   plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Stable top shadow | Repeated open/close order retains shadow | Runtime interaction matrix plus draw-order contract | General collapse/reopen and Representation open/close retained the root shadow; recursive render-order contract passes |
| Reversible animations | Smooth open, close, and mid-flight reversal | Animation tests plus runtime capture | Animation and composed-ImGui tests pass; Performance exchange captured mid-transition and settled |
| Practical slider travel | Complete slider inventory and reachable values | Source contract plus runtime sampling | Exhaustive source contract passes; default-off Override Visual Maxes control confirmed live |
| Full-gamut rounded picker | Corners look rounded and endpoint colors remain reachable | Geometry tests plus runtime picker pass | Exact white, hue, and black pointer tests pass; live triangle is smooth and rounded |
| Rounded opacity control | Four visible rounded corners | Draw-geometry test plus runtime pass | Tight side/corner checker coverage passes and the live bottom-right corner is rounded |
| Material background cleanup | No white fill remains | Source contract plus runtime pass | Direct Material body contract and live Material drawer pass |
| Picker depth layers | Margin and two translucent layers match requested hierarchy | Draw-order contract plus runtime pass | Full-padding geometry contract and live translucent-layer inspection pass |
| No integration regression | Full build and tests pass | Release build, CTest, document checks, diff check | Release build, 40/40 CTest, title-case and line-count checks, diff check, and clean pinned submodules pass |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-11 | Branch directly from `dcecace` and preserve its detached canonical build as baseline. | The user pinned the base; the ordinary local `main` is divergent and has unrelated untracked work. | Whole task |
| 2026-08-11 | Use read-only parallel audits and one coordinator writer. | The visible changes are coupled through `src/uvsr.cpp`, ordered ImGui overrides, and shared animation state. | All assignments |
| 2026-08-11 | Resolve final Settings chrome from the completed visible child topology and merge its draw channels before painting. | The mutable last-called-child pointer can select hidden measurement children and does not model Dear ImGui's recursive render order. | Shadow repair |
| 2026-08-11 | Use an onto radial warp from the stock SV triangle to a finely tessellated rounded triangle and keep popup depth layers on the existing draw list. | Rounded-rectangle corners were visibly oversized and pixelated. The radial triangle mapping retains exact hue, white, and black endpoints while a nested child would escape the existing appearance transform. | Picker polish |
| 2026-08-11 | Submit every Performance effect through a stable keyed toggle region with zero inter-region spacing. | Keeping hidden regions alive from the first frame lets the outgoing table roll up while the incoming table is measured and rolls down without a transient extra gap. | Performance animation |
| 2026-08-11 | Separate logical slider limits from practical pointer-travel limits. | Exact input remains available through the authored value lane, presets keep their established values, and only exaggerated mouse travel is reduced. | Slider ranges |
| 2026-08-11 | Use the larger of FrameRounding and the draw-list fringe as the rounded selector endpoint snap. | A one-pixel target was technically onto but too brittle for human access and popup subpixel movement. The four-pixel authored-radius affordance preserves the mapping while making exact white, black, and full saturation reliably reachable. | Picker interaction |
| 2026-08-11 | Tessellate each opacity checker cell through the shared rounded SDF at one-fringe spacing. | Independent review found that four vertices per cell would interpolate the outer mask across half a bar width. Per-cell grids retain hard checker seams while constraining edge antialiasing to the rounded mask fringe. | Opacity geometry |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-11 | Project preflight `/root` | Complete | This plan | Base, worktrees, branches, active plans, open PRs, visible tasks, and roadmap inspected | No remaining action |
| 2026-08-11 | Shadow and indicator audit `/root/shadow_indicators` | Complete | Read-only handoff against `dcecace` | Window topology, visibility, channels, and both drawer timelines traced | Implement topology resolver and scoped vertex rotation |
| 2026-08-11 | Picker and Material audit `/root/picker_material` | Complete | Read-only handoff against `dcecace` | Picker geometry/layers and duplicate Material surface traced | Implement full-gamut rounded mapping and shared rounded alpha geometry |
| 2026-08-11 | Slider and Performance audit `/root/sliders_performance` | Complete | Read-only handoff against `dcecace` | All slider ranges and 14 timing-table layouts inventoried | Implement soft travel and keyed table exchange |
| 2026-08-11 | Integrated UI candidate `/root` | Automated checks complete | `build/bin/uvsr.exe`, SHA-256 `23D44ABD3FDAA3E0C133FF6E14B5F7DAA947EE68A4B133DD559908A45C37FF28` | Release build plus 40/40 CTest pass | Run the visual interaction matrix after the existing baseline window releases the runtime lease |
| 2026-08-11 | Independent frozen-diff review `/root/shadow_indicators` | Complete | Read-only candidate and repair handoffs | All requested systems reviewed; one high-confidence checker-edge finding repaired, regression-tested, and narrowly re-reviewed | No remaining source/policy findings; runtime visual risk remains |
| 2026-08-11 | User visual feedback `/root` | Active | Screenshot of exact candidate; runtime PID `31716` | Confirmed picker scale/pixelation and shadow still visible over General only | Complete read-only refinement audits, integrate serially, then request/observe a controlled candidate restart |
| 2026-08-11 | Refinement audits `/root/shadow_indicators`, `/root/picker_material`, `/root/sliders_performance` | Complete | Read-only source and screenshot handoffs | Root-frame shadow geometry, safe slider override, temporal hierarchy, unavailable timing rows, picker layers, and rounded-triangle mapping resolved | Complete disjoint implementation and focused tests |
| 2026-08-11 | Source and documentation refinement `/root` | Complete | Current dirty candidate | Drawer copy/hierarchy, timing filtering, root-anchored shadow, visual-max override, material ellipsis, rounded picker, and durable docs integrated | No remaining implementation action |
| 2026-08-11 | Refined source contracts `/root/sliders_performance` | Complete | `tests/ui_source_contract_tests.cpp` | Standalone target built; focused CTest passed 1/1; picker contracts reconciled and the combined suite passed | No remaining action |
| 2026-08-11 | Replacement candidate verification `/root` | Complete | `build/bin/uvsr.exe`, SHA-256 `13DA5CBEEBFAB259A057C5F694292D2ADA60E2F426EBFA221E5AEFDBC639E519` | Release build, 40/40 CTest, 132/132 document Title Case checks, README line counts, diff check, and pinned submodule check pass | Candidate remains intentionally local and live for user review |
| 2026-08-11 | Runtime interaction matrix `/root` | Complete | PID `43904`; `uvsr-general-reopened-aware.png`, `uvsr-representation-open-shadow.png`, `uvsr-rounded-triangle-picker.png`, and Performance transition captures | Shadow retained across drawer changes; picker, opacity edge, Material surface, status text, control default, and table exchange inspected | Product acceptance remains with the user |

## Risks and Escalation Triggers

- Window draw-list or clip-rect state may explain the intermittent shadow and
  could be masked by a visual-only repaint; the repair must fix lifecycle
  ownership rather than merely add duplicate shadows.
- Rounded gamut geometry can accidentally make extreme hue/saturation values
  unreachable; selection math and visual clipping must remain decoupled.
- Ordered ImGui patches can apply cleanly yet compose incorrectly; generated
  output needs byte-level and build verification.
- Runtime input and the UVSR window are serialized under the coordinator.

Stop and ask the user if:

- the only viable picker design would sacrifice selectable colors;
- practical slider bounds require removing a currently necessary value rather
  than changing only travel mapping;
- the requested visuals require a broader skin redesign.

## Completion

- Historical phase record: final acceptance and publication are superseded by
  `docs/exec-plans/completed/ui-picker-followup.md`.
- Final integrated commit: intentionally none unless separately authorized
- Verification summary: the replacement rounded-triangle candidate passed its
  Release build, all 40 CTest cases, focused geometry/source contracts, README
  line counts, 132 document Title Case checks, diff validation, and the live
  interaction matrix.
- Independent review: complete; checker-edge, endpoint-hit-area, layer-rounding,
  Material-label input, exhaustive slider-range, and source-contract findings
  were repaired and rerun.
- Coming Soon and documentation update: complete for the local candidate;
  generated line counts are current.
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual acceptance of the live exact
  candidate; no implementation or automated-verification work remains.
- Active ownership released: all worker and coordinator file ownership released;
  runtime PID `43904` remains live for user review.
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/ui-polish-interactions.md`
