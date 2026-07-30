# Bend Screen-Space Shadows Experiment

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/bend-screen-space-shadows` in
  `work/uvsr-bend-shadows`
- Base commit: `a55e215e4bf0eddb20330283d9a4f8e853bda49f`
- Started: 2026-07-18
- Last updated: 2026-07-18
- Planned archive:
  `docs/exec-plans/completed/bend-screen-space-shadows.md`

## Goal and Done Condition

Goal: add the smallest isolated renderer integration for Bend Studio's released
screen-space shadow implementation, preserving the released CPU and GPU source
files byte-for-byte behind a UVSR-owned adapter.

Done when:

- [x] The live canonical merge is established as a clean exact-commit technical
      baseline before feature edits.
- [x] Bend's released files are preserved byte-for-byte with source hashes and
      attribution, while UVSR owns only a thin resource, dispatch, settings, and
      composite adapter.
- [x] The adapter reads the existing reverse-Z depth buffer, dispatches a
      compile-time `SAMPLE_COUNT` variant, writes an `R8_UNORM` visibility
      texture, and composites only directional-light shadow visibility.
- [x] Every requested setting and speed-to-quality profile is present, with
      **Performance** mapping to the frozen Bend Exact defaults and optional
      features off.
- [ ] Wave and edge debug views are verified before the requested directional
      light, camera, reverse-Z, resize, edge, and grazing-surface matrix.
- [ ] Exact feature GPU time, total frame time, artifacts, and the longest
      reliable compiled sample-count variant are recorded.

## Scope

In scope:

- Enabled control.
- Profiles: Performance, Balanced, Quality, and Custom, mapped to the frozen
  60-, 240-, and 960-pixel preset values.
- Compiled 60, 120, 240, 480, and 960 `SAMPLE_COUNT` variants.
- Surface thickness, bilinear threshold, shadow contrast, hard-shadow samples,
  fade-out samples, ignore-edge-pixels, precision-offset,
  bilinear-offset-mode, and early-out controls.
- Edge, thread, and wave debug views.
- Existing depth input, `R8_UNORM` visibility output, compute dispatch, and
  directional-light composite.
- File and pass boundaries that let a future hierarchical far tracer compose
  separately without modifying validated Bend source.

Non-goals:

- Stochastic sampling.
- Temporal filtering or history resources.
- Hi-Z construction or hierarchical far tracing.
- New thickness buffers.
- Bend-source algorithm edits.
- Unrelated renderer, lighting, material, display, or dependency changes.
- Push, pull request, merge, release, or Canonical promotion.

Affected subsystems and paths:

- First-party renderer and settings integration in `src/`.
- Shader registration in `src/shaders.cfg`.
- Root build and packaging configuration.
- A dedicated vendored Bend-source directory plus a UVSR adapter.
- Focused CPU reference tests and user-facing renderer documentation.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- `src/shaders.cfg`
- `src/uvsr.cpp`
- CPU/HLSL bindings and preset contracts
- All runtime, GPU timing, and window control

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main`
  `a55e215e4bf0eddb20330283d9a4f8e853bda49f`
- Local versus remote state: experiment starts equal to live `origin/main`.
- Verified source commit/build: the user clarified on 2026-07-18 that the build
  with the newest UI is the canonical build. That is live `a55e215`, whose tree
  is byte-identical to the exact user-accepted `87eb377` contender head. Run a
  fresh post-merge technical baseline before feature edits.
- GPU, scene, camera, resolution, and settings preset when relevant: detect and
  record during the baseline run; use PBR Sponza Decorated, Benchmark Position
  1, 1920x1080, deferred PBR defaults for repeatable timing.
- Known pre-existing failures: none recorded; establish exact evidence.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `a55e215` | Integrated as base | Whole experiment |
| Bend Studio release | Original archive bytes and defaults | Integrated and hash-locked | Vendor source and adapter |
| UVSR depth contract | Existing reverse-Z device depth | Integrated | Bend dispatch adapter |
| Directional lighting | Existing main directional-light state | Integrated | Dispatch constants and composite |
| GPU performance monitor | Existing timer-query API | Integrated; runtime samples pending | Timing report |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Vendored Bend files are immutable inputs and must retain recorded hashes.
- UVSR-specific resource bindings, reverse-Z/light conversion, variant
  selection, and composite behavior live outside those files.
- The visibility target is full-resolution `R8_UNORM`: `1.0` means visible and
  lower values attenuate directional-light contribution.
- Sample length is a finite compile-time variant list owned by one adapter table
  so later variants require one registration entry rather than Bend-source
  edits.
- The validated Bend near tracer stays a standalone producer. A future
  hierarchical far tracer must be a separate producer/composition stage and
  must not be represented by a dormant placeholder implementation now.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `canonical-audit` | `/root/repo_canonical_audit` | Read-only shared context | Repository state | None | Live refs and records | Complete |
