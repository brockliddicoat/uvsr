# Ray-Traced Sky Visibility

## Status

- State: technically verified candidate awaiting product acceptance
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/ray-traced-sky-visibility` in
  `C:/Users/brock/OneDrive/Documents/uvsr/work/ray-traced-sky-visibility`
- Base commit: `efff7ecaf45024d949998567b480c131e348abef`
- Started: 2026-08-06
- Last updated: 2026-08-06
- Planned archive:
  `docs/exec-plans/completed/ray-traced-sky-visibility.md`

## Goal and Done Condition

Goal: add a standalone full-resolution current-frame ray-traced sky-visibility
pass that estimates the cosine-weighted geometric-normal hemisphere visibility
of each valid surface pixel and modulates diffuse IBL at both of its required
consumption points without changing the proven Heitz shadow pass.

Done when:

- [x] One ray per surface pixel is the default, and exact 1, 2, 4, 8, 16, 32,
      and 64 sample counts average opaque-hit zero and miss one into scalar
      `R8_UNORM` visibility.
- [x] The pass reuses the Heitz full-resolution ray-query, shared TLAS,
      blue-noise choices, reverse-Z reconstruction, geometric-normal bias,
      representable-position offset, ray distance, and fail-open behavior
      without refactoring or behaviorally changing the shadow pass.
- [x] Visibility affects only diffuse IBL before final-image composition and
      before that diffuse IBL becomes GI source radiance; direct lighting,
      direct shadows, specular IBL, emissive, ambient occlusion, and traced
      indirect lighting remain unchanged.
- [x] Disabled or unavailable operation supplies exact white visibility, and
      the feature owns no temporal history, cache, denoiser, reconstruction,
      visibility bitmask, bent normal, spherical harmonics, adaptive schedule,
      shadowed-pixel restriction, or directional-shadow dependency.
- [x] The bottom of the Sky drawer shows only a default-off Enable control when
      collapsed and exposes shadow-style Sample Count, Noise Pattern, Animate
      Samples, and Ray Bias controls when enabled/expanded.
- [ ] Source-contract, deterministic math, build, shader-package, runtime,
      resize/fallback, UI, and independent rendering review evidence pass for
      the exact candidate artifact.

## Scope

In scope:

- UVSR-owned sky-visibility settings and command-catalog entries.
- A separate full-resolution `R8_UNORM` output, compute shader, dispatch, timing,
  resize-safe lifetime, and white fallback.
- Reuse of the existing consumer-neutral Representation/TLAS service.
- Deferred diffuse-IBL consumption in both single-sample and MSAA paths, plus
  the GI source-radiance path.
- Focused tests and durable renderer documentation.

Non-goals:

- Any changes to the Heitz ratio-estimator shadow algorithm, pass, resources,
  settings, or directional-shadow composition.
- Temporal accumulation, caching, denoising, reconstruction, visibility-bitmask
  integration, bent normals, spherical harmonics, adaptive scheduling, or
  shadowed-pixel-only tracing.
- Direct lighting/shadows, specular IBL, emissive, AO, or traced-indirect-light
  modulation.
- Changes under `donut/`, release packaging, or unrelated renderer cleanup.

Affected subsystems and paths:

- `src/uvsr.cpp` and UVSR settings/command headers.
- New first-party sky-visibility C++ and HLSL files under `src/`.
- Deferred PBR lighting bindings and shaders.
- `src/shaders.cfg`, `CMakeLists.txt`, focused tests, and renderer documentation.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/shaders.cfg`, and `src/uvsr.cpp`.
- Representation/TLAS ownership and all CPU/HLSL binding contracts.
- Deferred lighting inputs and both single-sample/MSAA shader consumers.
- All builds, shader packaging, UVSR windows, screenshots, and GPU checks.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main`
  `efff7ecaf45024d949998567b480c131e348abef` after a 2026-08-06 fetch.
- Local versus remote state: isolated feature worktree starts equal to live
  `origin/main`; the original checkout's divergent local `main` is preserved.
- Verified source commit/build: the Heitz feature commit `ca4bd62` has recorded
  full Release, 35-test, shader, TLAS, fallback, and live Sponza evidence; run a
  fresh task-local baseline for current `efff7ec` before candidate comparison.
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090
  Laptop GPU; Sponza Decorated; Benchmark Position 1; 1920x1080; factory
  defaults, with explicit sky-visibility settings recorded per capture.
- Known pre-existing failures: none yet established for this isolated base.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical target | `efff7ec` | Integrated as base | Whole feature |
| Proven Heitz ray-query path | `ca4bd62` contracts | Audited and unchanged | Sky pass |
| Representation/TLAS service | Existing consumer-neutral readiness contract | Audited and integrated | Sky dispatch |
| Deferred diffuse IBL | Exact pre-final and pre-GI-source insertion points | Audited and integrated | Lighting integration |
| Open PRs #10 and #11 | No runtime ownership collision | Confirmed | Publication ordering |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Sky visibility is scalar full-resolution `R8_UNORM`; `1.0` means fully sky
  visible and `0.0` means fully occluded.
- Invalid pixels, disabled state, unavailable ray query/TLAS, and failed
  dispatch remain white and therefore reproduce the prior lighting result.
- The sample-count domain is exactly `{1, 2, 4, 8, 16, 32, 64}`, default `1`.
- Every sample is a cosine-weighted direction about the geometric normal; each
  contributes binary opaque-hit zero or miss one before arithmetic averaging.
- The texture is consumed only as a multiplier on diffuse IBL at the two named
  points and has no directional-shadow binding or dependency.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `sky-pass-implementation` | `/root/shadow_reference_audit` | Shared feature worktree | `efff7ec` | New sky-pass files only | Frozen Heitz and representation contracts | Complete |
| `lighting-consumers` | `/root/lighting_ui_audit` | Shared feature worktree | `efff7ec` | Deferred PBR and screen-space composite files only | Frozen diffuse-IBL contract | Complete |
| `sky-tests` | `/root/verification_design` | Shared feature worktree | `efff7ec` | Two new sky test files only | Completed verification design and integrated producer API | Complete |
| `integrate` | `/root` | Feature worktree | `efff7ec` | Renderer/UI/build/tests/docs; review all combined changes | Audit decisions and worker handoffs | Active |
| `independent-review` | `/root/integration_review` | Read-only shared feature worktree | Candidate | None | Integrated candidate | Complete |

## Assignment Contracts

### Sky Tests: Add Deterministic and Source-Contract Coverage

- Owner/thread: `/root/verification_design`
- Branch/worktree: shared feature worktree on `codex/ray-traced-sky-visibility`.
- Base commit/state: `efff7ec` plus integrated producer/consumer working diff.
- Write scope: new `tests/ray_traced_sky_visibility_tests.cpp` and
  `tests/ray_traced_sky_visibility_source_contract_tests.cpp` only.
- No-touch scope: all source, existing tests, CMake/config/docs, `donut/`, Git
  index/refs, build trees, processes, and external state.
- Interface contract: cover exact defaults/domains, deterministic noise,
  cosine-hemisphere math, binary averaging/R8 quantization, origin/bias math,
  producer/full-resolution/fail-open contracts, renderer/TLAS/MSAA/phase
  wiring, diffuse-only consumers, UI controls/commands, and forbidden features.
- Required checks: source self-review only; coordinator owns registration,
  compilation, and all broader verification.
- Handoff: changed files, covered contracts, assumptions, and risks; release
  paths only after coordinator acknowledgement.

### Sky Pass Implementation: Add the Independent Producer

- Owner/thread: `/root/shadow_reference_audit`
- Branch/worktree: shared feature worktree on `codex/ray-traced-sky-visibility`.
- Base commit/state: `efff7ec` plus coordinator-owned plan/roadmap changes.
- Write scope: new `src/ray_traced_sky_visibility_settings.h`,
  `src/ray_traced_sky_visibility_cb.h`, `src/ray_traced_sky_visibility.h`,
  `src/ray_traced_sky_visibility.cpp`, and
  `src/ray_traced_sky_visibility_cs.hlsl` only.
- No-touch scope: every existing file, `donut/`, Git index/refs, build trees,
  running processes, and external state.
- Interface contract: standalone full-resolution `R8_UNORM` ray-query producer;
  exact sample domain and defaults; independent noise phase; Heitz-equivalent
  reconstruction, bias, offset, ray distance, flags, and fail-open behavior;
  no dependency on directional visibility.
- Required checks: source self-review only; coordinator owns compilation.
- Handoff: changed files, public interfaces, assumptions, and risks; release
  paths only after coordinator acknowledgement.

### Lighting Consumers: Apply Visibility to Diffuse Environment Lighting

- Owner/thread: `/root/lighting_ui_audit`
- Branch/worktree: shared feature worktree on `codex/ray-traced-sky-visibility`.
- Base commit/state: `efff7ec` plus coordinator-owned plan/roadmap changes.
- Write scope: `src/pbr_deferred_lighting_pass.h`,
  `src/pbr_deferred_lighting_pass.cpp`, `src/pbr_deferred_lighting_cb.h`,
  `src/pbr_deferred_lighting_cs.hlsl`,
  `src/pbr_deferred_lighting_msaa_cs.hlsl`,
  `src/screen_space_visibility.h`, `src/screen_space_visibility.cpp`,
  `src/screen_space_visibility_cb.h`, and
  `src/screen_space_indirect_composite_cs.hlsl` only.
- No-touch scope: renderer/UI integration, new producer files, build/test/docs,
  `donut/`, Git index/refs, build trees, processes, and external state.
- Interface contract: optional full-resolution single-sample `R8_UNORM` input;
  bind it at `t22` in both PBR variants and `t12` in the composite; branch to
  literal white when inactive/invalid; conditionally multiply only
  `environmentDiffuse`, never direct/specular/emissive/AO/traced-indirect terms.
- Required checks: source self-review only; coordinator owns compilation.
- Handoff: changed files, binding/constant contracts, assumptions, and risks;
  release paths only after coordinator acknowledgement.

### Shadow Reference Audit: Freeze Reuse Contracts

- Owner/thread: `/root/shadow_reference_audit`
- Branch/worktree: read-only shared feature worktree
- Base commit/state: clean `efff7ec`
- Read scope: Heitz C++/HLSL/constants/tests, representation/TLAS code, and
  related completed documentation.
- Write scope: none.
- No-touch scope: all files, refs, indices, build trees, processes, and external
  state.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: canonical Heitz implementation.
- Interface/invariant contract: identify exact code/equations/resources to reuse
  while keeping the shadow pass unchanged.
- Deliverable: file/line map, invariants, hazards, and minimal reuse design.
- Done when: every user-named reuse requirement and fail-open condition maps to
  existing source evidence.
- Required verification: read-only source and test inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: reuse would require shadow-pass refactoring or a disputed
  public contract.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

### Lighting and UI Audit: Locate Exact Integration Points

- Owner/thread: `/root/lighting_ui_audit`
- Branch/worktree: read-only shared feature worktree
- Base commit/state: clean `efff7ec`
- Read scope: deferred lighting C++/HLSL/MSAA, GI source flow, Sky drawer,
  settings catalog, and focused tests/docs.
- Write scope: none.
- No-touch scope: all files, refs, indices, build trees, processes, and external
  state.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: current PBR and UI contracts.
- Interface/invariant contract: identify the two diffuse-IBL-only consumption
  points and matching shadow-style UI without affecting excluded terms.
- Deliverable: binding/UI/test map and exact old-result preservation strategy.
- Done when: single-sample, MSAA, and GI-source paths are accounted for.
- Required verification: read-only source and test inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: requested isolation conflicts with current lighting
  topology or an overlapping task owns the same contract.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

### Verification Design: Define Acceptance Evidence

- Owner/thread: `/root/verification_design`
- Branch/worktree: read-only shared feature worktree
- Base commit/state: clean `efff7ec`
- Read scope: CMake/test registration, shader contracts, launcher, existing
  Heitz runtime evidence, and relevant test files.
- Write scope: none.
- No-touch scope: all files, refs, indices, build trees, processes, and external
  state.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: current build and test infrastructure.
- Interface/invariant contract: every requested behavior has deterministic or
  runtime evidence, including exact-white fallback and excluded lighting terms.
- Deliverable: ordered targeted/full build, test, runtime, visual, and fallback
  matrix with commands and failure conditions.
- Done when: evidence distinguishes shader compilation, pass correctness,
  resource lifetime, UI behavior, and product appearance.
- Required verification: read-only build/test/script inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: a behavior cannot be observed without expanding scope.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

### Integrate: Build the Isolated Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/ray-traced-sky-visibility` in the feature worktree.
- Base commit/state: clean `efff7ec` plus this coordinator-owned plan/roadmap.
- Read scope: all first-party source/tests/docs and pinned dependency APIs.
- Write scope: all task-owned first-party integration files, tests, plan, and
  required documentation.
