# Horizon Approximation: Diffuse AO and GI Estimator

## Status

- State: completed locally; ready for product review
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/horizon-approximation` at
  `work/horizon-approximation`
- Base commit: `efff7ecaf45024d949998567b480c131e348abef`
- Started: 2026-08-06
- Completed: 2026-08-06
- Planned archive: `docs/exec-plans/completed/horizon-approximation.md`

## Goal and Done Condition

Goal: Add a first-listed, budget-oriented `Horizon Approximation` estimator to
the Diffuse drawer. It must serve AO and GI through UVSR's existing one-slice,
two-sided near-to-far traversal while compiling out the radial visibility mask,
sector phase, interval endpoint, and popcount work used by bitmask estimators.

Done when:

- [x] The new estimator is a distinct compile-time permutation and never builds,
  reads, updates, or falls back to a visibility bitmask.
- [x] Each side maintains one incremental horizon; a horizon-raising sample owns
  the full newly covered receiver-cosine-weighted angular wedge for AO and GI.
- [x] GI fetches material-resolved source radiance and source normal only after a
  sample raises the horizon, with a cheap radius falloff. No separate material
  fetch is added because the current source-radiance buffer already contains it.
- [x] The configured sample count remains the total budget split across the two
  sides; slice rotation and radial jitter remain noise-controlled.
- [x] Horizon thickness can be disabled and, when enabled, uses only a cheap,
  monotonic horizon-specific approximation without reconstructing front/back
  intervals.
- [x] Focused contracts, mathematical reference tests, shader compilation, the
  Release build, full CTest, and document validation pass for the exact final
  candidate. AO-only, GI-only, AO+GI, and thickness runtime transitions passed
  on the shader-identical pre-review build; the post-review UI-only rebuild was
  not relaunched after user input took control of the existing renderer window.
- [x] Static DXIL evidence demonstrates that the horizon permutation excludes
  mask/popcount/sector machinery; no matched runtime speed claim is made without
  a valid benchmark window.

## Scope

In scope:

- A new estimator enum/UI option ordered before the three bitmask estimators.
- An independently expressed horizon-only AO/GI integration path informed by
  NVIDIA's incremental HBAO algorithm and Mayaux's HBIL angular-ownership math.
- Minimal horizon-specific thickness enablement and approximation.
- Focused reference and source-contract tests, user/design documentation, and
  execution-plan/roadmap updates.

Non-goals:

- HBAO, HBAO+, GTAO, or Mayaux HBIL as complete pipelines.
- Additional directions, depth preprocessing, deinterleaving, blur, new passes,
  history resources, material buffers, debug views, or speculative controls.
- Changes under `donut/`, publication, pull requests, merges, or releases.
- Incorporating open PR #10 or #11; the requested base is the live `main` tip.

Affected subsystems and paths:

- `src/screen_space_visibility*`, `src/visibility_estimator*`, and the existing
  Diffuse UI/settings path in `src/uvsr.cpp`.
- Visibility estimator/reference/source-contract tests under `tests/`.
- `README.md`, `docs/screen-space-visibility.md`, and this execution plan.

Shared hotspots reserved for the coordinator:

- `README.md`, `src/uvsr.cpp`, shared CPU/HLSL estimator constants and contracts,
  shader configuration, documentation, build trees, packaged shaders, and the
  runtime/GPU lease.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`,
  live `origin/main`
- Local versus remote state: isolated branch equals live remote tip at start;
  the primary checkout's divergent local `main` is preserved and out of scope.
- Verified source commit/build: requested live base
  `efff7ecaf45024d949998567b480c131e348abef`; exact prior verification artifact
  is being inventoried before build reuse is considered.
- GPU, scene, camera, resolution, and settings preset when relevant: runtime
  smoke will use the repository's stable bundled scene and fixed estimator
  transitions; any performance comparison requires the full Position-1 hygiene
  protocol and a separately recorded matched baseline.
- Known pre-existing failures: none established; open PR #10 changes shared
  shader helpers and open PR #11 adds degenerate-path coverage but neither is in
  the requested base.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Live GitHub main | `efff7ecaf45024d949998567b480c131e348abef` | Pinned | All tasks |
