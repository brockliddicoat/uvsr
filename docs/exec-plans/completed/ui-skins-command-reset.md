# UI Skins, Slash Commands, and Smooth Roll Reset

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/ui-skins-command-reset` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\ui-skins-command-reset`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Started: 2026-07-30
- Last updated: 2026-07-30 13:47 -05:00
- Governing agent policy: `AGENTS.md` version `2026-07-22.1`
- Archived to:
  `docs/exec-plans/completed/ui-skins-command-reset.md`

## Goal and Done Condition

Goal:

Give users and experiment-running agents five coherent UI skins, including a
fully immediate stock-ImGui mode, plus an in-app slash command interface that
can perform common renderer operations without waiting for UI motion. Replace
the camera roll-reset snap on `V` with a smooth exponential response that
intentionally overshoots level once and then settles.

Done when:

- [x] General Settings exposes Current UVSR, Original ImGui, ChatGPT + Codex,
      Unreal Engine 5, and UVSR Signal skins.
- [x] Original ImGui uses stock ImGui presentation and bypasses UVSR, widget,
      popup, and structural animations without bypassing frame-end mutation
      safety.
- [x] The `/` command interface supports discovery plus useful renderer,
      camera, scene, UI, skin, and agent-workflow operations.
- [x] `V` preserves camera position and view direction while roll follows a
      frame-rate-independent overshoot-and-settle trajectory.
- [x] Focused and full builds/tests pass, document headings pass the repository
      validator, and runtime screenshots prove the visible skin variants.
- [x] The local candidate is reviewed independently and remains uncommitted and
      unpublished unless the user later authorizes those actions.

## Scope

In scope:

- A session-only UI skin preference with Current UVSR as the launch default.
- Runtime selection of style tokens, first-party UI motion, patched ImGui widget
  behavior, popup motion, and backdrop treatment.
- A topmost command bar opened by `/`, with parsing, history, completion help,
  validation, and deferred safe execution.
- Common commands for skin, Settings visibility, scene, camera mode and
  location, renderer settings, shader reload, screenshot, and restart where
  the existing application exposes safe actions.
- Smooth roll leveling on `V`, including one controlled overshoot, exponential
  settling, cancellation, and deterministic tests.
- Tests, user documentation, UI integration procedure updates, and local
  visual/runtime validation.

Non-goals:

- Persisting skin, console history, or settings across process restarts.
- Editing Donut sources under `donut/`.
- Creating a general remote-control protocol, scripting language, or network
  console.
- Changing camera position, target direction, or the meaning of `V` beyond
  replacing the roll-leveling snap.
- Benchmarking renderer performance, publishing changes, or modifying
  unrelated feature work.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/camera_controllers.h`
- `src/ui_skin.h`
- `src/ui_commands.h`
- `overrides/imgui-runtime-policy.patch`
- `overrides/donut-app.patch`
- `CMakeLists.txt`
- Focused skin, command, camera, ImGui lifecycle, and UI source-contract tests
  under `tests/`
- `README.md`, `docs/advanced-settings.md`, and
  `docs/ui-integration-agent-procedure.md`
- `tools/update_readme_line_counts.py`

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`
- `src/ui_animation.h`
- `CMakeLists.txt`
- Both ImGui override patches and their contract tests
- `README.md`, UI documentation, and this execution plan
- Integration, all Git operations, builds, runtime/GPU control, screenshots,
  and the completion claim

## Baseline

