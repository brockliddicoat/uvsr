# UI CLI Margin and Complete Settings Coverage

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/ui-skins-command-reset` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\ui-skins-command-reset`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` plus the
  uncommitted UI-skins/slash/V-reset candidate recorded in
  `docs/exec-plans/completed/ui-skins-command-reset.md`
- Started: 2026-07-30
- Last updated: 2026-07-30 22:05 -05:00
- Governing agent policy: `AGENTS.md` version `2026-07-22.1`
- Governing UI reference: `docs/ui-integration-agent-procedure.md` version
  `2026-07-30.7`
- Planned archive:
  `docs/exec-plans/completed/ui-cli-margin-coverage.md`

## Goal and Done Condition

Goal:

Revise the local UI-skins and slash-command candidate so it keeps only the
Amp and OG profiles, places the command interface at the bottom using one
consistent margin shared with Settings, keeps it full-width in every Settings
state, caps Settings above its permanently reserved lane, exposes every Settings
operation to agents through the command interface, and records the fast
experiment workflow in agent-facing guidance.

Done when:

- [x] The skin model contains only Amp and OG; the three rejected profiles and
      their styling, aliases, tests, and documentation are removed end to end.
- [x] The command interface always spans the full margin-to-margin work width,
      with the same bottom margin in expanded, collapsed, hidden, opening,
      closing, resize, loading, and skin-transition states.
- [x] Settings never extend below one consistent margin above the command
      interface's permanently reserved fixed-height lane, so command output
      growth cannot create overlap.
- [x] The Settings title matches the resting drawer-header blue, corner radius,
      and outline path in Amp and OG without changing with focus or collapse.
- [x] Every user-facing Settings value and action is discoverable, readable
      where meaningful, and invocable through validated command paths or
      actions with the same side effects and safety locks as the UI.
- [x] Agent guidance requires fast shader permutations, OG, and slash commands
      for experiments whenever that is faster than animated menu interaction.
- [x] Focused/full tests, documentation checks, diff hygiene, and exact-build
      runtime geometry/command smoke checks pass.
- [x] The candidate remains local, uncommitted, and unpublished unless the user
      later authorizes a Git or publication action.

## Scope

In scope:

- Rename Current UVSR to Amp and Original ImGui to OG.
- Delete ChatGPT + Codex, Unreal Engine 5, and UVSR Signal implementation.
- Define `m_SettingsPanelMarginPixels` as the consistent margin shared by
  Settings, the command interface, and the vertical gap between them.
- Reserve a fixed-height, full-width bottom command lane from the viewport
  work rectangle and cap both the Settings root and scrolling body above it.
- Match the Settings title to the active skin's resting drawer-header color,
  corner radius, and outline path.
- Complete command discovery and dispatch for every Settings control and footer
  action, including dynamic scene/light/material choices where exposed.
- Exact validation, side effects, history/reset behavior, controlled-experiment
  locks, completion, help, tests, and agent/user documentation.

Non-goals:

- Persisting skins, command history, or renderer settings between launches.
- Adding another visual profile or replacing the two-profile requirement.
- Turning the slash interface into a network service or external shell.
- Changing renderer algorithms, camera behavior, shader equations, or
  performance characteristics beyond choosing the existing fast experiment
  shader bundle.
- Committing, pushing, opening a pull request, merging, or publishing.

Affected subsystems and paths:

- `src/ui_skin.h`
- `src/ui_commands.h`
- `src/uvsr.cpp`
- `overrides/imgui-runtime-policy.patch`
- `CMakeLists.txt`
- Focused skin, command, UI layout, and source-contract tests under `tests/`
- `AGENTS.md`
- `README.md`
- `docs/advanced-settings.md`
- `docs/ui-integration-agent-procedure.md`
- This execution plan

Shared hotspots reserved for the coordinator:

- All affected implementation and documentation paths
- Command schema and side-effect contracts
- Builds, tests, renderer/GPU control, screenshots, Git state, integration, and
  the completion claim

## Baseline

- Canonical repository/remote: task worktree remains on
  `codex/ui-skins-command-reset` at canonical base
  `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`.
- Local versus remote state: the branch is local-only and contains the
  uncommitted files listed by `git status --short`; those changes are the
  accepted starting candidate, not unrelated work.
- Verified source commit/build: the prior candidate passed a Release build,
  focused 8-of-8 tests, full 30-of-30 tests, and exact-build runtime QA before
  this revision. All affected evidence becomes stale after this work begins.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU, PBR Sponza Decorated, 1920x1080 renderer output,
  Current/Amp factory settings; no performance benchmark is planned.
- Known pre-existing failures: none in the prior candidate.
- Overlapping work: `codex/flashlight-ambient-fill` also edits `src/uvsr.cpp`
  and Settings/Sky UI in a separate worktree. This task remains isolated and
  will send its finalized input/layout contract before any later integration.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior UI-skins candidate | Treat the uncommitted tested candidate as the starting state and remove only user-rejected profiles | Decided | All |
| Settings inventory | Enumerate every visible control, value domain, side effect, dynamic owner, and reset/action contract | Complete | Command coverage |
| Command geometry | Reserve one Settings-independent full-width lane and cap Settings one margin above it | Decided | CLI layout |
| Fast shader workflow | Identify the exact existing CMake/launch contract before documenting it | Complete | Agent guidance |
| Flashlight/Ambient Fill task | F remains captured while the command bar is open; Sky and selected-flashlight additions need command-path coverage during later integration | Coordinated; camera-offset contract recorded below | Cross-task integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `UiSkin` has exactly two values: Amp and OG. Amp is the launch default. OG
  alone uses stock ImGui widgets and disables presentation motion.
- Command skin values and visible labels are exactly `amp`/`Amp` and `og`/`OG`.
- The consistent margin is the existing Settings edge margin in logical ImGui
  pixels. The command interface uses it on its left, right, and bottom edges;
  Settings use it above the permanently reserved command lane.
- The command rectangle always spans the margin-to-margin work width and has a
  fixed outer height. Settings visibility, collapse, presentation scale, and
  command output do not change that rectangle.
- The lane is renderable only when it preserves ImGui's active minimum command
  width and the worst-case collapsed Settings height. Otherwise the command
  surface is withheld rather than allowing either window to escape its bounds.
- Amp command presentation scales only vertically about its bottom edge. OG
  remains immediate. Both preserve the exact horizontal margins.
- The Settings title uses the active skin's resting drawer-header color
  composited over the effective Settings body surface, and shares the header's
  `FrameRounding` plus skin-specific `RenderFrame` outline policy. Amp uses one
  custom gradient outline; OG uses the stock square frame without that authored
  gradient. Title backdrop masks match the exact half-pixel-inset rendered
  surface in expanded and collapsed states.
- Every Settings control maps to one stable lowercase command path or action.
  Command dispatch reuses the UI's validation, side effects, deferred mutation
  barrier, and experiment locks rather than creating a second renderer policy.
- Dynamic selections use discoverable stable identifiers plus human-readable
  aliases. Invalid paths, types, values, indices, or unavailable dynamic owners
  cause no mutation.
- The fast experiment workflow uses the repository's existing reduced shader
  permutation contract, then selects OG and configures the run through `/`.

### Deferred Flashlight Camera Offset Contract

- The later flashlight merge adds the user-facing **Camera Offset** float,
  stored in `FlashlightSettings::cameraLateralOffsetMeters`, with domain
  `0.0..0.40` meters and default `0.20` meters.
- The value scales the flashlight's fixed 2:1 right/down mount vector. Forward
  offset remains `0.04` meters and convergence is `6` meters. Zero is coaxial.
- The Settings reset for this row restores `0.20`; renderer Reset already
  restores the complete flashlight settings structure.
- Add exactly one flashlight-only get/set/reset command using the catalog's
  established selected-light naming convention. The proposed path is
  `light.selected.flashlight.camera-offset`.
- Command availability requires the selected public light to be exactly
  `flashlight_1`. Preserve configuration while PBR is off.
- After semantic integration, the complete operation target is `236`: Lights
  gains one operation and the Flashlight/Ambient Fill merge contributes 14
  additions in total.
- This is a deferred semantic-integration contract only. Do not implement it in
  the isolated UI/CLI branch before the flashlight handoff is integrated.

### Deferred Flashlight Editor Order Contract

- The selected public flashlight editor order is **Enabled (F)**, **Cast
  Shadows**, then **Realistic Flashlight**.
- The complete animated realistic-only region follows **Realistic Flashlight**
  immediately: **Hotspot Size**, **Hotspot Strength**, **Sway**, then **Aim
  Correction**. The region ends before **Brightness**, followed by the remaining
  established flashlight controls.
- Preserve the existing PBR-disabled availability boundary: only **Enabled
  (F)** is inside the disabled scope. **Cast Shadows** and **Realistic
  Flashlight** retain editable configuration while PBR is off.
- This ordering changes no command operation. The merged catalog target remains
  `236`.
- The upstream flashlight UI reference for this contract is
  `2026-07-30.3`, including source-contract ordering assertions.

### Deferred Shadows Drawer Contract

- The later flashlight merge adds a separate top-level **Shadows** sibling
  drawer immediately after **Lights**. Its body identifier is `##ShadowsBody`;
  the drawer remains collapsed at launch.