| NVIDIA ScreenSpaceAO sample | `HorizonBasedAOEngine.fx` incremental horizon equations | Complete | IMPL-1 |
| Mayaux HBIL package | Pinned commit `f620f83028f1f131afbac8f2937516296135522e` | Complete | IMPL-1 |
| UVSR estimator contract | Existing sample budgeting, projection, noise, AO/GI outputs | Complete | IMPL-1 and TEST-1 |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve existing bindings, resources, output formats, sample-budget meaning,
  and the factory default unless explicit evidence makes a change necessary.
- Add one compile-time estimator value and UI label. Existing bitmask options
  remain behaviorally intact and retain stable names.
- Horizon AO and GI share the same normalized newly claimed receiver-cosine
  angular mass. Samples below the running side horizon contribute nothing.
- No horizon permutation may call or reference radial-mask, sector-phase,
  back-endpoint, interval-construction, or popcount helpers.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| RES-1 | Read-only research agent | Shared read-only | `efff7ec` plus pinned sources | None | None | Done |
| ARCH-1 | Read-only repository agent | Shared read-only | `efff7ec` | None | None | Done |
| IMPL-1 | `/root` | `codex/horizon-approximation` | `efff7ec` | All task source/tests/docs | RES-1, ARCH-1 | Done |
| REVIEW-1 | Independent read-only reviewer | Candidate state | Candidate | None | IMPL-1 | Done |

## Assignment Contracts

### Res-1: Distill NVIDIA and Mayaux Integration Math

- Owner/thread: read-only research agent
- Branch/worktree: shared read-only access; no repository writes
- Base commit/state: UVSR `efff7ec`; NVIDIA SDK 10.5 sample and Mayaux commit
  `f620f83028f1f131afbac8f2937516296135522e`
- Read scope: supplied source archive, papers, pinned HBIL shader code, and only
  enough UVSR shader context to map conventions.
- Write scope: none
- No-touch scope: all repository files, Git state, builds, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: report exact angular integral, horizon update,
  normalization, falloff, and late-fetch ordering suitable for one slice and two
  sides; distinguish source-backed details from proposed adaptation.
- Deliverable: concise equations, source locations, free/cheap quality wins,
  licensing notes, and implementation hazards.
- Done when: NVIDIA `integrate_direction` and
  `AccumulatedHorizonOcclusion_LowQuality` plus Mayaux `IntegrateSolidAngle`,
  `SampleIrradiance`, and `GatherIrradiance` are traced precisely.
- Required verification: cite exact source files/functions and pinned revisions.
- Allowed Git and external actions: read-only network/source retrieval; no Git
  mutation and no repository writes.
- Stop and report if: the source archive/license is unavailable or source math
  conflicts materially with the requested ownership model.
- Handoff revision/artifact: source equations, licensing analysis, and pinned
  references returned against `efff7ec`
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Arch-1: Map UVSR Estimator, UI, and Verification Contracts

- Owner/thread: read-only repository agent
- Branch/worktree: `work/horizon-approximation`, read-only
- Base commit/state: `efff7ecaf45024d949998567b480c131e348abef`
- Read scope: estimator enums/settings, `src/screen_space_visibility*`, Diffuse UI,
  shader configs, tests, launch/build scripts, and verification records.
- Write scope: none
- No-touch scope: all repository files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: identify exact compile-time specialization and
  AO/GI sample traversal contracts without proposing unrelated architecture.
- Deliverable: path/function map, current enum/default/sample/thickness behavior,
  required permutations, focused checks, likely hazards, and a minimal write set.
- Done when: coordinator can implement without guessing about numeric contracts,
  shader packaging, UI ordering, or tests.
- Required verification: repository citations with line/function names.
- Allowed Git and external actions: read-only inspection only.
- Stop and report if: unexpected diffs or active ownership overlap touch the
  proposed write set.
- Handoff revision/artifact: enum, shader-permutation, sampling, UI, and test map
  returned against `efff7ec`
- Handoff acknowledged by/on: `/root`, 2026-08-06

### Impl-1: Implement and Integrate Horizon Approximation

- Owner/thread: `/root`
- Branch/worktree: `codex/horizon-approximation` at
  `work/horizon-approximation`
- Base commit/state: `efff7ecaf45024d949998567b480c131e348abef`
- Read scope: full task-relevant repository and supplied primary references
- Write scope: task-specific first-party source, tests, `README.md`, visibility
  documentation, and this plan