- Canonical repository/remote: local `origin/main` at
  `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Local versus remote state: the task branch is equal to its `origin/main`
  tracking base. The original checkout's local `main` at `891f4f0` is
  ahead one and behind six with a malformed generated-content commit; it is
  preserved and excluded from this work.
- Verified source commit/build: dirty local candidate on canonical base
  `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`; source manifest SHA-256
  `ed72bb23f7e3afd01f737d4bca9cc67d900b4b0f675f26639dfccdb05544d2b3`;
  Release `build/bin/uvsr.exe` SHA-256
  `56be21f373df8f2282d6d80982f0aa176ed593d1ecd80dcf480d5a3cee35bd9d`.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU, PBR Sponza Decorated, Benchmark Position 1,
  1920x1080 renderer output in a 1536x895 logical capture, factory settings,
  and each of the five session-only skins. No performance comparison was run.
- Known pre-existing failures: none established on this isolated worktree.
  Focused and full baselines will distinguish pre-existing failures from
  task-introduced failures.
- Other work: the downloaded-scenes linked worktree is dirty and owns its own
  changes. Historic Bend, Sparse VSM, CSM, and README owners released current
  main hotspots. The remaining historic SVSM ownership audit is read-only and
  cannot write this worktree.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Donut/ImGui overrides | Runtime policy must be implemented as first-party build-time patches; no `donut/` edits | Decided | Skin integration |
| UI session state | Skin and command history remain process-local; renderer reset preserves the selected skin | Decided | UI and command integration |
| Renderer mutations | UI and command actions apply only after ImGui composition; immediate presentation does not mean unsafe mid-widget mutation | Decided | Classic skin and slash commands |
| Command chronology | Older deferred UI choices fast-forward and commit before the newer slash command at the post-composition barrier | Complete | Slash command integration |
| Fullscreen shortcut | A staged Donut render-pass query lets the open command bar reserve Alt+Enter without changing ordinary fullscreen behavior | Complete | Command input capture |
| Camera semantics | `V` remains roll leveling and preserves position/view direction | Decided | Camera implementation |
| Visual research | Use official public references only as design guidance, without copying proprietary assets | Complete | Skin tokens |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `UiSkin` has exactly five stable values: `Current`, `ImGuiClassic`,
  `ChatGptCodex`, `UnrealEngine5`, and `RenderLab`.
- Current UVSR remains the default. Original ImGui alone selects stock widgets
  and disables motion.
- Patched ImGui exposes one runtime UVSR policy setter used every frame by
  `UIRenderer`; all patched translation units use the matching staged headers.
- The command parser is pure and returns validated command tokens without
  directly mutating renderer state. `UIRenderer` owns action dispatch and the
  frame-end execution barrier.
- Slash commands use stable lowercase engineering paths and support quoted
  values. Invalid enum/range/path input causes no mutation.
- `V` begins or restarts roll leveling only on key press. Repeated key events do
  not restart it, and camera-affecting user input cancels the pending motion.
- The roll curve is analytic and based on elapsed seconds so 30, 60, 144, and
  irregular frame sequences reach equivalent poses at equivalent times.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `visual-research` | `/root/skin_visual_research` | Shared, read-only | `3ee4ceb` contract | No writes | Official design references | Complete |
| `camera-reset` | `/root/camera_reset_review` | Shared task worktree | `3ee4ceb` plus this plan | `src/camera_controllers.h`, `tests/camera_controls_tests.cpp` | Camera contract below | Complete |
| `ui-models` | `/root/ui_models_impl` | Shared task worktree | `3ee4ceb` plus this plan | New pure skin/command headers and focused tests only | Stable enum and grammar below | Complete |
| `ui-integration` | `/root` | Shared task worktree | `3ee4ceb` plus writer handoffs | Coordinator hotspots | All contracts | Complete |
| `independent-review` | Camera, visual, and UI-model reviewers | Shared, read-only after write freeze | Integrated candidate | No writes | Integrated diff | Complete |

## Assignment Contracts

### Visual Research: Distill Skin Design Tokens

- Owner/thread: `/root/skin_visual_research`
- Branch/worktree: shared task context, read-only
- Base commit/state: source-independent research
- Read scope: official OpenAI and Epic/Unreal public visual references
- Write scope: none
- No-touch scope: entire repository, Git state, build tree, and runtime
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: named skin concepts and product goal
- Interface/invariant contract: return practical palette, rounding, density,
  hierarchy, and motion guidance; do not reproduce proprietary assets.
- Deliverable: concise, cited visual-token handoff for ChatGPT + Codex,
  Unreal Engine 5, and UVSR Signal.
- Done when: the coordinator can translate the guidance into code-owned tokens.
- Required verification: source URLs and clear separation between sourced facts
  and design inference.
- Allowed Git and external actions: read-only web research only
- Stop and report if: official sources cannot support a claimed detail.
- Handoff revision/artifact: agent message
- Handoff acknowledged by/on: 2026-07-30

### Camera Reset: Implement the Roll Trajectory

- Owner/thread: `/root/camera_reset_review`
- Branch/worktree:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\ui-skins-command-reset`
- Base commit/state: `3ee4ceb` plus this execution plan
- Read scope: camera controller implementation and camera tests
- Write scope: `src/camera_controllers.h`,
  `tests/camera_controls_tests.cpp`
