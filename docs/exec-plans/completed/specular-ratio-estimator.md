# Specular Ratio Estimator: Reflections and Estimator Repair

## Status

- State: completed; local candidate intentionally uncommitted and unpublished
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/specular-ratio-estimator` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\specular-ratio-estimator`
- Base commit: `51bad6a7fccb88afd46078dcd95ef4bbdb509d7a` plus the exact
  uncommitted Main Lighting, Denoising, and Controls candidate identified below
- Started: 2026-08-07
- Completed: 2026-08-07
- Archived to:
  `docs/exec-plans/completed/specular-ratio-estimator.md`

## Goal and Done Condition

Goal: repair the sun and sky Ratio Estimator controls so each compares a real
full stochastic estimator with a correlated ratio estimator at the same ray
count, and add a production Specular system with standalone stochastic screen
space reflections, optional hybrid ray traced fallback, Frostbite-style spatial
ratio reuse, deferred HDR-pyramid lookup, and specular elongation.

Done when:

- [x] Ratio Estimator on and off select mathematically distinct estimators at
  the same configured sample count for the sun and sky, with deterministic
  reference coverage and an observable runtime comparison.
- [x] Sun analytic normalization accounts for finite angular extent, while the
  Statistics presentation reports scene analytic-light and environment-source
  inventory without implying that lights are acceleration-structure geometry.
- [x] A top-level Specular drawer appears after Diffuse. Its Stochastic Screen
  Space Reflections and Hybrid Reflections subdrawers start closed, each starts
  disabled, and each has a default-closed Advanced drawer.
- [x] The Specular pass stores hit identity, distance, direction/footprint, and
  source PDF; optionally defers color lookup; uses a roughness, hit-distance,
  and elongation HDR-pyramid mip; couples trace narrowing with filter bias; and
  applies the preintegrated BRDF exactly once after normalized spatial reuse.
- [x] Hybrid mode traces only low-confidence screen misses when ray traversal,
  DXR 1.1, and a coherent TLAS are available, otherwise fails open to screen or
  environment lookup. No AMD denoiser code or settings are present.
- [x] Existing indirect specular is deferred only when the new pass is ready,
  then composited exactly once; inactive, unsupported, MSAA, loading, resize,
  shader-reload, and reset paths remain safe.
- [x] Required focused and complete Release checks pass, the exact candidate is
  live-exercised without competing for a peer-owned renderer, an independent
  rendering review has no surviving P0 through P2 finding, and the final
  `uvsr.exe` is linked in the handoff.

## Scope

In scope:

- Heitz 2018 correlated ratio-estimator semantics for finite directional-light
  shadows and environment visibility.
- A first-party NVRHI/DX12 Specular pass adapted from the MIT-licensed FidelityFX
  SDK v1.1.4 SSSR and Hybrid Reflections implementation, with Frostbite 2015
  ratio reuse and filtered importance sampling.
- Settings, UI, commands, statistics, timings, build/shader manifests, source
  and reference tests, notices, durable design documentation, and exact runtime
  validation required by the new behavior.
- High-value reversible policies: sparse ray selection, compact/indirect work,
  hierarchical screen tracing, low-confidence-only inline RayQuery fallback,
  roughness/distance culling, and screen-radiance reuse.

Non-goals:

- AMD FidelityFX denoising, temporal accumulation copied from AMD, async
  acceleration-structure work, BLAS LOD policy, particles, transparency, or a
  general bindless material-system rewrite.
- Editing Donut, changing the DirectX 12 product backend, publishing, pushing,
  opening a pull request, merging, or releasing.
- Claiming performance improvements without a matched accepted benchmark.

Affected subsystems and paths:

- Directional shadows and PBR direct-light consumption under `src/heitz_*`,
  `src/pbr_*`, and their focused tests.
- Ray traced sky visibility and environment-light consumption under
  `src/ray_traced_sky_visibility*`, `src/pbr_*`, and focused tests.