- No-touch scope: `donut/`, `Donut-Samples/`, unrelated active-plan files,
  generated build/work artifacts, local primary-checkout changes, and peer paths
- Build directory and runtime/GPU/resource lease: dedicated final tree
  `build-horizon-approximation-final`; coordinator was sole build/runtime operator
- Dependencies already integrated: live base only
- Interface/invariant contract: preserve existing estimator behavior and public
  resource layout; isolate the new permutation with preprocessor specialization.
- Deliverable: locally verified candidate and exact executable link
- Done when: all goal checks map to evidence and independent review has no
  unresolved high-risk blocker.
- Required verification: focused unit/source contracts, all relevant CTest,
  shader and Release builds, document checker, diff checks, runtime transitions,
  and static DXIL inspection.
- Allowed Git and external actions: local edits/builds/tests/launches only; no
  commit, push, PR, merge, release, or deployment.
- Stop and report if: implementation requires a new pass/resource, changes a
  shared external ABI, has uncertain licensing, collides with another writer, or
  cannot preserve strong normalized AO/GI without a material quality tradeoff.
- Handoff revision/artifact:
  `build-horizon-approximation-final/bin/uvsr.exe`, SHA-256
  `19CDBB2962B25910C89616566509600A4529A15B9E369778EEC1CF6764623D62`
- Handoff acknowledged by/on: local handoff prepared 2026-08-06

### Review-1: Review High-Risk Shader and Integration Changes

- Owner/thread: independent read-only reviewer after candidate freeze
- Branch/worktree: `work/horizon-approximation`, read-only candidate state
- Base commit/state: candidate diff from `efff7ec`
- Read scope: full task diff, source references, focused tests, build evidence
- Write scope: none
- No-touch scope: all files, Git state, builds, renderer, and GPU
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: IMPL-1 candidate freeze
- Interface/invariant contract: verify mathematical normalization, traversal
  ordering, true compile-time mask exclusion, late GI fetches, settings/default
  safety, and absence of bitmask fallback.
- Deliverable: prioritized findings with exact paths/lines and acceptance gaps.
- Done when: all high-risk shader, performance-contract, and integration risks
  are independently assessed.
- Required verification: read-only diff/source/test inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: candidate changes during review.
- Handoff revision/artifact: frozen-candidate diff
  `d37e6a9e0755c50c49f9529bceb969e1200c1251`; no shader or math blocker
- Handoff acknowledged by/on: `/root`, 2026-08-06; all actionable findings
  repaired and covered by focused/full verification

## Integration Order

1. Pin reference equations and UVSR contracts from RES-1 and ARCH-1.
2. Implement enum/UI/config specialization and horizon AO/GI math as one coherent
   writer-owned change.
3. Add focused mathematical and compile-time source contracts.
4. Build shaders and Release UVSR in the dedicated tree, then run tests and
   document/DXIL checks.
5. Freeze writes for REVIEW-1, repair any blockers serially, and reverify.
6. Perform runtime estimator transitions and leave the exact candidate artifact
   linked for user review; keep all work local.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| First UI option without default regression | Enum/UI/default tests and source review | Focused estimator and UI contract tests | Pass; existing bitmask default preserved and hidden Horizon flag ignored by bitmask preset matching |
| Distinct mask-free compile-time path | Preprocessed/source contract plus DXIL evidence | Shader build and visibility DXIL tooling | Pass; zero forbidden references in preprocessed HLSL and DXIL assembly |
| Correct incremental wedge integration | CPU reference equations and edge cases | Visibility estimator reference tests | Pass |
| Late GI payload fetch | Source contract and compiled shader review | Focused source test and DXIL inspection | Pass |
| Total samples split across two sides | Existing and new sampling reference cases | Visibility sampling tests | Pass for total budgets 1 through 64 |
| Optional monotonic cheap thickness | Reference monotonicity tests and source contract | Focused estimator tests | Pass; default off and slider disabled while off |
| No existing estimator regressions | Full task-relevant CTest and shader build | Release build plus CTest | Pass; 265 UVSR shaders and 36/36 CTest |
| Runtime viability | Exact executable, scene/settings, transitions, screenshot/log | Controlled runtime smoke | AO-only, GI-only, AO+GI, and thickness transitions passed on the shader-identical pre-review build; exact post-review UI-only executable relaunch deferred after user input took over the open renderer |