- Move **Bend Screen-Space Shadows**, **Sparse Virtual Shadow Maps**, and
  **Diagnostic Cascaded Shadow Maps** from Lights into Shadows. Their hidden
  identifiers end in `##Shadows`, each nested section remains default-open, and
  every **Enabled** default remains off.
- Keep selected `flashlight_1` **Cast Shadows** under Lights. Do not move it into
  the Shadows drawer.
- Preserve the factory disabled barrier, primary-directional-light availability
  gate, and deferred mutation behavior for all three shadow producers.
- Footer-anchored layout and command-catalog integrations occur after the
  Shadows drawer. Moving the controls adds no command operation; the merged
  operation target remains `236`.
- Latest upstream handoff evidence reports fresh isolated production and
  factory builds with 516/51 shader tasks and both 29/29 suites passing. Its
  exact production executable SHA-256 is
  `4A96BB5EFC73AEC4CFA586228C2F345B328EACB0ECFA77748D1E4D4BEE2EDB89`.
- This is a deferred semantic-integration contract only. The current isolated
  UI/CLI branch retains its pre-flashlight hierarchy until the flashlight
  handoff is integrated.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `settings-inventory` | Read-only explorer, then command-catalog writer | Shared task worktree | Current dirty candidate | `src/ui_settings_command_catalog.h`, `tests/ui_settings_command_catalog_tests.cpp` | Frozen 222-operation inventory | Complete |
