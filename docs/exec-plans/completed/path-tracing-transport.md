# Path-Tracing Transport: Shared Zero-Raster Solver Architecture

## Status

- State: complete; the shared transport checkpoint is technically verified and
  authorized for a direct fast-forward to `origin/main`. Native SER, PSR, path
  NRD, and NVIDIA-parity solver work remain capability-gated follow-ups.
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/path-tracing-transport` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\path-tracing-transport`
- Base commit: `d95e68247105819353672c14a8a8fc15a1d46e0c`
- Started: 2026-08-12
- Last updated: 2026-08-13
- Archived:
  `docs/exec-plans/completed/path-tracing-transport.md`
- Repository policy version: `2026-07-30.1`
- UI reference version: `2026-08-12.4`

## Goal and Done Condition

Goal: build the smallest complete DX12 path-tracing implementation on the exact
requested main commit, expose it as a first-class Lighting Solution, and keep
NVIDIA-derived RTX PT, RESTIR PT, and RESTIR GI behavior behind one extensible
transport core instead of three independent renderers.

Done when:

- [x] General begins with a deferred **Lighting Solution** selector whose
      **Ray Marching** choice preserves the current renderer and whose
      **Path Tracing** choice changes topology only after the authored dropdown
      transition barrier.
- [x] Path Tracing smoothly removes screen-space and selective-ray controls,
      including Diffuse, Buffers, Aliasing, and Shadows, while preserving their
      stored Ray Marching state; Sky, Debug, and Denoising show only relevant
      Path Tracing controls.
- [x] A **Pathing** drawer immediately follows General and exposes executable
      **RTX PT**, NEE/NEE-AT, an optional first-party direct reservoir, a
      clean-room **RESTIR PT** seed-replay subset, and a clean-room
      **RESTIR GI** indirect-checkpoint subset. Neither subset claims NVIDIA
      parity or geometric/hybrid reconnection; SER remains unavailable.
- [x] Noise exposes **Accumulate Samples** in both lighting solutions. Static
      successful samples persist and receive lower retry priority; camera,
      lighting, geometry, material, resolution, solver, or transport changes
      invalidate the complete progressive state. Path Tracing skips traversal.
      Ray Marching prepares the shared attempt mask before guarded stochastic
      producers and retains rejected pixels transactionally. Focused contracts,
      production C++ compilation, and independent transaction review passed.
- [x] Firefly handling is executable and biased. RTX PT has a first-party
      spatial Stable Plane Resolve with one to three path layers and raw
      fallback; RESTIR PT/GI retain raw output until selected candidates carry
      a sound layer identity. NRD and PSR remain honestly disabled.
- [x] The exact source snapshot builds, focused and full tests pass, a bundled
      scene completes a smoke exercise in Ray Marching and every Path Tracing
      preset, and the resulting executable is identified precisely.
- [x] `docs/path-tracing-transport.md`, `docs/pbr-foundation.md`, the canonical
      UI reference, README, tests, and source comments describe the retained
      architecture and visible behavior without overstating NVIDIA parity.

## Scope

In scope:

- One zero-raster DXR 1.1 compute/ray-query transport path that traces camera
  rays and subsequent diffuse/specular transport through the shared scene BVH.
- A shared material/light/environment contract, path state, throughput,
  Russian roulette, emissive/environment termination, alpha-tested candidate
  handling, and deterministic history invalidation.
- Independently implemented NVIDIA-reference-informed labels and directly
  useful options whose source contracts can be implemented and verified in
  UVSR without copying proprietary RTX SDK source.
- Per-pixel progressive accumulation with success-aware scheduling in both
  Lighting Solution modes.
- UI gating, command/reset coverage, runtime statistics, tests, documentation,
  shader packaging, and an isolated build.

Non-goals:

- Editing pinned Donut sources, enabling Vulkan or DX11, adding raster
  prerequisites to the Path Tracing pipeline, or retaining dormant controls.
- Claiming bit-identical output or certified NVIDIA SDK conformance where RTXPT
  is a sample integration rather than a drop-in library.
- DLSS Ray Reconstruction, Neural Radiance Cache, Shader Execution Reordering
  on unsupported hardware, or optional SDK downloads that are not required for
  a correct raw transport fallback.
