# Flashlight Beam Shape

## Status

- State: complete
- Coordinator: `/root`
- Project branch and worktree: `codex/flashlight-ambient-fill` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\flashlight-ambient-fill`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Resumed candidate:
  - Tracked diff SHA-256:
    `ab6ff365425b2eb31c814483ce7f651549bf5cb047a4ed3e35ab4fc0a82d8641`
  - `src/flashlight.h` SHA-256:
    `7345a8d565f47d13035036ecc68f1346dbfa6734f7b400bec3437e1c6b4176a7`
  - `tests/flashlight_tests.cpp` SHA-256:
    `13fc38ac1d8b536c0ff74d2317891868b2615f0e5203a6e9139591f104028ece`
- Prior plan:
  `docs/exec-plans/completed/flashlight-realism-controls.md`
- UI reference version recorded before implementation: `2026-07-30.1`
- Active-worktree `AGENTS.md` policy version recorded before implementation:
  `2026-07-22.1`
- Started: 2026-07-30
- Last updated: 2026-07-30
- Completed: 2026-07-30
- Archived as:
  `docs/exec-plans/completed/flashlight-beam-shape.md`

## Goal and Done Condition

Goal:

Change Flashlight roundness from circular edge softness into a real projected
beam-shape control that continuously morphs between a softly rounded, nearly
square aperture and a circle while retaining plausible lens falloff, geometry
shadows, and camera-relative behavior.

Done when:

- [x] **Beam Roundness** visibly controls footprint geometry from nearly square
      to circular, while **Edge Softness** independently controls the cutoff.
- [x] The rounded-square profile is stable under camera rotation, finite at
      every supported setting, symmetric, and smoothly converges to the
      existing circular endpoint.
- [x] Simple and realistic flashlight modes have explicit coherent behavior,
      with the realistic lens hotspot and spill retaining one shared shadow
      geometry render.
- [x] Forward PBR, deferred PBR, and deferred PBR MSAA produce matching beam
      shapes without changing ordinary scene lights or directional visibility.
- [x] Reset, scene recreation, resize, exact-off, shadow-disable, controlled
      benchmark, PBR-disable, and command-bar input contracts remain intact.
- [x] Focused tests, production and factory Release builds, both full CTest
      suites, independent rendering review, exact-build DX12 visual smoke, and
      repository documentation checks pass.

## Scope

In scope:

- Flashlight shape and softness settings, sanitation, defaults, and reset.
- Analytic rounded-aperture attenuation in the active first-party PBR paths.
- Stable beam orientation derived from the camera-relative flashlight pose.
- Shadow-frustum compatibility without an additional shadow geometry render.
- Purpose-built Lights UI, focused reference/source contracts, user and
  renderer documentation, and Coming Soon wording.

Non-goals:

- Bitmap cookies, licensed lens assets, volumetric scattering, anamorphic lens
  simulation, flashlight hardware meshes, a general scene-light cookie system,
  Donut edits, non-PBR flashlight support, persistence, or publication.

Affected subsystems and paths:

- `src/flashlight.h`
- `src/uvsr.cpp`
- First-party PBR lighting CPU/HLSL contracts selected after architecture review
- Focused flashlight, renderer, UI, lighting, and shader-bundle tests
- `CMakeLists.txt` and shader registries only if the selected contract requires
  registration
- README, PBR/user documentation, and this execution plan

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, all CPU/HLSL lighting contracts, shader registries,
  `CMakeLists.txt`, README, shared documentation, build trees, renderer process,
  and GPU

## Baseline

- Canonical repository: local `main` is at `891f4f0`; remote freshness is not
  required because publication or integration is out of scope.
- Local versus remote state: local-only dirty feature branch; no commit, push,
  pull request, merge, or release is authorized.
- Verified source/build: the resumed candidate passed production and factory
  Release builds, 29/29 tests in each topology, exact production DX12 smoke,
  documentation checks, and independent review.
- Runtime preset: PBR Sponza Decorated, Benchmark Position 1, deferred PBR,
  maximized 1920-wide internal presentation, serialized DX12 UI/GPU lease.
- Known pre-existing failures: none in the resumed feature candidate.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior flashlight candidate | Exact dirty identity above | Selected | Whole follow-up |
| UI command-bar contract | Open command bar consumes keyboard and character input before `F` | Confirmed externally | Input preservation |
| Cleanroom visibility contract | Three unchanged full-resolution `R8_UNORM` directional slots and neutral build names | Confirmed externally | Future semantic merge |
| Beam architecture review | Analytic profile, orientation, and PBR insertion contract | Complete | Implementation |
| UI and test map | Separate shape/softness semantics and evidence matrix | Complete | Implementation and verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- **Beam Roundness** is a normalized flashlight-only setting. Its circle
  endpoint must match the existing radial cone; its low endpoint is a softened
  rounded square rather than a discontinuous hard rectangle.
- **Edge Softness** remains a separate normalized flashlight-only setting.
- Ordinary scene spotlights retain their current equations and UI.
- Both flashlight lobes share one resolved pose and one shadow map; realistic
  mode may add lighting evaluation but never a second geometry render.
- Exact off, disabled shadows, non-PBR, and controlled benchmarks remain
  independent zero-work gates.
- The three directional visibility slots and deferred-lighting ABI remain
  unchanged unless architecture review proves a first-party extension is
  necessary and it is documented before implementation.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| beam-shape-architecture | `/root/beam_shape_architecture` | Shared read-only | Resumed candidate | None | Current lighting contracts | Complete |
| beam-shape-ux-tests | `/root/beam_shape_ux_tests` | Shared read-only | Resumed candidate | None | Current UI and tests | Complete |
| implementation | `/root` | Feature worktree | Resumed candidate | All task paths and this plan | Both explorer handoffs | Complete |
| independent-review | `/root/beam_shape_review` | Shared read-only after freeze | Final dirty candidate | None | Implementation and checks | Complete |

## Assignment Contracts

### Beam Shape Architecture: Define the Analytic Optics Contract

- Owner/thread: `/root/beam_shape_architecture`
- Base commit/state: exact resumed candidate recorded above
- Read scope: flashlight runtime, PBR CPU/HLSL paths, Donut light/shadow
  contracts, packaging, tests, and relevant documentation
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, GPU, UI, and
  external state
- Interface contract: preserve ordinary lights, all active PBR paths, one
  shadow geometry render, exact-off behavior, and directional visibility
- Deliverable: minimum coherent math and CPU/HLSL contract, exact insertion
  points, orientation and frustum treatment, cost, tests, and risks
- Done when: implementation can proceed without guessing about bindings,
  profile math, or fallback behavior
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: the design requires Donut edits, a general light ABI
  expansion, bitmap/licensed assets, or an active-task collision

### Beam Shape UI and Tests: Define User Semantics and Evidence

- Owner/thread: `/root/beam_shape_ux_tests`
- Base commit/state: exact resumed candidate recorded above
- Read scope: Lights UI, settings helpers, reset/lifecycle, focused tests,
  documentation, and UI conventions
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, GPU, UI, and
  external state
- Interface contract: shape and softness are distinct, every setting has a
  deterministic default/range/reset, and the circle endpoint matches the
  existing presentation
- Deliverable: labels, defaults, tooltips, mode ownership, reference/source
  tests, runtime matrix, and documentation changes
- Done when: every visible setting and clarified user outcome has an evidence
  path
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: a material visible tradeoff requires user preference or
  the control cannot be effective in every claimed path

## Integration Order

1. Freeze shape math, orientation, data transport, shadow, and UI semantics.
2. Add deterministic helper/reference tests.
3. Implement settings, runtime data, and all active PBR evaluations.
4. Update the purpose-built Flashlight UI, reset/lifecycle, and documentation.
5. Build and run focused then full production/factory checks.
6. Freeze writes for independent rendering review and repair findings.
7. Run serialized exact-build visual smoke, repository checks, and archive.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Shape interpolation | Reference values, symmetry, monotonic corners, exact circle endpoint | Flashlight helper tests | Pass; runtime confirmed circle at `1.00`, intermediate corners at `0.67`, and a softly rounded near-square at `0.00` |
| Shape/softness UI | Labels, ranges, tooltips, reset, no generic controls | UI source-contract tests | Pass; runtime confirmed independent `0.00` and `1.00` Edge Softness at fixed near-square shape plus per-control reset |
| PBR parity | Forward, deferred, and MSAA consume identical shape data/equation | Lighting and renderer source contracts | Pass |
| Shadow coherence | Corners stay inside the shared frustum and light/shadow pose matches | Source contracts and wall/column smoke | Pass; simple and realistic rounded-square beams remained aligned across wall, column, and camera-turn views without a visible snap |
| Lifecycle safety | Reset, recreate, resize, PBR/off/shadow/benchmark gates | Helper and renderer tests | Pass; automated contracts passed and runtime confirmed F-key exact off, smooth return, and stable camera correction |
| Packaging | Production and factory bundle topology | Release builds and bundle tests | Pass; production 76 and factory 39 runtime blobs |
| Repository quality | Full CTest, independent review, Title Case, counts, diff audit | Repository commands | Pass; production 29/29, factory 29/29, no P0-P2 review findings, 1,005 Title Case headings checked, README counts verified, and exact-build runtime completed |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Treat the user's clarification as a footprint-shape correction, not a cosmetic slider rename. | The current circular spotlights cannot produce a square footprint; retaining the existing equation would leave the requested behavior inert. | Whole follow-up |
| 2026-07-30 | Separate Beam Roundness from Edge Softness. | Aperture geometry and cutoff falloff are perceptually independent, and overloading one slider cannot provide both outcomes predictably. | Settings, UI, optics, and tests |
| 2026-07-30 | Prefer an analytic softened aperture over a bitmap cookie. | It is deterministic, resolution-independent, asset-free, smoothly parameterized, and can preserve a mathematically exact circle endpoint. | Architecture and packaging |
| 2026-07-30 | Expose the public camera light under the exact stable identifier `flashlight_1`. | The user clarified the final cross-task naming intent after an accidental retraction. The primary leaf, node, dropdown, and future slash discovery/selection share this identifier; the internal hotspot stays private and `F` stays the toggle. | Runtime naming, UI, tests, and future command integration |
| 2026-07-30 | Use a tagged flashlight-only superellipse with exponent `p = 2^(1 + 3(1 - roundness))`. | This gives an exact `p=2` circle at roundness 1 and a visibly square but still rounded `p=16` endpoint at 0. The exact circle branch retains the current cosine spotlight equation; a hard rectangle, texture cookie, or linear circle/square blend was rejected. | Optics and tests |
| 2026-07-30 | Transport the exponent in a remote negative `LightConstants.radius` tag and the resolved right axis in `shadowChannel.yzw` through a first-party `FlashlightSpotLight` override. | These fields are unused by UVSR PBR for spotlight area shape and by every current consumer except `shadowChannel.x`. The override preserves Donut ownership, ordinary spotlights, struct layout, bindings, and shader registries while providing a camera-stable basis. | CPU/HLSL contract and future semantic merge |
| 2026-07-30 | Publish the same full direction/right/up basis to both flashlight nodes, packed light constants, and the shared square shadow projection. | Direction alone cannot preserve housing roll and can misalign a square aperture with its shadow frustum. A full orthonormal basis keeps the footprint, hotspot, and one rendered shadow map concentric during sway and correction. | Motion, shadows, and both lobes |

## Mandatory New-Element Intake

- Reference reconciliation:
  - Current UI reference: `2026-07-30.1`
  - Current active-worktree project policy: `2026-07-22.1`
  - The intake checklist contains an older literal `2026-07-29.3`; the
    version printed below the document title is authoritative and no cached
    older guidance is being used.
- Owner: top-level **Lights** drawer and its purpose-built Flashlight surface.
- Controls:
  - **Beam Roundness** is an immediate normalized slider, default and reset
    `1.0`, with `0` described as a softly rounded square and `1` as the exact
    circular beam.
  - **Edge Softness** is an immediate normalized slider, default and reset
    `0.60`, retaining the existing inner-to-outer lens falloff behavior.
- IDs and presentation: existing `DrawSliderFloat` and trailing
  `DrawPresetResetIcon` paths own unique visible labels and reset IDs. No raw
  ImGui primitive or new body ID is needed.
- Direct consumers: `FlashlightSettings`, sanitation, lobe resolution,
  flashlight presentation, flashlight-only light-constant encoding, and the
  shared PBR spotlight sampler.
- Derived consumers: simple spill, realistic spill and hotspot, forward PBR,
  deferred PBR, deferred PBR MSAA, the shared shadow frustum, reset, scene
  recreation, tests, and documentation.
- Consumer classification: both controls are value-only; they do not change
  row structure, allocation, render-target topology, pass ownership, or shader
  permutation.
- Animation and scroll ownership: the existing **Lights** drawer owns the two
  fixed rows. Neither control owns a gated region or structural transition.
- Renderer cost: no allocation, binding, history, or geometry-render change.
  A tagged flashlight lobe performs the analytic aperture evaluation during
  its existing local-light sample; realistic mode evaluates its existing second
  lobe. Ordinary lights bypass the new math.
- Reset behavior: each reset restores only its owning setting; footer Reset
  restores the complete `DefaultFlashlightSettings` snapshot.

## Progress and Handoffs

| Date | Task Or Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | `/root` | Active | Follow-up plan and preflight | Dirty candidate, worktrees, active plans, and Coming Soon inspected | Await read-only architecture and UI/test contracts |
| 2026-07-30 | `/root/beam_shape_ux_tests` | Complete | Read-only handoff | UI, math, tests, docs, and runtime matrix inspected | Recommended global Beam Roundness plus independent Edge Softness; ownership released |
| 2026-07-30 | `/root/beam_shape_architecture` | Complete | Read-only handoff | Donut, PBR, shadow, transform, and packaging contracts inspected | Recommended tagged exponent, packed right axis, exact circular fallback, and no ABI/resource change; ownership released |
| 2026-07-30 | `/root` | Complete | Production `build-flashlight-beam-shape` and factory `build-flashlight-ambient-fill-factory` | Both Release builds passed; both full suites passed 29/29; counts, Title Case, and diff checks passed | Serialized exact-build runtime smoke remained |
| 2026-07-30 | `/root` | Complete | `build-flashlight-beam-shape\bin\uvsr.exe`, SHA-256 `4F4E4A65C05B5A57A5DCA08C57BDBEA5A40E61331C4D14347F00B2051DB8EE52`, PID 32812, title `UVSR Renderer D3D12 (beamshape-3ee4ceb-1722)` | DX12 PBR Sponza Decorated at Benchmark Position 1: Ambient Fill off; roundness `1.00`, `0.67`, and `0.00`; softness `0.00`, `1.00`, and reset `0.60`; realistic mode; camera turns; F off/on | Passed; exact task-owned process closed and no `uvsr.exe` remained |
| 2026-07-30 | `/root/beam_shape_review` | Complete | Frozen dirty candidate | No P0-P2 findings across transport, HLSL, basis/shadow, naming, gates, tests, and docs | Independent review complete; ownership released |

## Risks and Escalation Triggers

- A rounded-square local-light profile needs orientation around the flashlight
  axis; deriving it unstably would make the footprint roll or flip.
- Existing spot attenuation and shadow projection are radial; a shape override
  must not leave circular clipping before the rounded-square corners.
- Any packed-field reuse must be explicit, finite, collision-free for ordinary
  spotlights, and identical across active PBR paths.
- HLSL, `src/uvsr.cpp`, CMake, and documentation overlap with independent local
  tasks and require future semantic integration rather than textual assumptions.

Stop and ask the user if:

- Realism requires a bitmap cookie, licensed asset, or a choice between a fixed
  optical orientation and camera roll behavior.
- The only safe path requires editing Donut, changing general scene-light
  behavior, or accepting a material quality/performance tradeoff.
- Completion requires a commit, push, pull request, merge, release, or
  deployment.

## Completion

- Final integrated commit: intentionally none unless separately requested
- Verification summary: production and factory Release builds passed; both full
  CTest suites passed 29/29; production packaged 76 runtime shader blobs and
  factory packaged 39; exact production DX12 smoke passed the circle,
  intermediate, near-square, independent-softness, realistic-lens,
  camera-tracking, shadow, Ambient Fill, and F-key checks.
- Exact candidate:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\flashlight-ambient-fill\build-flashlight-beam-shape\bin\uvsr.exe`,
  SHA-256
  `4F4E4A65C05B5A57A5DCA08C57BDBEA5A40E61331C4D14347F00B2051DB8EE52`.
- Independent review: complete with no P0-P2 findings.
- Coming Soon and documentation update: complete.
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: future semantic integration must
  preserve the command-bar F-key capture contract and unchanged three-slot
  directional-visibility ABI; no additional work remains in this local scope.
- Active ownership released: yes
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/flashlight-beam-shape.md`
