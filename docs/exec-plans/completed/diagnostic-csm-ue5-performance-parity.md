# Diagnostic Csm Ue5 Performance Parity

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/bend-screen-space-shadows` in the existing UVSR Bend/SVSM worktree
- Base commit: `ea566bc88a98c9322759694539212f281408de0d` with preserved tracked and untracked work
- Started: 2026-07-22
- Last updated: 2026-07-24
- Planned archive: `docs/exec-plans/completed/diagnostic-csm-ue5-performance-parity.md`

## Goal and Done Condition

Goal: audit the diagnostic CSM against UE 5.6 conventional cascaded shadow maps, remove correctness or performance bugs, make any beyond-reference optimization independently reversible, and provide evidence-based before/after probability estimates for performance within ten percent of UE5.

Done when:

- [x] Relevant UE renderer and shader source is cross-referenced against the UVSR caster, cache, projection, resolve, filter, timing, and resource paths.
- [x] Confirmed bugs are fixed without changing or discarding unrelated Bend, SVSM, Donut, NVRHI, or user work.
- [x] Any optimization beyond UE-equivalent behavior is controlled by an independent toggle whose disabled path preserves the reference work and performance behavior.
- [x] Release builds, focused tests, full CTest, title-case checks, diff checks, runtime image review, and thermally valid stage timings are recorded where the machine permits.
- [x] Initial and revised probability estimates explicitly state benchmark comparability limits.

## Scope

In scope:

- The local diagnostic CSM implementation, shaders, settings, profiles, tests, UI, documentation, and benchmark evidence.
- Read-only comparison with authenticated UE5 source and authoritative conventional CSM references.
- Minimal bug fixes and evidence-backed reversible optimizations.

Non-goals:

- Replacing CSM with VSM, SVSM, ray tracing, or a renderer-wide bindless/indirect architecture.
- Modifying Donut, NVRHI, main, submodules, Git history, or remote state.
- Claiming a matched UE benchmark without identical UE content, coverage, resolution, filtering, hardware state, and measurement boundaries.

Affected subsystems and paths:

- `src/diagnostic_cascaded_shadow_map*`
- `src/uvsr.cpp`
- `tests/diagnostic_cascaded_shadow_map_tests.cpp`
- CSM build and shader packaging entries
- This execution plan and relevant user-facing CSM documentation

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `README.md`, `src/uvsr.cpp`, shader catalogs, and all CSM source/test files.

## Baseline

- Canonical repository/remote: existing local feature branch and its configured origin; no remote mutation authorized.
- Local versus remote state: the worktree intentionally contains extensive preserved tracked and untracked work.
- Verified source commit/build: prior aggregate Release build and CTest passed before this audit; the current CSM source is uncommitted.
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090 Laptop, Sponza benchmark position 1, UE5 CSM Reference, four 2048-by-2048 cascades, full redraw, UE-like 5-by-5 filtering. User-observed cost is approximately 2.9 ms and is contextual evidence, not yet an agent-controlled matched run.
- Known pre-existing failures: no directly comparable UE5 run exists on this laptop; live resize remained untested in the prior implementation pass.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Epic UE source | Authenticated `release` commit `7deeb413d3dc1fc034f48d1aacc0861301829d32` | Reviewed | Source audit and fixes |
| Donut/NVRHI | Pinned local revisions remain authoritative and unchanged | Preserved | Caster and resource implementation |
| Machine benchmark gate | Thirty-second clean preflight, High renderer priority, at least 30 TFLOPS for accepted position-1 timing | Passed for the accepted interval | Runtime measurements |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Directional-light identity matching and full-resolution linear `R8_UNORM` visibility output remain unchanged.
- UE-reference profile behavior remains available; optional additions must not burden the disabled path.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| UE source and benchmark audits | `/root/ue5_csm_bias_fade_audit`, `/root/ue5_csm_benchmark_research` | Shared, read-only | Current state | None | Authenticated UE source | Complete |
| UVSR cache and edge audit | `/root/csm_cache_perf_edge_audit`, `/root/csm_edge_test_audit` | Shared, read-only | Current state | None | Current CSM source | Complete |
| Submission-parity audit | `/root/csm_submission_parity_audit`, `/root/csm_remaining_bug_audit` | Shared, read-only | Current state | None | Current CSM and authenticated UE source | Complete |
| Integration and verification | `/root` | Existing worktree | Current state | All in-scope CSM files | Both audits | Complete |

## Integration Order

1. Record the initial estimate and benchmark limitations.
2. Complete UE/source and UVSR hot-path audits.
3. Add deterministic tests for confirmed bugs, then implement minimal fixes.
4. Add only evidence-backed optional optimizations with neutral disabled paths.
5. Build, run automated checks, and perform clean runtime diagnostics and measurements.
6. Complete independent source review and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| UE filtering parity | Source citations plus shader/static tests | Focused CSM tests and shader build | Passed. The exact nine-gather receiver, bias behavior, and opaque-depth clamp are source-matched, the final Release build passed, and the CSM reference test passed in 0.36 seconds. |
| Stable, artifact-free shadows | Visual inspection across cascade and scene motion cases | UVSR debug-menu smoke | Position-1 normal, cascade-selection, and cache-action views were coherent through uncached/cached profile and optimization-toggle transitions; no cut-up shadows, seams, stale regions, or missing alpha-tested casters were visible. Deterministic movement, scroll, dirty-region, and cache-transition cases pass; a matched free-camera image sequence was not captured in this final interval. |
| Four-cascade cost breakdown | Setup, culling, clear/update, raster, sampling, and total values | Position-1 clean preflight and runtime timing | The pre-optimization accepted interval at 42.3-42.8 TFLOPS measured 1.248-1.279 ms GPU total, 0.040-0.042 ms clear, 1.142-1.172 ms raster, and 0.053-0.055 ms sample. CPU all was 0.828-0.885 ms, including 0.617-0.661 ms culling and 0.139-0.170 ms recording. The post-optimization mirrored interval at 41.7-42.9 TFLOPS kept GPU work within noise at 1.269-1.305 ms while one-pass classification reduced CPU culling from 0.677-0.854 ms with four walks to 0.264-0.429 ms with one walk. Optimized cached stationary measured 0.062-0.067 ms GPU and 0.046-0.056 ms CPU with four reused cascades, zero redraws, zero clear/raster, zero caster work, and zero scene walks or sorts. |
| Repository health | Release build, full CTest, title checks, and diff check | Repository-required commands | Passed. Release CTest was 15 of 15 in 3.48 seconds; the title checker self-test and full 545-heading audit passed with zero violations; `git diff --check` reported no whitespace errors; all six frozen Bend hashes match. |

For performance work, preserve the executable hash, dirty-source identity, adapter/driver, exact preset, scene and camera, thermal/process gate, warmup, raw samples, total frame time, per-stage CSM cost, and image-quality guardrails. Never compare unlike runs or describe estimates as measurements.

## Benchmark Evidence

No matched RTX 4090 Laptop conventional UE CSM benchmark was found. The user's
2.9 ms observation is therefore a valid regression signal for this implementation,
not evidence of a measured UE parity delta.

| Source | Configuration | Result | Comparability |
| --- | --- | --- | --- |
| Epic Distance Field Soft Shadows comparison | Radeon HD 7870, 1080p full game scene, three cascades over 10,000 units | 3.1 ms | Official but old; shadow resolution, filtering, cache state, engine revision, and timing boundary are not disclosed. |
| Same Epic comparison | Same scene, six cascades over 30,000 units | 4.9 ms | Same limitations; it cannot be scaled into a 4090 Laptop parity target. |
| Current UE AutomatedPerfTesting defaults | Generic client report thresholds | `ShadowDepths` 1.00 ms and `ShadowProjection` 0.75 ms | Budgets, not measurements; the source says projects may override them. |
| MJP bindless deferred sample | GTX 970, Crytek Sponza, 1080p, no MSAA, four 2048 maps | 1.30 ms average raster over 64 frames | Credible timestamp evidence but not Unreal; receiver cost is fused into deferred lighting. |
| Chetan Jags CSM sample | Unspecified GPU, Sponza, three dynamic plus one static 2048 map | 0.605 ms depth; 0.442 ms one-tap or 0.666 ms eight-tap receiver | Hardware and architecture are too underspecified for parity. |

A defensible matched capture must isolate one movable directional light, disable
VSM, distance-field, contact, far, and per-object shadows, then compare UE
`ShadowDepths` with UVSR clear plus raster and the sum of UE's four
`ShadowProjection WholeScene splitN` passes with UVSR sampling. GPU Insights or
`profilegpu` is required; `stat ShadowRendering` reports CPU cycles.

## Authenticated Ue Source Findings

- Fully dynamic conventional CSM uses effective split exponent four, not the
  component property's visible default of three.
- Interior cascades extend outward for transition overlap; the last dynamic-only
  cascade fades inward. Maximum-distance fade is quadratic.
- UE contains all eight asymmetric frustum corners in the cascade sphere, rounds
  radius outward, enforces at least a 10,000-unit directional depth span, and
  snaps with `fmod` truncation on a four-texel grid.
- D3D12 conventional shadow depth is R16 typeless storage with D16 depth and R16
  sampling views. It clears to one and uses less-equal normal depth.
- Default raw/component bias inputs are 10, 3, 0.5, and 0.5. UE applies the
  normalized constant and slope bias in the vertex shader. Receiver bias 0.9
  changes the soft-transition scale using GBuffer shading-normal `NoL`; it is not
  a raw receiver-depth subtraction.
- Quality levels four and five use nine `Gather4` operations, 36 soft comparisons,
  a reconstructed 5-by-5 box, and squared visibility through a point sampler.
- UE gathers primitives once and tests all cascades during that traversal. UVSR
  now mirrors that organization through its independently reversible one-pass
  classifier while retaining the original per-cascade traversal path for
  comparison.

## Implemented Corrections

- Replaced the old 25-load approximation with the exact nine-gather UE receiver,
  selected D16 with capability-checked D32 fallback, and moved normalized bias
  into the caster vertex path.
- Corrected split exponent, final fade direction, quadratic distance fade,
  eight-corner containment, exact snap phase, full snap/filter projection guard,
  GBuffer-normal receiver bias, and conservative missing-normal behavior.
- Fixed a stale input-buffer SRV when material state merging crossed geometry
  buffer groups, a first-draw normal-offset bug, and format-dependent fixed
  raster bias.
- Added UE radius-threshold culling and a downstream projected-hull cap. Both are
  independent toggles, and all view-dependent culling is disabled for cached
  maps so off-camera casters cannot disappear from reused depth.
- Removed disabled/cached hull-construction cost, normalized custom filter radius
  to the cache-safe UI range, made dirty projections fail open, clipped huge
  finite projected bounds before integer conversion, and rejected non-overlap or
  unrepresentable scroll offsets before cast/negation.
- Strengthened deterministic coverage for exact snap phases, projection guards,
  normalized bias, receiver bias, radius threshold, cache gating, huge bounds,
  odd 7-by-7 and 2049-by-2049 scroll content, and INT32-limit scroll motion.
- Added the missing alpha-tested depth binary to durable runtime shader staging;
  compilation had succeeded, but the app post-build step deleted the shader tree
  and copied only the unsuffixed opaque variant.
- Removed transform and world-AABB construction when cache metadata, radius
  culling, and projected-hull culling are all disabled, preserving a genuinely
  neutral reference path. Added matched CPU total and command-recording timing so
  setup/culling no longer imply complete CPU coverage.
- Added one-pass multi-cascade caster classification, matching UE's single
  primitive traversal while retaining independent per-cascade membership and
  sorts. The reversible path propagates parent cascade masks, reuses per-mesh
  transforms and bounds, computes radius rejection once per geometry, and skips
  zero-threshold or invalid-hull bounds work.
- Added reversible direct caster submission. Timing-only mode no longer copies
  or pre-walks `DrawItem` records; detailed mode collects the same alpha,
  instance, triangle, and batching counters inside the renderer's one iterator
  pass. The copied Donut-compatible path remains available unchanged.
- Fixed non-triangle geometry admission in the shared gather and added a
  deterministic within-instance geometry tie-breaker so legacy and shared
  sorting converge without changing the disabled path's instance-major order.
- Fixed cache provenance so an uncached, view-culled full redraw can never seed a
  later whole-map or whole-cascade reuse. Entering a cached profile now requires
  one cache-safe redraw.
- Matched UE's opaque receiver-depth clamp exactly at `0.99999f`.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-22 | Initial probability is 15 percent | The user-observed 2.9 ms is plausible but unmatched, and the receiver shader already has a confirmed 25-fetch versus UE nine-gather mismatch. | Entire audit |
| 2026-07-22 | Authenticated UE `release` is the primary source revision | Commit `7deeb413d3dc1fc034f48d1aacc0861301829d32` is available locally and supplies exact conventional CSM behavior. | UE source audit |
| 2026-07-23 | Treat source-parity defects as unconditional fixes | Incorrect split/fade/bias/filter/cache behavior cannot be a useful reference and therefore receives no compatibility toggle. | Projection, caster, receiver, cache |
| 2026-07-23 | Keep beyond-reference depth-path optimizations independent | Opaque state merging, position-only opaque vertices, projected-hull culling, radius culling, and D16 selection can each be disabled without retaining hidden shader or submission work. | Settings, UI, caster path |
| 2026-07-23 | Reject the first runtime interval | Preflight timed out with a 54.969 C GPU hotspot at the conservative 55 C ceiling. External CPU averaged 15.664 percent and no competing renderer, build, or capture existed, but the thermal stability requirement did not pass. | Runtime timing |
| 2026-07-23 | Accept the rebuilt position-1 interval | The explicit RTX 4090 gate passed for 30 seconds with a 45.531 C hotspot, 50 C thermal headroom, inactive NVIDIA slowdown flags, 12.491 percent external CPU average, High renderer priority, and more than 39 TFLOPS in the live run. CPU temperature was unavailable, so the allowed fallback relied on full CPU performance-limit telemetry plus cold GPU evidence. | Runtime timing |
| 2026-07-23 | Keep projected-hull culling independently reversible | Disabling it increased candidate caster projections from 519 to 1,652 and reproduced about 2.88 ms CSM cost; enabling it returned about 1.40 ms without a visible output change. The disabled stress interval fell below 30 TFLOPS and is diagnostic rather than accepted benchmark evidence. | UE5 Reference profile and custom controls |
| 2026-07-23 | Accept the post-reboot position-1 interval | After closing nonessential user workloads, the preflight held for 30 seconds with 45 C thermal headroom, inactive NVIDIA slowdown flags, 15.527 percent external CPU average, and no renderer, build, capture, or overlay contamination. The absolute ceiling was raised from 45 C to 60 C because the memory-junction sensor reported 56 C while the GPU core was 41 C and all headroom and limiter checks were healthy. The 15-second measurement completed with High renderer priority, 11.918 percent external CPU average, no limiter, 42.3-42.8 TFLOPS, coherent shadows, 1,732 coarse casters, 80 radius rejects, 1,169 hull rejects, and 483 candidates. The result supports thermal, process, or prior camera-state skew in the earlier approximately 1.39-1.42 ms interval, but does not isolate those factors because that interval reported 519 candidates. Artifacts are `outputs/svsm-thermal-state-20260723T061202550Z-35464-0a1ed6b8.txt`, `outputs/svsm-thermal-state-20260723T061521545Z-32404-e533671a.txt`, and `outputs/csm-measurement-ready-20260723T011521390.txt`. | Runtime timing |
| 2026-07-24 | Reject the first post-optimization live interval | Repeated NVIDIA telemetry held this driver at P0 and heated the idle GPU despite zero reported utilization. The final renderer still opened correctly at benchmark position 1 and showed coherent four-cascade shadows, but enabling CSM reduced the stat line from 38.6 to 26.9 TFLOPS. That violates the user's explicit 30 TFLOPS acceptance gate, so the renderer was stopped immediately and no timing from the interval is evidence. | Runtime timing |
| 2026-07-24 | Accept the post-restart optimized interval | A cold preflight reported 38 C GPU core, 46.781 C hotspot, 48 C thermal headroom, inactive limiters, 11.894 percent external CPU, and no competing renderer. Every accepted live sample remained above 30 TFLOPS. The mirrored four-cascade comparison preserved identical 1,732 coarse, 80 radius-rejected, 1,169 hull-rejected, and 483 candidate caster decisions and coherent shadows. Four independent scene walks measured 0.677-0.854 ms culling; one shared walk measured 0.264-0.429 ms. GPU total remained within ordinary interval noise at 1.269-1.305 ms. The direct-versus-copy snapshots were too noisy to claim an isolated gain, so direct submission remains independently reversible and is not credited in the estimate. The optimized cached profile reached four reused cascades, zero redraws, zero clear/raster/caster work, and 0.062-0.067 ms GPU total after warmup. | Runtime timing and final estimate |
| 2026-07-24 | Revise the within-ten-percent probability to 40 percent end-to-end | The source-parity defects are fixed, GPU work is stable, shadows are coherent, and UE-style one-pass classification materially closes the CPU gather gap. The independent final review found no remaining local CSM defect. GPU-only parity is estimated at 60 percent and the full CPU path at 30 percent. The estimate remains below even odds because no matched UE capture exists and UE retains renderer-wide cached draw commands, GPU Scene integration, scene acceleration structures, and parallel dispatch outside this diagnostic's allowed scope. There is no evidence that UVSR is faster than UE. | Final assessment |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-22 | `/root` | Complete | Initial estimate | 15 percent prior probability recorded | Audit exact source and benchmarks |
| 2026-07-23 | UE source/benchmark agents | Complete | Authenticated source commit and public evidence | Splits, fades, projection, bias, D16, receiver, Gather4, profiling scopes, and benchmark limits reviewed | Integrate fixes and measure |
| 2026-07-24 | `/root` | Complete | Current dirty worktree; accepted cold preflight `outputs/svsm-thermal-state-20260724T001347430Z-25564-fbb63a17.txt`; executable SHA-256 `2CC8E0DA516749B9E55631E106AE7FAF6D2CB1C06CB70918CFCA8EA75507AF8C` | Release build passed; CTest 15 of 15; title-case self-test and 545-heading audit passed; diff check clean apart from line-ending warnings; frozen Bend hashes match; runtime A/B and cached reuse passed above 30 TFLOPS; independent UE/Donut/NVRHI review found no remaining concrete CSM defect | A matched UE capture remains the only way to replace the final probability with measured parity |

## Risks and Escalation Triggers

- UE and UVSR timings are not directly comparable unless measurement boundaries, scene content, coverage, resolution, filtering, platform, and hardware state match.
- Caster batching changes can silently alter alpha-tested material behavior or instancing; opaque and masked paths require separate validation.
- CSM shader changes can introduce reverse-Z, cascade-edge, bias, or out-of-bounds sampling artifacts.
- Existing user and agent work is intentionally dirty and must remain untouched outside the narrow CSM scope.

Stop and ask the user only if a required action would expand into Donut/NVRHI modifications, discard preserved work, or require an unauthorized external/Git operation.

## Completion

- Final integrated commit: intentionally none, as required
- Verification summary: Release build, 15 of 15 CTest, focused CSM test, title-case self-test and full audit, diff check, Bend hashes, thermally accepted runtime A/B, debug-view image review, and cached stationary reuse all passed
- Independent review: authenticated UE5, pinned Donut, and pinned NVRHI review found no remaining concrete CSM correctness or local performance defect
- Coming Soon/documentation update: implementation and benchmark notes are recorded in this completed plan and the existing diagnostic CSM documentation
- Pushed/PR/merged, or intentionally local: intentionally local; no commit, push, pull request, merge, main change, or submodule update
- Remaining experiments or follow-ups: matched UE5 GPU Insights capture on identical content, settings, coverage, and hardware if measured parity is required; optional precomputed hull-axis optimization only as a separate reversible experiment
- Active ownership released: yes
- Archived to completed/abandoned path: completed
