# UI Material, Hardware, and White Skin Refresh

## Status

- State: completed local candidate; product visual acceptance pending
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-material-hardware-skin` in
  `work/ui-material-hardware-skin`
- Base commit: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Started: 2026-08-09
- Last updated: 2026-08-09
- Planned archive:
  `docs/exec-plans/completed/ui-material-hardware-white-skin.md`

## Goal and Done Condition

Goal: refine UVSR's settings hierarchy and Amp-derived presentation so Material
and Statistics are independently accessible, hardware identity is reported in
one appropriate place, scene startup is consistently Position 1, and the
interactive controls remain visually consistent and legible.

Done when:

- [x] Material is a drawer whose open state automatically owns the crosshair.
- [x] Statistics sits directly below the Settings header and offers Hardware
      details for RAM, VRAM, their speeds, and honest CPU/GPU TFLOPS values.
- [x] The header no longer repeats GPU bandwidth or TFLOPS.
- [x] Position 1 is the visible label and the spawn/default camera for every
      bundled scene.
- [x] A selectable Amp-derived white-accent skin has readable headers, soft
      edges, darker hover feedback, uniform white slider knobs, and verified
      red/green toggle knobs.
- [x] Slider values avoid their knobs, dropdown rows and corners are uniform,
      and popup transitions remain smooth.
- [x] Settings uses the available screen height and smoothly yields space only
      while the command interface is open.
- [x] Focused tests, full Release build/tests, title-case validation, source
      audit, and an exact-candidate launch smoke pass.
- [ ] Product visual acceptance remains pending because the window-state capture
      helper failed inside its control runtime; do not infer visual acceptance
      from the successful launch.

## Scope

In scope:

- Settings, Material, Statistics, command-interface, slider, toggle, combo, and
  skin presentation.
- Cached Windows/DX12 hardware identity and theoretical-capability reporting.
- Camera preset labels and startup/default application for bundled scenes.
- Focused tests and durable UI/settings documentation.

Non-goals:

- Renderer algorithm, shader, lighting, scene-asset, benchmark methodology, or
  command-line benchmark semantic changes.
- Donut source changes, vendor-specific monitoring dependencies, background
  hardware polling, publication, pull request, merge, or release.
- Removing the existing Amp or OG skins unless implementation evidence proves a
  three-skin contract cannot remain coherent.

Affected subsystems and paths:

- `src/uvsr.cpp`, `src/ui_skin.h`, and focused first-party UI/hardware helpers.
- Camera/scene preset helpers under `src/`.
- UI, scene, source-contract, and helper tests under `tests/`.
- `README.md`, `docs/advanced-settings.md`, and
  `docs/ui-integration-agent-procedure.md`.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/uvsr.cpp`, all UI contracts, all camera
  defaults, this execution plan, the build tree, and the UVSR window.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main` at
  `7202ff958d2ed5ffa5a54f7374c1d15c772307a5` after a 2026-08-09 fetch and
  `ls-remote` check.
- Local versus remote state: isolated branch/worktree starts exactly equal to
  live `origin/main`; the canonical checkout is intentionally untouched because
  it is ahead two, behind 28, and contains an unrelated untracked document.
- Verified source commit/build: `7202ff9` was Canonical verified in the prior
  task with Standard and NRD 40/40 tests; a fresh task-local build is required.
- GPU, scene, camera, resolution, and settings preset when relevant: detect at
  runtime; inspect bundled scenes at Position 1 using factory settings.
- Known pre-existing failures: none recorded for the exact base.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live GitHub main | Exact `7202ff9` base | Complete | Whole task |
| UI architecture audit | Existing state, helper map, and final review | Complete | Coordinator implementation |
| Hardware/color research | Honest metrics, backend, and verified Blender colors | Complete | Hardware panel and skin |
| UI test audit | Deterministic regression matrix and skin contract | Complete | Verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Renderer settings remain factory-reset and non-persistent.
- Existing command identifiers and benchmark automation semantics remain stable
  even when the visible camera label becomes Position 1.
- The Material drawer alone forces crosshair visibility while open; closing it
  releases only that forced state.
- Hardware figures are cached identity/capability data, not per-window render
  throughput, and unavailable values are labeled honestly.
- All skins expose identical controls and renderer state.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `ui-architecture-audit` | `/root/ui_architecture_audit` | Read-only shared context | `7202ff9` | None | Base | Complete |
| `hardware-color-research` | `/root/hardware_color_research` | Shared task worktree | `7202ff9` | Hardware monitor header/source/test | Base | Complete |
| `ui-test-audit` | `/root/ui_test_audit` | Shared task worktree | `7202ff9` | Skin header/test | Base | Complete |
| `integrate` | `/root` | Task worktree | `7202ff9` | All remaining task paths | Audit decisions | Complete |
| `independent-review` | `/root/ui_architecture_audit` | Read-only candidate | Candidate | None | Integrated diff | Complete |

## Assignment Contracts

### UI Architecture Audit

- Owner/thread: `/root/ui_architecture_audit`
- Branch/worktree: read-only shared task worktree
- Base commit/state: `7202ff9`
- Read scope: first-party UI/source/tests/docs and Donut APIs as reference
- Write scope: none
- No-touch scope: all files, refs, build trees, processes, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: exact base
- Interface/invariant contract: map existing helpers without redesigning public
  renderer state
- Deliverable: line-specific implementation map and risk audit
- Done when: every requested behavior has a concrete seam and smallest design
- Required verification: read-only source inspection
- Allowed Git and external actions: read-only only
- Stop and report if: base changes or unexpected diffs appear
- Handoff revision/artifact: line-specific seam map plus severity-ranked frozen
  candidate review
- Handoff acknowledged by/on: `/root`, 2026-08-09

### Hardware and Color Research

- Owner/thread: `/root/hardware_color_research`
- Branch/worktree: shared task worktree with disjoint path ownership
- Base commit/state: `7202ff9`
- Read scope: existing telemetry plus official Microsoft, CPU/GPU, and Blender
  sources
- Write scope: `src/gpu_performance_monitor.h`,
  `src/gpu_performance_monitor.cpp`, and
  `tests/hardware_statistics_tests.cpp`
- No-touch scope: all other files, refs, build trees, processes, and external
  state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: exact base
- Interface/invariant contract: report only supportable values and compensate
  verified accent colors for actual alpha compositing
- Deliverable: data/API recommendation, official color evidence, fallbacks, and
  the independently valid cached hardware-capability backend
- Done when: all six hardware rows and both toggle states have defensible values
- Required verification: source inspection and primary-source research
- Allowed Git and external actions: read-only browsing and inspection
- Stop and report if: a value requires privileged or unreliable per-frame work
- Handoff revision/artifact: identity-keyed hardware capability contract with
  SMBIOS, DXGI, NVML, and Intel Control Library tests
- Handoff acknowledged by/on: `/root`, 2026-08-09

### UI Test Audit

- Owner/thread: `/root/ui_test_audit`
- Branch/worktree: shared task worktree with disjoint path ownership
- Base commit/state: `7202ff9`
- Read scope: tests, validators, build registration, launch tooling, and relevant
  source contracts
- Write scope: `src/ui_skin.h` and `tests/ui_skin_tests.cpp`
- No-touch scope: all other files, refs, build trees, processes, and external
  state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: exact base
- Interface/invariant contract: preserve factory and command semantics while
  testing visible labels and defaults
- Deliverable: focused test map plus the stable Amp/White/OG skin contract and
  its pure tests
- Done when: requested state/layout changes map to deterministic evidence
- Required verification: read-only test/tool inspection
- Allowed Git and external actions: read-only only
- Stop and report if: testing would require taking the runtime/GPU lease
- Handoff revision/artifact: header-only skin enum/labels/commands and tests
- Handoff acknowledged by/on: `/root`, 2026-08-09

### Integrate the Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/ui-material-hardware-skin` in task worktree
- Base commit/state: clean `7202ff9`
- Read scope: full first-party repository and read-only dependency APIs
- Write scope: task source, focused tests, scoped docs, README roadmap, and this
  plan
