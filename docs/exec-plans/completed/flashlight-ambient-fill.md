# Flashlight and Ambient Fill

## Status

- State: completed
- Coordinator: `/root`
- Project branch and worktree: `codex/flashlight-ambient-fill` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\flashlight-ambient-fill`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Started: 2026-07-30
- Last updated: 2026-07-30
- Planned archive:
  `docs/exec-plans/completed/flashlight-ambient-fill.md`

## Goal and Done Condition

Goal:

Add a camera-mounted flashlight that toggles with plain `F`, visibly animates
on and off, lights scene geometry with a realistic finite cone and falloff, and
casts geometry shadows through the renderer's supported local-light path. Add
an independent **Ambient Fill** toggle to the **Sky** settings section so AO can
be inspected without indirect sky fill.

Done when:

- [x] Plain `F` toggles the flashlight without leaking through text-entry UI.
- [x] The flashlight uses a finite spotlight cone, distance attenuation,
      physically plausible color and intensity, and a short monotonic
      power/beam transition in both directions.
- [x] Flashlight direct light affects deferred PBR geometry and is occluded by
      scene geometry through the safest supported local-shadow implementation.
- [x] **Sky > Ambient Fill** independently gates environment diffuse/specular
      fill without disabling the visible sky, direct sun, emissive lighting,
      or mutating AO settings. Unused AO work may be culled when no indirect
      consumer remains.
- [x] Factory reset, resize, resource recreation, and disabled paths remain
      deterministic and safe.
- [x] Targeted tests, Release build, full available CTest, Title Case audit,
      diff checks, runtime smoke, and independent rendering review pass or are
      reported precisely.

## Scope

In scope:

- Camera-mounted spotlight state, `F` input, and frame-rate-independent
  on/off animation.
- Deferred PBR local-light evaluation with finite cone and range attenuation.
- A shadow map or existing equivalent geometry-visibility path for the single
  flashlight.
- Sky-section ambient-fill toggle and the smallest shader/CPU gate needed to
  remove environment illumination.
- Focused deterministic tests and user-facing controls documentation.

Non-goals:

- A general dynamic-light editor, arbitrary light arrays, volumetric fog, lens
  flare, flashlight mesh/hands, battery simulation, cookies, or persistence.
- Changes under `donut/`, new graphics backends, unrelated UI redesign, shadow
  quality tradeoffs outside the flashlight, or publication.

Affected subsystems and implemented paths:

- `src/uvsr.cpp` for ownership, input, animation, settings, and render ordering.
- `src/image_based_lighting_shared.h` for the ambient-fill contribution gate.
- Existing Donut spotlight and local-light shadow evaluation, with one
  first-party PlanarShadowMap depth submission in `src/uvsr.cpp`.
- `CMakeLists.txt`, focused tests, and user documentation. No deferred-light
  HLSL binding or `src/shaders.cfg` change was required.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`
- Deferred-lighting CPU/HLSL bindings and resource layouts
- `CMakeLists.txt`
- `src/shaders.cfg`
- `README.md` and shared user documentation
- Build tree, renderer window, and GPU

## Baseline

