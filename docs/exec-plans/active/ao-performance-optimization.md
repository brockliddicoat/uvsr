# AO Performance Optimization

This plan coordinates the measured AO-only optimization project requested for
the canonical UVSR visibility-bitmask renderer.

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ao-performance-optimization`
  at `C:\Users\brock\Documents\Codex\2026-07-16\5f43205-codex-text-link-https-github\work\uvsr`
- Base commit: `5f43205ecfe00e31fd64af34cad0f031472a224c`
- Started: 2026-07-16
- Last updated: 2026-07-16
- Planned archive:
  `docs/exec-plans/completed/ao-performance-optimization.md`

## Goal and Done Condition

Goal: reduce complete AO-only GPU work while preserving the 32-sector
visibility-bitmask product architecture, reference behavior, all consumers, and
an inactive-path zero-cost contract for every optional experiment.

Done when:

- [x] Every supplied, Activision, XeGTAO, and newly researched candidate has an
  evidence-backed optimization-ledger disposition.
- [x] Measurement controls and per-stage median/p95 capture distinguish depth,
  trace, reconstruction, application, composition, and residual work.
- [x] The requested advanced drawers and curated profiles select CPU-side
  permutations and conditionally allocate/dispatch optional resources.
- [x] Defensible exact, numerical, and algorithmic candidates are implemented
  in that order, with the reference generic path retained.
- [ ] Fixed-count, noise, depth, packed-edge, temporal, and fused-application
  experiments have focused correctness coverage and individual smoke/performance
  sanity evidence without a Cartesian combination sweep.
- [ ] Release build, shader compilation, focused tests, documentation validation,
  and reference-versus-candidate smoke captures pass.
- [ ] Target Intel Arc/Xe-LPG measurements not available locally are clearly
  handed off without unsupported performance claims.

## Scope

In scope:

- AO depth preparation, visibility trace, reconstruction, history, full-resolution
  application, composition attribution, resource traffic, and GPU timing.
- Shared fixed-count and traversal improvements for GI, AO+GI, and later bounces
  when they preserve near-to-far source ownership.
- Same-engine diagnostic controls, including constant/depth-only/bitmask-only
  trace floors, temporal copy, bilinear reconstruction, isolated composition,
  fused application, horizon GTAO, and closest-match GTAO profiles when safe.
- Curated math, precision, scheduler, depth, dispatch, resource, and reconstruction
  permutations with explicit classifications and costs.
- Documentation, tests, benchmark export, shader evidence, and local commits.

Non-goals:

- Replacing the production visibility-bitmask estimator with horizon-only GTAO.
- Changing canonical AO composition semantics or GI source ownership.
- Running an exhaustive profile-combination benchmark matrix.
- Claiming target-GPU gains without a controlled Core Ultra 9 185H run.
- Push, pull request, merge, release, deployment, or unrelated cleanup.

Affected subsystems and paths:

- `README.md`, `docs/screen-space-visibility.md`,
  `docs/ao-optimization-ledger.md`, and this execution plan.
- `src/screen_space_visibility*`, depth hierarchy, indirect composition,
  visibility estimator/projection/mask/noise helpers, renderer settings/UI, and
  build-time shader permutations.
- Visibility reference tests, benchmark tooling, shader inspection tooling, and
  optional noise assets generated for retained packed/vector modes.

Shared hotspots reserved for the coordinator:

- Renderer visibility orchestration, constant-buffer/resource contracts, UI,
  CMake integration, documentation, and final commits.
- Existing visibility tests that overlap PR #11.
- Shared shader helpers that overlap PR #10.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`,
  default branch `main`.
- Local versus remote state: clean and equal at
  `5f43205ecfe00e31fd64af34cad0f031472a224c` before branch creation.
- Verified source commit/build: user-named canonical verified commit `5f43205`;
  local Release build verification pending.
- Target preset: Intel Core Ultra 9 185H integrated Arc/Xe-LPG, PBR Sponza
  Decorated Benchmark Position 1, 1920x1080, half-resolution AO, GI/adaptive/
  temporal/spatial disabled initially, eight total samples, exponent 2.0, radius
  3.0, thickness 0.5, Toroidal Blue Noise or retained packed FAST candidate.
