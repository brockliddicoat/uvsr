# Main Lighting, Denoising, and Control Baseline

## Status

- State: completed local technically verified candidate
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/main-lighting-denoising-controls` in
  `C:/Users/brock/OneDrive/Documents/uvsr/work/main-lighting-denoising-controls`
- Base commit: `51bad6a7fccb88afd46078dcd95ef4bbdb509d7a`
- Started: 2026-08-06
- Last updated: 2026-08-06
- Planned archive:
  `docs/exec-plans/completed/main-lighting-denoising-controls.md`

## Goal and Done Condition

Goal: update the live main renderer baseline with the requested lighting and
sampling defaults, one authoritative ray traversal control, RT shadowed
flashlight behavior and positioning, a complete NVIDIA NRD denoising surface,
consistent loading progress, and removal of screen space directional shadows
from the production lineage while preserving them with the CSM/SVSM
experiments.

Done when:

- [x] Every requested default, range, and drawer control has focused source and
      behavioral coverage.
- [x] `Allow Ray Traversal` gates every current ray-query effect without
      discarding each effect's saved enable/configuration state.
- [x] The flashlight is a finite cone light consumed by the RT shadow path,
      supports horizontal and vertical offsets in `[-0.40, 0.40]` meters, and
      has no private shipping shadow system.
- [x] NRD can denoise AO, GI, shadows, and sky visibility through explicit
      per-signal choices with correct inputs, histories, resets, resize, and
      fail-open behavior.
- [x] AO, GI, sky visibility, and shadow producers each expose an explicit
      `Output Hit Distance` control. Disabled controls preserve the established
      raw output and avoid the hit distance production cost.
- [x] Sky visibility and sun shadows expose independent ratio estimator
      controls. Disabling either estimator produces the clean scalar signal and
      closest blocker distance required by the selected NRD method.
- [x] The shipping shadow drawer is named `Ray Traced Shadows`; new user facing
      labels and prose avoid compound word hyphenation.
- [x] Screen space directional shadows are absent end to end from this
      production branch and remain recoverable on the CSM/SVSM experiment
      lineage.
- [x] The second loading line always ends with a truthful progress fraction.
- [x] Both Release configurations, both full CTest matrices, exact shader and
      runtime packaging, document validators, diff checks, and independent
      high risk review pass for the exact candidate.
- [x] Runtime smoke passes for the exact NRD artifact after the unrelated UVSR
      window from `work/ray-traced-sky-visibility` releases the renderer and GPU
      lease.

## Scope

In scope:

- Sky visibility and specular IBL, diffuse, sun, and flashlight defaults and
  ranges.
- Representation level ray traversal policy and every current ray query caller.
- Flashlight light/shadow integration, offsets, beam angle, and roundness.
- NVIDIA NRD runtime integration and a dedicated `Denoising` drawer for AO,
  GI, RT shadows, and RT sky visibility.
- Explicit hit distance output and ratio estimator policy for every applicable
  producer.
- Loading screen second line progress formatting and estimation.
- Complete production removal and experimental preservation of screen space
  directional shadows.
- Focused tests, runtime shader/dependency packaging, durable documentation,
  roadmap reconciliation, and execution-plan lifecycle updates.

Non-goals:

- Changes under `donut/`.
- Restoring CSM or SVSM to production.
- New ray traced effects, speculative denoiser consumers, temporal upscaling,
  unrelated UI redesign, or release/publication work.
- Push, pull request, merge, deployment, or release.

Affected subsystems and paths:

- First-party renderer/UI/settings and lighting code under `src/`.
- First-party shaders, shader manifests, and runtime resource staging.
- `CMakeLists.txt`, dependency metadata, tests, `README.md`, and renderer docs.
- The CSM/SVSM preservation branch, through coordinator-owned local Git
  operations only after the exact preservation strategy is proven.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- `src/shaders.cfg`
- `src/uvsr.cpp`
- Global renderer settings, CPU/HLSL bindings, and runtime shader/package
  manifests
- Git refs, branches, worktrees, index, builds, tests, runtime, and GPU control
- This execution plan and the prior sky-visibility plan lifecycle

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, fetched live
  `origin/main` at `51bad6a7fccb88afd46078dcd95ef4bbdb509d7a`.
- Local versus remote state: the original `main` checkout is clean but diverged
  with two local-only commits and 26 fetched remote-only commits. It remains
  untouched. This isolated branch equals live `origin/main` at task start.
- Verified source commit/build: the preceding sky-visibility branch recorded a
  technically verified Release artifact before its merge; the fetched merge
  commit itself has not yet been reverified in this worktree.
- GPU, scene, camera, resolution, and settings preset when relevant: use the
  existing Sponza Benchmark Position 1 smoke configuration; performance claims
  are not in scope.
- Known pre-existing failures: none established on the fetched merge yet.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `51bad6a` | Integrated as base | Whole task |
| Prior sky-visibility work | Merged result and active-plan evidence | Audited; lifecycle closeout pending | Defaults and denoising |
| CSM/SVSM experiment | Preserve `codex/svsm-csm-preserved` at `f7c0c87` | Frozen | Shadow quarantine |
| Pinned Donut/NRD surface | NRD v4.17.3 `792eff1`, optional license gated build | Frozen | NRD integration |
| Ray query and flashlight contracts | One spot light, t20 flashlight, t21 sun, t22 sky | Integrated | Renderer integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `Allow Ray Traversal` is the authoritative runtime permission. Per-effect
  enable and quality settings remain intact while permission is off.
- `Output Hit Distance` is independently controllable for AO, GI, sky
  visibility, and shadows. A selected NRD method may require the corresponding
  output and must report that requirement in the drawer rather than silently
  fabricating a guide.
- Sky visibility and sun shadows retain independent ratio estimator controls.
  The scalar path supplies a matched raw signal and nearest blocker distance
  for clean NRD input; the ratio path remains available for raw comparison and
  raw rendering.
- AO retains its aggregate raw estimator and reports an equal sector measure
  expected first bounce guide over the exact same sector mask. GI reports an
  NRD luminance contribution weighted first bounce guide over its exact raw RGB
  contribution terms. Both explicitly declare that their guide matches the
  signal before NRD may consume it.
- Every disabled, unsupported, failed, or history-invalid denoising path must
  preserve the producer's established neutral/fail-open output.
- The flashlight is a finite local cone light. Its RT visibility must use the
  same scene representation and conservative origin-offset policy as the
  existing ray-traced shadow implementation while respecting its finite range.
- NRD signal contracts, bindings, history identity, and method mapping will be
  frozen from the pinned dependency audit before implementation.
- Screen space directional shadow implementation, shader, build, package, UI,
  tests, and docs leave production together; the preserved experiment retains
  an exact recoverable copy.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `audit-ui-defaults-quarantine` | `/root/ui_defaults_quarantine_audit` | Read-only shared source | `51bad6a` | None | Live base | Complete |
| `audit-ray-flashlight` | `/root/ray_flashlight_audit` | Read-only shared source | `51bad6a` | None | Live base | Complete |
| `audit-nrd-integration` | `/root/nrd_integration_audit` | Read-only shared source | `51bad6a` | None | Live base | Complete |
| `implement-settings-loading` | `/root/settings_loading_impl` | Shared task worktree | `51bad6a` plus plan | Six settings/policy headers or sources and five focused tests listed below | UI audit | Complete |
| `implement-flashlight-rt-pass` | `/root/flashlight_rt_impl` | Shared task worktree | `51bad6a` plus plan | Dedicated flashlight RT/profile files and focused reference test only | Ray/flashlight audit | Complete |
| `implement-nrd-backend` | `/root/nrd_backend_impl` | Shared task worktree | `51bad6a` plus plan | New backend, guide shaders, settings, notices, and focused backend tests only | NRD audit | Complete |
| `implement-hit-distance-producers` | `/root/hit_distance_impl` | Shared task worktree | `51bad6a` plus plan | Screen space visibility, sky visibility, Heitz producers and focused tests only | Frozen producer contract | Complete |
| `integrate` | `/root` | This worktree | `51bad6a` | Shared hotspots and composed task diff | All audits | Complete; superseded by the combined noise, shadow, and exposure candidate |
| `independent-review` | `/root/rendering_integration_review` | Read-only frozen candidate | Candidate snapshot | None | Integrated candidate | Complete; no P0 through P2 findings |

## Assignment Contracts

### Implement the NRD Backend

- Owner/thread: `/root/nrd_backend_impl`
- Branch/worktree: shared task worktree; no branch operations
- Base commit/state: `51bad6a` plus the frozen NRD audit
- Read scope: the historical prototype, official pinned NRD source, current
  first party renderer interfaces, and focused tests
- Write scope: new `src/denoiser_*`, `src/nrd_*`, and `src/denoising_*` files;
  `tests/denoiser_backend_tests.cpp`; `third_party/NRD.md`; and the NRD notice
  block in `THIRD_PARTY_NOTICES.md`
- No-touch scope: CMake, `src/uvsr.cpp`, existing render passes and settings,
  shared CPU/HLSL bindings, shader manifests, existing tests, Git, builds, and
  every file not named above
- Interface contract: one independent backend handle per AO, GI, sky, sun, and
  flashlight signal; ReBLUR AO, ReBLUR or ReLAX GI and sky, separate SIGMA sun
  and flashlight signal states; NRD off builds remain a zero allocation fail open
  stub; all methods default to None
- Required checks: source inspection only; coordinator compiles and runs tests
- Handoff: exact new public API, integration steps, assumptions, and ownership
  release

### Implement Hit Distance Producers

- Owner/thread: `/root/hit_distance_impl`
- Branch/worktree: shared task worktree; no branch operations
- Base commit/state: `51bad6a` plus the frozen producer contract
- Read scope: current AO/GI, sky, and Heitz source, shaders, settings, and tests
- Write scope: `src/screen_space_visibility*`,
  `src/ray_traced_sky_visibility*`, `src/heitz_ratio_estimator_shadows*`,
  `src/directional_shadow_settings.h`, and their focused existing tests
- No-touch scope: `src/uvsr.cpp`, PBR files, flashlight files, CMake, shader
  manifests, command catalog, shared documentation, Git, builds, and every file
  outside the listed producer families
- Interface contract: independent default false hit distance controls; default
  true sun and sky ratio controls; matched one ray scalar paths when ratio is
  off; physical R16 hit distance values `0`, nearest hit, and `65504`; AO and GI
  aggregate raw paths remain unchanged while matched first moment guides make
  their exact aggregate signals eligible
- Required checks: source inspection only; coordinator compiles and runs tests
- Handoff: exact result texture contracts, root integration steps, assumptions,
  and ownership release

### Audit UI, Defaults, Loading, and Shadow Quarantine

- Owner/thread: `/root/ui_defaults_quarantine_audit`
- Branch/worktree: read-only shared source
- Base commit/state: `51bad6a`
- Read scope: first-party source/tests/docs and relevant shadow Git history
- Write scope: none
- No-touch scope: all files, refs, indices, worktrees, builds, processes, and
  remotes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: live base
- Interface/invariant contract: enumerate exact settings/default/progress
  sources and the complete production directional-shadow footprint
- Deliverable: line- and symbol-specific implementation map and preservation
  recommendation
- Done when: every applicable request is mapped to code, tests, and risks
- Required verification: read-only source and Git inspection
- Allowed Git and external actions: read-only only
- Stop and report if: branch state differs, active ownership overlaps, or
  preservation would destroy or rewrite history
- Handoff revision/artifact: source and Git preservation map for `51bad6a` and
  `f7c0c87`
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Audit Ray Traversal and Flashlight Architecture

- Owner/thread: `/root/ray_flashlight_audit`
- Branch/worktree: read-only shared source
- Base commit/state: `51bad6a`
- Read scope: first-party source/shaders/tests/history and read-only pinned Donut
  APIs
- Write scope: none
- No-touch scope: all files, refs, indices, worktrees, builds, processes, and
  remotes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: live base
- Interface/invariant contract: one non-destructive permission gate for all
  ray queries; one finite flashlight light consumed by RT shadows
- Deliverable: exact caller/resource/binding map and minimal complete design
- Done when: CPU/UI/HLSL changes and tests are explicit and material ambiguity
  is surfaced
- Required verification: read-only source/history inspection
- Allowed Git and external actions: read-only only
- Stop and report if: safe integration requires modifying Donut or conflicts
  with another active writer
- Handoff revision/artifact: CPU, HLSL, resource, and finite light design map
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Audit NVIDIA NRD Integration

- Owner/thread: `/root/nrd_integration_audit`
- Branch/worktree: read-only shared source
- Base commit/state: `51bad6a`
- Read scope: dependency/build metadata, pinned Donut APIs, first-party
  producer/consumer paths, tests/docs, and official NVIDIA primary references
  when local sources are insufficient
- Write scope: none
- No-touch scope: all files, refs, indices, worktrees, builds, processes, and
  remotes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: live base
- Interface/invariant contract: current AO, GI, RT-shadow, and RT-sky signals
  must each have a concrete NRD method and complete input/history contract
- Deliverable: dependency/version/license result, method mapping, bindings,
  resets, packaging, UI, tests, risks, and minimal complete architecture
- Done when: implementation can proceed without guessing or dormant plumbing
- Required verification: local inspection and official primary references only
  if needed
- Allowed Git and external actions: read-only only
- Stop and report if: source acquisition/license is unclear, Donut must be
  modified, or a requested signal cannot satisfy NRD's required inputs
- Handoff revision/artifact: NRD v4.17.3 dependency, license, method, binding,
  history, and packaging design
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Integrate the Production Candidate

- Owner/thread: `/root`
- Branch/worktree: this task worktree
- Base commit/state: `51bad6a` plus accepted audit contracts
- Read scope: entire first-party repository and read-only dependency APIs
- Write scope: all task-owned first-party files and this plan
- No-touch scope: `donut/`, unrelated worktrees/branches, user-owned state,
  generated `build/` and `work/` artifacts, remotes, and publication
- Build directory and runtime/GPU/resource lease: task-local `build`; coordinator
  alone controls builds, tests, UVSR windows, and GPU work
- Dependencies already integrated: all audit handoffs before interface-sensitive
  edits
- Interface/invariant contract: the contracts listed above plus any audit
  decisions recorded in this plan
- Deliverable: composed, locally verified production candidate
- Done when: every done-condition row has exact evidence
- Required verification: focused and full Release checks, exact shader/package
  audit, runtime smoke, document validators, diff checks, and independent review
- Allowed Git and external actions: local branch/worktree edits and reversible
  experiment preservation only; no commit, push, PR, merge, or release without
  later user authorization
- Stop and report if: correctness needs an unstated visible quality tradeoff,
  dependency licensing blocks use, user/peer work overlaps, or preservation
  would rewrite published history
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Implement the Flashlight RT Producer

- Owner/thread: `/root/flashlight_rt_impl`
- Branch/worktree: shared task worktree; no branch operations
- Base commit/state: `51bad6a` plus the accepted ray/flashlight audit
- Read scope: existing Heitz/sky passes, flashlight math, PBR light structures,
  world-representation contract, NVRHI APIs, and focused tests
- Write scope: `src/flashlight_shared.h`, new files named
  `src/ray_traced_flashlight_shadows*`, and a new focused test named
  `tests/ray_traced_flashlight_shadow_tests.cpp`
- No-touch scope: `src/flashlight.h`, `src/uvsr.cpp`, PBR lighting/pass files,
  CMake, shader manifests/catalogs, existing tests, docs, Git state, build
  trees, processes, dependencies, and every file not named above
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: completed ray/flashlight architecture audit
- Interface/invariant contract: one full-resolution single-sample producer for
  one ordinary finite SpotLight; emit R16_FLOAT nearest committed hit distance
  (`0` invalid/outside cone/NoL, `CommittedRayT` hit, `65504` miss) and R8_UNORM
  raw scalar visibility (`1` invalid or miss, `0` hit); trace receiver-to-light
  with finite TMax before the 0.025m emitter, FORCE_OPAQUE and
  SKIP_PROCEDURAL_PRIMITIVES, never ACCEPT_FIRST_HIT; expose a first-party
  two-lobe superellipse profile (beam right, exponent, spill/hotspot cosines
  and weights) that root can bind to the exact PBR light index without
  negative radius or `shadowChannel` transport; absent/incompatible inputs fail
  open and do not dispatch
- Deliverable: apply-patch-only pass/profile/reference implementation and
  distilled handoff naming every root integration step
- Done when: CPU reference coverage proves finite direction/TMax, behind-light
  rejection, encoding semantics, exact two-lobe profile weights, and output
  resource compatibility
- Required verification: source inspection only; coordinator compiles/tests
- Allowed Git and external actions: none
- Stop and report if: correctness requires a Donut modification, material-aware
  candidate traversal, a file outside write scope, or an unstable NRD detail
- Handoff revision/artifact: flashlight RT producer and focused reference test
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Implement Settings and Loading Policies

- Owner/thread: `/root/settings_loading_impl`
- Branch/worktree: shared task worktree; no branch operations
- Base commit/state: `51bad6a` plus this execution plan
- Read scope: first-party settings/policy files and their focused tests
- Write scope: `src/world_space_representation_settings.h`,
  `src/ray_traced_sky_visibility_settings.h`, `src/screen_space_visibility.h`,
  `src/screen_space_visibility.cpp`, `src/flashlight.h`,
  `src/scene_loading.h`, `tests/world_space_representation_settings_tests.cpp`,
  `tests/ray_traced_sky_visibility_tests.cpp`,
  `tests/screen_space_visibility_tests.cpp` when present,
  `tests/flashlight_tests.cpp`, and `tests/scene_loading_tests.cpp`
- No-touch scope: `src/uvsr.cpp`, renderer/shader bindings, CMake, shader
  manifests, command catalog, source-contract tests, documentation, Git state,
  build trees, processes, dependencies, and every file not named above
- Build directory and runtime/GPU/resource lease: none; compile or runtime work
  remains coordinator-owned
- Dependencies already integrated: completed defaults/loading/quarantine audit
- Interface/invariant contract: add a default-true non-invalidating
  `allowRayTraversal`; make sky visibility's specular consumer default true;
  define and apply common maxima of 8 for distribution and occlusion; make the
  common/High diffuse sample count 16; replace flashlight lateral offset with
  signed horizontal and vertical offsets in `[-0.40, 0.40]`, preserving the
  current default mount at `(+0.17888544, -0.08944272)` meters, with 16-degree
  beam and 0.8 roundness; replace private-shadow-only helpers/constants only
  when their removal is wholly contained in the assigned files; add a pure,
  bounded scene-load estimator that maps exact import progress and bounded GPU
  stage progress monotonically into `0..99`, reserving 100 for complete
- Deliverable: apply-patch-only implementation plus distilled handoff
- Done when: focused deterministic tests cover defaults, ranges, sanitization,
  independent mount axes/extrema, non-invalidating traversal toggles, and
  progress bounds/monotonicity
- Required verification: source inspection only; coordinator compiles/tests
- Allowed Git and external actions: none
- Stop and report if: required behavior needs a file outside write scope or an
  interface conflicts with the ray/NRD contract
- Handoff revision/artifact: defaults, ranges, traversal policy, flashlight
  settings, and scene loading estimator
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Independently Review the Frozen Candidate

- Owner/thread: `/root/rendering_integration_review`
- Branch/worktree: read-only final task worktree
- Base commit/state: exact frozen candidate
- Read scope: complete task diff and relevant CPU/HLSL/build/runtime contracts
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, and remotes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: complete candidate
- Interface/invariant contract: review ray gating, light math, NRD signal and
  history correctness, resource lifetime, fail-open behavior, shader/package
  completeness, and end-to-end shadow removal
- Deliverable: prioritized findings with exact evidence
- Done when: no P0 through P2 finding remains
- Required verification: source/diff inspection and existing test evidence
- Allowed Git and external actions: read-only only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: frozen shared candidate; no P0 through P2 findings
- Handoff acknowledged by/on: `/root`, 2026-08-06

## Integration Order

1. Freeze settings, ray/light, NRD, and quarantine contracts from the audits.
2. Preserve the production shadow implementation on the CSM/SVSM experimental
   lineage, then remove it end to end from the task branch.
3. Apply isolated defaults/range/loading changes with focused tests.
4. Add the authoritative ray permission and finite-light RT shadow contract.
5. Integrate NRD dependency, signal adapters, histories, shader/resource
   bindings, fail-open paths, and Denoising drawer.
6. Reconcile UI/command/reset behavior, shader/package manifests, tests, README,
   prior sky-plan lifecycle, and this plan.
7. Freeze writes, run focused/full verification and runtime smoke, obtain an
   independent review, repair only substantiated task defects, and rerun stale
   evidence.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Requested defaults and ranges | Deterministic settings/UI/reset tests and source checks | Focused CTest | Passed in both 39 test matrices |
| Master ray permission | All caller gates, state retention tests, runtime off/on smoke | Focused tests and UVSR smoke | Source and tests passed; exact NRD artifact launched and rendered successfully |
| Flashlight finite RT shadows | CPU geometry/range tests, shader/source contract, visible occluder smoke | Focused CTest and UVSR smoke | Source and tests passed; visible smoke pending |
| NRD signal mappings | Method/input/history/reset/resize/fail-open tests for four signals | Focused and full CTest, source review, runtime matrix | Both full CTest matrices and independent source review passed; runtime matrix pending |
| Directional shadow quarantine | Zero production references/assets/build entries and exact experimental preservation | `rg`, Git tree/diff, CMake/build audit | Passed; preserved at `codex/svsm-csm-preserved` `f7c0c87` |
| Loading progress consistency | Deterministic monotonic fraction and rendered line source contract | Scene loading tests and UI smoke | Source and deterministic tests passed; UI smoke pending |
| Integrated candidate | Release application build, exact shaders/assets, all tests, docs, diff checks | Full build and CTest | Superseding standard and NRD Release builds passed; 40 of 40 tests passed in each |
| High risk correctness | Independent P0 through P2 renderer/build/lifetime review | Frozen candidate review | Passed with no surviving P0 through P2 findings |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-06 | Start from fetched live `origin/main` `51bad6a` in an isolated linked worktree. | The original `main` is clean but diverged; rewriting it risks unrelated local documentation commits. | Whole task |
| 2026-08-06 | Reserve shared renderer, build, package, UI, and Git hotspots until three read-only audits complete. | The requested features cross CPU/HLSL/resource and branch contracts; premature concurrent writers would compete on unstable interfaces. | All implementation |
| 2026-08-06 | Interpret the requested x/100 loading suffix as a truthful monotonic estimate whose denominator may become a more precise live total only when the loader exposes one. | This preserves the requested visual consistency without displaying invented precision. | Loading UI |
| 2026-08-06 | Preserve `codex/svsm-csm-preserved` unchanged at `f7c0c87` and remove the production screen-space directional-shadow path in a forward task diff. | That branch is the exact ancestor that already contains CSM, SVSM, Bend SSS, and the screen-space implementation; updating it with later renderer-core commits would remove or rewrite the other experiments. | Shadow quarantine |
| 2026-08-06 | Pin any approved NRD integration to official v4.17.3 `792eff1`, use embedded DXIL through a first-party NVRHI adapter, and make every denoiser default to None. | This avoids NRI and Donut changes, keeps the existing output until explicitly selected, and uses the latest released version rather than unreleased master. Dependency mutation remains paused pending acceptance of NVIDIA's proprietary RTX SDK obligations. | NRD |
| 2026-08-06 | Do not reinterpret current AO, GI, Heitz, or sky outputs as NRD hit-distance inputs. | AO/GI do not expose a representative first-hit distance, Heitz is a material-dependent RGB ratio, and sky exposes only a miss fraction. Correct ReBLUR/ReLAX/SIGMA integration requires explicit producer changes and matched-resolution guides. | NRD and producers |
| 2026-08-06 | Resume the complete NRD integration with the pinned v4.17.3 dependency and denoiser specific producers. | The user explicitly said to continue with the plan after reviewing the dependency, performance, and signal tradeoffs. Every denoiser remains defaulted to None and hit distance output is opt in. | NRD and producers |
| 2026-08-06 | Add `Output Hit Distance` to AO, GI, sky visibility, and shadows, plus independent ratio estimator controls for sky visibility and sun shadows. | This makes producer cost and estimator semantics directly observable and allows the cleaner scalar NRD contract without deleting the established raw estimators. | NRD, producers, and UI |
| 2026-08-06 | Rename the shipping drawer to `Ray Traced Shadows` and avoid new compound word hyphenation in user facing labels and prose. | This follows the requested product language while preserving code identifiers and literal external names where required. | UI and documentation |
| 2026-08-06 | Model the flashlight as one positive radius `SpotLight` with an authored two lobe beam and a separate full resolution RT visibility producer. | This lets ordinary PBR light transport own the flashlight while eliminating its duplicate hotspot light, negative radius marker, shadow map, and private shadow channel. | Flashlight and PBR |
| 2026-08-06 | Use ReBLUR diffuse radiance for AO, ReBLUR or ReLAX diffuse radiance for GI and sky visibility, and separate SIGMA instances for sun and flashlight shadows. | These are the pinned NRD 4.17.3 methods that match the retained signals. AO replicates its scalar value as radiance; sky uses the same scalar radiance adapter. | NRD backend |
| 2026-08-06 | Keep flashlight SIGMA spatial only and expose no inactive Shadows history or response controls. | Reliable local light reprojection is unavailable. Shadows quality and disocclusion therefore describe only sun temporal stabilization, while resolution controls both sources. | Denoising UI and SIGMA |
| 2026-08-06 | Preserve AO and GI raw estimators and produce honest matched first moment hit distance guides. | AO uses equal sector measure with misses censored at radius; GI weights first bounce distance by the NRD luminance of the exact raw RGB contribution. This avoids filtering a signal with an unrelated guide. | Producers and NRD |
| 2026-08-06 | Package the pinned NRD license and consolidated third party notice beside the NRD build. | The optional dependency is license gated and a distributable artifact must retain both documents. | Build and packaging |
| 2026-08-06 | Defer runtime smoke while PID 12576 runs UVSR from the separate sky visibility worktree. | Repository policy permits only one designated UVSR window and GPU lease. The other process is user or peer owned and must not be closed or competed with. | Runtime verification |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-06 | `/root` preflight | Complete | `51bad6a`; this plan | Fetch, divergence, worktrees, branches, active plans, open PRs, visible tasks, Coming Soon, and instructions inspected | Await audits and freeze contracts |
| 2026-08-06 | `/root/ui_defaults_quarantine_audit` | Complete; ownership released | `51bad6a`; preservation point `f7c0c87` | Read-only source and history inspection | Production removal inventory accepted |
| 2026-08-06 | `/root/ray_flashlight_audit` | Complete; ownership released | `51bad6a` | Read-only CPU/HLSL/resource inspection | Finite-light R16 hit-distance plus R8 visibility contract accepted |
| 2026-08-06 | `/root/nrd_integration_audit` | Complete; ownership released | Official NRD v4.17.3 `792eff1` and source at `51bad6a` | Read-only official API/license and signal-contract inspection | Await license and producer-policy authorization before dependency mutation |
| 2026-08-06 | `/root/settings_loading_impl` | Complete; ownership released | Shared task worktree | Deterministic policy sources and tests handed off | Integrated by coordinator |
| 2026-08-06 | `/root/flashlight_rt_impl` | Complete; ownership released | Shared task worktree | Flashlight RT pass, beam contract, and focused reference tests handed off | Integrated by coordinator |
| 2026-08-06 | `/root/nrd_backend_impl` | Complete; ownership released | Shared task worktree | NRD adapter, preparation and resolve shaders, optional stub, and focused tests handed off | Integrated by coordinator |
| 2026-08-06 | `/root/hit_distance_impl` | Complete; ownership released | Shared task worktree | Matched AO, GI, sky, and sun guide contracts handed off | Integrated by coordinator |
| 2026-08-06 | `/root/rendering_integration_review` | Complete; ownership released | Frozen shared candidate | CPU/HLSL bindings, method packing, histories, formats, fallbacks, and requirement traceability reviewed | No surviving P0 through P2 findings |
| 2026-08-06 | `/root` standard verification | Complete | `build/bin/uvsr.exe` | Full Release build; 39 of 39 CTest tests passed | Default NRD off path verified |
| 2026-08-06 | `/root` NRD verification | Complete | `build-nrd/bin/uvsr.exe`; SHA-256 `3A587F93935C1AFA1BD1DBD000A2F381B6018D21089A46A0291AC494FBB56C21` | NRD v4.17.3 full Release build; 303 UVSR shaders; 39 of 39 CTest tests passed; license and notice packaged | Runtime smoke waits for the renderer lease |
| 2026-08-08 | `/root` superseding combined verification | Complete | `build-nrd/bin/uvsr.exe`; SHA-256 `5434D6E47BB6ABB2FB5AEB9CFD080E88D39CA2E020413C09D0FF3124B1603AAB` | Standard and NRD Release builds; 306 first-party shader permutations; 40 of 40 CTest tests passed in each; exact NRD artifact rendered the Sponza scene | This baseline is complete and carried forward by the noise, shadow, and exposure plan |

## Risks and Escalation Triggers

- NRD integration may require motion, hit-distance, disocclusion, or signal
  semantics not currently produced by every requested effect. Missing evidence
  must fail open; it must not be synthesized deceptively.
- Extending a directional shadow estimator to a local finite cone light changes
  ray direction and maximum-distance contracts and needs explicit shader review.
- Removing the screen-space implementation touches source, UI, CMake, shader
  bundles, tests, vendored attribution, and docs; partial removal is not done.
- Branch preservation must not race or overwrite the long-lived CSM/SVSM
  experiment lineage.
- Runtime and GPU work must pause if the user is using the machine or the
  documented preflight gate is not available.

Stop and ask the user if:

- A complete NRD path requires a new dependency whose license or acquisition
  cannot be resolved safely.
- The current signals cannot meet NRD's correctness requirements without a
  visible quality/performance policy choice.
- The CSM/SVSM preservation lineage contains incompatible accepted work that
  cannot retain both results without choosing a product outcome.

The prior NRD dependency and producer policy stop condition is resolved. The
implementation must preserve NVIDIA's required license and notice material,
pin the audited release, keep the dependency optional at configure time, and
fail open when NRD is unavailable.

## Completion

- Final integrated commit: intentionally none
- Verification summary: the superseding standard and NRD Release builds passed;
  both full CTest matrices passed 40 of 40; 306 first-party shaders compiled;
  README counts, document Title Case, package contents, and diff checks passed.
  The exact NRD artifact launched and rendered the Sponza scene successfully.
- Independent review: complete with no surviving P0 through P2 findings; final
  traceability audit found no missing or partial user requirement.
- Coming Soon/documentation update: complete for the technically verified
  candidate; final roadmap reconciliation waits for integration authority.
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: runtime visual validation of AO and GI
  first moment guides, NRD output, flashlight occlusion, and the master ray
  switch after the unrelated UVSR window closes; measure the five independent
  NRD instances before making any performance claim.
- Active ownership released: all worker and reviewer ownership released.
- Archived to completed path:
  `docs/exec-plans/completed/main-lighting-denoising-controls.md`