- New first-party `src/specular_*` pass/settings/shaders and `tests/specular_*`.
- Renderer orchestration/UI/commands/statistics in `src/uvsr.cpp`, the command
  catalog, CMake, shader manifest, README, UI reference, notices, and docs.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, `CMakeLists.txt`, `src/shaders.cfg`, `README.md`,
  `THIRD_PARTY_NOTICES.md`, all PBR deferred-lighting files, screen-space
  visibility integration, command catalog, global source-contract tests,
  documentation, and this execution plan.

## Baseline

- Canonical repository/remote: live remote `origin/main` was recorded by the
  parent Main Lighting task at `51bad6a7fccb88afd46078dcd95ef4bbdb509d7a`.
  This task intentionally follows that exact feature candidate, not local
  divergent `main`.
- Local versus remote state: task branch starts from the remote commit and is
  dirty by design with the exact inherited candidate. No Git publication or
  history operation is authorized.
- Verified source commit/build: inherited patch identity
  `42C4522FA5B7393B53F8CC193E8129C8EA82C8D1BD548743380A6BE5520DA30D`;
  source executable
  `work/main-lighting-denoising-controls/build-nrd/bin/uvsr.exe`; SHA-256
  `3A587F93935C1AFA1BD1DBD000A2F381B6018D21089A46A0291AC494FBB56C21`.
  The parent recorded 303 packaged shaders and 39 of 39 Release CTests passing;
  runtime smoke was pending.
- GPU, scene, camera, resolution, and settings preset when relevant: runtime
  comparison will use one bundled scene, fixed camera, resolution, TAA state,
  light/sky state, and sample count recorded with captures. No performance
  claim is planned unless the documented benchmark preflight is available.
- Known pre-existing failures: none in the inherited automated suite. Runtime
  evidence was pending because another worktree held the renderer/GPU lease.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Main Lighting candidate | Exact dirty patch and executable identity above | Frozen | Whole task |
| Frostbite deck | Local 2015 deck slides 43-55, 64, 67-75, and 84-87 | Audited | Specular estimator and filtering |
| Heitz 2018 | Matched `U`, `S_N`, `U_N` semantics and Stachowiak attribution | Audited | Sun and sky estimator repair |
| FidelityFX SDK | Official v1.1.4 tag `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`; preserve MIT attribution | Audited | SSSR and Hybrid pass |
| World representation | Coherent ready TLAS gated by Allow Ray Traversal | Integrated in inherited candidate | Hybrid RayQuery |
| Environment/PBR | Scene color excludes indirect specular only while Specular is operational; DFG applied once | Frozen below | Resolve and composite |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `SpecularSettings` owns nested `SssrSettings` and
  `HybridReflectionSettings`. Both `enabled` defaults are false. Hybrid mode
  owns the one final result whenever enabled; standalone SSSR is used only when
  Hybrid is disabled, preventing double composition.
- `SpecularReflectionInputs` supplies the resolved single-sample PBR G-buffer,
  pre-specular linear-HDR scene color, environment probe resources, view,
  optional coherent TLAS, and the global ray-traversal permission/readiness.
- `SpecularReflectionResult` returns one RGBA16F indirect-specular texture plus
  dispatch/routing/status metadata. An unavailable pass returns no result and
  must not suppress existing environment specular.
- Each selected ray record contains screen or environment hit identity, hit
  distance, sampled reflection direction or equivalent footprint, source PDF,
  confidence, and validity. Deferred lookup off may additionally store source
  radiance; it never changes the ray set.
- Spatial resolve evaluates destination GGX BRDF divided by the stored source
  PDF, normalizes by the finite positive weight sum, then applies UVSR's
  preintegrated environment BRDF once. Ratio Estimator off retains the same
  rays and performs ordinary source-sample averaging before the same one-time
  BRDF factor.
- The HDR mip footprint is monotonic in perceptual roughness and hit distance,
  expands for grazing-angle specular elongation, and uses the exact Frostbite
  elongation factor `lerp(saturate(NdotV * 2), 1, sqrt(roughness))` on the cone
  tangent. One signed Trace And Filter Bias narrows the sampled lobe while
  increasing resolve filtering by the compensating amount.