- No-touch scope: all other source, CMake, documentation, overrides, Git index,
  branches, history, build trees, and running processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: stable mathematical and input contracts in
  this plan
- Interface/invariant contract: preserve position and view direction; capture a
  level target basis; use a continuous two-phase analytic exponential curve
  with one roughly 15-percent overshoot; finish exactly level; cancel on other
  camera input or exact-pose replacement; remain finite near vertical.
- Deliverable: task-owned source/test edits plus a distilled handoff.
- Done when: deterministic tests cover crossing, overshoot, convergence,
  frame-rate invariance, repeats, cancellation, third-person inheritance, and
  near-vertical stability.
- Required verification: source review and any test command that does not write
  outside the owned files; coordinator owns shared builds.
- Allowed Git and external actions: no Git mutation, publication, or external
  action
- Stop and report if: the required cancellation hook would need a file outside
  the assigned scope or current camera semantics differ materially.
- Handoff revision/artifact: changed-path list, invariants, checks, and risks
- Handoff acknowledged by/on: 2026-07-30

### UI Models: Implement Pure Skin and Command Contracts

- Owner/thread: `/root/ui_models_impl`
- Branch/worktree:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\ui-skins-command-reset`
- Base commit/state: `3ee4ceb` plus this execution plan
- Read scope: existing command-line parser conventions and focused unit-test
  style
- Write scope: new `src/ui_skin.h`, new `src/ui_commands.h`, new
  `tests/ui_skin_tests.cpp`, and new `tests/ui_commands_tests.cpp`
- No-touch scope: `src/uvsr.cpp`, CMake, existing tests, override patches,
  documentation, Git index, branches, history, build trees, and runtime
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: stable enum, labels, behavior flags, and
  command grammar in this plan
- Interface/invariant contract: header-only pure code; no Donut/ImGui
  dependency; ASCII lowercase normalization; quoted argument parsing; clear
  parse errors; no renderer mutation; exact five-skin mapping.
- Deliverable: task-owned files plus a distilled handoff.
- Done when: focused tests cover skin uniqueness/default/aliases and command
  tokenization, quoting, escaping, verbs, aliases, invalid input, and completion
  primitives.
- Required verification: source review and any isolated compilation that does
  not modify shared build state; coordinator registers and builds targets.
- Allowed Git and external actions: no Git mutation, publication, or external
  action
- Stop and report if: a required integration behavior cannot remain pure or a
  public name in this plan is ambiguous.
- Handoff revision/artifact: changed-path list, public API, checks, and risks
- Handoff acknowledged by/on: 2026-07-30

### UI Integration: Apply Skins, Runtime Policy, and Commands

- Owner/thread: `/root`
- Branch/worktree:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\ui-skins-command-reset`
- Base commit/state: `3ee4ceb` plus acknowledged writer handoffs
- Read scope: full repository as needed
- Write scope: coordinator hotspots and integration/documentation paths
- No-touch scope: `donut/`, unrelated worktrees, generated `build/` and `work/`
  content as committed source
- Build directory and runtime/GPU/resource lease: coordinator only
- Dependencies already integrated: this plan and accepted visual research
- Interface/invariant contract: Current reproduces existing behavior; Original
  ImGui is fully immediate and visually stock; other skins remain animated;
  command input captures `/`, text, `V`, `Z`, `M`, `F`, Escape, and Alt+Enter
  before application shortcuts; older deferred UI actions drain before the
  newer command executes after composition.
- Deliverable: integrated source, override, tests, docs, screenshots, and
  verification record.
