# Heitz Ratio Estimator Repair

## Status

- State: complete; task-local Release candidate with automated verification
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/heitz-ratio-estimator-repair` in
  `work/heitz-ratio-estimator-repair`
- Base commit: `5d15efd730c574e433bfc58ea4727010ff3a6619`
- Started: 2026-08-17
- Last updated: 2026-08-17
- Planned archive:
  `docs/exec-plans/completed/heitz-ratio-estimator-repair.md`

## Goal and Done Condition

Goal: repair and improve UVSR's Heitz-style ratio estimators by comparing the
exact main-branch implementation at `5d15efd` with Brock's downloaded
`Heitz2018Shadow-reference-code`, preserving UVSR's renderer contracts while
making the estimator and relevant lighting behavior as faithful to the
reference as practical.

Done when:

- [x] The reference estimator, UVSR estimator, CPU/HLSL contracts, and lighting
      integration are mapped line by line and material differences are recorded.
- [x] Confirmed estimator defects and missing safe reference behavior are fixed
      without modifying Donut or destabilizing unrelated lighting paths.
- [x] Deterministic tests cover estimator math, degenerate inputs, sparse
      visibility, finite/range invariants, and source/binding contracts.
- [x] The isolated Release build and focused/full task-relevant tests pass.
- [x] A production-GPU candidate is smoke-tested and the exact final executable
      is handed to Brock for product/visual evaluation. The final artifact was
      not launched concurrently with the still-open user-active smoke candidate.
- [x] An independent rendering review finds no unresolved correctness defect.

## Scope

In scope:

- Heitz ratio-estimator math and sampling behavior used by direct-light shadows
  and sky/environment visibility.
- Closely coupled lighting features from the downloaded demo when they are both
  materially relevant to estimator correctness and safe to fit UVSR's existing
  renderer contracts.
- CPU/HLSL bindings, dispatch scheduling, temporal state, compositing, tests,
  shader packaging, and focused documentation needed for the repaired path.
- Sparse and degenerate edge cases, including zero/one samples, all-visible and
  all-occluded neighborhoods, invalid history, near-zero denominators, resize,
  camera cuts, and disabled/inactive light paths.

Non-goals:

- Editing Donut or replacing UVSR with the demo's G3D/OptiX renderer.
- Importing demo features unrelated to ratio-estimator effectiveness.
- A renderer-wide lighting redesign, new third-party runtime dependency, or an
  unbounded quality/performance tradeoff.
- Push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/heitz_ratio_estimator_shadows*`
- Direct-light and sky-visibility producers/consumers in `src/`
- Focused estimator, lighting, packaging, and renderer contract tests
- `CMakeLists.txt`, `src/shaders.cfg`, `README.md`, and estimator documentation
  only when the final design requires them

Shared hotspots reserved for the coordinator:

- All writable files
- CPU/HLSL resource and constant-buffer contracts
- `CMakeLists.txt`, `README.md`, `src/shaders.cfg`, and `src/uvsr.cpp`
- All build trees, runtime/GPU control, integration, and documentation closeout

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`; named main snapshot and
  current `origin/main` are
  `5d15efd730c574e433bfc58ea4727010ff3a6619`.
- Local versus remote state: the original checkout's `main` is ahead two and
  behind 58, with unrelated untracked files; this task is isolated in a clean
  worktree at the exact named remote commit.
- Verified source commit/build: the isolated Release baseline at `5d15efd`
  compiled all 311 UVSR shader tasks and linked `b/bin/uvsr.exe`; the focused
  Heitz, source-contract, and PBR tests passed 3/3. This is a task-local
  technical baseline, not a Canonical verified checkpoint.
- GPU, scene, camera, resolution, and settings preset when relevant: use PBR
  Sponza Decorated, Benchmark Position 1, 1920x1080, factory defaults, then
  estimator-off/on and targeted sparse/high-contrast cases if a safe testing
  window is available.
- Known pre-existing failures: none established for this worktree; capture
  baseline test/build evidence before attributing failures.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Named main snapshot | Exact `5d15efd` tree | Integrated as base | Whole task |
| Downloaded Heitz demo | Local source and shader behavior | Audit complete | Design and tests |
| UVSR lighting contracts | Existing G-buffer, visibility, history, and binding meanings | Audit complete | Implementation |
| Existing ratio-shadow history | Prior branch plans, tests, and commits | Audit complete | Regression prevention |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve current UVSR descriptor/binding ownership unless a demonstrated
  reference mismatch requires a narrowly tested change.
- Preserve `1.0` as fully visible and `0.0` as fully occluded at lighting
  consumers.
- Every estimator output and accumulated moment must remain finite; visibility
  and blend weights must stay in their documented ranges.
- Reference behavior must be adapted to UVSR's DX12 compute pipeline without
  importing G3D, OptiX, or demo-only renderer assumptions.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| REF-AUDIT | `/root/reference_audit` | Shared read-only | `5d15efd` plus downloaded demo | None | None | Complete; released |
| UVSR-AUDIT | `/root/uvsr_audit` | Shared read-only | `5d15efd` | None | None | Complete; released |
| TEST-AUDIT | `/root/test_history_audit` | Shared read-only | `5d15efd` plus relevant history | None | None | Complete; released |
| IMPLEMENT | `/root` | Isolated task worktree | `5d15efd` | Final scoped source/tests/docs | Audit handoffs | Complete; frozen |
| REVIEW | `/root/uvsr_audit` | Shared read-only after write freeze | Candidate diff | None | IMPLEMENT | Complete; released |

## Assignment Contracts

### Ref-Audit: Map the Downloaded Heitz Reference

- Owner/thread: `/root/reference_audit`
- Branch/worktree: shared read-only access
- Base commit/state: downloaded reference as present on 2026-08-17
- Read scope: all source, shaders, scene/settings, and bundled documentation in
  `C:\Users\brock\Downloads\Heitz2018Shadow-reference-code`
- Write scope: none
- No-touch scope: all repository files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: report exact equations, inputs, outputs,
  sampling/temporal behavior, lighting features, and failure guards without
  assuming UVSR compatibility
- Deliverable: distilled source-cited algorithm map, safe transferable features,
  and edge-case risks
- Done when: all relevant demo code and shaders are accounted for
- Required verification: cross-check CPU setup against every estimator shader
- Allowed Git and external actions: read-only local inspection only
- Stop and report if: source is missing, generated-only, or licensing/intent is
  ambiguous
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### UVSR-Audit: Audit Current Estimator and Lighting Integration

- Owner/thread: `/root/uvsr_audit`
- Branch/worktree: `work/heitz-ratio-estimator-repair`, read-only
- Base commit/state: clean `5d15efd`
- Read scope: estimator source/shaders, all producers and consumers, bindings,
  settings, dispatch order, resize/cut/reset behavior, and related docs
- Write scope: none
- No-touch scope: all files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: named main snapshot
- Interface/invariant contract: trace actual runtime values and resource states;
  distinguish proven defects from stylistic differences
- Deliverable: source-cited pipeline map, bug list ranked by severity, and
  compatibility constraints
- Done when: direct-shadow and sky-visibility estimator paths are both traced
  end to end
- Required verification: static cross-check of CPU/HLSL layouts and shader
  packaging entries
- Allowed Git and external actions: read-only local inspection only
- Stop and report if: an unexpected diff or active ownership collision appears
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Test-Audit: Find Coverage Gaps and Prior Decisions

- Owner/thread: `/root/test_history_audit`
- Branch/worktree: `work/heitz-ratio-estimator-repair`, read-only
- Base commit/state: clean `5d15efd` plus relevant reachable history and
  abandoned/completed plans
- Read scope: estimator/lighting tests, CMake registration, commit history,
  ratio-estimator documentation, and prior ratio-shadow branches without
  reading unrelated dirty working-tree contents
- Write scope: none
- No-touch scope: all files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: named main snapshot
- Interface/invariant contract: identify false-positive tests, missing sparse
  edge cases, and rationale that must be preserved
- Deliverable: exact recommended tests/check commands and regression hazards
- Done when: existing coverage is mapped to behavior and gaps are prioritized
- Required verification: inspect test registration and relevant historical diffs
- Allowed Git and external actions: read-only local inspection only
- Stop and report if: conclusions would require relying on unrelated dirty work
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Implement: Integrate the Safest Faithful Repair

- Owner/thread: `/root`
- Branch/worktree: `codex/heitz-ratio-estimator-repair` in isolated worktree
- Base commit/state: clean `5d15efd`
- Read scope: whole repository plus audit handoffs and downloaded reference
- Write scope: smallest final set of source, shader, test, and documentation
  files required by confirmed defects
- No-touch scope: Donut, downloaded reference, other worktrees/branches, generated
  build output for version control, and unrelated user/peer changes
- Build directory and runtime/GPU/resource lease: dedicated task build tree;
  coordinator alone controls build/runtime/GPU resources
- Dependencies already integrated: REF-AUDIT, UVSR-AUDIT, and TEST-AUDIT
- Interface/invariant contract: preserve current public lighting and resource
  meanings while matching Heitz estimator behavior where demonstrated safe
- Deliverable: reviewed candidate diff, tests, Release executable, and evidence
- Done when: all task acceptance criteria pass or any remaining runtime-only
  uncertainty is explicitly bounded
- Required verification: focused reference tests, source contracts, shader
  compilation/package checks, full relevant CTest, Release build, runtime smoke,
  and independent review
- Allowed Git and external actions: local edits/build/test only; no commit,
  push, PR, merge, release, or deployment
- Stop and report if: a fix requires a visible unbounded quality/performance
  tradeoff, destructive uncertain deletion, or renderer-wide redesign
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

## Integration Order

1. Complete and reconcile all three read-only audits.
2. Freeze an explicit estimator/lighting contract and baseline checks.
3. Implement confirmed repairs and deterministic edge coverage serially.
4. Build, test, and smoke the integrated candidate in the dedicated build tree.
5. Freeze writes for independent rendering review, repair confirmed findings,
   rerun invalidated checks, and archive the plan on completion.

## UI Intake

- Policy references read before editing: `AGENTS.md` version `2026-07-30.1`
  and UI reference version `2026-08-15.2`.
- State owner/default: retain
  `RayTracedSkyVisibilitySettings::useRatioEstimator` and its `true` default as
  a compatibility-owned legacy field; change only its visible description from
  a Heitz ratio to binary visibility sample averaging.
- Control class/range/commands: existing checkbox and existing 1/2/4/8/16/32/64
  sample slider; preserve every serialized setting and command value so no
  settings snapshot or automation breaks.
- Reset/group behavior: unchanged; the existing per-control reset returns the
  authoritative factory default and the existing Sky group reset remains
  coherent.
- Consumers: the sky visibility shader's ray count, hit-distance availability,
  built-in/third-party denoising eligibility, diffuse IBL visibility, specular
  IBL visibility, settings fingerprint, and command/status presentation.
- Mutation class: constants/shader execution count only; no new resource,
  scene, process, popup, scroll, focus, or animation topology.
- Scene/loading/unavailable behavior: unchanged and still owned by the existing
  Ray Traced Sky Visibility group and Representation gates.
- Command coverage: preserve the legacy command path and storage name for
  compatibility; update help/presentation copy where it describes the math.
- Focused/live evidence: UI source/animation/dropdown tests, renderer and sky
  contracts, full Release tests, and exact-candidate exercise of both toggle
  states. No other visible layout or interaction may change.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Reference fidelity | Equation and data-flow comparison with exact source locations | Static audit plus focused CPU reference tests | Complete; paired filter-then-divide topology and four-mode matrix verified |
| Finite and bounded math | Exhaustive boundary/randomized deterministic tests | Focused estimator test executable | Passed, including 20,000 randomized ratio cases and 20,000 binary16 packed-pair cases |
| Sparse edge correctness | Zero/one-hit, all-visible/all-occluded, invalid-history, and denominator cases | Focused estimator and source-contract tests | Passed; sparse schedule bypasses paired filtering |
| Binding and packaging integrity | Matching CPU/HLSL layouts and compiled/staged shader variants | Source-contract tests and Release build | Passed; 314 shaders compiled and exact 50-file runtime bundle staged |
| Renderer integration | Successful startup and representative estimator-off/on behavior | Isolated runtime smoke and screenshots/logs | Immediately preceding candidate launched on D3D12, loaded Sponza, and presented at 1920x1080; exact final artifact was not launched beside the still-open user-active PID 41048, so final-mode visual comparison remains for Brock |
| Regression safety | Task-relevant and full deterministic suites pass | CTest in isolated Release build | Passed 47/47 |
| Independent correctness | No unresolved high-risk rendering finding | Read-only candidate review | Passed; no P0/P1/P2 estimator, ABI/DX12, MSAA/sparse/GI, fallback, packaging, or resource-lifetime blocker |

## Decisions

| Date And Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-17 | Use exact `5d15efd` in a new isolated branch/worktree | The original checkout is diverged and dirty; the user named this exact main snapshot | All |
| 2026-08-17 | Use one coordinator-writer and parallel read-only audits | CPU/HLSL and lighting contracts are coupled and high risk; competing writers would create unsafe overlap | All |
| 2026-08-17 | Treat demo lighting additions as conditional, not automatic | Only features required for estimator effectiveness and compatible with UVSR's current product scope are authorized | REF-AUDIT, IMPLEMENT |
| 2026-08-17 | Keep Heitz estimation scoped to the directional sun | The reference explicitly excludes sky, environment, emissive, and non-area lighting; UVSR's sky toggle is a binary multi-ray mean, not a response ratio | REF-AUDIT, UVSR-AUDIT, IMPLEMENT |
| 2026-08-17 | Preserve the legacy sky setting/command while correcting visible semantics | Renaming serialized state would risk snapshots and automation; relabeling accurately fixes the user-facing claim without changing behavior | IMPLEMENT |
| 2026-08-17 | Preserve per-receiver MSAA and closest-source ownership | The prior pooled/cross-owner experiment passed tests but caused excessive GI; the repair must not repeat it | TEST-AUDIT, IMPLEMENT |
| 2026-08-17 | Pair-filter only the 1x soft-sun ratio route | This corrects the defining Heitz ordering while leaving hard/one-ray and established 2x-through-16x per-receiver MSAA behavior unchanged | UVSR-AUDIT, IMPLEMENT |
| 2026-08-17 | Bypass paired filtering during sparse accumulation scheduling | Attempt-mask-zero pixels are intentionally unwritten; reading their private raw evidence could consume stale data | UVSR-AUDIT, IMPLEMENT |
| 2026-08-17 | Keep center-direction finite-sun lighting and omit LTC/area-light import | Analytic finite-emitter lighting would intentionally alter authored direct-light energy, while the demo assets/source have ambiguous provenance and UVSR has no area-light type | REF-AUDIT, UVSR-AUDIT, IMPLEMENT |
| 2026-08-17 | Store paired RGBA16F evidence with one shared power-of-two exponent | Independent half-range clamps bias `S_N/U_N`; shared scaling preserves the pair through large finite PBR responses and binary16 storage | UVSR-AUDIT, IMPLEMENT |
| 2026-08-17 | Keep the paired path optional and dimension-latch runtime failures | A filter PSO or large texture failure must preserve the core local ratio and must not retry a costly allocation every frame | UVSR-AUDIT, IMPLEMENT |
| 2026-08-17 | Retain fixed user-radius geometry filtering | The demo's adaptive-radius and analytic-`U` guide need extra passes/resources and measured quality evidence; they are not required for estimator correctness | REF-AUDIT, UVSR-AUDIT, IMPLEMENT |

## Progress and Handoffs

| Date And Time | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-17 | Coordinator `/root` | Active | Clean task worktree at `5d15efd` | Repository, worktree, active-plan, branch, and PR preflight | Dispatch read-only audits |
| 2026-08-17 | REF-AUDIT `/root/reference_audit` | Complete; acknowledged and released | Local downloaded demo | Full CPU/shader/data-flow and provenance audit | Independently implement topology; do not copy unlicensed LTC/source assets |
| 2026-08-17 | TEST-AUDIT `/root/test_history_audit` | Complete; acknowledged and released | `5d15efd` plus prior plans/history | Coverage matrix and exact check set | Replace touched source-token policy with production-coupled invariants |
| 2026-08-17 | UVSR-AUDIT `/root/uvsr_audit` | Complete; acknowledged and released | `5d15efd` renderer pipeline | End-to-end sun/sky, resource, schedule, and consumer audit | Implement the frozen safe contract without pooling MSAA evidence |
| 2026-08-17 | Coordinator `/root` | Baseline complete | `b/bin/uvsr.exe` at unmodified `5d15efd` | 311 shaders compiled; focused Heitz/source/PBR 3/3; Release link passed | Freeze estimator contract after UVSR audit handoff |
| 2026-08-17 | Coordinator `/root` | Candidate implementation complete | Dirty task-local candidate based on `5d15efd` | 314 shaders and Release renderer built; full CTest 47/47; `git diff --check` passed | Final independent review and runtime smoke |
| 2026-08-17 | REVIEW `/root/uvsr_audit` | Initial review complete; finding repaired | Stable pre-lifecycle-fix candidate | Confirmed estimator math, ABI, bindings, DX12 state, packaging, sparse/MSAA/GI contracts; found inactive paired-texture lifetime | Re-read explicit resource retirement and failure latch |
| 2026-08-17 | Coordinator `/root` | Production smoke complete | `b/bin/uvsr.exe`, immediately preceding candidate | Exact process launched through the repository launcher; D3D12 window loaded and presented Sponza at 1920x1080 | User input was detected in the window; stop automation and do not close or duplicate it |
| 2026-08-17 | Coordinator `/root` | Final build and automated verification complete | `b-final/bin/uvsr.exe`, SHA-256 `7BA96351213A025308D32EFD5A9F5A6F8560F9E1181233D9A7B7043F6E178726` | 314/314 shaders; Release link; 47/47 CTest; 2,189 document headings; 32-case heading self-test; README counts; clean diff check | Hand exact artifact to Brock; exact-build runtime smoke and product visual acceptance remain |
| 2026-08-17 | REVIEW `/root/uvsr_audit` | Complete; acknowledged and released | Frozen final source and diff | No P0/P1/P2 blocker in estimator math, ABI/DX12, ownership, scheduling, fallback, packaging, or lifetime | Optional future GPU readback, fault injection, convergence, and performance evidence only |

## Risks and Escalation Triggers

- The demo's estimator may depend on G3D/OptiX area-light or denoiser behavior
  that cannot be transplanted safely into UVSR's DX12 compute architecture.
- Estimator changes can bias visibility while appearing perceptually smoother;
  tests must distinguish lower variance from shifted expectation.
- Sparse visibility and near-zero control estimates can produce unstable ratios,
  NaNs, fireflies, or silent clamping bias.
- Temporal history, resize, camera cuts, and disabled light paths can preserve
  stale estimates even when local shader math is correct.
- Runtime visual and performance evidence depends on an uncontended testing
  window and cannot be fabricated from compilation alone.

Stop and ask the user if:

- faithfulness requires replacing UVSR's current light model or accepting a
  visible quality/performance default tradeoff that cannot be safely opt-in;
- a required implementation would edit Donut, the downloaded reference, another
  worktree's dirty state, published history, or remote state;
- licensing or provenance makes copying a material code section unsafe.

## Completion

- Final integrated commit: intentionally none unless separately authorized
- Verification summary: 314 production DXIL tasks compiled; the Release renderer
  linked; all 47 CTest tests passed; README counts are current; the 32-case
  heading self-test and all 2,189 repository-maintained headings passed; and
  `git diff --check` reported no whitespace error. The immediately preceding
  candidate completed a D3D12 Sponza startup/presentation smoke. The exact final
  artifact was not launched concurrently with the still-open user-active smoke
  candidate.
- Independent review: passed with no P0/P1/P2 estimator, CPU/HLSL ABI, DX12,
  MSAA/sparse/GI, fallback, packaging, or resource-lifetime blocker.
- Coming Soon and documentation update: no temporary Coming Soon entry was
  needed; durable estimator, PBR, path-tracing, advanced-settings, UI, and README
  documentation is updated.
- Pushed, PR, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: Brock's exact-build visual comparison of
  Ratio Estimation off/on and Raw versus Joint Bilateral/Gaussian filtering;
  optional GPU numeric readback, allocation-failure injection, rendered
  RMSE/convergence, and controlled performance measurements are evidence work,
  not source-integration blockers.
- Active ownership released: yes
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/heitz-ratio-estimator-repair.md`