- Hybrid routing performs hierarchical screen tracing first. Samples below the
  Screen Space Confidence threshold enter an indirect RayQuery dispatch only
  when its Ray Tracing toggle and the global traversal/TLAS gates are true.
  Projectable RT hits reuse the HDR pyramid; offscreen/unshadeable hits use the
  environment. This is the audited AMD “don't reshade” route and does not
  require a Donut bindless-scene mutation.
- Sun shadows use the same configured sample count for both modes. Ratio on
  returns `U * S_N / U_N`; Ratio off returns `S_N`; hard shadows remain one
  center-direction visibility sample. NRD hit-distance output is a separate raw
  first-ray guide and does not choose the displayed estimator.
- Sky visibility becomes separate diffuse and specular environment estimates,
  or an equivalent packed result with distinct consumers. Ratio on uses matched
  visibility-weighted and unoccluded environment/BRDF sums; Ratio off uses the
  full stochastic sum at the same sample count. NRD's raw guide remains
  independent and fail-open.

## Mandatory New-Element Intake

- Policy versions: `AGENTS.md` `2026-07-30.1`; UI reference
  `2026-08-06.1`. Recheck both at handoff.
- State owner and defaults: one `SpecularSettings` instance owned by the
  renderer. Top-level Specular follows Diffuse. Both effect enable toggles are
  false; both subdrawers and both Advanced drawers initially collapse. Ratio
  Estimator and Deferred Color Lookup default true inside an inactive effect.
  Hybrid Ray Tracing defaults true inside inactive Hybrid.
- Visible SSSR controls: Enable boolean; Ratio Estimator boolean; Deferred
  Color Lookup boolean; Max Roughness float `[0,1]` default `0.22`; Samples Per
  Quad enum `1/2/4` default `1`; Depth Thickness world-view float default
  `0.05`. Advanced owns Max Traversal Iterations `[8,256]` default `128`, Min
  Traversal Occupancy `[0,64]` default `4`, Most Detailed Mip `[0,5]` default
  `0`, Trace And Filter Bias `[-2,2]` default `0`, and diagnostic view.
- Visible Hybrid controls: Enable boolean; Ray Tracing boolean; Screen Space
  Confidence `[0,1]` default `0.9`; Max Roughness `[0,1]` default `0.22`.
  Advanced owns Ray Max Distance in meters, Ray Bias in meters, confidence
  backface/depth rejection, compact dispatch, and diagnostic view. Only
  controls backed by active consumers will survive integration.
- Command paths: `specular.sssr.*` and `specular.hybrid.*`, with `get`, `set`,
  and `reset` where consistent with the existing catalog. Enum values use the
  visible integer choices. No control is intentionally UI-only.
- Reset behavior: each effect reset restores its complete owning settings
  struct without changing the other effect. Individual leaf resets use the one
  authoritative settings defaults. The top-level drawer has no synthetic
  master enable.
- Direct consumers: Specular pass constants, permutation/resource selection,
  ray routing, resolve, composite, statistics, command catalog, and unavailable
  status. Derived consumers are scene-color deferral and history invalidation;
  no setting changes scene or representation ownership.
- Mutation class: numeric and boolean sampling controls change constants or
  shader routing; Enable, Deferred Color Lookup, and compact-dispatch policy may
  change pass-owned resource topology only after ImGui composition. Ray
  Tracing never builds the TLAS and cannot bypass Allow Ray Traversal.
- Animation/scroll/popup/focus: existing animated drawer, toggle-region, combo,
  tooltip, reset, and deferred structural presentation helpers own all motion
  and mutation. Both skins expose identical state; no hidden placeholder
  controls are added.
- Loading/unavailable behavior: stored settings persist. The pass reports
  disabled, preparing, MSAA unavailable, no resolved G-buffer, ray traversal
  unavailable, TLAS preparing/failed, or ready in direct product language. SSSR
  remains usable when Hybrid ray traversal is unavailable.
- Focused tests and live exercise: settings/default/range/reset tests; ratio,
  PDF, DFG-once, mip monotonicity, elongation, bias coupling, reverse-Z, routing,
  fail-open, renderer order, UI order/animation/commands; then Amp and OG live
  exercise of every endpoint and unavailable state on the exact executable.