For performance work, static instruction/resource evidence may support the claim
that unused machinery compiled out. A numerical speedup or `cheapest` runtime
ranking requires a fully matched Position-1 benchmark with all repository
hygiene gates; otherwise it remains an architectural expectation, not a measured
performance claim.

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-06 | Base exclusively on live `origin/main` at `efff7ec`. | The primary checkout is 2 commits ahead and 23 behind; rebasing or overwriting it would risk unrelated work. | All |
| 2026-08-06 | Keep one coordinator/writer and parallelize read-only research. | Shader, CPU enum, UI, tests, and docs share contracts; competing writers would increase semantic and file-ownership risk. | RES-1, ARCH-1, IMPL-1 |
| 2026-08-06 | Preserve the current factory default unless source/runtime evidence requires otherwise. | "First" is interpreted as UI cost ordering, not authorization to change default image quality. | IMPL-1, TEST-1 |
| 2026-08-06 | Exclude PR #10 and #11 from the base. | The user named live main; importing unmerged helper/test work would broaden scope and change provenance. | All |
| 2026-08-06 | Treat the implementation as independent and source-informed. | Review found no substantial NVIDIA source expression; identifiers, control flow, and the cumulative primitive are independently written. Documentation distinguishes algorithmic reference from copied code. | RES-1, IMPL-1, REVIEW-1 |
| 2026-08-06 | Preserve the open smoke-test renderer and build the final artifact in a fresh tree. | Computer control detected user input, so interrupting that window would violate the runtime lease. | IMPL-1 |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-06 | RES-1 | Done | Research handoff against `efff7ec` and pinned NVIDIA/Mayaux sources | Exact equations, ownership, normalization, falloff, and licensing traced | Integrated into implementation |
| 2026-08-06 | ARCH-1 | Done | Repository-map handoff against `efff7ec` | Enum, permutation, scheduler, material, and test contracts traced | Integrated into implementation |
| 2026-08-06 | `/root` | Done | Final Release artifact SHA-256 `19CDBB2962B25910C89616566509600A4529A15B9E369778EEC1CF6764623D62` | 265 UVSR shaders; 36/36 CTest; zero forbidden DXIL references; runtime transitions on shader-identical pre-review build | Ready for user product review |
| 2026-08-06 | REVIEW-1 | Done | Frozen diff `d37e6a9e0755c50c49f9529bceb969e1200c1251` | No shader/math blocker; one UI preset issue and bookkeeping/provenance notes repaired | Findings closed by coordinator verification |

## Risks and Escalation Triggers

- An incorrect 2D angular integral can systematically weaken AO/GI even when
  the horizon rises correctly; normalized receiver-cosine mass needs independent
  CPU reference coverage.
- A visually mask-free branch may still compile shared sector/mask setup if the
  specialization boundary is too late; static compiled evidence is required.
- GI payload loads must remain control-flow dominated by a genuine horizon rise;
  compiler flattening and unconditional helper calls need inspection.
- A thickness approximation can become non-monotonic or effectively reconstruct
  a second endpoint; tests must constrain both behavior and source structure.
- Open visibility PRs touch nearby contracts and can create future merge work,
  but they are not dependencies of this local experiment.

Stop and ask the user if:

- Achieving the requested strong, normalized response requires changing the
  existing AO/GI output meaning, adding passes/resources, or choosing a visible
  quality-versus-performance compromise not already authorized.
- Licensing or provenance prevents a clean adaptation of the source math.
- Runtime verification requires stopping or changing an unrelated process or a
  user-controlled renderer session.

## Completion

- Final integrated commit: none; no commit was authorized
- Verification summary: final Release build passed, all 265 UVSR shader
  permutations compiled, 36/36 CTest passed, the document and line-count
  contracts passed, and representative Horizon DXIL contained zero forbidden
  mask/interval/sector/popcount references
- Independent review: complete; no shader or math blocker, and all actionable
  integration findings were repaired and regression-tested
- Coming Soon/documentation update: reconciled to Ready for Review
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual acceptance of the exact final
  executable and a matched Position-1 benchmark before claiming measured cost
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/horizon-approximation.md`