- No-touch scope: `donut/`, unrelated worktrees/branches, unrelated active
  plans, binary assets, and remote publication until the verified candidate is
  ready.
- Build directory and runtime/GPU/resource lease: task-local `build`; `/root`
  exclusively owns builds, shader packaging, UVSR windows, and GPU checks.
- Dependencies already integrated: live main and completed read-only audits.
- Interface/invariant contract: separate pass, exact-white fail-open, diffuse
  IBL-only consumption, and no behavior change to Heitz shadows.
- Deliverable: reviewed candidate commit/artifact and integration evidence.
- Done when: every goal checkbox maps to passing evidence.
- Required verification: targeted tests, full Release build/test, shader bundle,
  runtime fallbacks/resize, UI inspection, visual comparison, and independent
  rendering review.
- Allowed Git and external actions: local branch/worktree edits and commits;
  publication is deferred until the exact candidate and GitHub workflow are
  ready for the user-requested `main` destination.
- Stop and report if: a required interface conflicts with unrelated work,
  shadow behavior would change, or product acceptance is required after a
  candidate-changing repair.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Integration Order

1. Freeze existing Heitz, TLAS, lighting, and UI contracts from audits.
2. Add independent settings/constants/resource/pass/shader and focused math
   tests.
3. Bind and dispatch only when enabled and representation-ready; preserve white
   on all inactive/failure paths.