- Unchanged behavior: inactive Specular is bit-for-bit routed through the
  inherited lighting path; existing debug views, direct specular, diffuse
  transport, raw denoiser producers, AA order, and global representation
  ownership remain unchanged.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| ESTIMATOR-1 | `/root/ratio_estimator_audit` | Shared task worktree | Frozen dirty candidate | Sun/sky producer files and focused tests listed below | Frozen estimator contract | Complete |
| SPECULAR-1 | `/root/specular_integration_audit` | Shared task worktree | Frozen dirty candidate | New `src/specular_*` and `tests/specular_*` only | Frozen Specular API/resource contract | Complete |
| INTEGRATE-1 | `/root` | Task branch/worktree | Both handoffs | Shared renderer, PBR, UI, build, docs, and tests | ESTIMATOR-1 and SPECULAR-1 | Complete |
| REVIEW-1 | `/root/amd_reflections_audit` | Read-only frozen candidate | Integrated candidate | None | INTEGRATE-1 | Complete |

## Assignment Contracts

### ESTIMATOR-1: Implement Correct Sun and Sky Estimator Producers

- Owner/thread: `/root/ratio_estimator_audit`
- Branch/worktree: shared task worktree; no branch operations
- Base commit/state: `51bad6a` plus inherited patch identity `42C452...` and
  this execution plan
- Read scope: current Heitz, sky, PBR/environment helpers, ratio shared helper,
  denoising signal contract, and focused tests
- Write scope: `src/directional_shadow_settings.h`,
  `src/heitz_ratio_estimator_shadows.cpp`,
  `src/heitz_ratio_estimator_shadows.h`,
  `src/heitz_ratio_estimator_shadows_cb.h`,
  `src/heitz_ratio_estimator_shadows_cs.hlsl`,
  `src/ray_traced_sky_visibility.cpp`,
  `src/ray_traced_sky_visibility.h`,
  `src/ray_traced_sky_visibility_settings.h`,
  `src/ray_traced_sky_visibility_cs.hlsl`, focused existing
  `tests/heitz_ratio_estimator*` and
  `tests/ray_traced_sky_visibility*`, plus new private helper files beginning
  `src/environment_ratio_estimator_` or focused tests beginning
  `tests/environment_ratio_estimator_` when needed
- No-touch scope: PBR deferred files, `src/uvsr.cpp`, Specular files, command
  catalog, CMake, shader manifest, docs, Git state, build trees, processes, and
  every file not named above
- Build directory and runtime/GPU/resource lease: none; coordinator builds and
  runs the renderer
- Dependencies already integrated: exact inherited candidate and frozen
  estimator semantics in this plan
- Interface/invariant contract: both modes retain identical sample count and
  ray sequence; sun result distinguishes normalized modulation from stochastic
  radiance/modulation explicitly; hard shadow unchanged; raw first-ray hit
  distance is independent; sky returns distinct diffuse/specular data or a
  clearly documented equivalent contract and never labels a binary average as
  a ratio estimator; invalid denominators fail open without NaN/Inf
- Deliverable: apply-patch-only producer/math implementation and deterministic
  reference/source-contract tests, with exact PBR integration steps for root
- Done when: unequal BRDF/PDF and colored-environment fixtures prove estimator
  distinction, N=1 behavior is intentionally documented, sample-count parity
  is tested, and existing fail-open/hit-distance behavior is retained
- Required verification: source inspection and any focused non-build checks;
  coordinator compiles and runs CTest
- Allowed Git and external actions: none
- Stop and report if: correct sky radiance requires a shared PBR binding or
  consumer outside write scope; implement the owned producer side and report
  the exact interface rather than editing the hotspot
- Handoff revision/artifact: paired sun/sky producer and focused test patch
- Handoff acknowledged by/on: `/root`, 2026-08-07

### SPECULAR-1: Implement the First-Party Specular Core

- Owner/thread: `/root/specular_integration_audit`
- Branch/worktree: shared task worktree; no branch operations
- Base commit/state: `51bad6a` plus inherited patch identity `42C452...` and
  this execution plan
- Read scope: current NVRHI passes, G-buffer/PBR helpers, world representation,
  FidelityFX v1.1.4 audited source, and Frostbite/Heitz contracts recorded here