| `margin-layout` | Read-only explorer, then layout-file writer | Shared task worktree | Current dirty candidate | `src/ui_command_layout.h`, `tests/ui_command_layout_tests.cpp` | Frozen geometry contract | Complete |
| `experiment-workflow` | Read-only explorer, skin-model writer, then workflow-doc writer | Shared task worktree | Current dirty candidate | `AGENTS.md`, `docs/advanced-settings.md`, `docs/ui-integration-agent-procedure.md` | Integrated two-skin model and frozen fast workflow | Complete |
| `integration` | `/root` | Shared task worktree | Explorer handoffs | Coordinator hotspots | All contracts | Complete |
| `runtime-command-integration` | Command-catalog worker after catalog handoff | Shared task worktree | Integrated helpers/catalog | `src/uvsr.cpp` | Frozen Amp/OG, layout, and 222-operation catalog | Complete |
| `independent-review` | Read-only reviewers after freeze | Shared task worktree | Integrated candidate | None | Integrated diff | Complete |

## Assignment Contracts

### Settings Inventory: Enumerate Complete Command Coverage

- Owner/thread: read-only explorer
- Branch/worktree: shared task worktree
- Base commit/state: current dirty candidate plus this plan
- Read scope: `src/uvsr.cpp`, first-party settings headers, command model/tests,
  and relevant documentation
- Write scope: none
- No-touch scope: repository files, build tree, runtime, Git state, and external
  systems
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: current Settings UI and slash parser
- Interface/invariant contract: inventory every user-facing setting and action;
  record type/domain, source owner, reset/default, side effects, lock rules, and
  dynamic discovery needs.
- Deliverable: coverage table plus implementation risks and a proposed stable
  path taxonomy.
- Done when: the coordinator can prove every Settings operation has one command
  contract and can identify deliberate read-only/action-only exceptions.
- Required verification: source trace and comparison against all interactive
  calls in `buildUI`.
- Allowed Git and external actions: read-only only
- Stop and report if: a setting cannot be identified or requires a materially
  new renderer API.
- Handoff revision/artifact: agent message
- Handoff acknowledged by/on: pending

### Margin Layout: Derive Non-Overlapping Geometry