- User-reported baseline: about 2.5 ms AO-only; AO+GI about 3.1 ms; trace about
  1.2 ms; reported filter about 1.1 ms and other about 0.9 ms even with temporal
  and spatial filtering disabled. These are hypotheses to reproduce, not local
  measured results.
- Known pre-existing failures: none established; build and test baseline pending.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Shared visibility shader helpers, PR #10 | Do not import into the exact requested base; minimize mechanical helper churn and reconcile after local completion | Open/in review | Trace, depth, temporal, and filter shaders |
| Visibility degenerate tests, PR #11 | Preserve its additive coverage and avoid conflicting rewrites; reconcile after local completion | Open/in review | Mask, projection, and sampling tests |
| Bilateral-grid local tone mapping | Keep ownership of local tone mapping and AgX bindings separate; fused AO must preserve the pre-AgX visibility-composition contract | Active roadmap work; branch not visible on remote | AO application/composition |
| Target Intel Xe-LPG hardware | User performs most final performance testing; local data must identify the actual adapter and cannot be generalized | External measurement pending | Final performance disposition |
| Primary-source research | Activision, XeGTAO, visibility-bitmask, vendor, and related implementation evidence | In progress | Ledger and permutation selection |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve the reference constant-buffer layout, bindings, resource formats,
  dispatches, history behavior, and output equations when Reference is selected.
- Optional shaders are selected on the CPU; optional textures, bindings, passes,
  and history exist only for the active profile.
- Settings changes that alter history reset it; profiles explicitly set every
  relevant field and report their exact permutation key and validity.
- Raw/temporal AO remains range-safe for estimators that may exceed one; R8 is
  allowed only for validated bounded/final values. GI accumulation and ownership
  remain FP32/reference unless a separately validated experiment is selected.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Primary-source inventory and optimization ledger | `/root/primary_research` | Shared checkout | `5f43205` | `docs/ao-optimization-ledger.md` only | Public primary sources and supplied prompt | Complete and reconciled |
| Renderer architecture audit | `/root/renderer_architecture` | Shared read-only checkout | `5f43205` | None | Required repository files | Complete |
| Performance profile and resource-plan model | `/root/performance_plan` | Shared checkout | `5f43205` plus coordination commit | New `src/visibility_performance_plan.*` and `tests/visibility_performance_plan_tests.cpp` only | Architecture findings and reference contracts | Complete and integrated |
| Frame-correlated benchmark statistics | `/root/benchmark_statistics` | Shared checkout | `5f43205` plus coordination commit | New `src/visibility_benchmark_statistics.*` and `tests/visibility_benchmark_statistics_tests.cpp` only | Existing asynchronous timer behavior and prompt accounting rules | Complete and integrated |
| Coordination, implementation, integration, and verification | `/root` | Task branch/shared checkout | `5f43205` | All task-owned paths | Research and architecture findings | Active |

## Assignment Contracts

### Primary-Source Inventory and Optimization Ledger

- Owner/thread: `/root/primary_research`
- Branch/worktree: shared checkout, read-only
- Base commit/state: `5f43205`, before substantive implementation
- Read scope: all named primary sources plus relevant vendor papers, source code,
  presentations, and profiling guidance discovered during further research
- Write scope: `docs/ao-optimization-ledger.md` only
- No-touch scope: all other repository files, Git history, external publication
- Deliverable: exhaustive source-referenced optimization inventory with
  classification and additional candidates
- Done when: Activision, XeGTAO, and further-research items are individually
  represented with implementation details sufficient for ledger disposition
- Required verification: direct source/page/symbol references rather than
  secondary summaries
- Allowed Git and external actions: read-only repository and web access
- Stop and report if: a source is inaccessible, licensing is restrictive, or a
  claim cannot be supported by a primary source

### Performance Profile and Resource-Plan Model