- Write scope: new files matching `src/specular_*` and `tests/specular_*` only
- No-touch scope: all existing files, PBR integration, `src/uvsr.cpp`, command
  catalog, CMake, shader manifest, docs/notices, Git state, build trees,
  processes, and every new file not beginning with the assigned prefixes
- Build directory and runtime/GPU/resource lease: none; coordinator builds and
  runs the renderer
- Dependencies already integrated: frozen settings, API, routing, ratio, mip,
  DFG, and fail-open contracts in this plan
- Interface/invariant contract: one pass/API and result as specified above;
  Hybrid supersedes standalone SSSR but shares no double contribution; no AMD
  denoiser/temporal/variance surface; no Donut edit or bindless-scene rewrite;
  reverse-depth-aware HiZ; RayQuery only for compact low-confidence work; pass
  owns and safely recreates all resources; no unavailable state suppresses the
  inherited environment specular
- Deliverable: apply-patch-only new settings/pass/shaders/reference tests and a
  concise list of required coordinator bindings, pass order, and manifest rows
- Done when: new code implements the complete selected route, deterministic
  CPU tests prove ratio/PDF/DFG/mip/elongation/routing invariants, and all source
  bindings/resource formats are internally consistent
- Required verification: source inspection only; coordinator compiles/tests
- Allowed Git and external actions: none
- Stop and report if: a correct pass needs an existing/shared file change,
  shader/resource contract conflicts with NVRHI, or AMD source carries a license
  ambiguity; report rather than broadening write scope
- Handoff revision/artifact: 13 new `src/specular_*` and `tests/specular_*` files
- Handoff acknowledged by/on: `/root`, 2026-08-07

### INTEGRATE-1: Integrate Renderer, UI, Build, and Documentation

- Owner/thread: `/root`
- Branch/worktree: task worktree
- Base commit/state: frozen inherited candidate plus accepted worker handoffs
- Read scope: complete repository and research artifacts
- Write scope: all shared hotspots reserved above and narrowly required
  first-party integration files after worker ownership is released
- No-touch scope: `donut/`, unrelated worktrees/branches, generated build/work
  artifacts, remotes, and publication
- Build directory and runtime/GPU/resource lease: coordinator-only task-local
  build directory; one UVSR process after clean lease/preflight
- Dependencies already integrated: ESTIMATOR-1 and SPECULAR-1 before touching
  their producer/consumer interfaces
- Interface/invariant contract: all frozen contracts and UI intake above
- Deliverable: composed locally verified candidate
- Done when: every goal and verification row has exact evidence
- Required verification: focused/full Release checks, shader/package audit,
  runtime smoke and captures, docs validators, diff checks, independent review
- Allowed Git and external actions: local reversible edits only; no commit,
  push, PR, merge, release, or deployment
- Stop and report if: correctness requires an unstated visible quality tradeoff,
  user/peer work overlaps, or a safe runtime lease cannot be obtained
- Handoff revision/artifact: local executable SHA-256 `9F0505E2DB2F372A76AF96E268B03B17043A5CB64B5175BB587C679E97FC3CA1`
- Handoff acknowledged by/on: verification recorded 2026-08-07

### REVIEW-1: Independently Review the Frozen Rendering Candidate

- Owner/thread: `/root/amd_reflections_audit`
- Branch/worktree: read-only task worktree
- Base commit/state: exact frozen integrated candidate
- Read scope: complete task diff and relevant CPU/HLSL/resource/build contracts
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, and remotes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: INTEGRATE-1 frozen candidate
- Interface/invariant contract: review estimator math, PDF/weight provenance,
  DFG-once, mip/elongation, hybrid routing, scene-color feedback, reverse depth,
  resource lifetime, fail-open behavior, UI truthfulness, and license/build
  completeness
- Deliverable: prioritized findings with exact evidence
- Done when: no P0 through P2 finding remains
- Required verification: source/diff inspection and existing test evidence
- Allowed Git and external actions: read-only only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: initial findings plus clean focused follow-up review
- Handoff acknowledged by/on: `/root`, 2026-08-07

## Integration Order