- No-touch scope: `donut/`, unrelated worktrees/state, renderer algorithms,
  shader assets, and remote refs
- Build directory and runtime/GPU/resource lease: one task-local build directory;
  coordinator is sole build and UVSR window operator
- Dependencies already integrated: audit decisions before interface-sensitive
  implementation
- Interface/invariant contract: all public contracts listed above
- Deliverable: locally verified exact candidate and executable
- Done when: all goal rows have evidence and no P0-P2 review finding remains
- Required verification: focused tests, full Release build/CTest, title-case
  validator, diff checks, runtime smoke, and visual inspection
- Allowed Git and external actions: local edits/build/tests/runtime only; no
  commit, push, PR, merge, or release
- Stop and report if: metrics require dishonest inference, UI behavior needs a
  material user choice, or unrelated state changes in owned paths
- Handoff revision/artifact: Release candidate at `build/bin/uvsr.exe`
- Handoff acknowledged by/on: local handoff pending user visual review

## Integration Order

1. Freeze the exact base, architecture, hardware, color, and test contracts.
2. Add focused pure helpers/tests for hardware formatting, colors, layout, and
   scene defaults.
3. Integrate Material/Statistics hierarchy and camera behavior.
4. Integrate the white skin and widget/dropdown/menu presentation.
5. Update durable UI documentation and Coming Soon once.
6. Build, run all tests, inspect the runtime at fixed scenes/settings, and
   capture the exact candidate identity.
7. Freeze the diff for independent review, repair findings, rerun stale checks,
   and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Exact clean base | Git and live remote identity | `git status`, fetch, `ls-remote` | `7202ff9`; complete |