- Owner/thread: `/root/performance_plan`
- Branch/worktree: shared checkout
- Base commit/state: `5f43205` plus the task coordination commit
- Read scope: current visibility settings/resources and focused test conventions
- Write scope: new `src/visibility_performance_plan.h`,
  `src/visibility_performance_plan.cpp`, and
  `tests/visibility_performance_plan_tests.cpp` only
- No-touch scope: all existing files, CMake/shader integration, renderer UI,
  orchestration, documentation, Git history, and external publication
- Deliverable: typed curated-profile settings, explicit CPU-side resolved
  resource/pass plan, stable permutation/history keys, packing helpers, and
  exhaustive focused tests for optional-off and fixed-profile contracts
- Done when: the new files are warning-clean and the tests cover Reference,
  diagnostic, Exact, Numerical, and Algorithmic profile resolution without a
  Cartesian permutation surface
- Required verification: standalone compilation where practical; coordinator
  integrates into CMake and renderer after review
- Allowed Git and external actions: read-only inspection, scoped file creation,
  and local build commands; no commits, runtime launches, or remote actions
- Stop and report if: an existing-file change is required or the requested model
  would silently alter the reference path

### Frame-Correlated Benchmark Statistics

- Owner/thread: `/root/benchmark_statistics`
- Branch/worktree: shared checkout
- Base commit/state: `5f43205` plus the task coordination commit
- Read scope: existing timer rings, UI statistics, and benchmark requirements
- Write scope: new `src/visibility_benchmark_statistics.h`,
  `src/visibility_benchmark_statistics.cpp`, and
  `tests/visibility_benchmark_statistics_tests.cpp` only
- No-touch scope: all existing files, renderer integration, documentation,
  CMake, Git history, runtime sessions, and external publication
- Deliverable: frame-identity-aware stage sample collection, warmup and measured
  windows, complete-frame median/p95 calculations, CSV/JSON-ready summary data,
  and focused tests proving totals come from per-frame sums rather than summed
  stage percentiles
- Done when: delayed/out-of-order stage samples associate correctly, incomplete
  frames are excluded, and reset/profile metadata behavior is covered
- Required verification: standalone compilation where practical; coordinator
  integrates the module and export path after review
- Allowed Git and external actions: read-only inspection, scoped file creation,
  and local build commands; no commits, launches, or remote actions
- Stop and report if: an existing-file change is required or correlation cannot
  be established with the available timer interface

### Renderer Architecture Audit

- Owner/thread: `/root/renderer_architecture`
- Branch/worktree: shared checkout, read-only
- Base commit/state: `5f43205`, before substantive implementation
- Read scope: mandated visibility sources plus orchestration, UI, resources,
  timings, tests, and build integration
- Write scope: none
- No-touch scope: repository files, Git history, runtime launches, publication
- Deliverable: pass/resource/permutation map, likely timing-attribution root
  causes, implementation touchpoints, and invariant risks
- Done when: every major requested experiment has a concrete code landing point
  or a technically justified blocker
- Required verification: file/symbol references from the exact base
- Allowed Git and external actions: read-only inspection
- Stop and report if: repository state differs from the recorded base or active
  changes appear

### Coordination, Implementation, Integration, and Verification

- Owner/thread: `/root`
- Branch/worktree: `codex/ao-performance-optimization` in the shared checkout
- Base commit/state: clean `5f43205` plus task coordination documentation
- Read scope: whole repository and required primary-source material
- Write scope: all task-owned files listed above
- No-touch scope: unrelated roadmap work, remote branches/PRs, published history,
  canonical `main`, release/deployment state
- Build directory and runtime/GPU/resource lease: local `build`; only task-owned
  renderer processes; do not disturb user-owned sessions
- Dependencies already integrated: none beyond canonical `5f43205`
- Interface/invariant contract: reference path unchanged; bitmask and finite
  thickness retained; GI near-to-far ownership retained; optional off-state has
  zero GPU/resource/pass cost; no unrestricted permutation Cartesian product
- Deliverable: locally committed build ready for user-led target-hardware testing
- Done when: implementation, focused verification, ledger, documentation, and
  evidence are complete to the locally provable level
- Required verification: Release build, focused tests, shader compilation and
  inspection, per-feature smoke/performance checks, image/reference checks,
  heading validator, and clean scoped diff