1. Freeze research, math, routing, and UI contracts in this plan.
2. Implement sun/sky producer corrections and the new Specular core in disjoint
   files, then acknowledge both distilled handoffs and release ownership.
3. Integrate PBR environment deferral, pass lifecycle/order, commands/UI,
   statistics, timings, manifests, tests, attribution, README, and durable docs.
4. Freeze writes, run focused and full verification, then obtain independent
   review and repair only substantiated defects.
5. Rebuild stale evidence, live-exercise the exact candidate, reconcile this
   plan, and archive it without publishing.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Estimator semantics | Deterministic unequal-weight, N=1, sample-parity, finite-denominator tests | Focused Heitz/sky tests and source contracts | Passed in focused and complete Release test runs |
| Specular math | Ratio/PDF normalization, DFG-once, mip monotonicity, elongation, bias, and near-plane tests | Focused Specular tests | Passed, including behind-eye endpoint regression |
| Hybrid routing | Reverse-Z HiZ, confidence, RT gate/TLAS, environment fallback and no-double-add tests | Specular and renderer source-contract tests | Passed |
| UI and commands | Exact drawer order/default closure, controls, reset/catalog counts, Amp/OG lifecycle | Required UI source/animation/dropdown tests | Passed; 189 commands and 21 Specular endpoints |
| Build and package | Complete Release app, exact shader bundle, required MIT notice | Release build and production shader bundle checks | Passed; 311 first-party shader permutations compiled and runtime bundle synchronized |
| Integrated candidate | All project tests, title-case validator, diff/no-dead-reference checks | Full Release CTest and repository validators | Passed: 41 of 41 Release tests and 1,326 document headings/lead-ins validated |
| Runtime image behavior | Fixed scene/camera/settings captures with estimator and reflection routes toggled | `tools/launch_uvsr.ps1 -BuildDirectory build-specular-nrd` | Passed in Sponza Decorated at Benchmark Position 1; sun/sky ratio, NRD bypass, SSSR, Hybrid, TLAS, and Analytic Lights exercised |
| High-risk correctness | Independent P0 through P2 source/resource review | REVIEW-1 | Two P1 and one P2 repaired; focused follow-up found no remaining P0 through P2 |

For performance work, record:

- No performance claim is part of the done condition. If an exploratory timing
  comparison is accepted later, use the repository's full benchmark hygiene,
  exact executable hashes, Position 1 control, fixed scene/settings, three
  clean repetitions, total frame time, and per-pass costs.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-07 | Preserve and branch from the exact dirty NRD candidate rather than a commit selected by time. | The user supplied an executable SHA. Its source worktree, patch identity, build, and parent evidence were recoverable exactly; another commit would not be the named base. | Whole task |
| 2026-08-07 | Treat current sun and sky no-op behavior as estimator-definition defects, not a missing analytic-light registration. | The sun is already an analytic DirectionalLight; at one sample the existing ratio and one-ray path are identical. Sky currently averages binary visibility and has no matched BRDF/PDF denominator. | ESTIMATOR-1 |
| 2026-08-07 | Report analytic lights as scene inventory beside, but not inside, BVH/TLAS/BLAS statistics. | Analytic lights are lighting inputs rather than acceleration-structure geometry. This matches the useful RTXPT concept without making a false structural claim. | INTEGRATE-1 |
| 2026-08-07 | Use FidelityFX SDK v1.1.4 as the adaptation source. | It is the final complete 1.x implementation and fixes concrete SSSR errors present in v1.1.0; 2.x no longer carries the sample implementation. | SPECULAR-1 |
| 2026-08-07 | Make Hybrid own the single final reflection solution when both effect toggles are enabled. | Layering standalone SSSR and Hybrid would double-count indirect specular; forcing mutual UI mutation would violate independent stored settings. Precedence preserves both settings while producing one result. | SPECULAR-1, INTEGRATE-1 |
| 2026-08-07 | Use screen-radiance reuse plus environment fallback for RT hits instead of expanding Donut bindless scene shading. | The current Scene intentionally has no DescriptorTableManager, so material-correct offscreen reshading is not available. AMD's documented don't-reshade route supplies a coherent low-risk hybrid result within first-party scope. | SPECULAR-1 |
| 2026-08-07 | Exclude AMD's denoiser and low-value risky optimizations. | The user explicitly excluded the denoiser and requested only large, reversible optimization wins. | SPECULAR-1 |
| 2026-08-07 | Keep first-ray sun/sky guides independent and bypass their scalar NRD frontends. | Replacing paired ratio/full lobe outputs with one scalar guide erased the estimator choice. A compatible denoiser must preserve both lobe contracts; flashlight SIGMA remains active. | ESTIMATOR-1, INTEGRATE-1 |
| 2026-08-07 | Clip long SSSR endpoints in homogeneous space before projection. | Scene-diagonal or 100-meter endpoints can pass behind the eye despite containing a valid nearer hit. The shared helper handles positive W plus forward and reversed D3D near planes. | SPECULAR-1, INTEGRATE-1 |

