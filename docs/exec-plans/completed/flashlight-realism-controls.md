# Flashlight Realism Controls

## Status

- State: complete
- Coordinator: `/root`
- Project branch and worktree: `codex/flashlight-ambient-fill` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\flashlight-ambient-fill`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Resumed local-candidate identity: tracked diff
  `67da71b95dd2a07ea247db7d47ffde87ba1f6a5e`, plus the untracked files
  recorded under **Baseline**
- Prior plan:
  `docs/exec-plans/completed/flashlight-ambient-fill.md`
- Started: 2026-07-30
- Last updated: 2026-07-30
- Completed: 2026-07-30
- Planned archive:
  `docs/exec-plans/completed/flashlight-realism-controls.md`

## Goal and Done Condition

Goal:

Make the UVSR camera flashlight selectable and purposefully configurable in the
existing **Lights** drawer, and add an optional physically motivated lens and
hand-motion presentation that looks more like a handheld flashlight while
preserving the simple original mode.

Done when:

- [x] The **Select Light** dropdown includes a clearly named **Flashlight**
      entry without exposing controls that camera tracking immediately
      overwrites.
- [x] Flashlight-specific controls include beam size, edge roundness or
      softness, brightness, range, color, shadow enablement, and explicit reset
      behavior. Emitter size remains internal because current PBR paths would
      not make that control consistently visible.
- [x] A **Realistic Flashlight** toggle enables a lensed-beam profile plus
      configurable sway and stabilization/correction while the original simple
      spotlight remains available when disabled.
- [x] Camera-relative motion is frame-rate-independent, bounded, deterministic,
      and does not drift or affect the settled-off zero-work path.
- [x] Deferred PBR, local-light shadows, Ambient Fill isolation, command-bar
      input capture, factory reset, resize/recreation, and factory shader
      topology remain safe.
- [x] Focused tests, production and factory Release builds, both full CTest
      suites, exact-build DX12 visual smoke, documentation checks, and
      independent rendering review pass.

## Scope

In scope:

- Flashlight-specific state and deterministic lens/motion helpers.
- Flashlight selection and controls in the existing Lights drawer.
- The smallest first-party CPU/HLSL extension needed for a lensed angular beam
  profile, if existing spotlight parameters cannot express it faithfully.
- Camera-relative bounded sway, correction lag, and reset/scene lifecycle.
- Focused tests and durable renderer/user documentation.

Non-goals:

- A general light-cookie system, bitmap lens assets, volumetric scattering,
  flashlight mesh or hands, battery simulation, persistence across sessions,
  a general scene-light refactor, or performance claims.
- Donut edits, directional-visibility ABI changes, new backends, publication,
  or integration of the independent UI and cleanroom branches.

Affected subsystems and paths:

- `src/flashlight.h`
- `src/uvsr.cpp`
- Potential first-party PBR lighting CPU/HLSL contract files after architecture
  review
- Focused tests, `CMakeLists.txt` only if shader registration changes, README,
  user documentation, and this plan

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, CPU/HLSL light contracts, shader registries, `CMakeLists.txt`,
  README, shared documentation, build trees, renderer process, and GPU

## Baseline

- Canonical repository/remote: base commit `3ee4ceb`; no remote publication is
  requested.
- Local versus remote state: local feature branch with the uncommitted,
  Technically verified Flashlight and Ambient Fill candidate.
- Verified source/build: prior local candidate passed production and factory
  Release builds, 29/29 tests in each topology, exact-build DX12 runtime smoke,
  independent review, and repository checks.
- Resumed untracked identities:
  - `src/flashlight.h`:
    `8114d98a7e36bf77025d313265b722ec04d12f6a7e6e40c9c5e46e317a1295d1`
  - `tests/flashlight_tests.cpp`:
    `72fd5c0d0e75ed542de4947afc543f21b0c28435323a1514e7dc353d7ba1220c`
  - Prior completed plan:
    `fed945a05ceccb98140b5f1dbd9a8d262bec70f705e2b65288a0320db773c21a`
- Runtime preset: PBR Sponza Decorated, Benchmark Position 1, deferred PBR,
  1920x1080 internal resolution, serialized DX12 UI/GPU lease.
- Known pre-existing failures: none in the resumed candidate.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior flashlight candidate | Exact resumed diff identity above | Selected | Whole follow-up |
| UI command-bar contract | All open-bar keyboard/character input is consumed before shortcuts | Confirmed externally | `F` input |
| Cleanroom visibility contract | Three unchanged full-resolution `R8_UNORM` directional slots; neutral screen-space-shadow build names | Confirmed externally | Future semantic merge |
| Donut local light | SpotLight plus PlanarShadowMap remains the geometry-shadow carrier | Integrated | Flashlight |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- The flashlight remains one synthetic UVSR editor selection backed by one
  ordinary spotlight in simple mode and one additional concentric hotspot only
  while realistic mode is enabled.
- Camera position remains authoritative; realistic motion may offset or filter
  flashlight aim only and must converge back to the camera aim.
- Settled off remains exact zero intensity with no flashlight shadow submission
  or sampling.
- The three directional visibility slots and deferred-lighting ABI remain
  unchanged.
- Any new lensed-beam data must use a first-party contract with an explicit
  ordinary-spotlight fallback and matching production/factory packaging.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| optics-architecture | `/root/lighting_architecture` | Shared read-only | Resumed candidate | None | Current source and Donut contracts | Complete |
| ui-verification-map | `/root/verification_design` | Shared read-only | Resumed candidate | None | Current UI/tests and external UI contract | Complete |
| implementation | `/root` | Feature worktree | Resumed candidate | All task paths and this plan | Explorer handoffs | Complete |
| independent-review | `/root/flashlight_render_review` | Shared read-only after freeze | Final dirty candidate | None | Implementation and checks | Complete |

## Assignment Contracts

### Optics Architecture: Map the Lens and Motion Path

- Owner/thread: `/root/lighting_architecture`
- Base commit/state: exact resumed candidate recorded above
- Read scope: flashlight helper/runtime, Donut light constants, PBR local-light
  evaluation, local shadows, camera APIs, and relevant tests
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, GPU, and
  external state
- Interface contract: preserve ordinary spotlights, directional visibility,
  exact-off behavior, and Donut ownership
- Deliverable: recommended settings model, lensed angular profile, bounded
  frame-rate-independent sway/correction algorithm, exact insertion points,
  and contract risks
- Done when: one minimal implementation can be selected without speculative
  infrastructure
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: realism needs Donut edits, an asset/license choice, or a
  deferred-light ABI expansion

### UI and Verification Map: Define Controls and Evidence

- Owner/thread: `/root/verification_design`
- Base commit/state: exact resumed candidate recorded above
- Read scope: Lights drawer, reset/lifecycle, input routing, UI animation and
  deferred mutation conventions, focused tests, CMake packaging, and docs
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, GPU, and
  external state
- Interface contract: Flashlight must be selectable, purpose-built controls
  must not be overwritten, every control has deterministic default/reset
  behavior, and open command-bar input still wins
- Deliverable: control taxonomy, exact UI seams, focused test gaps, build/test
  commands, and runtime acceptance matrix
- Done when: each requested control and realism behavior has an evidence path
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: the independent UI branch requires an unavailable helper
  or competing visible behavior

## Integration Order

1. Freeze the configurable state, lens profile, and motion contracts.
2. Add deterministic helper/reference tests.
3. Implement state, runtime transform and light presentation.
4. Integrate the purpose-built Lights drawer controls and reset/lifecycle.
5. Add any required first-party shader contract and packaging changes.
6. Build and run focused then full production/factory checks.
7. Run serialized exact-build visual smoke and independent review.
8. Repair findings, update documentation, and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Configurable settings | Boundary/default/reset tests plus Lights drawer source contract | Focused flashlight and UI tests | Tests pass; exact-build selection, settings, reset icons, and global reset verified |
| Lensed beam | Finite angular-profile reference tests and visible hotspot/soft edge comparison | Focused helper test and exact-build capture | Tests pass; centered hotspot, broad spill, beam width, roundness, and brightness verified |
| Sway and correction | Partition-invariant bounded convergence tests plus exact-build realistic-mode smoke | Focused helper test and runtime sequence | Determinism, bounds, convergence, phase continuity, and exact-zero behavior pass; realistic mode is stable in the exact build |
| Shadow and off safety | Resource/order contracts and settled-off runtime | Renderer contract test and smoke | Contracts pass; F animation, exact off, shared shadow presentation, and Cast Shadows control verified |
| Packaging | Production and factory shader bundles | Release builds and bundle tests | Production and factory Release builds pass |
| Repository quality | Full CTest, independent review, Title Case, line counts, diff audit | Repository commands | Both full suites pass 29/29; independent review reports no unresolved P0-P2; repository checks pass |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Resume on the exact dirty local candidate and preserve the historical completed plan. | The user extended the accepted local feature; mutating its archived evidence or discarding the verified candidate would lose provenance. | Whole follow-up |
| 2026-07-30 | Treat size as cone width and roundness as the soft-edge/lens profile. | These map to visible flashlight characteristics and avoid an ambiguous geometry-only control. | UI and optics |
| 2026-07-30 | Use a second concentric hotspot only behind the optional realistic toggle, which defaults off. | Two ordinary spotlights preserve forward and deferred behavior without a Donut edit, packed-field sentinel, or ABI change. Realistic mode intentionally costs one extra prioritized light and shadow sample; simple mode retains the verified one-light path and cost. | Optics, shadows, and performance |
| 2026-07-30 | Keep the existing CPU/HLSL light ABI and render one shared shadow map from the broad lobe. | Donut already supports the required spotlight cones in every active PBR path. A new cookie, packed-field sentinel, or second map would add binding, packaging, or geometry cost without being necessary for the requested presentation. | Optics, packaging, and semantic merge |
| 2026-07-30 | Wrap deterministic sway at its shared `20*pi`-second phase period. | The first independent review found that the original 120-second wrap did not align the four sine phases and would visibly snap every two minutes. The shared analytic period preserves continuity and has a boundary regression test. | Motion and independent review |
| 2026-07-30 | Make the flashlight explicitly PBR-only and exact-zero when PBR is disabled. | Final review found that Donut's legacy shaders treat spot angles as half angles while UVSR PBR and the shadow projection use full apex angles. Gating avoids a wider unshadowed legacy skirt without editing Donut or changing every scene spotlight. | Cross-path correctness and UI |
| 2026-07-30 | Keep the flashlight shadow near plane inside the minimum post-collision emitter clearance. | Review found that the former 8 cm plane could clip a wall still legal 5 cm ahead of the forward-offset emitter. A 3 cm fixed floor and `0.25` collision-radius scale retain depth precision while keeping the closest legal occluder in the map. | Near-contact shadow quality |

## Progress and Handoffs

| Date | Task Or Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | `/root/lighting_architecture` | Complete | Read-only handoff | Source and Donut contracts inspected | Recommended opt-in concentric hotspot, shared broad-lobe map, and bounded correction; no ABI change |
| 2026-07-30 | `/root/verification_design` | Complete | Read-only handoff | UI, lifecycle, tests, and packaging inspected | Control taxonomy and exact-build acceptance matrix incorporated |
| 2026-07-30 | `/root/flashlight_render_review` | Complete | Final dirty-candidate review | No unresolved P0-P2 findings after phase-wrap, PBR-gate, near-contact, documentation, and test repairs | Ownership released |
| 2026-07-30 | `/root` | Complete | Dirty implementation candidate | Production and factory Release builds pass; both full suites pass 29/29; repository checks pass | Exact production DX12 smoke verified selection, controls, simple/realistic optics, F animation, shadows, and reset; renderer lease released |

## Risks and Escalation Triggers

- A first-party lensed-beam extension must not silently alter ordinary scene
  spotlights or the cleanroom directional-visibility contract.
- Camera smoothing must not introduce time-step dependence, unbounded drift,
  horizon bias, or shadow/light transform mismatch.
- Shader and `src/uvsr.cpp` overlaps with independent local tasks require a
  future semantic merge; this task does not integrate or publish them.

Stop and ask the user if:

- Realistic lens behavior requires a bitmap cookie or licensed asset choice.
- The only safe implementation requires editing Donut, changing a shared
  deferred-light ABI, or accepting a material quality/performance tradeoff.
- Completion requires a commit, push, pull request, merge, or release.

## Completion

- Final integrated commit: intentionally none unless separately requested
- Final dirty-candidate identity:
  - Tracked diff SHA-256:
    `ab6ff365425b2eb31c814483ce7f651549bf5cb047a4ed3e35ab4fc0a82d8641`
  - `src/flashlight.h` SHA-256:
    `7345a8d565f47d13035036ecc68f1346dbfa6734f7b400bec3437e1c6b4176a7`
  - `tests/flashlight_tests.cpp` SHA-256:
    `13fc38ac1d8b536c0ff74d2317891868b2615f0e5203a6e9139591f104028ece`
- Verification summary: production and factory Release builds pass; both full
  CTest suites pass 29/29; focused contracts, title validation, line-count
  validation, and diff audit pass. Exact production DX12 smoke verified the
  purpose-built Flashlight selection, F synchronization and animation,
  simple/realistic lens presentation, settings, shadows, per-setting reset, and
  global reset. The retained Ambient Fill gate is covered by dedicated
  reference/source contracts and its prior exact-build smoke.
- Independent review: complete with no unresolved P0-P2 findings
- Coming Soon and documentation update: complete
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: future UI-skins and cleanroom
  integrations must preserve the coordinated input and directional-visibility
  contracts
- Active ownership released: yes
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/flashlight-realism-controls.md`