| Material/crosshair ownership | State-transition tests plus runtime behavior | UI source contracts and exact launch smoke | Automated contract complete; visual review pending |
| Hardware data and header removal | Unit/source-contract tests plus runtime rows | Hardware and source-contract tests | Complete; unsupported CPU TFLOPS is `--` |
| Position 1 labels/defaults | Catalog/preset tests across every bundled scene | Camera/preset and full CTest | Complete |
| White skin and widget geometry | Pure style/layout tests plus screenshots | Skin and exact-color ImGui tests | Automated contract complete; screenshots pending |
| Dropdown consistency | Popup lifecycle tests and first/middle/last inspection | ImGui dropdown lifecycle test | Automated contract complete; visual review pending |
| Menu/CLI reflow | Layout tests and open/close runtime inspection | Command layout and source-contract tests | Automated contract complete; visual review pending |
| Integrated health | Clean build, full tests, docs, and diff audit | Release build, 41-test CTest, validators | Complete |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-09 | Use a new linked worktree from exact live `origin/main` `7202ff9`. | The canonical checkout is diverged and dirty; isolation preserves unrelated work and the user's explicit base. | Whole task |
| 2026-08-09 | Keep one coordinator as the only writer and runtime operator. | The requested hierarchy, animation, style, camera, and statistics changes converge in shared UI hotspots. | Implementation and verification |
| 2026-08-09 | Treat the white presentation as a selectable Amp-derived skin while preserving Amp and OG. | "Make a new skin" implies an additional presentation; removing or silently replacing existing skins would broaden scope. | Skin and docs |
| 2026-08-09 | Treat hardware TFLOPS as cached theoretical capability, not live renderer throughput. | This avoids per-window duplication and directly supports comparisons between two render windows in one process. | Hardware Statistics |
| 2026-08-09 | Leave CPU FP32 peak as `--` with an explanation. | Windows does not expose the microarchitecture-specific SIMD/FMA issue data needed for an honest portable theoretical result; core count times clock would fabricate precision. | Hardware Statistics |
| 2026-08-09 | Detect shared/UMA memory from the selected D3D12 node architecture. | Zero dedicated bytes is not a reliable UMA test, and reserved firmware memory must not be reported as dedicated VRAM. | Hardware Statistics |
| 2026-08-09 | Use opaque Blender theme endpoints `#FF3352` and `#8BDC00` for authored-skin toggle knobs. | Alpha compositing over UVSR's translucent surfaces visibly dulled the official colors; opaque endpoints preserve their verified appearance. | Amp and White skins |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-09 | `hardware-color-research` `/root/hardware_color_research` | Complete | Hardware monitor header/source and new focused test | Scoped diff check | Integrated and released |
| 2026-08-09 | `ui-test-audit` `/root/ui_test_audit` | Complete | Skin header and focused test | Scoped diff check | Integrated and released |
| 2026-08-09 | `integrate` `/root` | Complete | `build/bin/uvsr.exe` | Release build; 41/41 CTest; 1,355-heading validation; exact-window launch smoke | Product visual acceptance pending because capture control failed |
| 2026-08-09 | `independent-review` `/root/ui_architecture_audit` | Complete | Frozen composed candidate | No P0-P2 findings in the four repaired areas; no repair regression identified | Ownership released |

## Risks and Escalation Triggers

- CPU peak TFLOPS and memory speeds can be unavailable or topology-dependent;
  the UI must state unavailable rather than fabricate precision.
- Popup row distortion may be coupled to the patched ImGui transition; changes
  must preserve click blocking and deferred mutation safety.
- Camera preset application may be scene-specific; the default must not teleport
  after user movement or invalidate command-line benchmark contracts.
- White translucent accents can lose edge definition; source colors and borders
  need visual checks over the actual backdrop.

Stop and ask the user if:

- reliable hardware values require a new privileged/vendor dependency;
- adding the white skin cannot preserve the existing Amp and OG contracts;
- Position 1 cannot be defined consistently for a bundled scene without a new
  authored camera choice;
- publication or deletion beyond this local candidate is requested.

## Completion

- Final integrated commit: none; implementation authority was local-only
- Verification summary: Release build passed; 41 of 41 CTest tests passed;
  README counts are current; Title Case self-test and 1,355-heading scan passed;
  `git diff --check` passed; exact executable launched as one responsive window
  and was closed cleanly
- Exact executable: `build/bin/uvsr.exe`, 2,692,096 bytes, SHA-256
  `DEC66C9E26D2AE751008750D88DB33F29F3D75193CF4D5F7322E5F98C6FDAF74`
- Independent review: initial P1/P2 findings were repaired or design-resolved;
  the final targeted recheck found no remaining P0-P2 issue and no regression
- Coming Soon/documentation update: complete for this local candidate
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual review of the exact candidate;
  the computer-control state capture failed with `node_repl exec context not
  found`, so no visual acceptance is claimed
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ui-material-hardware-white-skin.md`