## Progress and Handoffs

| Date/Time | Task Or Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-07 | `/root` preflight | Complete | Exact inherited patch and SHA identities above | Git status/worktrees/active plans/Coming Soon/instructions inspected | Spawn disjoint writers |
| 2026-08-07 | `/root/ratio_estimator_audit` | Complete; read-only ownership released | Source-level estimator diagnosis | Producer/consumer/shader inspection | Implement frozen correction |
| 2026-08-07 | `/root/amd_reflections_audit` | Complete; read-only ownership released | FidelityFX v1.1.4 pass/resource/control audit | Official source and local integration inspection | Reuse agent for frozen review |
| 2026-08-07 | `/root/specular_integration_audit` | Complete; read-only ownership released | Renderer/PBR/UI/lifecycle audit | Source/resource/order inspection | Implement new core |
| 2026-08-07 | `/root` research | Complete | Rendered 92-slide Frostbite deck; audited Heitz and AMD PDFs | Slide text plus visual review of all slides and focused formula pages | Contracts frozen above |
| 2026-08-07 | `/root` integration | Complete | Exact local executable SHA-256 `9F0505E2DB2F372A76AF96E268B03B17043A5CB64B5175BB587C679E97FC3CA1` | NRD Release build and 311 first-party shaders succeeded | Run final complete validation after archive |
| 2026-08-07 | `/root/amd_reflections_audit` | Complete; ownership released | Initial and focused follow-up reviews | Confirmed NRD overwrite, near-plane endpoint, and tooltip defects were repaired | No remaining P0 through P2 |
| 2026-08-07 | `/root` runtime | Complete | Sponza Decorated, Benchmark Position 1, exact task executable | SSSR, Hybrid, sky/sun ratio toggles with NRD selected, Specular timing, ready TLAS, and 29 Analytic Lights observed | No performance claim made |

## Risks and Escalation Triggers

- A shader can compile while estimator provenance, PDF space, resource state,
  reverse-depth comparison, or indirect-specular ownership remains wrong.
  Deterministic reference tests and independent review are required.
- Deferring inherited environment specular too early would produce a black or
  incomplete image when Specular is unsupported. Readiness gates suppression.
- The current non-bindless scene cannot material-shade arbitrary offscreen RT
  hits. The accepted first implementation uses AMD-style radiance reuse and an
  environment fallback; documentation and UI must not imply full offscreen
  material reshading.
- Runtime and GPU work must pause if another UVSR task or user owns the machine.

Stop and ask the user if:

- Correctness requires choosing between materially different visible outcomes
  not resolved by the requested Frostbite/AMD behavior.
- Required licensed source cannot be used under its published MIT terms.
- User or peer changes overlap this task's isolated worktree unexpectedly.
- Full hybrid correctness proves impossible without a broad Donut or scene
  material-system redesign rather than the bounded screen-radiance route.

## Completion

- Final integrated commit: intentionally none unless separately authorized
- Verification summary: NRD Release build and shader compile passed; complete
  CTest and repository validators are recorded in the final handoff
- Independent review: clean after repairing two P1 findings and one P2 finding
- Coming Soon/documentation update: reconciled in README and durable design docs
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: performance benchmarking and visual
  product acceptance remain optional; neither was required for this local
  technically verified candidate
- Active ownership released: all worker and reviewer ownership released
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/specular-ratio-estimator.md`