- Allowed Git and external actions: local branch, edits, builds, tests, launches,
  screenshots, benchmark artifacts, and focused local commits; no push/PR/merge
- Stop and report if: an invariant cannot be preserved, target behavior requires
  destructive/unrelated work, licensing blocks implementation, or external
  authority is required

## Integration Order

1. Establish measurement attribution, diagnostic floors, benchmark capture, and
   reference-profile identity.
2. Implement exact shader/resource/permutation improvements and fixed-count
   specializations, validating AO/GI ownership before approximations.
3. Implement separately classified numerical math/precision experiments.
4. Implement optional algorithmic depth/noise/edge/reconstruction/fusion/GTAO
   experiments with CPU-side selection and conditional resources.
5. Complete focused tests and individual smoke/performance sanity checks.
6. Reconcile documentation, ledger dispositions, shader evidence, UI costs,
   target-hardware handoff, and local commits.

## Expected Performance Impact

The ranked forecast is maintained in
[`docs/ao-optimization-ledger.md`](../../ao-optimization-ledger.md#expected-performance-impact)
and summarized in
[`docs/screen-space-visibility.md`](../../screen-space-visibility.md#expected-performance-impact).
It uses the user's 2.5-2.7 ms baseline, labels every range as an engineering
forecast, treats overlapping savings as non-additive, and ranks every distinct
implemented runtime family. No forecast is promoted to a result until a
controlled Intel Core Ultra 9 185H/Xe-LPG export records the exact build,
profile/permutation, warm-up, measured-frame count, median, p95, and quality
review.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact requested base | Commit identity and clean pre-edit state | `git rev-parse HEAD`; `git status` | Confirmed `5f43205`; task branch created |
| Canonical reference retained | Reference resource/dispatch/output contract and reference image comparison | Unit tests plus reference profile smoke capture | Plan/resource-invariance tests and gross-corruption capture pass; exact same-phase GPU comparison remains pending |
| Fixed AO/GI order and masks | Mask, AO, claimed-bit, source-owner, raw-GI fixtures for 8/12/16/20 and generic fallback | Focused visibility tests | 589,824 deterministic comparisons plus AO, GI-only, and AO+GI consumer-profile smoke pass |
| Packed edge correctness | All byte combinations, reverse depth, boundaries, slopes, depth/normal discontinuity, receiver mapping | Focused edge tests | Focused CPU fixtures and packed-edge profile smoke pass |
| Optional off-state zero cost | Reference pipeline key, bindings, allocations, dispatches, history, and output unchanged | Resource counters, code inspection, and reference capture | Plan-level reference-invariance assertions pass; target physical-resource capture remains pending |
| Isolated feature sanity | No correctness regression and directionally useful local timing/traffic result per optimization | One-at-a-time smoke/benchmark actions; no permutation matrix | Fixed (5), noise (6), reconstruction (12), math (2), and comprehensive (53) sequences completed; slower optional experiments are retained and documented |
| Generated shader evidence | Source optimizations survive DXC; static code/load/store changes are recorded without treating them as runtime counts | `tools/measure_visibility_dxil.ps1`; [`docs/visibility-dxil-evidence.md`](../../visibility-dxil-evidence.md) | Core 18-variant DXIL report complete; physical Intel registers/spills/occupancy pending |
| Release readiness | All task targets build; tests and title-case validator pass | CMake Release build, `ctest`, heading validator | Pre-commit Release build, 13/13 tests, DXIL generation, 413-row ledger audit, and heading validator pass; committed-identity rebuild/smoke remains the final handoff check |
| Target performance | Median/p95 on the exact Intel target after warmup with saved key/settings/clock state | User-led benchmark export, 120 warmup and at least 240 measured frames | Pending external handoff |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-16 | Use exact canonical commit `5f43205` on `codex/ao-performance-optimization` | The user explicitly named the verified base; importing newer open PRs would change provenance | All |
| 2026-07-16 | Record PR #10 and PR #11 as integration dependencies without cherry-picking | Both overlap task paths but are unmerged and absent from the requested base; their additive/mechanical intent can be preserved during later reconciliation | Shader and test edits |
| 2026-07-16 | Keep performance claims adapter-scoped | The local GPU may not be the target Xe-LPG device, and the prompt forbids extrapolating target wins | Benchmarking and report |
| 2026-07-16 | Run isolated smoke and traffic/performance checks, not combination permutations | This follows the user's explicit verification priority while still gathering evidence for each retained feature | Verification |
| 2026-07-16 | Keep Mixed-Precision AO and XeGTAO Closest Match unavailable | No compiled mixed-precision trace or complete XeGTAO depth/noise/denoise/application pipeline exists; substituting the conservative FP32 filter or analytic horizon control would mislabel evidence | Profiles, UI, and ledger |
| 2026-07-16 | Use an outer effect timer plus a signed residual | A sum of named queries omits transitions and gaps, while summing stage percentiles mixes frames; the outer envelope and frame-correlated stage sum expose both scopes honestly | Benchmark statistics and export |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-16 | Coordinator preflight | Complete | `5f43205` and this plan | Full user prompt read; remote branches, open PRs, worktrees, roadmap, and execution plans reviewed | Baseline build and required-file audit next |
| 2026-07-16 | Primary-source inventory | Complete | `docs/ao-optimization-ledger.md` | Primary sources and current-build source evidence reconciled | Target measurements remain pending |
| 2026-07-16 | Renderer architecture audit | Complete | Read-only pass/resource/permutation map | Exact-base and current-diff source inspection | Findings incorporated into curated implementation |
| 2026-07-16 | Profile/resource plan and benchmark statistics | Integrated | `src/visibility_performance_plan.*`; `src/visibility_benchmark_statistics.*` | Focused test targets exist; final aggregate verification pending | Preserve pending result placeholders until final run |
| 2026-07-16 | Renderer and UI implementation | Integrated | Fixed/group/radius-clamp/packed-noise/packed-edge/fused/diagnostic shaders; four advanced drawers; sequential runners; CLI/export | Packaged-shader audit and 53-entry comprehensive smoke pass | Controlled target timing and manual product-quality review remain pending |
| 2026-07-16 | Generated shader evidence | Partially complete | `docs/visibility-dxil-evidence.md`; `tools/measure_visibility_dxil.ps1` | Reproducible 18-variant optimized-DXIL table; packed FAST, filter algebra, fusion, diagnostics, and fixed code growth confirmed | Target Intel native ISA, GRF, spill, SIMD width, and occupancy remain pending |
| 2026-07-16 | Documentation reconciliation | Complete | README, design, 413-row ledger, DXIL evidence, and this plan | Dispositions, ranked forecasts, deferred work, current smoke evidence, 413 unique IDs, and Title Case reconcile | Preserve target timing and physical-ISA limitations in the final handoff |

## Risks and Escalation Triggers

- Optional-profile breadth can create an unbounded shader Cartesian product;
  curate supported combinations and retain the generic fallback.
- Fixed traversal can accidentally reorder GI ownership; test owner identities,
  not only final masks.
- Lower-precision raw AO can silently clamp estimators above one; retain R16F
  unless a range-preserving encoding is demonstrated.
- Fused application can overlap local tone-mapping integration or change
  composition ordering; preserve the existing lighting write and pre-AgX stage.
- Target-GPU register/occupancy/ISA evidence may require Intel GPA/driver tooling
  unavailable locally; distinguish missing target evidence from implementation
  failure.
- PR #10 and PR #11 may merge while this task is active; recheck live state
  before final integration and document conflicts rather than rewriting them.

Stop and ask the user if:

- A required result would need push, pull request, merge, release, deployment, or
  modification of an unrelated active project.
- Preserving the product bitmask, finite thickness, existing estimators, AO
  composition, or GI ownership becomes incompatible with the requested feature.
- A missing preference changes visible default behavior rather than adding an
  optional classified experiment.

## Completion

- Final integrated commit: pending
- Verification summary: pending
- Independent review: pending
- Coming Soon/documentation update: active entry added; final baseline pending
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: pending
- Active ownership released: no
- Archived to completed/abandoned path: pending
