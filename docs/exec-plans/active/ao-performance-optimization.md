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
- [x] A repaired, explicitly approximate Activision PS4 pipeline and a separate
  pinned Intel XeGTAO 1.30 High source port are implemented without relabeling
  either as the UVSR bitmask estimator.
- [x] Fixed-count, noise, depth, packed-edge, temporal, and fused-application
  experiments have focused correctness coverage and individual smoke/performance
  sanity evidence without a Cartesian combination sweep.
- [x] Release build, shader compilation, focused tests, documentation validation,
  and reference-versus-candidate smoke captures pass.
- [x] Target Intel Arc/Xe-LPG measurements not available locally are clearly
  handed off without unsupported performance claims.

This done condition means the curated manual-test candidate and every ledger
disposition are documented to the locally provable level. It does not mean that
every isolatable experiment was implemented, measured, or rejected; the
specific feasible follow-ups remain explicit in
[`docs/ao-optimization-ledger.md`](../../ao-optimization-ledger.md#ledger-status).

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
  the current local Release build and 13 CTest targets pass. The 58-entry
  all-profile smoke, controlled nine-profile 600-frame sequence, independent
  two-profile precision repeat, and clean final static captures also complete.
  Dynamic motion/disocclusion, target Xe-LPG evidence, and the final committed-
  identity rebuild/smoke remain separate handoff checks.
- Target preset: Intel Core Ultra 9 185H integrated Arc/Xe-LPG, PBR Sponza
  Decorated Benchmark Position 1, 1920x1080, half-resolution AO, GI/adaptive/
  temporal/spatial disabled initially, eight total samples, exponent 2.0, radius
  3.0, thickness 0.5, Toroidal Blue Noise or retained packed FAST candidate.
- User-reported baseline: about 2.5 ms AO-only; AO+GI about 3.1 ms; trace about
  1.2 ms; reported filter about 1.1 ms and previously unattributed work about
  0.9 ms even with temporal and spatial filtering disabled. These are
  hypotheses to reproduce, not local measured results.
- Known pre-existing failures: none established.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Shared visibility shader helpers, PR #10 | Do not import into the exact requested base; minimize mechanical helper churn and reconcile after local completion | Open/in review | Trace, depth, temporal, and filter shaders |
| Visibility degenerate tests, PR #11 | Preserve its additive coverage and avoid conflicting rewrites; reconcile after local completion | Open/in review | Mask, projection, and sampling tests |
| Bilateral-grid local tone mapping | Keep ownership of local tone mapping and AgX bindings separate; fused AO must preserve the pre-AgX visibility-composition contract | Active roadmap work; branch not visible on remote | AO application/composition |
| Target Intel Xe-LPG hardware | User performs most final performance testing; local data must identify the actual adapter and cannot be generalized | External measurement pending | Final performance disposition |
| Primary-source research | Activision, XeGTAO, visibility-bitmask, vendor, and related implementation evidence | Complete and reconciled | Ledger and permutation selection |

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
- XeGTAO preserves Intel's 96-byte constants prefix in a 176-byte adapter,
  emulates source R8 AO rounding in R16F storage, and runs prefilter, main,
  denoise, and composition passes. Activision approximation runs prepared depth,
  horizon trace, spatial, temporal, required upsample, and composition passes.
  Reference owns none of those optional contracts.

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
  zero GPU/resource/pass cost; no unrestricted permutation Cartesian product;
  PS4 and Xe source profiles require a perspective full-texture viewport;
  ordinary AO/GI workload edits invalidate named source identities; failed
  benchmark re-export leaves no partial artifact set
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
implemented runtime family. The overall ranking forecasts -0.40 to 0.80 ms for
XeGTAO High versus Reference with low estimator-comparability confidence,
0.20-0.60 ms for the same-estimator Fixed 8 plus fusion finalist, -0.15 to
0.35 ms for XeGTAO mixed precision versus FP32, and -0.08 to 0.25 ms for the
Activision packed gather versus scalar filter. Published Intel/Activision
timings are provenance only, not inputs promoted to UVSR results.

The controlled 1920x1080 RTX 4090 Laptop run used 120 warm-up and 600 measured
frames per entry with zero incomplete frames. Exact fusion saved 6.250% median
and 17.510% p95; Fixed 8 plus fusion saved 7.870% and 19.066%; Fixed 8 alone
regressed median by 0.926% with an identical trace median. Packed PS4 saved
1.338% median and 7.845% p95 versus scalar, although both were slower than
Reference. XeGTAO LUT beat inline mixed by 1.463%, and FP32 beat mixed by
11.2-15.6% across the main run and precision repeat. These are local
adapter-scoped results and do not replace the Xe-LPG forecasts. A target result
requires the exact build, profile/permutation, warm-up, measured-frame count,
median, p95, and quality review.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact requested base | Commit identity and clean pre-edit state | `git rev-parse HEAD`; `git status` | Confirmed `5f43205`; task branch created |
| Canonical reference retained | Reference resource/dispatch/output contract and reference image comparison | Unit tests plus reference profile smoke capture | Plan/resource-invariance tests and gross-corruption capture pass; exact same-phase GPU comparison remains pending |
| Fixed AO/GI order and masks | Mask, AO, claimed-bit, source-owner, raw-GI fixtures for 8/12/16/20 and generic fallback | Focused visibility tests | 589,824 deterministic comparisons plus AO, GI-only, and AO+GI consumer-profile smoke pass |
| Packed edge correctness | All byte combinations, reverse depth, boundaries, slopes, depth/normal discontinuity, receiver mapping | Focused edge tests | Focused CPU fixtures and packed-edge profile smoke pass |
| XeGTAO source-port contract | Pinned math/constants, High workload, LUT/inline and mixed/FP32 distinctions, adapter resources, Hilbert mapping, perspective/full-texture viewport fallback, and strict shader build | Profile/resource tests plus strict standalone DXC | 4096-entry Hilbert coverage, profile/resource assertions, viewport guard inspection, and 40 strict DXC permutations pass; all three profiles complete 600/600 controlled local frames with clean captures. Xe-LPG timing and physical ISA/counters remain pending |
| Activision approximation contract | 2016 eight-tap schedule, closest-depth guide, spatial/temporal order, valid clamp-footprint/true-exit/odd-padding handling, scalar/gather variants, perspective/full-texture viewport fallback, named-stage attribution, and separation from the expanded 2019 memo | Profile tests, shader build, and `new-candidates` sequence | Both profiles complete 600/600 controlled local frames with clean captures; packed saves 1.338% median and 7.845% p95 versus scalar. Dynamic motion/disocclusion and target timing remain pending |
| Optional off-state zero cost | Reference pipeline key, bindings, allocations, dispatches, history, and output unchanged | Resource counters, code inspection, and reference capture | Plan-level reference-invariance assertions pass; target physical-resource capture remains pending |
| Isolated feature sanity | No correctness regression and directionally useful local timing/traffic result per optimization | One-at-a-time smoke/benchmark actions; no unrestricted permutation matrix | Fixed (5), noise (6), reconstruction (12), math (2), and the final all-profile smoke complete. The final smoke produced 58/58 entries, 116/116 complete frames, zero incomplete frames, and matching JSON/CSV/BMP sets. The controlled new-candidate and precision sequences completed nine and two entries respectively at 600/600 frames each with zero incomplete frames |
| Generated shader evidence | Source optimizations survive DXC; static code/load/store changes are recorded without treating them as runtime counts | `tools/measure_visibility_dxil.ps1`; [`docs/visibility-dxil-evidence.md`](../../visibility-dxil-evidence.md) | Representative 18-variant core DXIL report complete. Dedicated Xe/PS4 passes, fixed GI/later-bounce, most edge variants, groups/clamps, and diagnostics are outside that table; physical Intel registers/spills/occupancy remain pending |
| Release readiness | All task targets build; tests and title-case validator pass | CMake Release build, `ctest`, heading validator | Pre-commit Release build, 13/13 tests, DXIL generation, 413-row ledger audit, heading validator, 58-entry smoke, controlled local finalist run, and precision repeat pass; committed-identity rebuild/smoke remains the final coordinator check |
| Target performance | Median/p95 on the exact Intel target after warm-up with saved key/settings/clock state | User-led benchmark export, 120 warm-up and at least 240 measured frames | Local NVIDIA evidence is complete and adapter-scoped; Xe-LPG measurement is an explicit external handoff |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-16 | Use exact canonical commit `5f43205` on `codex/ao-performance-optimization` | The user explicitly named the verified base; importing newer open PRs would change provenance | All |
| 2026-07-16 | Record PR #10 and PR #11 as integration dependencies without cherry-picking | Both overlap task paths but are unmerged and absent from the requested base; their additive/mechanical intent can be preserved during later reconciliation | Shader and test edits |
| 2026-07-16 | Keep performance claims adapter-scoped | The local GPU may not be the target Xe-LPG device, and the prompt forbids extrapolating target wins | Benchmarking and report |
| 2026-07-16 | Run isolated smoke and traffic/performance checks, not combination permutations | This follows the user's explicit verification priority while still gathering evidence for each retained feature | Verification |
| 2026-07-16 | Implement XeGTAO High as a separate pinned source port | Intel's MIT-licensed commit `a5b1686c7ea37788eeb3576b5be47f7c03db532c` supplies the complete depth/main/denoise math and published timing provenance; explicit UVSR adapters avoid falsely claiming bit-identical output | Profiles, shaders, UI, tests, and ledger |
| 2026-07-16 | Keep generic UVSR bitmask mixed precision unavailable while exposing XeGTAO mixed/FP32 | XeGTAO now provides an honest same-algorithm precision matrix, but its success cannot prove half-precision safety at UVSR bitmask sector boundaries | Profiles, UI, and ledger |
| 2026-07-16 | Replace the broken PS4 trace-only presentation with a coupled approximation | Published evidence supports eight total linear taps and depth-spatial-temporal order, but shipping code/constants are unavailable; the profile remains explicitly approximate and keeps older controls separate | Profiles, shaders, UI, and ledger |
| 2026-07-16 | Keep the 2016 PS4 workload distinct from `ATVI-TR-19-01` | The 2016 deck publishes the eight-tap console schedule and filter order used by the approximation; the expanded 2019 memo supplies fuller theory, effective spatiotemporal sample counts, and combined 0.5 ms provenance but no replacement shipping source or complete constants | Profiles, source documentation, and ledger |
| 2026-07-16 | Use an outer effect timer plus a signed residual | A sum of named queries omits transitions and gaps, while summing stage percentiles mixes frames; the outer envelope and frame-correlated stage sum expose both scopes honestly | Benchmark statistics and export |
| 2026-07-16 | Retain fusion finalists and drop standalone Fixed 8 from local production consideration | The controlled NVIDIA run found 6.250-7.870% median savings for fusion, while Fixed 8 had an identical trace median and a 0.926% complete-median regression | Profiles, UI guidance, and ledger |
| 2026-07-16 | Keep PS4 and XeGTAO variants as algorithm/reference comparisons | Both PS4 profiles and all three XeGTAO profiles were slower than Reference locally; packed PS4, Xe LUT, and Xe FP32 remain the locally preferred variants inside their own comparison families | Profiles, benchmark guidance, and target handoff |
| 2026-07-16 | Make source-profile identity and export failure states conservative | Both source pipelines now fall back for orthographic or non-full-texture views; ordinary AO/GI workload edits switch to Generic Fallback/Custom; PS4 border reprojection preserves only the valid clamp footprint; and failed re-export removes its whole new artifact set | Runtime guards, UI identity, benchmark export, tests, and documentation |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-16 | Coordinator preflight | Complete | `5f43205` and this plan | Full user prompt read; remote branches, open PRs, worktrees, roadmap, and execution plans reviewed | Baseline build and required-file audit next |
| 2026-07-16 | Primary-source inventory | Complete | `docs/ao-optimization-ledger.md` | Primary sources, current-build evidence, and duplicate candidate semantics reconciled across 413 unique rows | Target measurements remain pending |
| 2026-07-16 | Renderer architecture audit | Complete | Read-only pass/resource/permutation map | Exact-base and current-diff source inspection | Findings incorporated into curated implementation |
| 2026-07-16 | Profile/resource plan and benchmark statistics | Integrated | `src/visibility_performance_plan.*`; `src/visibility_benchmark_statistics.*` | Focused tests, 58-entry all-profile smoke, controlled new-candidate sequence, and precision repeat pass | Preserve adapter scope and the target evidence boundary |
| 2026-07-16 | Renderer and UI implementation | Integrated | Fixed/group/radius-clamp/packed-noise/packed-edge/fused/diagnostic shaders; compact familiar AO controls; folder buttons; explicit stage statistics; sequential runners; CLI/export | Packaged-shader audit, 58/58-entry smoke with 116/116 complete frames, nine-profile controlled run, and precision repeat pass | Committed-identity rebuild/smoke remains the coordinator's final local check |
| 2026-07-16 | Activision PS4 approximation repair | Integrated | Closest-depth guide, horizon, scalar/gather spatial, temporal, upsample, and composition chain | Both profiles complete 600/600 controlled frames; captures are clean and packed is faster than scalar locally | Dynamic motion/disocclusion and controlled Xe-LPG timing remain pending |
| 2026-07-16 | Intel XeGTAO 1.30 High source port | Integrated | Pinned prefilter/main/denoise shaders; LUT/inline and mixed/FP32 profiles; adapter contract | 40 strict DXC permutations, focused profile/Hilbert/resource tests, three controlled 600-frame profiles, independent precision repeat, and clean captures pass | Native Xe-LPG timing/ISA/counters remain pending |
| 2026-07-16 | Generated shader evidence | Partially complete | `docs/visibility-dxil-evidence.md`; `tools/measure_visibility_dxil.ps1` | Reproducible representative 18-variant optimized-DXIL table confirms packed FAST, filter algebra, fusion, and fixed code growth | The table is not exhaustive; target Intel native ISA, GRF, spill, SIMD width, and occupancy remain pending |
| 2026-07-16 | Documentation reconciliation | Complete | README, design, 413-row ledger, DXIL evidence, and this plan | Source-port/approximation boundaries, controlled local ranking, keep/drop guidance, 413 unique rows, and Title Case validator pass | Preserve target timing, dynamic-motion, and physical-ISA limitations in the final handoff |

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
- Verification summary: pre-commit Release build, 13/13 tests, 40 strict XeGTAO
  DXC permutations, 58/58-entry smoke with 116/116 complete frames, controlled
  nine-profile run, independent precision repeat, and clean static captures pass
- Independent review: optimization-coverage audit reconciled stale duplicate
  ledger semantics; coordinator final review remains
- Coming Soon/documentation update: active entry and final local evidence
  reconciled across README, design document, ledger, and this plan
- Pushed/PR/merged, or intentionally local: intentionally local
- Ready/manual-candidate boundary: ready means the curated build is available
  for user testing; it does not mean every isolatable experiment was
  implemented or rejected
- Remaining implementation/evidence families: Auto Fixed (D-005), trace LDS
  (D-020), reconstruction LDS (D-024/R-011), four-output coarsening
  (D-026/R-013), staged AO-only ILP (L-015/O-022), and exhaustive generated-
  code coverage (F-030); see the
  [ledger status](../../ao-optimization-ledger.md#ledger-status)
- Feasible curated source experiments: screen-space-size horizon-thickness EMA
  (A-011), depth-derived receiver normals (A-014), velocity-agreement adaptive
  clamp width (Q-027), XeGTAO Low (X-002), XeGTAO Medium (X-003), native R8 AO
  storage/decode (X-018), standalone depth-derived-normal generation (X-020),
  and in-main depth-derived normals (X-021); these are deferred, not impossible
- Remaining external validation: dynamic motion/disocclusion stress, exact same-
  phase GPU image equivalence, Xe-LPG timing, and Intel physical ISA/register/
  occupancy counters
- Active ownership released: no
- Archived to completed/abandoned path: pending