- Owner/thread: read-only explorer
- Branch/worktree: shared task worktree
- Base commit/state: current dirty candidate plus this plan
- Read scope: Settings composition/appearance, command-interface layout,
  viewport/DPI metrics, and UI geometry tests
- Write scope: none
- No-touch scope: repository files, build tree, runtime, Git state, and external
  systems
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: consistent-margin contract in this plan
- Interface/invariant contract: reserve one fixed full-width bottom lane from
  viewport geometry, maintain exact left/right/bottom margins, and cap Settings
  one margin above it in every visibility and animation state.
- Deliverable: precise lane/cap formulas, transition cases, minimum-size
  behavior, and focused test assertions.
- Done when: layout behavior is deterministic across hidden, collapsed,
  expanding, expanded, closing, resize, and OG immediate states.
- Required verification: source trace and mathematical edge-case review.
- Allowed Git and external actions: read-only only
- Stop and report if: the viewport work rectangle or active-font metrics are
  unavailable before either surface is composed.
- Handoff revision/artifact: agent message
- Handoff acknowledged by/on: pending

### Experiment Workflow: Identify Fast Agent Launch Contract

- Owner/thread: read-only explorer
- Branch/worktree: shared task worktree
- Base commit/state: current dirty candidate plus this plan
- Read scope: `AGENTS.md`, CMake options, launch/build tools, experiment docs,
  command documentation, and source-contract tests
- Write scope: none
- No-touch scope: repository files, build tree, runtime, Git state, and external
  systems
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: current fast shader permutation support
- Interface/invariant contract: cite the exact configure/build/launch sequence;
  do not invent a new mode or weaken benchmark hygiene.
- Deliverable: exact agent-facing guidance, enforcement opportunities, and
  documentation/test destinations.
- Done when: an agent can reliably choose fast permutations, launch the right
  artifact, switch to OG, and configure the experiment through commands.
- Required verification: source/doc trace.
- Allowed Git and external actions: read-only only
- Stop and report if: fast permutations are unsafe or incompatible with the
  requested experiment workflow.
- Handoff revision/artifact: agent message
- Handoff acknowledged by/on: pending

## Integration Order

1. Complete read-only settings, geometry, and experiment-workflow inventories.
2. Freeze the two-skin model, command taxonomy, complete-coverage boundary, and
   geometry formulas.
3. Remove rejected profiles and update skin/parser tests.
4. Implement bottom consistent-margin geometry and non-overlap tests.
5. Implement complete Settings command coverage with shared side effects,
   validation, completion, help, and safety tests.
6. Update agent/user/UI contracts and README once implementation is stable.
7. Freeze writes, run independent review, build/test, perform exact-build
   runtime QA, reconcile the plan, and report the local candidate.

## Follow-Up Writer Contracts

### Margin Layout: Implement Pure Geometry Contract

- Owner/thread: `consistent_margin_layout`
- Branch/worktree: shared task worktree; coordinator retains Git/build/runtime
  control
- Base commit/state: current dirty candidate after the read-only geometry trace
- Write scope: `src/ui_command_layout.h`,
  `tests/ui_command_layout_tests.cpp`
- No-touch scope: every other file, Git state, build trees, runtime, and external
  systems
- Interface/invariant contract: one pure ImGui-independent resolver using the
  work rectangle, consistent margin, minimum width, reserved height, and minimum
  command height; exact full-width side insets, bottom inset, Settings maximum
  bottom, and explicit compact/fits result
- Done when: focused tests cover state independence, translation, DPI, compact
  width, vertical-only Amp presentation, and impossible geometry
- Required checks: compile-independent review only; coordinator wires CMake and
  runs tests
- Handoff: changed files, public types/functions, checks, risks; release paths

### Skin Model: Reduce to Amp and OG

- Owner/thread: `fast_experiment_workflow`
- Branch/worktree: shared task worktree; coordinator retains Git/build/runtime
  control
- Base commit/state: current dirty candidate after the workflow trace
- Write scope: `src/ui_skin.h`, `tests/ui_skin_tests.cpp`
- No-touch scope: every other file, Git state, build trees, runtime, and external
  systems
- Interface/invariant contract: exactly `Amp`, `Og`, and `Count`; Amp is the
  default animated authored skin; OG is stock ImGui with no UI motion; only the
  exact normalized values and labels `amp`/`Amp` and `og`/`OG` parse