- Push, pull request, merge, release, deployment, or submodule mutation.

Affected subsystems and paths:

- `src/uvsr.cpp`, Settings helpers/state, pass ordering, scene invalidation,
  performance rows, and renderer-facing commands.
- New first-party path-transport CPU/shared/HLSL files under `src/`.
- Existing PBR, world-space representation, noise, and denoising contracts.
- `src/shaders.cfg`, root `CMakeLists.txt`, focused tests, README, and renderer
  engineering documentation.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/uvsr.cpp`, `src/shaders.cfg`, global
  settings, CPU/HLSL binding contracts, `docs/ui-integration-agent-procedure.md`,
  `docs/pbr-foundation.md`, and this execution plan.

## Baseline

- Canonical repository/remote: live `origin/main` resolved to
  `d95e68247105819353672c14a8a8fc15a1d46e0c` on 2026-08-12; no open pull
  requests were reported by `gh pr list`.
- Local versus remote state: the original shared `main` checkout is diverged
  (`5e300c9`, ahead 2/behind 49) with unrelated untracked work. It remains
  untouched. This isolated feature worktree is clean and exactly equal to live
  `origin/main` at creation.
- Verified source commit/build: the requested source base is exact, but no
  trustworthy task-local verified manifest has yet been found. Establish a
  baseline build or state that limitation explicitly before comparison claims.
- GPU, scene, camera, resolution, and settings preset when relevant: bundled
  Sponza or Bistro, camera position 1 for any performance evidence, native
  single-sample deferred mode for Ray Marching, and the same scene/camera for
  all Path Tracing solver smoke checks.
- Known pre-existing failures: none established in this isolated worktree.
- Overlap decision: abandoned shadow-map plans touch older branches and do not
  block this exact-main lineage. Active legal and sky-visibility plans are
  separate branches; this coordinator owns all integration choices here and
  will not publish or alter those branches.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| PT-ARCH | Existing renderer/BVH/material/pass map and minimal resource contract | complete | PT-CORE |
| PT-NVIDIA | Exact RTXPT 1.8.1 and RTXDI 3.0 feature/licensing/API findings | complete | PT-DESIGN, PT-CORE |
| PT-UI | Existing Settings transition/gating/test map | complete | PT-UI-IMPLEMENT |
| PT-DESIGN | Frozen CPU/HLSL bindings, invalidation keys, and preset table | complete | all implementation |
| PT-SETTINGS | Header-only settings, preset, and accumulation math contract | complete | PT-CORE, PT-UI-IMPLEMENT |
| PT-CORE | Shared transport and accumulation runtime | complete for the documented transport domain | presets, denoising, UI |
| PT-UI-IMPLEMENT | Deferred Lighting Solution and gated drawer composition | complete | verification |
| PT-REVIEW | Independent shader/lifetime/licensing review | solver, RM, stable-resolve, strict-domain, and finite-emitter reviews complete | final verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `LightingSolution::{RayMarching, PathTracing}` is factory-defaulted to
  Ray Marching and applied through the shared deferred topology barrier.
- `PathTracingSolver::{RtxPt, RestirPt, RestirGi}` chooses a preset over one
  transport resource set; a solver change resets all Path Tracing histories.
- The transport owns current/accumulated radiance, sample count, optional
  direct-reservoir history, RESTIR GI local-checkpoint history, RESTIR PT local
  seed/statistics history, and one invalidation fingerprint. Only the effective
  solver's history family is full resolution; no history survives a mismatched
  camera, light, geometry, material, environment, resolution, or transport
  fingerprint.
- Existing Ray Marching resource contracts and stored settings remain unchanged
  while its drawers are gated out of view.

## Frozen Design Contract

The three reference audits are complete. The implementation contract is frozen
as follows:

- UVSR will not copy or vendor RTXPT, RTXDI, RTXDI-Library, NRD, or NVAPI source.
  Their RTX SDK licenses do not clearly permit source incorporation into this
  repository. The user-requested **RTX PT**, **RESTIR PT**, **RESTIR GI**, and
  **RTXDI** labels identify independently implemented algorithmic presets; the
  UI and engineering documentation must state that these are NVIDIA-reference-
  aligned UVSR implementations, not certified or bit-identical NVIDIA SDK
  integrations.
- The raw **RTX PT** preset is an unbiased Monte Carlo estimator over UVSR's
  documented opaque and alpha-tested metallic-roughness transport boundary.
  It has no reservoir reuse, firefly clamp, stable-plane reconstruction, or
  denoising in the raw history. The product must not claim mathematical ground
  truth, support for excluded transmissive/blended domains, or output identity
  with RTXPT.
- One DXR 1.1 compute megakernel owns primary rays, committed-hit reconstruction,
  alpha coverage, material texture evaluation, GGX/Lambert sampling and PDFs,
  analytic-light NEE, environment emission, emissive hits, throughput, Russian
  roulette, ray offsets, and finite-result validation. Analytic-light and
  BSDF-reached emitter proposal sets do not overlap, so no active MIS combiner
  exists. All effective solver paths call this one core.
- **Uniform**, **Power**, and **NEE-AT** are separate light-sampling strategies.
  NEE-AT must retain matching sampling PDFs. If the complete temporal/global and
  local tile feedback distribution is not implemented, the control must report
  the independently implemented adaptive approximation instead of claiming
  RTXPT identity.
- RESTIR PT is an executable UVSR seed-space subset. It persists a complete
  deterministic `uint2` local path seed, replays the current, previous
  same-pixel, and one radiance-independent previous-neighbor seed through the
  exact receiving-pixel integrator, and replaces rather than adds the local
  indirect suffix. It does not implement NVIDIA's hybrid geometric
  reconnection, Jacobians, or recursive combined-reservoir feedback.
- RESTIR GI is an executable UVSR indirect-checkpoint subset. It combines
  the current and previous same-pixel finite local indirect suffix, counts
  finite black proposals, and persists only the current local checkpoint with
  `M=1`. It has no secondary-surface spatial transform or reconnection.
- The optional first-party RTXDI-like direct-light reservoir is orthogonal to
  every solver and replaces primary conventional NEE so it cannot double-count
  that contribution. None of these labels claims NVIDIA certification,
  bit-identical output, source compatibility, or one-to-one SDK parity.
- Native SER requires a separate SM 6.9/HitObject shader bundle. UVSR's current
  SM 6.5 bundle and ray-query megakernel remain the mandatory fallback. The SER
  preference is visible but capability-gated and cannot silently behave as if
  reordering occurred.
- The path pass owns persistent scene-linear mean, successful-sample count,
  optional direct-reservoir and surface-signature history, conditional GI
  checkpoint/count ping-pong, conditional PT seed/statistics ping-pong, and an
  epoch. Inactive history families retain only 1x1 binding dummies and no
  persistent denoising guides. Valid zero/black/miss estimates count as
  successful. With **Accumulate Samples** on, retry probability is
  `1 / (successfulCount + 1)` and the decision is independent of radiance. A
  new successful estimate updates the online mean; a skipped pixel retains
  every prior successful estimate.
- One authoritative frame signature covers the nonjittered camera/projection,
  WSR allocation generation and dynamic-content revision, material revision,
  packed lights, environment content/scale, resolution, solver/transport/noise
  settings, scene activation, and shader reload. Any mismatch clears all path,
  reservoir, stable-plane, denoiser, and Ray Marching accumulation history.
- Path Tracing bypasses the raster G-buffer, selective ray visibility, deferred
  lighting, screen-space lighting, separate sky-background pass, and temporal
  AA. It retains Representation, Noise, physical Sky/environment and Lights,
  on-demand raster material picking, Auto Exposure, AgX, optional presentation-
  only FXAA/CMAA, output transfer/dither, and the interface.
- The retired `DeferredUiStructuralPresentation` remains absent. Lighting
  Solution and solver selectors use `BeginRoundedCombo`,
  `DrawDeferredDropdownOption`, and the existing end-of-composition action
  barrier. Each gated whole drawer has its own stable-ID animated toggle region;
  shared drawers submit independent Ray Marching and Path Tracing body regions
  every frame without erasing disclosure or inactive settings.
- **Pathing** appears directly after General. Diffuse, Buffers, Aliasing, and
  Shadows animate out in Path Tracing. Denoising, Debug, Sky, and Lights retain
  their headers and show only mode-relevant bodies. Representation, Noise,
  Material, and Interface remain shared.
- Firefly handling is an active, explicitly biased first-party option. RTX PT
  can persist coherent path layers and guides for UVSR's spatial Stable Plane
  Resolve. PSR, path NRD, and SER remain disabled with direct capability
  explanations. A disabled state is not treated as an implemented runtime
  path.

## Mandatory New-Element Intake

### Lighting Solution

- State owner/default: renderer settings state; **Ray Marching**.
- Control: two-choice deferred dropdown at the top of General; accepted command
  values `ray-marching` and `path-tracing`.
- Reset: complete Settings reset restores Ray Marching; no separate duplicate
  default in UI code.
- Consumers: drawer composition, render-pass topology, history/resource
  allocation, performance rows, command catalog, loading/unavailable status.
- Change class: renderer topology and resources.
- UI ownership: General owns the combo; the existing deferred combo and shared
  composition-idle barrier own popup, focus, and mutation timing.
- Loading/unavailable behavior: preserve the selected preference but report DXR
  or Representation prerequisites directly; never present partial resources.
- Required checks/exercise: UI source/animation/dropdown contracts, renderer
  source contract, rapid reversal while dependent drawers are visible/clipped,
  both skins, reset, scene load, and both lighting solutions.
- Unchanged behavior: Ray Marching output, defaults, and control state.

### Pathing Solver and Options

- State owner/default: path-tracing settings; **RTX PT** with Uniform NEE,
  eight bounces, Russian roulette after bounce three, one NEE candidate, and
  RTXDI/SER disabled.
- Control: deferred preset dropdown plus only active option toggles/dropdowns;
  no control may exist without a runtime consumer.
- Reset: Pathing reset restores the complete selected preset recipe; edits show
  a Custom marker only if that convention is retained in the frozen design.
- Consumers: shader constants/permutations, reservoir/stable-plane resource
  topology, invalidation fingerprint, denoising inputs, performance rows.
- Change class: solver selection may change resources; scalar tuning changes
  constants and resets affected histories.
- UI ownership: Pathing owns controls and animated dependent regions; the shared
  deferred barrier owns topology mutation.
- Loading/unavailable behavior: SER is disabled with a direct capability reason;
  raw RTX PT transport remains the fallback when optional denoising is absent.
- Required checks/exercise: every preset, toggle, reset, unavailable state,
  scene load, and output/resource transition.
- Unchanged behavior: Representation remains the one owner of BVH/TLAS policy.

### Accumulate Samples

- State owner/default: global Noise settings; off to preserve the current
  renderer at process start.
- Control: direct toggle, accepted command values `on` and `off`.
- Reset: Noise/global Settings reset restores off.
- Consumers: Ray Marching prepare-before-production attempt scheduling,
  guarded stochastic producer work, post-AA transactional resolve, and retained
  successful signal; Path Tracing pre-traversal scheduling and radiance/sample-
  count accumulation; downstream history invalidation. Required raster,
  reconstruction, denoising, anti-aliasing, and presentation work may still run
  when a guarded stochastic attempt is rejected.
- Change class: history resources and dispatch constants, not scene state.
- UI ownership: Noise owns the toggle and tooltip; no popup; an animated
  unavailable explanation is used only if a solution has no active stochastic
  producer.
- Loading/unavailable behavior: history is discarded across scene loading and
  never read before its fingerprint matches the ready renderer state.
- Required checks/exercise: static convergence, successful-pixel retry decay,
  every invalidation cause, both lighting solutions, reset, and rapid camera
  movement.
- Unchanged behavior: existing Pattern, Resolution, and Animate Samples meaning.

### Path-Tracing Denoising and Debug Controls

- State owner/default: path-tracing settings with raw output as the mandatory
  no-SDK fallback. Stable Plane Resolve, PSR, biased firefly filtering, and
  path NRD are disabled by default.
- Control: executable raw and biased-firefly controls plus diagnostic stable-
  plane classification and the RTX PT spatial resolve; capability gates
  explain solver-specific availability, PSR, path NRD, indirect reuse, and SER
  absence.
- Reset: Pathing/Denoising group resets restore the selected preset's recipe.
- Consumers: stable-plane classification, radiance demodulation/resolve,
  firefly rejection, PSR reconstruction, optional NRD backend, and Debug views.
- Change class: constants plus history/resource topology where planes or NRD
  signal layouts differ.
- UI ownership: Denoising and Debug own their relevant animated sections; legacy
  AO/GI/Shadows/Sky Visibility groups retain stored state but collapse in Path
  Tracing.
- Required checks/exercise: raw fallback, every supported resolve path, missing
  backend state, debug composition, resets, and history invalidation.
- Unchanged behavior: Ray Marching denoiser/debug groups and explicit producer
  contracts.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| PT-ARCH | subagent | shared isolated worktree | `d95e682` | none | none | complete |
| PT-NVIDIA | subagent | official web sources | pinned releases | none | none | complete |
| PT-UI | subagent | shared isolated worktree | `d95e682` | none | none | complete |
| PT-DESIGN/CORE/UI | `/root` | `codex/path-tracing-transport` | `d95e682` | all task-owned implementation paths | explorers | combined candidate |
| PT-REVIEW | independent subagents | final task diff | candidate | none | implementation freeze | no P0/P1 findings remain |

## Assignment Contracts

### Renderer Contract Assignment

- Owner/thread: assigned read-only explorer.
- Branch/worktree: shared isolated worktree, no Git or filesystem writes.
- Base commit/state: clean `d95e682`.
- Read scope: first-party renderer, PBR, representation, noise, denoising,
  shaders, CMake, tests, and documentation.
- Write scope: none.
- No-touch scope: every file, Git state, build trees, processes, and GPU.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: identify the narrowest reusable BVH/material/
  light/environment seams and all lifetime/pass-order risks; do not invent code.
- Deliverable: distilled architecture map with exact paths/symbols, proposed
  resource/binding layout, checks, and blockers.
- Done when: the coordinator can freeze a complete transport contract without
  rereading the whole renderer.
- Required verification: source evidence only.
- Allowed Git and external actions: read-only commands; no checkout/fetch.
- Stop and report if: the base is dirty or a required contract is absent.

### NVIDIA Reference Audit

- Owner/thread: assigned read-only researcher.
- Branch/worktree: official NVIDIA web/GitHub sources; no UVSR writes.
- Base commit/state: RTXPT 1.8.1 and RTXDI 3.0 release/tag sources.
- Read scope: official repositories, tagged documentation/code, licenses, and
  linked NVIDIA primary documentation.
- Write scope: none.
- No-touch scope: UVSR files, Git refs, submodules, dependencies, and binaries.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: distinguish library APIs from sample behavior;
  enumerate exact NEE/NEE-AT, SER, RTXDI, RESTIR PT/GI, stable-plane, NRD,
  firefly, and PSR semantics/defaults with direct primary-source references.
- Deliverable: implementable preset table, licensing constraints, source paths,
  and honest parity boundaries.
- Done when: every requested NVIDIA-derived option is classified as core,
  portable integration, optional capability, or unsupported claim.
- Required verification: tagged primary-source citations and commit/tag IDs.
- Allowed Git and external actions: read-only web access.
- Stop and report if: the named version/tag cannot be verified.

### UI Contract Audit

- Owner/thread: assigned read-only explorer.
- Branch/worktree: shared isolated worktree, no Git or filesystem writes.
- Base commit/state: clean `d95e682` with UI reference `2026-08-12.3`.
- Read scope: `src/uvsr.cpp`, UI helpers/settings/commands, UI tests, renderer
  contract tests, and canonical UI documentation.
- Write scope: none.
- No-touch scope: every file, Git state, build trees, processes, and GPU.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: identify exact structural-deferred APIs and the
  smallest safe way to animate drawers out while preserving disclosure state.
- Deliverable: symbol/line map, proposed drawer visibility table for both
  solutions, command/reset changes, and focused test plan.
- Done when: the implementation can preserve the authored popup barrier and all
  stored Ray Marching state.
- Required verification: source evidence only.
- Allowed Git and external actions: read-only commands.
- Stop and report if: implementation requires changing the canonical transition
  contract or overlapping external work appears.

## Integration Order

1. Complete PT-ARCH, PT-NVIDIA, and PT-UI read-only audits.
2. Freeze PT-DESIGN: settings, preset recipes, CPU/HLSL bindings, invalidation
   fingerprint, UI visibility matrix, and dependency/licensing decision.
3. Implement and unit-test shared transport math/state before renderer wiring.
4. Integrate DXR resources, dispatch, accumulation, presets, and raw resolve.
5. Integrate deferred UI topology, gated drawers, commands, statistics, and
   denoising/debug routes.
6. Reconcile documentation and README once, then freeze writes.
7. Run focused/full checks and isolated build/smoke exercise.
8. Obtain independent PT-REVIEW, repair findings serially, and rerun affected
   verification.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Exact base and isolated lineage | Git identity and clean starting status | `git rev-parse HEAD`; `git status --short --branch` | `d95e682`, clean at creation |
| Transport math and invalidation | Deterministic focused tests | path-transport settings/source targets | passed |
| UI topology and transitions | Source/animation/dropdown contracts plus live reversal | focused contracts and runtime smoke | passed; the final PT and RM drawer sets had single consistent margins and no retained blank envelopes |
| Shader packaging and runtime bindings | shader bundle and renderer contracts | clean Release build and CTest | passed; 327 production shader tasks and 50 staged shader binaries |
| Ray Marching unchanged | focused existing tests and matched smoke scene | Release CTest and live exercise | passed; Bistro returned from PT to the live raster renderer with its complete drawer set |
| RTX PT/RESTIR PT/RESTIR GI execute | non-black stable output and per-preset statistics | isolated executable, same scene/camera | passed at 1920 x 1080; all three presets presented traced frames without crash or stall |
| Accumulation/invalidation works | sample count/convergence and reset observations | contracts plus static and scene-transition exercise | passed; accumulation filled the progressive image, solver changes restarted it, and Sponza-to-Bistro invalidated and rebuilt transport |
| Documentation is valid | Title Case, links, line counts, diff checks | repository scripts | passed; 115,620 first-party, 387,466 third-party, 503,086 total lines; diff check clean |
| Independent risk review | shader/lifetime/licensing findings resolved | PT-REVIEW handoff | no P0/P1 findings remain in the frozen production source |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-12 | Use an isolated worktree at exact `d95e682` | Shared main is diverged and dirty; preserving unrelated work is mandatory | all |
| 2026-08-12 | One shared transport with preset recipes | The user requested future solver extensibility and warned against needless complexity | design/core |
| 2026-08-12 | Keep Ray Marching as the default | Preserves current startup/output and makes Path Tracing opt-in | UI/runtime |
| 2026-08-12 | Make raw transport the mandatory fallback | Optional NVIDIA capabilities must not make the base path tracer inert | core/denoising |
| 2026-08-12 | Do not copy or vendor NVIDIA RTX SDK source | The reviewed releases use the proprietary RTX SDK license; first-party implementation avoids an unapproved source-redistribution decision | all |
| 2026-08-12 | Initially keep unsupported namesake paths visible but capability-gated | This protected the first transport candidate from dormant or mislabeled branches; superseded for RESTIR PT/GI on 2026-08-13 after sound subset contracts were frozen | core/UI/docs |
| 2026-08-12 | Upload analytic lights through a growing structured buffer | The reviewed 16-light constant array silently lost valid scene lights; one dynamic buffer keeps NEE complete for the submitted list | core |
| 2026-08-13 | Resolve every requested shader variant to an executable pipeline before dispatch | Optional PSO failures must not disable the baseline; authored settings remain visible while effective settings fall back first without RTXDI and then to Uniform RTX PT | core/UI |
| 2026-08-13 | Keep common post-processing and command-list submission outside the raster fallback scope | The first runtime candidate skipped close/execute after a successful PT dispatch, so no traced work could reach the GPU or presentation | renderer |
| 2026-08-13 | Clip whole drawers while collapsing instead of fading their complete contents | Height and opacity previously fell together, leaving large transparent layout envelopes; close-only clipping retains the smooth transition without blank gaps | UI |
| 2026-08-13 | Canonicalize every submitted light record before upload and history hashing | Donut leaves light-type-irrelevant lanes unspecified, which could spuriously invalidate a static accumulation history | core/invalidation |
| 2026-08-13 | Implement solver-specific clean-room RESTIR subsets instead of relabeling RGB accumulation | RESTIR GI may retain a complete indirect checkpoint, while RESTIR PT must retain a deterministic primary-sample seed and replay it at the receiving pixel; neither path may double-count the local indirect term | core/solver/docs |
| 2026-08-13 | Keep RESTIR PT and GI payloads physically distinct behind shared reservoir statistics | Both solvers share finite `W`, target, and `M` update rules, reset epochs, and ping-pong lifecycle, but a GI radiance checkpoint is not a replayable PT sample identity | core/resource lifetime |
| 2026-08-13 | Persist only local reservoirs and use previous-frame donors for the current estimate | Feeding recursively combined neighbor reservoirs back into history would require pairwise MIS or explicit non-overlap accounting; local-only persistence keeps the first executable subsets sound and bounded | core/solver |
| 2026-08-13 | Replace fallback-only RESTIR recipes with explicitly qualified executable UVSR subsets | Deterministic seed replay gives RESTIR PT a real path-sample identity; same-pixel local checkpoints give RESTIR GI a sound first temporal estimator. Both remain visibly below NVIDIA namesake parity without geometric/spatial reconnection | core/UI/docs |
| 2026-08-13 | Schedule Ray Marching attempts before guarded stochastic producers | A full-resolution prepare mask lets already-successful pixels skip stochastic visibility/shadow work while transactional resolve prevents partial producer failures from contaminating retained means | renderer/noise |
| 2026-08-13 | Add a clean-room spatial path-layer resolve only to RTX PT | The un-resampled reference solver owns a sound primary/diffuse/specular split. RESTIR winners do not yet carry layer identity, so they retain raw fallback rather than misclassifying donor paths | core/denoising/UI/docs |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-12 | `/root` | active | branch/worktree created | exact remote/base checked | complete audits and freeze design |
| 2026-08-12 | PT-CORE/UI workers | partial candidate | first-party path/RM accumulation passes, UI, commands, shaders, and docs | focused compiles and source checks passed | independent review and full verification |
| 2026-08-12 | Independent review | complete | frozen task diff | no P0; fixed RIS zero-proposal bias, silent 16-light truncation, material/backface/normalization/resource issues, and RM invalidation gaps | namesake capability and RM upstream-scheduling gaps remain |
| 2026-08-12 | `/root` | technically verified except runtime | `build-path-tracing/bin/uvsr.exe` | all-target Release build, 308 shader tasks, 42 of 42 CTest, README counts, and 166 in-scope Title Case headings passed | runtime smoke when GPU lease is free; original feature gaps remain |
| 2026-08-13 | Crash and submission repair | runtime candidate | `build-path-tracing-fix/bin/uvsr.exe` | Path Tracing selected without a crash; `0 tris` and changed lighting proved raster bypass; Sponza-to-Bistro scene load completed while PT remained active | rebuild required after subsequent light-canonicalization source repair |
| 2026-08-13 | UI collapse repair | runtime candidate | `build-path-tracing-fix/bin/uvsr.exe` | Diffuse, Buffers, Aliasing, and Shadows collapsed without retained blank margins; Settings and scene selection remained responsive | preserve exact four-gate opt-in contract |
| 2026-08-13 | Reservoir solver design | complete | frozen read-only audits | RESTIR GI complete-indirect checkpoint and RESTIR PT deterministic seed-space replay contracts frozen; no NVIDIA source copied | implement both as solver-specific shader variants and independently review estimator/resource lifetime |
| 2026-08-13 | RESTIR solver implementation | complete | 18 solver/RTXDI/NEE variants plus conditional GI/PT histories | all 18 DXC permutations, focused settings/source tests, direct pass compilation, independent P0/P1 review, and final live preset smoke passed | retain the qualified subset boundary |
| 2026-08-13 | Ray Marching attempt scheduling | complete | prepare mask plus guarded visibility/shadow producers and transactional resolve | focused renderer/Heitz/sky contracts, production C++ compile, independent P0/P1 review, and final live reversal passed | retain transactional producer gating |
| 2026-08-13 | RTX PT spatial path-layer resolve | complete | coherent primary/diffuse/specular signals plus fixed 5x5 cross-bilateral resolve | 19 DXC shaders, production C++ compile, 327-task package, focused tests, and independent resource/lifetime review passed after cache-release repair | exercise 1/2/3-layer modes in the final live smoke |
| 2026-08-13 | Finite analytic emitters and direct seed identity | complete | transport-local directional-disk and visible-sphere sampling plus `R32_UINT` direct-reservoir seed history | all 18 DXC variants, focused tests, direct C++ compile, exact-PDF numerical contracts, final Release build, and independent estimator/resource review passed | retain exact proposal identity through reuse |
| 2026-08-13 | `/root` final combined checkpoint | locally technically verified | `build-path-tracing-fix/bin/uvsr.exe`, SHA-256 `80FE957408C57969DB0A5AA2486AC2E283EC5257AA8E077FFE8B4616545A397E` | clean Release build, 327 shader tasks, 50 staged bins, 42 of 42 CTest, current README counts, clean diff check, and live 1920 x 1080 solver/scene/reversal smoke | retain locally; obtain product acceptance before any publication |

## Risks and Escalation Triggers

- RTXPT is an application/reference integration, not necessarily a drop-in SDK;
  visual parity claims require care even when algorithms are faithfully ported.
- RTXDI and NRD licensing, SDK version compatibility, and binary/source package
  requirements may prohibit vendoring or make an optional runtime unavailable.
- Existing world-space representation may expose visibility-only geometry data;
  complete transport needs reliable hit material attributes, emissive data, and
  light sampling without modifying Donut.
- SER requires hardware/API capability and shader authoring support; a visible
  toggle must never imply activation when the backend cannot dispatch it.
- Ray Marching now schedules one shared attempt mask before guarded stochastic
  visibility and ray-shadow producers and commits only after the selected
  scene-linear source is complete. Required deterministic raster,
  reconstruction, denoising, TAA, and presentation work remains outside that
  skip boundary. Focused contracts and independent review prove that no partial
  producer result can commit. The combined runtime exercise completed without
  a crash or stalled transition.
- RESTIR PT/GI are implemented as explicitly qualified clean-room
  subsets. They must not be described as NVIDIA 1:1 implementations: the GI
  subset has no spatial secondary-surface reconnection, and the PT subset has
  seed-space replay but no hybrid geometric reconnection. RTX PT now has a
  clean-room spatial path-layer resolve; SER, PSR, and path-transport NRD remain
  unavailable.
- Finite analytic-emitter sampling and persisted direct-reservoir sample
  identity are complete and independently reviewed. Exact zero-size delta
  behavior is retained while positive-size emitter surfaces use matching PDFs
  and sampled-endpoint visibility.
- The task is high-risk shader/resource-lifetime work and cannot integrate
  without an independent review.

Stop and ask the user if:

- official license terms require a materially different asset/dependency
  treatment than a first-party clean-room port;
- the only practical route requires editing Donut or installing/elevating an
  optional SDK/driver;
- a required product choice would trade ground-truth correctness for default
  performance or visible quality;
- publication, merge, release, or deletion of uncertain user work becomes
  necessary.

## Completion

- Final integrated commit: the publication commit containing this completed
  plan, with subject `feat: add path-tracing lighting solution`.
- Verification summary: the final combined Release build completed all 327
  shader tasks and staged 50 shader binaries; all 42 CTest cases passed. The
  exact executable selected and presented RTX PT, RESTIR PT, and RESTIR GI at
  1920 x 1080, accumulated a static Sponza image, exercised three-layer Stable
  Plane Resolve, loaded Bistro while RESTIR GI remained selected, and returned
  to Ray Marching. It did not crash, stall, retain stale menu frames, or leave
  blank drawer envelopes. README counts and the final diff check are current.
- Independent review: no P0 findings remain; solver-resource, Ray Marching
  transaction, stable-resolve, strict-domain, and finite-emitter reviews are
  complete and their actionable findings are repaired.
- Coming Soon/documentation update: reconciled with the executable transport
  boundary, including the warned omission of Sponza's alpha-blended decals.
- Pushed/PR/merged, or intentionally local: direct fast-forward publication to
  `origin/main` was explicitly authorized; no pull request or merge commit is
  required.
- Remaining experiments or follow-ups: add hybrid/geometric RESTIR PT
  reconnection, spatial secondary-surface RESTIR GI transforms, native SER,
  PSR, path NRD, and matched environment/emissive NEE before claiming NVIDIA
  namesake parity.
- Active ownership released: all worker and coordinator path leases are
  released by this publication checkpoint.
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/path-tracing-transport.md`.
