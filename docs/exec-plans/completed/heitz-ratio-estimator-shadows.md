# Heitz Ratio-Estimator Shadows

## Status

- State: completed initial implementation; product behavior is superseded and
  extended by `docs/exec-plans/active/heitz-shadow-follow-up.md`
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/heitz-stochastic-shadows` in `C:/Users/brock/Documents/Codex/2026-08-04/https-casual-effects-com-research-heitz2018shadow`
- Base commit: `578496047c4753077e36f39a68df8c2c323cee58` (`origin/main` fetched 2026-08-04)
- Started: 2026-08-04
- Last updated: 2026-08-05
- Planned archive: `docs/exec-plans/completed/heitz-ratio-estimator-shadows.md`

## Post-Completion Reconciliation

The first implementation recorded below established the representation and
ratio-estimator foundations, but subsequent product feedback intentionally
changed several of its visible and internal decisions. The current candidate:

- exposes screen-space and ratio-estimator shadows as independent producers,
  including both-off and both-on states;
- combines simultaneous producer results at the deferred-lighting consumer;
- runs the ratio estimator in one fused dispatch with no spatial denoiser;
- has no private temporal history, leaving final-color TAA as the sole temporal
  accumulator;
- advances frame-local stochastic phases independently of TAA and exposes two
  emitter-noise choices plus sample animation;
- has a dedicated receiver-gated hard-shadow branch and integer rates from one
  through 64 samples per pixel; and
- derives a true raster triangle normal for a single world-space Ray Bias origin
  displacement, with `RayDesc.TMin = 0`.

Verification and acceptance for that replacement candidate belong to the
active follow-up plan. Historical results in this file describe the initial
checkpoint only and must not be used to certify the replacement executable.

## Goal and Done Condition

Goal:

Add a first-party directional-light shadow technique based on Heitz et al.'s
correlated stochastic ratio estimator, plus a reusable world-space
representation layer for ray-tracing consumers.

Done when:

- [x] The Shadows drawer exposes independent screen-space and ray-traced
  ratio-estimator producers, and deferred lighting defines both-off, either-only,
  and both-on composition for the exact primary directional-light contribution.
- [x] The stochastic path evaluates correlated RGB unshadowed and shadowed
  estimates, accumulates matched numerator/denominator state before guarded
  division, and applies the result to UVSR's analytic direct-light estimate.
- [x] A new Representation drawer exposes active BVH, BLAS, and TLAS policies
  through deferred structural controls and command equivalents.
- [x] Unsupported hardware fails neutral and explains the unavailable state;
  the existing screen-space and disabled paths remain available.
- [x] Release builds, targeted tests, the full CTest suite, shader packaging,
  runtime smoke coverage, and an independent rendering review pass.

## Scope

In scope:

- DirectX 12 inline ray queries against a reusable TLAS.
- Static and dynamic triangle BLAS construction from Donut scene buffers.
- RGB Heitz/Stachowiak estimator history, correlated sampling, denominator
  protection, fractional blue-noise duty scheduling, and TAA-independent phase
  progression.
- Independent directional shadow controls, representation controls, command palette
  entries, centralized defaults, reset behavior, tooltips, and documentation.

Non-goals:

- Reflections or a shared reflection estimator consumer.
- Ray-traced point, spot, or environment-light shadows.
- Procedural geometry, transparency-aware any-hit shading, Vulkan, DirectX 11,
  deployment, pushing, or opening a pull request.

Affected subsystems and paths:

- `src/uvsr.cpp`, deferred PBR lighting, directional-visibility contracts, and
  new world-representation and ratio-estimator pass files.
- `src/shaders.cfg`, new compute shaders, CMake shader packaging, tests,
  `README.md`, and the UI reference procedure.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, `CMakeLists.txt`, `src/shaders.cfg`, UI contracts,
  documentation, integration, build output, and runtime GPU use.

## Baseline

- Canonical repository/remote: `origin` for UVSR; pinned Donut submodule at
  `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`.
- Local versus remote state: task branch is clean except coordinator-owned
  paper-analysis scratch under `tmp/`, and starts exactly at fetched
  `origin/main`; the separate canonical checkout was six commits behind before
  fetch and was not modified.
- Verified source commit/build: source commit is known; this worktree has not
  yet produced its own baseline build.
- GPU, scene, camera, resolution, and settings preset when relevant: record at
  runtime smoke verification; use a bundled Sponza scene, a finite directional
  angular size, and both the default and ray-traced shadow settings.
- Known pre-existing failures: none established in this worktree.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Heitz et al. paper and SIGGRAPH talk | Correlated RGB ratio estimator; filter numerator and denominator before division | Inspected | Shadow pass and tests |
| Published reference code | Estimator-order and parameter reference only; reimplement rather than copy | Inspected | Fused shadow pass |
| Donut/NVRHI | Pinned submodule; ray-query, acceleration-structure, and transform APIs | Inspected | Representation and shaders |
| UVSR deferred lighting | Consume independent screen-space and ratio-estimator producers and combine them after BRDF evaluation | Decided | PBR compute shaders |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `DirectionalLightVisibility` exposes optional screen-space and ratio-estimator
  slots with explicitly encoded scalar-or-RGB frame-local contracts.
- Deferred PBR constant layouts gain an encoding field and all matching C++ and
  HLSL declarations must remain 16-byte aligned.
- The ray pass binds one TLAS plus resolved G-buffer inputs and emits a
  single-sample RGB directional-light modulation texture in one dispatch.
- New shader binaries must appear in the production and runtime manifests.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Paper analysis | `/root/paper_analysis` | Shared, read-only | `5784960` | None | Published sources | Complete |
| Renderer architecture | `/root/renderer_architecture` | Shared, read-only | `5784960` | None | Repository and Donut | Complete |
| Test and risk audit | `/root/test_risk_audit` | Shared, read-only | `5784960` | None | Repository contracts | Complete |
| Implementation and integration | `/root` | Task worktree | `5784960` | All task-owned edits | Explorer handoffs | Complete |

## Assignment Contracts

### Paper Analysis: Extract the Technique Contract

- Owner/thread: `/root/paper_analysis`
- Branch/worktree: shared task worktree, read-only
- Base commit/state: `5784960`, coordinator scratch may be visible
- Read scope: supplied talk, companion paper, published project page, and
  reference-code archive
- Write scope: none
- No-touch scope: repository files and Git state
- Dependencies already integrated: none
- Interface/invariant contract: report equations, pass order, estimator edge
  cases, and sampling details with source locations
- Deliverable: concise implementation contract and fidelity risks
- Done when: final handoff is acknowledged
- Allowed Git and external actions: read-only research only
- Stop and report if: a source conflicts materially with the paper

### Renderer Architecture: Map the UVSR Integration

- Owner/thread: `/root/renderer_architecture`
- Branch/worktree: shared task worktree, read-only
- Base commit/state: `5784960`, initialized pinned submodules
- Read scope: renderer, UI, PBR, Donut scene, and NVRHI ray-tracing APIs
- Write scope: none
- No-touch scope: repository files and Git state
- Dependencies already integrated: none
- Interface/invariant contract: preserve independent producer ownership and
  centralized consumer composition, and avoid changes under `donut/`
- Deliverable: resource lifecycle, bindings, and integration map
- Done when: final handoff is acknowledged
- Allowed Git and external actions: read-only inspection only
- Stop and report if: pinned APIs cannot support inline ray queries

### Test and Risk Audit: Define Evidence and Failure Modes

- Owner/thread: `/root/test_risk_audit`
- Branch/worktree: shared task worktree, read-only
- Base commit/state: `5784960`, initialized pinned submodules
- Read scope: test targets, source contracts, runtime manifests, UI procedure,
  and renderer lifecycle
- Write scope: none
- No-touch scope: repository files and Git state
- Dependencies already integrated: none
- Interface/invariant contract: map every modified contract to targeted and
  full-suite evidence
- Deliverable: ordered verification matrix and high-risk review checklist
- Done when: final handoff is acknowledged
- Allowed Git and external actions: read-only inspection only
- Stop and report if: required runtime evidence cannot be produced locally

## Integration Order

1. Land settings, representation ownership, and acceleration-structure tests.
2. Land the ratio-estimator pass, shaders, PBR RGB modulation, and shader tests.
3. Connect renderer lifecycle, independent-producer routing, UI, commands, and
   documentation.
4. Build and run targeted tests, full CTest, runtime smoke coverage, and an
   independent review; repair regressions and rerun affected evidence.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Estimator math and guards | Deterministic CPU reference tests | Build/run new ratio-estimator tests | Passed in `uvsr_heitz_ratio_estimator_reference` and source-contract tests |
| Representation lifecycle and transforms | Unit/source-contract checks plus validation-layer runtime | Targeted representation and renderer tests | Passed; live Sponza reached Ready with 128/128 BLAS entries and 128 TLAS instances |
| UI topology, tooltips, and deferred controls | Required UI trio and catalog tests | Required Release build and CTest filters | Passed in the full 35-test Release suite; live drawers and commands inspected |
| Shader source and package completeness | Compiled DXIL and manifest tests | Release shader/app build and bundle tests | Passed; 263 main and 46 specialist first-party shaders compiled and packaged |
| Existing rendering contracts | PBR, screen-space, visibility, and AA tests | Targeted targets plus full CTest | Passed in the full 35-test Release suite |
| Runtime behavior | App launch, bundled scene, technique switching, representation rebuilds, MSAA/TAA/AO/GI combinations | Exact Release `uvsr.exe` with logged smoke procedure | Passed on RTX 4090 Laptop GPU; ratio estimator, TLAS rebuild/refit, MSAA neutral gate, Screen Space fallback, TAA on/off, AO on/off, GI on/off, and default restoration exercised |
| Documentation integrity | Title-case, README line-count, and whitespace checks | Repository document scripts and `git diff --check` | Passed; 1,209 headings/lead-ins checked, counts current, and no whitespace errors |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-04 | Replace the initial exclusive selector with independent producer toggles | Product feedback requires both techniques to run simultaneously or both be disabled; deferred PBR now owns explicit composition instead of last-writer behavior | UI, renderer, PBR |
| 2026-08-04 | Carry RGB estimator data and apply RGB modulation after direct BRDF evaluation | The paper identifies scalar visibility as an approximation that fails under directional color-response correlation | Shadow shaders, PBR |
| 2026-08-04 | Keep the representation layer consumer-neutral and lazy | Future ray techniques need the same BLAS/TLAS without coupling the scene representation to shadows | Representation, lifecycle |
| 2026-08-05 | Add cost-aware private numerator/denominator history | Fractional rates need a full-resolution result without hidden TAA state; animated integer rates use private history only with TAA below 64 spp, while hard and 64-spp modes retain none | Shadow pass, TAA |
| 2026-08-04 | Preserve opaque/nonopaque BLAS geometry flags but treat nonopaque candidates as solid in this first shadow query | Full material-aware candidate testing requires a separate bindless candidate-evaluation contract; the shared representation retains the material distinction for future consumers | Representation, ray query |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-04 | `/root` | Complete | Local task-owned implementation based on `5784960`; exact Release executable in `build/bin/uvsr.exe` | Full Release build, 35/35 CTest, shader bundle, document checks, and live GPU smoke | Intentionally local and uncommitted |
| 2026-08-05 | Explorer agents | Superseded by follow-up review | Initial paper fidelity, renderer lifecycle, and test-risk reviews received | The replacement review found material-history and shared-decoder follow-ups; resolution and re-review are tracked by the active plan | Ownership released |

## Risks and Escalation Triggers

- Ray-query support varies by adapter; unsupported devices must keep a neutral
  factor and an actionable UI state.
- Mesh upload, skinning, or scene transform changes can stale BLAS/TLAS content;
  every active update policy needs a real renderer consumer.
- RGB visibility changes a mature PBR binding/constant contract and therefore
  requires explicit multisample and single-sample regression coverage.
- Very small denominators, zero-contribution pixels, and finite-sample bias can
  produce NaNs or bright outliers unless numerator and denominator are
  accumulated as a matched pair and guarded.

Stop and ask the user if:

- The pinned DirectX 12/NVRHI path cannot expose a reusable TLAS without
  modifying the pinned `donut/` dependency.
- Correct implementation would require a materially broader transparency or
  material-system redesign rather than an isolated future extension point.

## Completion

- Final integrated commit: intentionally uncommitted unless the user asks to
  save it
- Verification summary: full all-target Release build succeeded; 35/35 CTest
  passed; 263 main and 46 specialist first-party shaders compiled; live Sponza
  smoke passed ratio-estimator rendering, Representation readiness, TLAS
  rebuild/refit, MSAA fallback/restoration, TAA on/off, AO on/off, and GI
  on/off on an RTX 4090 Laptop GPU; defaults restored afterward
- Independent review: initial paper fidelity, renderer architecture, and
  test/risk reviews completed; replacement-candidate review is owned by the
  active follow-up plan
- Coming Soon/documentation update: README, advanced settings, UI reference,
  and the dedicated implementation/fidelity note updated
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: reflection consumer is explicitly
  deferred
- Active ownership released: yes
- Archived to completed/abandoned path: yes