- Done when: the three rejected profiles and every old alias disappear from
  both files and the tests prove the exact two-value contract
- Required checks: compile-independent review only; coordinator runs tests
- Handoff: changed files, public contract, checks, risks; release paths

### Settings Inventory: Implement the Complete Command Catalog

- Owner/thread: `settings_cli_inventory`
- Branch/worktree: shared task worktree; coordinator retains Git/build/runtime
  control
- Base commit/state: current dirty candidate after the complete source inventory
- Write scope: `src/ui_settings_command_catalog.h`,
  `tests/ui_settings_command_catalog_tests.cpp`
- No-touch scope: every other file, Git state, build trees, runtime, and external
  systems
- Interface/invariant contract: encode every inventoried production Settings
  value/action exactly once with stable lowercase path/action, value kind,
  supported verbs, factory-build mutation policy, dynamic/static classification,
  and concise domain/side-effect summary; distinguish navigation/telemetry
  exemptions from operations
- Done when: tests prove uniqueness, lowercase syntax, exactly 222 production
  operations, boolean-toggle/reset/read contracts, action contracts, factory
  fail-closed metadata, dynamic selection metadata, required section counts, and
  the eight proven factory-safe presentation, camera, and statistics operations
- Required checks: compile-independent review only; coordinator wires CMake,
  dispatch, and runs tests
- Handoff: changed files, exact counts/public contract, checks, risks; release
  paths

### Experiment Workflow: Update Agent and User Guidance

- Owner/thread: `fast_experiment_workflow`
- Branch/worktree: shared task worktree; coordinator retains Git/build/runtime
  control
- Base commit/state: current dirty candidate with the integrated Amp/OG header
- Write scope: `AGENTS.md`, `docs/advanced-settings.md`,
  `docs/ui-integration-agent-procedure.md`
- No-touch scope: every other file, Git state, build trees, runtime, and external
  systems
- Interface/invariant contract: document the exact isolated fast-build
  configure/build/launch sequence, immediate `/skin og`, `/list`, slash-first
  experiment setup, factory-topology limitations/fail-closed mutations, full
  final verification requirement, and mandatory path/action intake for every
  future Settings control; update all existing skin/CLI wording to Amp/OG
- Done when: the three complete documents use Title Case headings and contain no
  rejected skin names or legacy Current/Original profile wording
- Required checks: full-document heading/wording scan only; coordinator runs
  repository validators
- Handoff: changed sections, checks, risks; release paths

### Runtime Command Integration: Wire the Complete Catalog

- Owner/thread: `settings_cli_inventory` after releasing its catalog paths
- Branch/worktree: shared task worktree; coordinator retains Git/build/runtime
  control
- Base commit/state: integrated `ui_skin.h`, `ui_command_layout.h`, and
  `ui_settings_command_catalog.h`
- Write scope: `src/uvsr.cpp`
- No-touch scope: every other file, Git state, build trees, runtime, and external
  systems
- Interface/invariant contract:
  - remove all three rejected skin implementations, legacy names/aliases, and
    regular-font branch; retain Amp pixel identity and OG stock/no-motion
  - capture actual transformed Settings bounds each frame, resolve the bottom
    command layout from the shared margin helper, constrain long output, and
    honor `fits=false` without drawing an overlapping window
  - derive `/list`, completion, supported-verb checks, domains, and factory
    policy from the 222-operation catalog
  - implement every catalog value/action against its real runtime owner with
    validated no-op failure, dynamic discovery, resets where declared, and UI
    side effects/availability; keep command execution at the post-ImGui barrier
  - factory experiment builds permit only catalog `UiSafe` mutations and give a
    clear topology-lock error for every other mutation/action while reads/list
    remain available
- Done when: every catalog entry reaches a concrete get/set/reset/toggle/run
  dispatch path or a dynamic type-specific dispatcher, no old 17-entry list or
  raw boolean-pointer bypass remains, and slash help reports Amp/OG plus the
  catalog/discovery workflow
- Required checks: scoped source audit only; coordinator builds, tests, and
  performs runtime QA