4. Consume the scalar factor at both diffuse-IBL-only sites in single-sample and
   MSAA rendering.
5. Add UI/command controls, shader/build registration, source contracts, and
   durable documentation.
6. Build, test, inspect runtime behavior, obtain independent review, repair, and
   rerun affected evidence.
7. Commit the exact verified candidate, reconcile this plan/roadmap, and use the
   established GitHub integration path for `main`.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Exact sample-count and averaging math | Deterministic unit/source contracts for 1 through 64 and binary hit/miss mean | Focused sky-visibility tests | Passed in Release CTest |
| Shadow implementation unchanged | Zero diff in existing Heitz pass files plus passing Heitz tests | Diff and focused CTest | Passed; all named Heitz paths are unchanged |
| Shader/pass/resource correctness | All variants compile/package; format, dispatch, resize, and failure paths inspected | Release build and source/runtime contracts | Build and contracts passed; live resize was stopped after unexpected input |
| Diffuse IBL-only modulation | Single-sample/MSAA source contracts and visual A/B with excluded terms checked | Focused tests and runtime matrix | Contracts passed; live 1x and 4x MSAA rendering passed |
| UI and defaults | Default-off, collapsed bottom-of-Sky layout and command round trip | UI tests and runtime inspection | Passed; default-off showed only Enable, then all four controls appeared |
| Old result exact when inactive | Disabled and unavailable branch proof plus live off/on/off comparison | Runtime fallback matrix | Source proof passed; live disabled result returned immediately |
| Integrated quality | Independent P0 through P3 rendering/lifetime review | Read-only reviewer handoff | Passed with no remaining findings after one fail-open repair |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-06 | Base the feature on fetched live `origin/main` `efff7ec` in an isolated linked worktree. | The original checkout has two unique local documentation commits and is 23 commits behind; moving or rewriting it could lose unrelated work. | Whole feature |
| 2026-08-06 | Freeze interfaces through read-only audits, then split new producer files and diffuse-IBL consumer files into disjoint writer leases. | The audits resolved the binding and fallback contracts; disjoint ownership now improves speed without competing edits, while renderer/TLAS/UI/build integration remains coordinator-owned. | Sky pass, lighting consumers, integration |
| 2026-08-06 | Treat open PRs #10 and #11 as publication-order checks only. | They touch screen-space helper extraction and tests, not this ray-query producer or deferred IBL contract. | Integration/publication |
| 2026-08-06 | Drive contribution history solely from the actual producer result. | The independent reviewer found that optimistic state could repeatedly reset AA when pipeline creation or binding failed; the dispatch gate now checks pass support and only a real result changes contribution state. | Fail-open integration |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-06 | `/root` preflight | Complete | `efff7ec` | Fetch, worktree, branch, PR, task, roadmap, and active-plan audit | Run three bounded source audits |
| 2026-08-06 | `/root/shadow_reference_audit` producer | Complete | Five new `ray_traced_sky_visibility*` files | Static producer contract smoke | Coordinator accepted and released paths |
| 2026-08-06 | `/root/lighting_ui_audit` consumers | Complete | Nine deferred/composite files | `git diff --check`; focused binding and diffuse-only assertions | Coordinator accepted and released paths |
| 2026-08-06 | `/root/verification_design` tests | Complete | Two new sky reference/source-contract tests | C++17 self-review; Release build; full CTest | Coordinator accepted and released paths |
| 2026-08-06 | `/root/integration_review` review | Complete | Combined working-tree candidate | High-risk shader, TLAS, binding, MSAA, fail-open, and diffuse-only review | No remaining P0 through P3 findings |
| 2026-08-06 | `/root` automated verification | Complete | `build/bin/uvsr.exe`, SHA-256 `F1B4FED497E37B68833F2ED133112D92667162DC60F05C592ED11B1EC1EBCA7C` | Full Release build; 38 of 38 CTest; shader bundles; README counts; 1,260 Title Case checks; diff checks | Exact artifact live-smoked next |
| 2026-08-06 | `/root` runtime smoke | Partial | Exact candidate at Sponza Benchmark Position 1 | Default off; 1 and 64 samples; both noise modes; fixed/animated toggle; off fallback; 4x MSAA independence | Stopped desktop automation after unexpected camera input during resize; leave exact build open for product review |

## Risks and Escalation Triggers

- The diffuse IBL value may feed final lighting and GI through shared code; the
  factor must be applied once before both consumers without touching specular or
  traced indirect terms.
- Ray-query availability, TLAS construction timing, resize, and first-frame
  failures must not leave stale or black visibility.
- A full-resolution 64-ray mode is intentionally expensive; correctness and
  exposure are required, not a default performance claim.
- The local canonical checkout is divergent and must remain untouched.

Stop and ask the user if:

- Correct diffuse-only placement requires a visible product tradeoff beyond the
  stated scope.
- Publishing to `main` would require choosing between materially incompatible
  accepted work or bypassing an established integration owner.

## Completion

- Final integrated commit: pending.
- Verification summary: Release build, 38 of 38 CTest, shader bundles,
  documentation validators, source invariants, and bounded runtime smoke passed;
  live resize and product acceptance remain.
- Independent review: complete with no remaining P0 through P3 findings.
- Coming Soon/documentation update: active entry added; completion pending.
- Pushed/PR/merged, or intentionally local: pending.
- Remaining experiments or follow-ups: pending.
- Active ownership released: pending.
- Archived to completed/abandoned path: pending.