- Done when: all done conditions and verification rows are satisfied.
- Required verification: focused builds/tests, full build/test suite, heading
  validator, README line counts, diff checks, runtime skin/command/camera smoke
  checks, and independent review.
- Allowed Git and external actions: local edits/build/runtime only; no commit,
  push, PR, merge, or release
- Stop and report if: visual direction needs a user preference, unsafe renderer
  mutation cannot use an existing frame-end barrier, or unrelated work appears
  in this isolated worktree.
- Handoff revision/artifact: final local diff and verification evidence
- Handoff acknowledged by/on: coordinator-owned

## Integration Order

1. Freeze the enum, behavior, grammar, and camera trajectory contracts in this
   plan.
2. Accept the pure camera and UI-model handoffs and release their path
   ownership.
3. Integrate skin selection, runtime ImGui policy, animation bypasses, and
   command dispatch in the coordinator-owned UI.
4. Register tests, build the combined candidate, and fix only task-introduced
   failures.
5. Freeze writes for independent review, resolve findings, and repeat affected
   checks.
6. Perform one controlled runtime/visual QA pass, update documentation and line
   counts, archive this plan, and report the local candidate.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Camera reset trajectory | Deterministic unit tests at multiple time steps and frame rates | `uvsr_camera_controls_tests` | Passed at 30, 60, 144, and irregular frame sequences; 14.93-percent overshoot and exact settle verified |
| Skin model and parser | Pure focused unit tests | `uvsr_ui_skin_tests`, `uvsr_ui_commands_tests` | Passed |
| UI and override integration | Source-contract, animation, popup, and renderer tests | Focused CMake build plus focused `ctest` regex | Passed, 8 of 8 |
| Complete compilation | Release application and all targets build | `cmake --build build --config Release --target uvsr`, then full build | Passed |
| Repository regression suite | All registered tests pass | `ctest --test-dir build -C Release --output-on-failure` | Passed, 30 of 30 |
| Document conformance | Self-test and complete Title Case scan pass | `tools\check_document_title_case.cmd --self-test`, then repository scan | Passed, 31 self-test cases and 972 repository headings/lead-ins |
| README counts | Generated counts are current and stable | `tools\update_readme_line_counts.cmd --write`, then `--check` | Passed: 119,315 first-party, 387,621 third-party, 506,936 total |
| Patch hygiene | No whitespace errors or unrelated diffs | `git diff --check` and complete diff review | Passed; line-ending notices only |
| Product behavior | Five skin endpoints, immediate Original ImGui interactions, slash capture/errors/actions, and shortcut isolation | Exact Release binary launch through Computer Use | Passed on final binary: five distinct Settings endpoints, `/get ui.skin`, ASCII chrome, immediate Original endpoint, Escape isolation, and Alt+Enter suppression |
| Independent safety review | Reviewer confirms resource/mutation, override, input, visual-token, and camera invariants | Read-only integrated-diff review | Passed; camera, visual, and UI-model reviewers report no actionable findings |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-07-30 11:40 -05:00 | Base work on canonical `3ee4ceb` in a new linked worktree | Local `main` contains a malformed generated-content commit and is behind canonical history. Resetting it would risk user work; inheriting it would contaminate this feature. | All |
| 2026-07-30 11:48 -05:00 | Treat `V` as roll leveling | Source and tests prove that current `V` behavior resets roll only. Reinterpreting it as a full pose reset would change product semantics beyond the request. | Camera reset |
| 2026-07-30 11:53 -05:00 | Keep UI skin and command state session-only | This matches the explicit current no-persistence contract and avoids a speculative settings store. | UI integration |
| 2026-07-30 11:58 -05:00 | Give Original ImGui both stock widget branches and zero-motion policy | Palette-only styling would leave patched checkbox, slider, dropdown, Settings, and first-party animations active, defeating the experiment workflow goal. | UI integration |
| 2026-07-30 12:04 -05:00 | Keep all mutations behind an end-of-composition barrier | Removing presentation waits must not allow a widget callback to invalidate resources while ImGui is traversing them. | UI integration |
| 2026-07-30 13:18 -05:00 | Drain older deferred selections before slash dispatch | Chronological ordering makes the newer command authoritative and prevents a stale popup choice from resurfacing after the command bar closes. | UI integration |
| 2026-07-30 13:20 -05:00 | Add a narrow staged Donut query for Alt+Enter capture | Donut toggles fullscreen before ordinary render-pass input. A read-only pass query preserves existing fullscreen behavior unless the command bar is open, without editing pinned dependency sources. | UI integration |
| 2026-07-30 13:21 -05:00 | Apply the controlled-experiment lock to screenshots | GPU readback and clipboard work can contaminate a benchmark even though it does not mutate a renderer setting. | UI integration |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 11:45 -05:00 | Overlap audit | Complete | Read-only handoff | Git/worktree/plan/task audit | Historic current-main hotspots released; dirty downloaded-scenes worktree remains isolated. |
| 2026-07-30 11:55 -05:00 | UI/command architecture | Complete | Read-only handoff | Source trace | Integrate runtime policy across both first-party and patched-ImGui motion. |
| 2026-07-30 11:56 -05:00 | Camera architecture | Complete | Read-only handoff | Source and mathematical trace | Implement analytic curve and cancellation tests. |
| 2026-07-30 12:09 -05:00 | Coordinator | Complete | This execution plan | Base and worktree reverified clean | Bounded writers assigned and handoffs integrated. |
| 2026-07-30 12:44 -05:00 | Camera and UI-model writers | Complete | Shared task worktree handoffs | Pure tests and source review | Ownership released to coordinator. |
| 2026-07-30 13:16 -05:00 | Integrated candidate review | Findings resolved | Read-only camera, visual, and UI-model reviews | Source and contract inspection | Added exact secondary-color guards, command chronology, screenshot lock, and Alt+Enter capture; final re-review found no issues. |
| 2026-07-30 13:28 -05:00 | Automated verification | Complete | Release build directory | Focused 8 of 8 and full 30 of 30 tests | No task-introduced failures. |
| 2026-07-30 13:44 -05:00 | Runtime verification | Complete | Release executable SHA-256 recorded above | Exact-build Computer Use walkthrough | Five final skin endpoints and slash input isolation passed; renderer closed. |
| 2026-07-30 13:47 -05:00 | Documentation and hygiene | Complete | README and UI documentation | Heading self-test/scan, README counts, `git diff --check` | All checks passed. |