- Handoff: source contracts changed, coverage accounting, unimplemented entries
  if any, risks; release path

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Two-skin cleanup | No rejected enum, label, token, alias, branch, test, or documentation references | Focused searches and skin/parser tests | Passed: focused UI suite 9/9 plus source audit |
| Consistent margin | Exact full-width side/bottom margins and Settings vertical cap across states/sizes | Geometry unit/source-contract tests plus exact-build captures | Passed: geometry and source contracts plus exact-build visual smoke |
| Settings title | Resting drawer-header color, radius, and outline path match the Settings title in both skins and every focus/collapse state; backdrop masks match rendered bounds | Source contracts plus exact-build captures | Passed: Amp and OG, expanded and collapsed |
| Complete CLI coverage | Every interactive Settings operation mapped exactly once with type/domain/side-effect tests | Coverage registry tests and UI source-contract audit | Passed: 222-operation catalog and dispatch contracts |
| Agent experiment workflow | Exact fast-permutation/OG/CLI instructions are discoverable and mechanically guarded where practical | Agent/document source-contract tests | Passed: agent and documentation source contracts |
| Compilation | Release application and all targets build | Focused target build, then full Release build | Passed: exact Release renderer build |
| Regression suite | All registered tests pass | Focused `ctest`, then full `ctest` | Passed: 32/32; final strengthened UI source-contract test 1/1 |
| Documentation | Heading self-test/full scan and README line counts pass | Repository tools | Passed: 32 checker cases, 996-item full scan, and line-count check |
| Runtime behavior | Collapsed/expanded/transition layouts, Amp/OG, command discovery, representative settings/actions | Exact Release launch through Computer Use | Passed: exact `main-3ee4ceb-2155` candidate; task-owned window closed |
| Independent review | Reviewer finds no remaining coverage, safety, deletion, geometry, or documentation issue | Read-only integrated-diff review | Passed: two independent read-only reviews; findings repaired and reverified |
| Hygiene | No task-owned whitespace errors, stale active plan, unrelated diff, process, or unreported Git action | `git diff --check`, status/diff/process audit | Passed: clean diff check; task-owned renderer closed; candidate remains local |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-07-30 16:01 -05:00 | Resume the archived feature through a new active plan | Repository policy requires fresh tracking when completed work resumes; the earlier evidence remains historical but becomes stale after revision. | All |
| 2026-07-30 16:01 -05:00 | Treat `m_SettingsPanelMarginPixels` as the consistent margin | It already represents the visible Settings-to-screen inset and gives one authoritative logical-pixel token instead of a second CLI-only inset. | Margin layout |
| 2026-07-30 16:01 -05:00 | Delete rather than hide the three rejected skins | The user explicitly rejected them; near-YAGNI and cleanup policy require removing dead enum/style/test/doc paths end to end. | Skin cleanup |
| 2026-07-30 18:18 -05:00 | Reserve one permanent full-width command lane and cap Settings above it | A state-independent fixed rectangle preserves every consistent margin and prevents same-frame multiline output from moving or overlapping either surface. Horizontal scaling is excluded from Amp command motion so full width remains literal throughout animation. | Margin layout |
| 2026-07-30 18:42 -05:00 | Enforce ImGui's real minimum window envelopes in the pure resolver | A merely positive width or Settings sliver is not renderable because ImGui expands roots to its style minimums. Withholding the command surface is the only overlap-free fallback at impossible resizes. | Margin layout |
| 2026-07-30 21:50 -05:00 | Render the Settings title through the drawer-header frame path | Color parity alone left Amp's title on the 8 px window radius. Independent title identity preserves OG stock behavior while `FrameRounding` and the same outline owner make the intrinsic title frame match its skin's drawer headers. Exact half-pixel backdrop masks prevent blur from leaking beyond the revised surface. | Settings title |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 16:01 -05:00 | Coordinator preflight | Complete | This plan and screenshot inspection | Branch/worktree/status/Coming Soon/active-plan audit | Launch bounded read-only inventories, then freeze contracts. |
| 2026-07-30 16:24 -05:00 | Margin layout explorer | Complete | Geometry handoff | Source trace and mathematical edge-case review | Pure helper/test writer assigned; coordinator will integrate window-state capture and constrained content. |
| 2026-07-30 16:24 -05:00 | Experiment workflow explorer | Complete | Fast-build handoff | CMake, launcher, docs, and test trace | Skin-model writer assigned; coordinator owns workflow docs and factory command lock. |
| 2026-07-30 16:29 -05:00 | Settings inventory explorer | Complete | 222-operation inventory handoff | Source trace through Settings, nested helper, headers, and Material Editor | Complete metadata-catalog writer assigned; coordinator owns runtime dispatch and side effects. |
| 2026-07-30 16:32 -05:00 | Skin-model writer | Complete | `src/ui_skin.h`, `tests/ui_skin_tests.cpp` | Scoped diff and contract review | Amp/OG contract integrated; workflow-document writer assigned. |
| 2026-07-30 16:41 -05:00 | Command-catalog writer | Complete | `src/ui_settings_command_catalog.h`, `tests/ui_settings_command_catalog_tests.cpp` | 222 unique entries, exact section/verb/factory/dynamic counts, clean diff | Runtime integration writer now owns only `src/uvsr.cpp`. |
| 2026-07-30 16:41 -05:00 | Workflow-document writer | Complete | `AGENTS.md`, `docs/advanced-settings.md`, `docs/ui-integration-agent-procedure.md` | Full heading and retired-wording audit | Coordinator will run repository validators after integration. |
| 2026-07-30 16:43 -05:00 | Coordinator focused helper verification | Complete | Release helper targets in `build` | 4/4 skin/parser/layout/catalog tests passed | Runtime source integration remains in progress. |
| 2026-07-30 21:50 -05:00 | Settings-title geometry review | Complete | `overrides/imgui-ui.patch`, `src/uvsr.cpp`, UI contracts and guidance | Standalone ImGui override apply-check passed; two independent read-only reviews completed | Reconfigure to prove layered patch composition, then build/test and run exact-build visual smoke. |
| 2026-07-30 22:05 -05:00 | Coordinator closeout | Complete | Release executable SHA-256 `71B2B4D256821AEF9F41FF6B46FA923C48012800B840DC4865589A1B79DDF663` | Layered patch composition, exact Release build, 32/32 tests, documentation checks, and Amp/OG expanded/collapsed visual smoke passed | Keep local and unpublished; integrate deferred flashlight contracts only in a later coordinated merge. |