- Canonical repository/remote: local `origin/main`
  `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Local versus remote state: the original checkout's `main` is clean but one
  commit ahead and six commits behind its tracking ref; it is preserved
  untouched. This task uses a new linked worktree at the tracking commit.
- Verified source commit/build: the local dirty candidate based on `3ee4ceb`
  passed production and factory Release builds and both 29-test suites. It is a
  Technically verified local candidate, not a Canonical verified checkpoint.
- GPU, scene, camera, resolution, and settings preset when relevant: default
  scene plus PBR Sponza Decorated at Benchmark Position 1, 1920x1080, deferred
  PBR defaults for repeatable runtime inspection.
- Known pre-existing failures: none encountered. A task-introduced DX12 device
  removal from the generic planar-shadow clear was repaired with an explicit
  depth-stencil clear and reverified.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical renderer source | `3ee4ceb` | Selected | Whole feature |
| UI command-bar input contract | Text entry consumes keys before app shortcuts; plain `F` remains free when closed | Confirmed by active UI task | Flashlight input |
| Directional visibility producers | Remain directional-light-only and pointer/exact-light safe | Preserved | Lighting integration |
| Local-light shadow support | Donut SpotLight evaluation plus UVSR-owned PlanarShadowMap and DepthPass | Integrated | Flashlight visibility |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- The flashlight is one UVSR-owned camera spotlight, not a general scene-light
  collection.
- `F` toggles the target state; animation progress, not input repetition,
  determines the emitted intensity and beam transition.
- Environment ambient fill has one explicit enable bit whose disabled value
  contributes exactly zero environment diffuse and specular light while the sky
  background remains rendered.
- Existing directional visibility slots and producers remain directional-only;
  flashlight visibility must not be inserted into an exact-light slot unless
  the light identity and projection contract match.
- The persistent flashlight shadow resource may remain allocated, but the
  settled-off path must not submit the flashlight, rasterize its shadow map, or
  sample stale shadow data.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| lighting-architecture | `/root/lighting_architecture` | Shared read-only | `3ee4ceb` | None | Current source | Complete |
| input-ui-architecture | `/root/input_ui_architecture` | Shared read-only | `3ee4ceb` | None | Current source and UI-task contract | Complete |
| verification-design | `/root/verification_design` | Shared read-only | `3ee4ceb` | None | Current tests/docs | Complete |
| implementation | `/root` | Feature worktree | `3ee4ceb` | All task files and this plan | Explorer handoffs | Complete |
| independent-review | `/root/flashlight_render_review` | Frozen feature worktree | Dirty candidate | None | Implementation freeze | Complete; findings resolved |

## Assignment Contracts

### Lighting Architecture: Map the Safe Rendering Path

- Owner/thread: `/root/lighting_architecture`
- Branch/worktree: shared feature worktree, read-only
- Base commit/state: clean `3ee4ceb`
- Read scope: first-party renderer, PBR deferred pass/shader, existing shadow
  systems, scene light APIs, build registration, and pinned Donut APIs when
  available
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, and external
  state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: canonical source base
- Interface/invariant contract: preserve directional visibility identity and
  recommend the smallest complete single-spotlight shadow path
- Deliverable: exact call graph, resource/binding changes, shadow recommendation,
  risks, and test seams
- Done when: the implementation path is specific enough to assign one writer
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: a complete local-light shadow requires Donut changes or a
  renderer-wide light system

### Input and UI Architecture: Map Controls and State

- Owner/thread: `/root/input_ui_architecture`
- Branch/worktree: shared feature worktree, read-only
- Base commit/state: clean `3ee4ceb`
- Read scope: `src/uvsr.cpp`, UI helpers, settings reset, input dispatch,
  animation utilities, Sky section, and related tests/docs
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, and external
  state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: UI task's documented command-bar input
  contract
- Interface/invariant contract: plain `F` only, no text-entry leakage, ambient
  control under Sky, factory-default state explicit
- Deliverable: exact insertion points, reusable conventions, conflicts, and
  deterministic test seams
- Done when: input/UI/reset behavior is fully mapped
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: `F` already has a product binding or settings mutations
  require a new shared UI contract

### Verification Design: Define Evidence and Test Gaps

- Owner/thread: `/root/verification_design`
- Branch/worktree: shared feature worktree, read-only
- Base commit/state: clean `3ee4ceb`
- Read scope: tests, CMake targets, shader bundle checks, launch scripts,
  documentation validators, and existing lighting/AO contracts
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, and external
  state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: user acceptance criteria and source base
- Interface/invariant contract: distinguish deterministic proof, runtime smoke,
  visual evidence, and performance evidence
- Deliverable: ordered commands, focused test recommendations, and failure
  interpretation
- Done when: every acceptance criterion has an evidence path
- Required verification: source and build-script inspection only
- Allowed Git and external actions: read-only
- Stop and report if: required runtime evidence would need unsafe or unmatched
  benchmarking

## Integration Order

1. Freeze the flashlight, ambient-fill, and local-shadow contracts from source
   inspection.
2. Add deterministic CPU policy tests before or with the runtime state model.
3. Add the ambient-fill gate and spotlight lighting evaluation.
4. Add or reuse the spotlight geometry-shadow path and resource lifetime.
5. Integrate input, animation, settings UI, and documentation.
6. Build and run automated checks.
7. Run serialized runtime smoke and visual inspection.
8. Freeze the candidate for independent rendering review.
9. Repair task-introduced findings, rerun affected evidence, and archive this
   plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Deterministic toggle and animation | Boundary, reversal, monotonicity, and frame-step tests | Focused CPU test | Passed |
| Finite spotlight lighting | Cone, range, energy, normal, and disabled-path tests plus shader compile | Focused PBR test and Release build | Automated checks passed; runtime beam visible |
| Geometry shadows | Resource/projection review and visible occluder matrix | Runtime fixed-camera captures | Explicit depth submission and light/map association passed review; the exact build showed finite-cone pillar and arch occlusion |
| Ambient fill isolation | Exact zero-fill shader/CPU contract and AO/direct-light preservation | Focused PBR test and runtime comparison | Common background radiance stayed independent in the tested contract; exact-build comparison removed IBL while direct flashlight lighting survived |
| Input and reset safety | Key-repeat/text capture/reset/resize inspection | Focused test and runtime smoke | Source contracts passed; exact-build `F`, settled off, and factory Reset behavior passed |
| Build and packaging | All required shader blobs staged | Release build and bundle tests | Final production 76-blob and factory 39-blob builds passed |
| Repository quality | Full tests, Title Case scan, whitespace and diff audit | CTest and repository scripts | Both 29/29 suites, Title Case self-test and 966-item scan, README counts, and diff check passed |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Model the flashlight as a spotlight rather than a point light. | A flashlight emits a forward cone with finite range; an omnidirectional point source would illuminate behind the camera and look physically wrong. | Lighting and shadows |
| 2026-07-30 | Isolate the task from local `main` on `3ee4ceb`. | Local `main` is a clean but divergent README lineage; the worktree preserves it and avoids concurrent task collisions. | Whole feature |
| 2026-07-30 | Preserve directional shadow/visibility producers as directional-only. | Their exact-light and projection contracts must not be repurposed for a local spotlight. | Lighting integration |
| 2026-07-30 | Keep the visible sky independent from ambient fill. | The requested AO comparison needs environment illumination off without turning the background black or changing direct sun behavior. | Sky and deferred lighting |
| 2026-07-30 | Clear the flashlight map explicitly as depth-stencil. | Donut's generic PlanarShadowMap clear uses a color clear for this depth resource and caused DX12 device removal; UVSR now selects the matching depth clear without modifying Donut. | Flashlight shadows |
| 2026-07-30 | Exclude the UVSR-owned flashlight from the general Lights drawer. | Its camera transform and animated intensity are not general editable scene-light state; exposing partial controls produced inert and non-resettable settings. | Settings UI |

## Progress and Handoffs

| Date | Task Or Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | Architecture explorers | Complete | Read-only handoffs | Rendering, input/UI, and evidence paths mapped | Contracts integrated |
| 2026-07-30 | `/root` | Complete | Dirty local candidate | Final production and factory Release builds and 29/29 suites passed; exact-build DX12 smoke covered default, on, off, Ambient Fill isolation, and Reset | Local handoff only |
| 2026-07-30 | Independent reviewer | Complete | Read-only candidate review | Critical depth-clear fix confirmed; Lights drawer and documentation findings reported | Findings repaired and affected evidence rerun |

## Risks and Escalation Triggers

- The single camera light stays first-party and must not become a general light
  editor or require a Donut modification.
- The persistent shadow resource is separate from animated submission; settled
  off must remain an exact zero-work render/sample path.
- The active UI and cleanroom tasks will eventually require semantic integration
  across overlapping `src/uvsr.cpp` and shader/build files.
- Cleanroom integration must preserve its neutral
  `UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS` target, shader configuration,
  shader filenames, staging directory, and unchanged three-slot directional
  visibility contract.

Stop and ask the user if:

- A realistic geometry-shadow path cannot be implemented without modifying
  Donut/NVRHI or accepting a material quality/performance tradeoff.
- The meaning of ambient fill cannot be isolated from the visible sky and direct
  sun without changing established product behavior.
- Completion would require a commit, push, pull request, merge, release, or
  publication.

## Completion

- Final integrated commit: intentionally none
- Verification summary: production and factory Release builds passed, both
  CTest suites passed 29/29, repository documentation and diff checks passed,
  and the exact production executable passed serialized DX12 visual smoke for
  flashlight on/off, Ambient Fill isolation, geometry response, and Reset.
- Independent review: complete; the depth-clear, Lights-drawer, documentation,
  and stale-plan findings were resolved and reverified.
- Coming Soon and documentation update: complete for this local candidate.
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: semantically integrate this local
  candidate with the independent UI-skins and cleanroom shader-renaming tasks
  when their coordinator combines the branches.
- Active ownership released: yes
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/flashlight-ambient-fill.md`