| `bend-audit` | `/root/bend_source_audit` | Read-only shared context | Published sources | None | Bend release | Complete |
| `validation-design` | `/root/testing_strategy` | Read-only shared context | Published sources | None | Requested matrix | Complete |
| `integrate` | `/root` | This worktree | `a55e215` | All experiment paths | All audits and stable contract | Active |
| `independent-review` | `/root/independent_render_review` | Read-only final candidate | Candidate source snapshot | None | Integrated candidate | Complete; no P0-P2 findings |

## Assignment Contracts

### Canonical Audit: Resolve the Exact Base

- Owner/thread: `/root/repo_canonical_audit`
- Branch/worktree: read-only shared repository context
- Base commit/state: local canonical checkout plus live GitHub refs
- Read scope: repository instructions, worktrees, plans, verification records,
  branches, and live remote metadata
- Write scope: none
- No-touch scope: all files, refs, indices, processes, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: classify Canonical verification only from
  recorded evidence
- Deliverable: exact base recommendation and overlap audit
- Done when: the live target, explicit verified checkpoint, and isolation method
  are supported by exact evidence
- Required verification: read-only Git and repository-record inspection
- Allowed Git and external actions: read-only
- Stop and report if: live and recorded state conflict materially
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Bend Audit: Preserve the Released Contract

- Owner/thread: `/root/bend_source_audit`
- Branch/worktree: read-only shared context
- Base commit/state: published Bend resources as of 2026-07-18
- Read scope: user-provided Bend article, archives, and technical references
- Write scope: none
- No-touch scope: workspace, Git state, and external publication
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: report released bytes, defaults, macros,
  bindings, and licensing without inventing compatibility behavior
- Deliverable: distilled integration and preservation audit
- Done when: adapter requirements and source constraints are explicit
- Required verification: cross-check article, archives, and code
- Allowed Git and external actions: read-only research
- Stop and report if: licensing or archive versions conflict
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Validation Design: Define Pass and Failure Evidence

- Owner/thread: `/root/testing_strategy`
- Branch/worktree: read-only shared context
- Base commit/state: requested behavior and published references
- Read scope: validation-related repository and published material
- Write scope: none
- No-touch scope: workspace, GPU, renderer process, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: every requested case receives observable pass,
  artifact, and timing criteria
- Deliverable: ordered validation matrix
- Done when: debug-first, correctness, resize, and performance evidence is
  unambiguous