## Risks and Escalation Triggers

- The Settings surface is large and includes dynamic scene, light, material,
  benchmark, and renderer-resource actions. A short hand-maintained path list
  can falsely claim complete coverage.
- UI callbacks currently combine presentation, validation, renderer side
  effects, resource invalidation, and experiment locks. Command dispatch must
  share those semantics rather than approximate them.
- The Settings rectangle animates and scrolls. Requested open state is not proof
  of its presented bounds, so command placement can overlap during transition
  if it uses only `ShowUI` or target dimensions.
- Small windows may not fit the preferred command width beside expanded
  Settings. The command rectangle must shrink or use another deterministic
  non-overlapping fallback without violating the bottom-margin contract.
- Removing three skin profiles touches staged ImGui runtime policy consumers and
  can leave unused fonts, token fields, colors, aliases, or documentation.
- The flashlight/Ambient Fill task adds Settings controls in another worktree;
  later integration must extend command coverage rather than silently reducing
  completeness.

Stop and ask the user if:

- Complete Settings coverage reveals a user-facing control whose command
  semantics require choosing between materially different destructive or
  performance behavior.
- The minimum supported window cannot fit the fixed command lane plus a positive
  Settings region at the consistent margins and a different visible fallback
  would materially change the requested design.
- Work would require deleting uncertain user/peer changes, editing pinned Donut
  sources, publishing, or broadening into an external automation service.

## Completion

- Final integrated commit: intentionally uncommitted unless later authorized
- Verification summary: exact Release build passed; focused UI tests passed 9/9;
  full suite passed 32/32; the strengthened final UI source-contract test passed
  1/1; documentation and line-count checks passed; exact-build Amp/OG expanded
  and collapsed visual smoke passed.
- Independent review: two read-only reviewers completed source, geometry, patch,
  and documentation audits. Their actionable findings were repaired and
  reverified with no remaining blocker.
- Coming Soon/documentation update: reconciled in `README.md`,
  `docs/advanced-settings.md`, and `docs/ui-integration-agent-procedure.md`.
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: integrate the recorded Flashlight,
  Ambient Fill, Camera Offset, and Shadows contracts after that isolated task
  hands off; this was deliberately outside the current branch's semantic scope.
- Active ownership released: yes; the task-owned renderer was closed and the
  GPU/UI lease release was sent. The separately owned flashlight renderer was
  left untouched.
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ui-cli-margin-coverage.md`
