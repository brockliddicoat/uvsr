# Ratio-Estimator Shadow, MSAA, and CMAA2 Preparation

## Status

- State: completed locally; integration intentionally pending
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/ratio-shadow-msaa-cmaa2-prep` in
  `work/ratio-shadow-msaa-cmaa2-prep`
- Base commit: `54a57b08a462ad83979ccc8912570f2c6cc7ea03`
  (`origin/main`, confirmed live on 2026-08-14)
- Started: 2026-08-14
- Last updated: 2026-08-14
- Archived to:
  `docs/exec-plans/completed/ratio-shadow-msaa-cmaa2-preparation.md`

## Goal and Done Condition

Goal:

Prepare an isolated, implementation-ready path for sample-correct
ratio-estimator ray-traced sun shadows under UVSR's deferred MSAA pipeline and
for complete CMAA2 removal after the active path-tracing task releases its
overlapping renderer contracts.

Done when:

- [x] Primary sources and current UVSR code establish the estimator, sampling,
      resource, and resolve contract without overstating a closest-surface
      broadcast as full per-sample support.
- [x] CMAA2 removal is traced end to end through source, shaders, settings, UI,
      commands, timers, packaging, legal inventory, tests, and current-facing
      documentation.
- [x] Isolated preparation code and deterministic tests prove the chosen MSAA
      sample-classification/output contract and the CMAA2-free presentation
      topology.
- [x] The isolated candidate builds and passes task-relevant tests, document
      checks, and an independent high-risk rendering/deletion review.
- [x] The handoff records the exact rebase/integration order after the active
      path-tracing task, including every overlapping shared hotspot.

## Scope

In scope:

- Heitz ratio-estimator directional shadows at 1x, 2x, 4x, 8x, and 16x raster
  sample topologies.
- A distinction between coherent-pixel reuse and heterogeneous edge samples.
- A pooled single-texture implementation plus an explicit per-raster-sample
  correctness oracle and promotion boundary for heterogeneous edge samples.
- Complete first-party CMAA2 removal and preservation of the remaining order:
  MSAA resolve, TAA, tone mapping, Fast Approximate AA, final transfer/dither.
- Deterministic CPU/source contracts, shader packaging, build evidence, and a
  later integration advisory.

Non-goals:

- Editing, rebasing, building, or launching the active
  `work/path-tracing-resampling-controls` candidate.
- Restoring retired visibility-sample rotation or speculative checkerboard
  visibility history.
- Changing Donut, adding non-DX12 backends, adding a new denoiser, benchmarking,
  publishing, pushing, opening a pull request, merging, or releasing.
- Claiming product acceptance without exact-build visual review after eventual
  composition with the active path-tracing work.

Affected subsystems and paths:

- Ratio-estimator pass, shader, resource contract, and focused tests.
- Deferred MSAA lighting and closest-surface visibility resolve contracts.
- CMAA2 files, renderer lifecycle/presentation order, AA settings and command
  surface, shader manifests, build/package metadata, tests, and current docs.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `README.md`, `src/shaders.cfg`, global AA settings,
  `src/uvsr.cpp`, CPU/HLSL visibility bindings, tests that inspect shared
  renderer source, and all task documentation.

## Baseline

- Canonical repository/remote: live `origin/main` at `54a57b0`.
- Local versus remote state: this isolated worktree is equal to live
  `origin/main`; the primary checkout is intentionally untouched and its local
  `main` is ahead two and behind fifty-one with unrelated untracked files.
- Verified source commit/build: the uncommitted task-local candidate is built at
  `b/bin/uvsr.exe`; SHA-256
  `4D3E4561263D3D0E275CB6893AC2B5A9BEC91C994877DCC332CADDBF834A8297`.
- GPU, scene, camera, resolution, and settings preset when relevant: no GPU or
  performance lease is requested during preparation; any later visual smoke
  uses an exact task-local build and a fixed bundled Sponza view.
- Known pre-existing failures: none established in this clean worktree.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Active path-tracing task | Final released diff and handoff from `work/path-tracing-resampling-controls` | Technically verified but still uncommitted; no-touch | Later rebase and shared-hotspot reconciliation |
| Heitz, Hill, and McGuire ratio-estimator sources | Matched numerator/denominator estimator and correlation rules | Reviewed | Shadow estimator contract |
| DirectX 12 multisample rules | Standard sample positions, Texture2DMS access, and legal output resource topology | Reviewed | Sample-aware shader design |
| UVSR deferred MSAA | Per-sample material/depth lighting plus closest-surface auxiliary resolve | Inspected | Shadow producer and consumer |
| UVSR presentation pipeline | CMAA2-free output order and resource lifetime | Verified | Removal patch |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- The shadow pass reads every coherent covered `Texture2DMS` receiver and pools
  matched numerator/denominator evidence over receiver times emitter samples.
- The single RGBA16F ratio is exact through the linear MSAA resolve when each
  sample's analytic center response is proportional to its sampled denominator;
  otherwise it is a documented lower-storage approximation whose oracle is a
  per-sample ratio result.
- A closest-surface receiver is never substituted for the sample-frequency sun
  shadow dispatch. The closest-surface auxiliary GI-source lighting pass leaves
  pooled sun visibility neutral; final MSAA deferred lighting consumes it.
- Physical hit distance and sun SIGMA remain 1x-only because a pooled receiver
  has no one matched blocker distance.
- CMAA2 removal must leave no selectable setting, command, GPU resource, timer,
  shader task, runtime license copy, or current-facing promise.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Shadow/MSAA architecture | `/root/shadow_msaa_architecture` | Shared, read-only | `54a57b0` | None | UVSR source/history | Completed |
| CMAA2 removal audit | `/root/cmaa2_removal_audit` | Shared, read-only | `54a57b0` | None | UVSR source/history | Completed; final review clean |
| Ratio-estimator research | `/root/ratio_estimator_research` | Shared, read-only | `54a57b0` | None | Primary sources | Completed; final review clean |
| Preparation implementation | `/root` | Isolated task worktree | `54a57b0` | Task-owned branch only | Explorer handoffs | Completed locally |

## Assignment Contracts

### Shadow/MSAA Architecture: Map the Correct Rendering Contract

- Owner/thread: `/root/shadow_msaa_architecture`
- Branch/worktree: shared repository, read-only
- Base commit/state: `54a57b0`; dirty experiments are evidence only
- Read scope: Heitz pass, deferred MSAA, closest-surface resolve, renderer
  routing, settings, tests, documentation, and relevant Git history
- Write scope: none
- No-touch scope: all files, Git state, builds, processes, and GPU/runtime
- Interface/invariant contract: distinguish per-pixel approximation from
  per-sample correctness and preserve material/receiver ownership
- Deliverable: file-level design, validation matrix, and overlap report
- Done when: a distilled handoff is acknowledged
- Required verification: read-only source/history evidence
- Allowed Git and external actions: read-only only
- Stop and report if: surface identity cannot be established from available
  G-buffer information without a new contract
- Handoff revision/artifact: final agent response
- Handoff acknowledged by/on: 2026-08-14

### CMAA2 Removal Audit: Trace Complete Deletion

- Owner/thread: `/root/cmaa2_removal_audit`
- Branch/worktree: shared repository, read-only
- Base commit/state: `54a57b0`; active path-tracing diff inspected only for
  future conflicts
- Read scope: source, settings, UI, commands, timers, shaders, packaging,
  legal inventory, tests, docs, and CMAA2 history
- Write scope: none
- No-touch scope: all files, Git state, builds, processes, and GPU/runtime
- Interface/invariant contract: preserve tone-map, FXAA, sharpen,
  transfer/dither, TAA, and MSAA behavior after removal
- Deliverable: ordered end-to-end deletion map and verification list
- Done when: a distilled handoff is acknowledged
- Required verification: read-only source/history evidence
- Allowed Git and external actions: read-only only
- Stop and report if: a resource or license is shared by a non-CMAA2 consumer
- Handoff revision/artifact: final agent response
- Handoff acknowledged by/on: 2026-08-14

### Ratio-Estimator Research: Establish Statistical and MSAA Rules

- Owner/thread: `/root/ratio_estimator_research`
- Branch/worktree: shared repository, read-only
- Base commit/state: `54a57b0`
- Read scope: primary paper/talk/source, official DXR/MSAA documentation, and
  the UVSR estimator implementation
- Write scope: none
- No-touch scope: all files, Git state, builds, processes, and GPU/runtime
- Interface/invariant contract: cite primary sources and separate evidence from
  implementation inference
- Deliverable: sourced memo with estimator math, bias/variance tradeoffs, and a
  recommended 2x/4x/8x/16x design
- Done when: a distilled handoff is acknowledged
- Required verification: primary-source citations and code cross-check
- Allowed Git and external actions: read-only research only
- Stop and report if: source rules conflict materially
- Handoff revision/artifact: final agent response
- Handoff acknowledged by/on: 2026-08-14

## Integration Order

1. Wait for the technically verified path-tracing candidate to be committed or
   otherwise released as an exact source snapshot. Do not compose from its
   current uncommitted worktree state.
2. Rebase this branch onto that released source, resolving `CMakeLists.txt`,
   `README.md`, `src/shaders.cfg`, `src/uvsr.cpp`, and shared tests semantically.
3. Reapply or retain the CMAA2 removal first, because
   it simplifies AA/presentation state and deletes conflicting shared code.
4. Reapply or retain the sample-aware ratio-estimator/MSAA contract onto the
   simplified renderer, resolving CPU/HLSL bindings semantically.
5. Recalculate shader, UI-command, and statistics inventory counts after
   composition; the prepared base has 326 shader permutations and 43 tests,
   while the path-tracing candidate changes those shared totals.
6. Rerun targeted shader/test evidence, then full build/tests and exact-build
   visual MSAA edge/shadow review.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Estimator correctness | Matched numerator/denominator tests and finite fail-open behavior | Focused Heitz tests | Passed |
| MSAA surface ownership | Deterministic coherent, partial-coverage, and heterogeneous-surface cases | New CPU/source contracts | Passed |
| Shader/resource completeness | Every supported sample-count variant compiles and packages | Release shader build and bundle tests | Passed; 326 permutations |
| CMAA2 deletion | Zero current runtime/source references and no stale selectable surface | `rg`, legal inventory, focused tests, shader bundle | Passed; historical documentation only |
| Remaining AA behavior | TAA, FXAA, MSAA, transfer/dither, and all enable combinations remain valid | Focused AA/renderer tests | Passed |
| Documentation integrity | All in-scope visible headings use Title Case | Heading validator and full in-scope document scan | Passed; 223 headings and bold lead-ins checked |
| High-risk review | Independent rendering/deletion review reports no unresolved P0-P2 issue | Frozen-diff review | Passed; both final reviews clean |
| Complete automated suite | Every configured test passes against the short-path Release build | `ctest --test-dir b -C Release --output-on-failure` | Passed; 43 of 43 |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-14 | Use an isolated branch/worktree at live `origin/main` | The active path-tracing task owns overlapping shared files and a build tree; a separate worktree preserves both candidates | All |
| 2026-08-14 | Treat closest-surface broadcast as an approximation, not full MSAA support | The current RGB ratio is evaluated for one material and receiver; heterogeneous samples may have different response and visibility | Shadow/MSAA design and claims |
| 2026-08-14 | Prepare CMAA2 removal before eventual shadow/MSAA composition | Removing an unwanted downstream AA stage first reduces shared presentation/UI conflicts | Integration order |
| 2026-08-14 | Keep one RGBA16F pooled ratio over valid receiver times emitter samples | It preserves the current direct-light consumer ABI and is statistically valid over the joint domain; a per-sample array is reserved as the correctness oracle because center-response mismatch can bias heterogeneous pixels | Shadow/MSAA implementation |
| 2026-08-14 | Normalize by valid covered receivers, not configured MSAA count | The ratio is homogeneous but the fixed `1e-4` fail-open guard is not; configured-count normalization would change thin-coverage behavior | Shadow shader and CPU tests |
| 2026-08-14 | Keep physical hit distance and sun SIGMA at 1x | A pooled multi-receiver signal has no single matched blocker distance | Shadow output and denoising routing |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | `/root` | Completed locally | Uncommitted branch diff at `54a57b0` | Release renderer and 326 shaders compiled; 43 of 43 tests, legal inventory, and heading checks passed | Preserve for later composition |
| 2026-08-14 | Read-only explorers | Completed | Architecture, estimator, and deletion handoffs | Primary sources and current code cross-checked; final reviews clean | Ownership released |
| 2026-08-14 | `/root/ratio_estimator_research` | Finding repaired and re-reviewed | `src/uvsr.cpp` timing predicate and focused source contract | Heitz timing expectation now includes the same noise dependency as dispatch; no remaining findings | Complete |
| 2026-08-14 | `/root/cmaa2_removal_audit` | Findings repaired and re-reviewed | Legal inventory and current-facing copy | Validator now observes unstaged deletions; obsolete sample index and CMAA2 copy removed; no remaining findings | Complete |

## Risks and Escalation Triggers

- Full pooled ray work scales as covered receivers times emitter samples and can
  reach 1024 rays for a fully covered 16x pixel at 64 emitter samples. No
  production-performance claim is made without a matched benchmark.
- The pooled result can differ from the per-sample oracle where analytic
  center-response-to-integrated-response ratios vary across samples. Mixed
  materials, glossy highlights, horizon clipping, sparse coverage, and full
  coverage are mandatory visual comparison cases after integration.
- MSAA hit distance and sun SIGMA intentionally remain unavailable; changing
  that requires a new sample-indexed signal and denoiser contract.
- CMAA2 shares presentation intermediates with FXAA, deferred temporal
  sharpening, transfer, and dithering; delete only exclusively owned pieces.
- Active path-tracing changes overlap nearly every shared renderer hotspot and
  invalidate any pre-composition build evidence.

Stop and ask the user if:

- Product review rejects the pooled-versus-per-sample edge difference; that
  decision promotes the reference sample-indexed output/consumer ABI.
- Performance review rejects the uncapped receiver-times-emitter cost; any
  adaptive reuse policy must be specified and validated rather than silently
  reducing work.

## Completion

- Final integrated commit: pending; commits are not authorized
- Verification summary: Release renderer and all 326 shaders compiled; 43 of
  43 CTest cases, legal inventory, exact shader count, source-reference scans,
  and `git diff --check` passed. Runtime visual review remains intentionally
  deferred until composition with the released path-tracing source snapshot.
- Independent review: estimator and deletion frozen-diff reviews completed with
  no remaining P0-P2 findings
- Coming Soon/documentation update: intentionally deferred until scope is
  stable after active-task composition
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: per-sample oracle visual comparison and
  matched performance measurement after integration
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ratio-shadow-msaa-cmaa2-preparation.md`