## Risks and Escalation Triggers

- ImGui override changes span multiple translation units. A header/source policy
  mismatch can compile partially and crash at runtime.
- A palette-only Classic mode would not satisfy the no-animation requirement.
- Immediate UI and command actions can expose resource-lifetime bugs if they
  bypass the frame-end barrier rather than only bypassing presentation waits.
- Switching skins mid-popup or mid-structural transition must drive existing
  animation state to a stable endpoint.
- Hard-coded colors in drawer, slider, footer, and backdrop helpers can leak the
  Current UVSR look into other skins unless tokenized.
- Camera roll near a vertical view direction needs a deterministic fallback
  basis to avoid NaNs.
- Runtime visual QA must not collide with another UVSR or build process and is
  not a performance benchmark.

Stop and ask the user if:

- Matching a named skin would require copying or distributing proprietary
  Unreal, ChatGPT, or Codex assets rather than implementing an inspired
  first-party theme.
- A visible product choice remains between materially different camera
  semantics or interaction defaults after source inspection.
- Any required action expands into publication, destructive cleanup, or
  overwriting unrelated user work.

## Completion

- Final integrated commit: intentionally uncommitted unless later authorized
- Verification summary: Release application and all targets build; focused
  feature matrix passes 8 of 8; full suite passes 30 of 30; final exact binary
  passes five-skin and slash-input runtime QA; documentation, line counts, and
  diff hygiene pass.
- Independent review: camera, visual-profile, and command/UI safety reviews
  closed with no actionable findings.
- Coming Soon/documentation update: complete
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: no required work; a local commit or
  publication remains available only if the user later authorizes it.
- Active ownership released: yes
- Archived to completed/abandoned path: completed path