- Required verification: source-grounded review
- Allowed Git and external actions: read-only research
- Stop and report if: a requested case cannot be made deterministic
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Integrate: Build the Isolated Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/bend-screen-space-shadows`,
  `work/uvsr-bend-shadows`
- Base commit/state: clean `a55e215`
- Read scope: full first-party repository, pinned dependency APIs, Bend release,
  and technical references
- Write scope: experiment source, shaders, build registration, focused tests,
  this plan, and required documentation
- No-touch scope: `donut/`, unrelated worktrees and untracked canonical assets,
  archived experiments, remote refs, and excluded renderer features
- Build directory and runtime/GPU/resource lease: task-local isolated build; the
  coordinator exclusively owns the UVSR window and GPU benchmark
- Dependencies already integrated: live canonical base; audit handoffs before
  interface-sensitive edits
- Interface/invariant contract: preserve Bend files byte-for-byte and keep every
  UVSR conversion in a small adapter
- Deliverable: locally verified candidate and evidence package
- Done when: every requested behavior is implemented and every verification row
  has a result or explicit blocker
- Required verification: baseline and candidate Release builds, full CTest,
  document checker, `git diff --check`, source hashes, runtime matrix, captures,
  and fixed-condition GPU timing
- Allowed Git and external actions: local branch/worktree edits, builds, tests,
  runtime control, and focused local commits; no push, PR, merge, or release
- Stop and report if: Bend source must be changed, a material lighting policy is
  ambiguous, licensing forbids vendoring, or validation exposes unrelated
  renderer changes as necessary
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Independent Review: Audit the High-Risk Candidate

- Owner/thread: assign after integration freeze
- Branch/worktree: read-only final candidate
- Base commit/state: frozen candidate revision
- Read scope: full task diff and relevant CPU/HLSL contracts
- Write scope: none
- No-touch scope: all files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: complete candidate
- Interface/invariant contract: review correctness, resource lifetime,
  reverse-Z math, bindings, variant completeness, upstream-byte preservation,
  compositing scope, and excluded-feature creep
- Deliverable: prioritized findings with exact paths and evidence
- Done when: no P0-P2 issue remains or every issue is repaired and re-reviewed
- Required verification: source and diff inspection
- Allowed Git and external actions: read-only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

## Integration Order

1. Establish and record the exact clean `a55e215` technical baseline.
2. Acquire, hash, and audit the Bend release and freeze the adapter contract.
3. Vendor untouched sources and add focused hash/default/variant reference
   tests.
4. Add the resource, dispatch, preset, UI, and directional-light composite
   adapter.
5. Build and run automated checks.
6. Verify debug views, then correctness cases, then fixed-condition timing.
7. Freeze the candidate for independent review.
8. Repair only task-introduced failures, rerun affected evidence, and archive
   the completed plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Clean newest-main baseline | Exact source, build, tests, and responsive labeled smoke | Release build, full CTest, launcher | `a55e215`; Release build and 12/12 baseline tests passed |
| Upstream files untouched | Archive and vendored SHA-256 equality | Hash manifest and reference test | CPU `23AAE596...02A`; GPU `7FBE24BD...A68C`; CMake hash gate passed |
| Performance/Bend Exact defaults | Exact reset values and optional features off | CPU reference test and UI inspection | Focused preset/reset test passed; UI defaults inspected |
| Compiled variant coverage | 60/120/240/480/960 shader artifacts and dispatch mapping | Build/package audit and reference test | All 45 length/hard/fade permutations compiled; reference test passed |
| Visibility resource contract | Full-resolution `R8_UNORM`, white clear, resize-safe lifetime | Resource inspection and runtime resize | Pending |
| Wave and edge debug views | Expected lane/edge structure without corruption | Debug-first fixed camera captures | Pending |
| Thread debug view | Stable workgroup visualization | Fixed camera capture | Pending |
| Directional-light projection | Onscreen, offscreen, and behind-camera behavior | Controlled light matrix | Pending |
| Reverse-Z correctness | Near/far ordering and ray comparisons are stable | Depth/debug inspection and scene run | Pending |
| Boundary robustness | No OOB artifacts at screen edges or resolution changes | Resize and edge sweep | Pending |
| Grazing-surface robustness | Bias/thickness behavior is bounded and documented | Grazing-angle sweep | Pending |
| Performance and reliability | Total and feature GPU time, artifacts, reliable maximum | Fixed benchmark with warmup/sample window | Pending |
| Excluded scope absent | No stochastic, temporal, Hi-Z, thickness-buffer, or unrelated diff | Final source/diff audit | Pending |

For performance work, record:

- baseline and candidate commits;
- GPU, scene, fixed camera, resolution, and settings preset;
- warmup and sample window/count;
- total frame time plus Bend dispatch and composite pass costs;
- correctness and image-quality guardrails;
- before/after captures and raw measurement artifact.

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-18 | Start from live `origin/main` `a55e215` in a linked worktree. | It is the newest canonical merge and its tree exactly matches the accepted contender head; the tracked canonical checkout is 22 commits behind and contains unrelated untracked assets. | Whole experiment |
| 2026-07-18 | Treat `a55e215`, with the newest UI, as the user-designated canonical build. | The user resolved the repository-record ambiguity directly; this keeps the accepted current UI and the live canonical line. | Base selection and provenance |
| 2026-07-18 | Require a fresh post-merge technical baseline before feature edits. | The check separates pre-existing current-UI behavior from task-introduced shadow behavior. | Baseline and provenance |
| 2026-07-18 | Keep one coordinator as the only writer and GPU operator. | Renderer, root build, settings, bindings, and composite paths are tightly coupled shared hotspots. | Implementation and validation |
| 2026-07-18 | Represent future far tracing through pass boundaries, not dormant code. | This preserves the requested extension seam without violating near-YAGNI or adding an unvalidated Hi-Z placeholder. | Architecture |
| 2026-07-18 | Write the volatile Bend constants before rebinding compute state for each released CPU dispatch. | NVRHI's D3D12 validation rejected the original bind-before-first-write order. Donut's multi-dispatch passes bind each volatile-buffer version after writing it. | Runtime adapter |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-18 | `integrate` `/root` | Active | Worktree at `a55e215` | Live fetch, tree equality, branch and overlap audit | Build clean baseline |
| 2026-07-18 | `integrate` `/root` | Runtime validation active | Corrected local candidate | Release build; full 13/13 CTest; title-case self-test and 380-heading scan | Complete debug-first matrix and timing |
| 2026-07-18 | `independent-review` `/root/independent_render_review` | Complete | Corrected local source snapshot | Vendored hashes, D3D12 resources, permutations, reverse-Z, light matching, debug, and timing audit | No P0-P2 findings; remove generated Python cache |

## Risks and Escalation Triggers

- Very long sample variants can exceed the algorithm's validated range or shader
  compiler/resource limits; compile support does not imply visual reliability.
- Directional lights outside the view or behind the camera can produce invalid
  homogeneous endpoints unless the adapter clips or disables them according to
  the released CPU contract.
- The renderer uses reverse-Z; all conversion must be isolated and tested.
- An `R8_UNORM` output may require a typed UAV capability path or a safe
  intermediate only if the existing backend cannot bind it directly.
- The Bend release license and attribution must be preserved exactly.

Stop and ask the user if:

- the released license conflicts with repository vendoring;
- a required workaround would modify Bend's files, add an excluded feature, or
  change unrelated lighting behavior;
- publication beyond this local experiment is requested without a clear target.

## Completion

- Final integrated commit: pending
- Verification summary: pending
- Independent review: pending
- Coming Soon/documentation update: pending
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: pending
- Active ownership released: no
- Archived to completed/abandoned path: pending
