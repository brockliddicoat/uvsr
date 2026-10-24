# UVSR Shadow and Temporal Provenance Cleanup

## Status

- State: complete local candidate
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/taa-shadow-cleanroom` in `work/taa-shadow-cleanroom`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Started: 2026-07-30
- Last updated: 2026-07-30
- Planned archive:
  `docs/exec-plans/completed/taa-shadow-cleanroom.md`

## Goal and Done Condition

Goal: replace the directly vendored screen-space directional-shadow CPU/GPU
implementation with materially different first-party UVSR code, retain the
Microsoft-derived temporal quality path under its complete MIT notice while
removing product-lineage branding, preserve renderer-facing contracts as closely
as practical, and add explicit temporal quality/performance modes grounded in
the official Wicked Engine implementation and independently expressed in UVSR
code.

Done when:

- [x] No current first-party source, test, build rule, runtime label, or
      maintained documentation contains the retired implementation-lineage
      names or misstates the origin of retained Microsoft-derived code.
- [x] No copied or substantially similar former shadow implementation remains
      in the current tracked tree; the Microsoft-derived temporal quality path
      is labeled generically and carries its complete required MIT notice.
- [x] The replacement screen-space shadow producer keeps the existing
      full-resolution `R8_UNORM` visibility contract and integrates with the
      same directional-light consumer.
- [x] The replacement temporal implementation keeps the existing history,
      motion-vector, debug, and renderer integration contracts while exposing
      explicit quality/performance modes.
- [x] The fastest temporal mode removes the largest avoidable dispatch,
      bandwidth, neighborhood, and precision costs identified from the current
      implementation and official Wicked Engine reference.
- [x] Focused tests, shader compilation, a Release build, and the relevant
      deterministic suite pass, with unrun GPU performance or visual evidence
      stated exactly.

## Scope

In scope:

- Original UVSR implementations for screen-space directional shadows and
  temporal anti-aliasing.
- Renaming related first-party files, symbols, targets, tests, shader binaries,
  UI labels, reports, and maintained documentation.
- Removal of the former third-party shadow source and its dedicated copy notice
  when no copied source remains.
- Temporal presets and implementation optimizations with explicit quality
  tradeoffs.
- Official Wicked Engine and Microsoft DirectX Graphics Samples sources as
  read-only behavioral and optimization references. Existing Microsoft-derived
  temporal code remains under MIT; newly added fast-path code does not copy
  reference implementation text.

Non-goals:

- Rewriting published Git history or erasing historical commit messages.
- Editing pinned `donut/` source or unrelated third-party notices.
- Redesigning SVSM, diagnostic CSM, CMAA2, MSAA, visibility AO/GI, lighting, or
  the renderer UI beyond names and controls required for this task.
- Publishing, opening a pull request, merging, releasing, or deploying.
- Claiming measured GPU-time parity without a matched accepted benchmark.

Affected subsystems and paths:

- Root `CMakeLists.txt`.
- Screen-space shadow source, shaders, settings, tests, reports, and docs.
- Temporal AA source, shaders, settings, tests, UI integration, reports, and
  docs.
- Runtime shader manifests and bundle-contract tests.
- `README.md` and maintained execution/history documentation containing active
  product claims.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- `src/shaders.cfg`, `src/shaders_production.cfg`, and
  `src/shaders_experiment_defaults.cfg`
- `src/uvsr.cpp`
- CPU/HLSL binding and resource-layout contracts
- All file renames/deletions, builds, runtime control, Git/index operations, and
  documentation integration

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, local tracking ref
  `origin/main` at `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Local versus remote state: task worktree starts equal to the locally recorded
  canonical tracking ref. The root `main` checkout is preserved at malformed,
  generated-artifact commit `891f4f0fe6b58c22e084290bfc207d345cf4c2df`
  and is not an implementation base.
- Verified source commit/build: the dirty task candidate has a successful
  production Release build, factory Release build, focused shader/test checks,
  and both complete CTest suites. It is a build-verified candidate, not runtime
  verified, committed, integrated, product-accepted, or Canonical verified.
- GPU, scene, camera, resolution, and settings preset when relevant: Intel PBR
  Sponza Decorated, Benchmark Position 1, 1920x1080, deferred defaults; exact
  adapter and presentation state must be recorded before any performance claim.
- Known pre-existing failures: none yet established in the isolated worktree.
- Overlap: the active `codex/ui-skins-command-reset` worktree shares the same
  base and may later overlap `src/uvsr.cpp`, UI tests, and CMake. This task owns
  only its isolated worktree and makes no integration or publication claim.
- Overlap: the separately isolated flashlight/ambient-fill task shares the base
  and may later overlap `src/uvsr.cpp` and deferred-lighting contracts. This
  candidate preserves the existing `DirectionalLightVisibility` interface and
  does not require that task to consume a new shader name or registration.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical source base | `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` | Integrated | Whole task |
| Provenance audit | Retained-code and notice boundary | Complete | Rewrite and removal |
| Temporal analysis | Prioritized modes and optimization contract | Complete | TAA implementation |
| Shadow analysis | Original algorithm and drop-in boundary | Complete | Shadow implementation |
| Independent review | Frozen final source diff | Re-review Active | Completion decision |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Directional visibility remains full-resolution `R8_UNORM`, where `1.0` is
  fully visible and lower values attenuate the primary directional light.
- Existing renderer selection and disabled/default behavior remain stable unless
  a rename is required to remove misleading provenance.
- Temporal AA consumes current HDR color, depth, and motion, maintains explicit
  history validity across resize/camera cuts/mode changes, and produces the same
  post-lighting HDR output contract.
- Presets resolve to one complete immutable settings bundle before dispatch;
  custom overrides remain possible only where the existing developer surface
  already supports them.
- All new shader binaries must be represented exactly once in build,
  compilation, staging, and runtime bundle manifests.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `provenance-audit` | `/root/provenance_audit` | Read-only task worktree | `3ee4ceb` | None | Canonical base | Complete |
| `taa-analysis` | `/root/taa_analysis` | Read-only task worktree | `3ee4ceb` | None | Canonical base | Complete |
| `shadow-analysis` | `/root/shadow_analysis` | Read-only task worktree | `3ee4ceb` | None | Canonical base | Complete |
| `integrate` | `/root` | Task worktree | `3ee4ceb` | All task paths | Three audits | Verification |
| `independent-review` | `/root/independent_review` | Read-only frozen candidate | Dirty task candidate | None | Integrated candidate | Re-review Active |

## Assignment Contracts

### Provenance Audit

- Owner/thread: `/root/provenance_audit`
- Branch/worktree: read-only `work/taa-shadow-cleanroom`
- Base commit/state: clean `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Read scope: current tracked tree, relevant licenses, and Git history
- Write scope: none
- No-touch scope: all files, Git metadata, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: canonical base
- Interface/invariant contract: no removal of a notice required by retained code
- Deliverable: exhaustive hit categories and lawful rewrite/removal boundary
- Done when: files, symbols, notices, and uncertainties are enumerated
- Required verification: `rg`, source/license inspection, and focused history
- Allowed Git and external actions: read-only
- Stop and report if: source provenance or notice requirements conflict
- Handoff revision/artifact: retained-code and notice-boundary audit
- Handoff acknowledged by/on: `/root`, 2026-07-30

### Temporal Analysis

- Owner/thread: `/root/taa_analysis`
- Branch/worktree: read-only `work/taa-shadow-cleanroom`
- Base commit/state: clean `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Read scope: temporal source, tests, bindings, docs, and official upstream
  repositories
- Write scope: none
- No-touch scope: all local and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: canonical base
- Interface/invariant contract: original UVSR expression, drop-in default
  behavior, explicit preset tradeoffs
- Deliverable: dispatch/resource/tap map and prioritized implementation plan
- Done when: high-impact optimizations and quality risks are concrete and
  source-backed
- Required verification: full local temporal path inspection and primary-source
  research
- Allowed Git and external actions: read-only
- Stop and report if: upstream provenance is ambiguous
- Handoff revision/artifact: prioritized mode, dispatch, bandwidth, and
  history-layout analysis
- Handoff acknowledged by/on: `/root`, 2026-07-30

### Shadow Analysis

- Owner/thread: `/root/shadow_analysis`
- Branch/worktree: read-only `work/taa-shadow-cleanroom`
- Base commit/state: clean `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Read scope: shadow producer, callers, bindings, build rules, tests, and docs
- Write scope: none
- No-touch scope: all local and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: canonical base
- Interface/invariant contract: preserve producer-neutral directional
  visibility contract with independently expressed implementation
- Deliverable: dependency graph, original algorithm boundary, and verification
  risks
- Done when: replacement can be implemented without guessing public contracts
- Required verification: exhaustive current-tree inspection
- Allowed Git and external actions: read-only
- Stop and report if: an uncertain runtime dependency makes deletion unsafe
- Handoff revision/artifact: producer dependency graph and dense-trace fidelity
  repair analysis
- Handoff acknowledged by/on: `/root`, 2026-07-30

### Integrate the Cleanup Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/taa-shadow-cleanroom`,
  `work/taa-shadow-cleanroom`
- Base commit/state: clean `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Read scope: full first-party repository, pinned dependency APIs, and official
  reference sources
- Write scope: every task-owned path after audit handoff
- No-touch scope: `donut/`, unrelated worktrees, root malformed `main`, remote
  refs, unrelated assets/features, and other tasks' branches
- Build directory and runtime/GPU/resource lease:
  `work/taa-shadow-cleanroom/build-cleanroom-production` and
  `work/taa-shadow-cleanroom/build-cleanroom-factory`; coordinator only
- Dependencies already integrated: all three audit decisions before
  interface-sensitive edits
- Interface/invariant contract: contracts listed above
- Deliverable: build-verified candidate and explicit evidence limits
- Done when: all done conditions have evidence or an explicit user-visible
  limitation
- Required verification: targeted tests, shader compilation, Release build,
  relevant CTest, title-case validator, `git diff --check`, exhaustive naming
  search, and independent source review
- Allowed Git and external actions: local edits, builds, tests, and focused
  branch/worktree management; no commit, push, PR, merge, release, or deployment
  without later user authorization
- Stop and report if: compliance requires retaining a prohibited attribution,
  a visible quality/default choice is irreducibly ambiguous, or overlap appears
  inside the isolated worktree
- Handoff revision/artifact: dirty local candidate and verification evidence
- Handoff acknowledged by/on: self-integrated by `/root`, 2026-07-30

### Independent Review

- Owner/thread: `/root/independent_review`
- Branch/worktree: read-only task worktree
- Base commit/state: frozen candidate diff
- Read scope: full task diff and relevant CPU/HLSL contracts
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, and external
  state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: complete candidate
- Interface/invariant contract: review provenance, correctness, resource
  lifetime, bindings, dispatch bounds, history validity, shader bundle
  completeness, and performance-mode truthfulness
- Deliverable: prioritized findings with exact paths and evidence
- Done when: no P0-P2 issue remains or every issue is repaired and re-reviewed
- Required verification: source and diff inspection
- Allowed Git and external actions: read-only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: initial findings repaired; final re-review active
- Handoff acknowledged by/on: pending

## Integration Order

1. Finish provenance, temporal, and shadow audits against the canonical
   worktree.
2. Freeze names, presets, bindings, resource formats, and removal boundaries.
3. Rewrite and rename temporal code and focused tests.
4. Rewrite and rename the shadow producer and focused tests.
5. Reconcile shared renderer/build/shader manifests and maintained docs once.
6. Configure and run focused tests, then compile all affected shaders and build
   the Release application.
7. Freeze the candidate and obtain independent high-risk review.
8. Repair task-introduced findings, rerun affected evidence, and archive this
   plan to `completed/`.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Clean current-tree provenance | Zero prohibited names outside explicitly reviewed history | Focused `rg` across maintained files and paths | Passed: 258 files, zero content or path hits |
| Original shadow replacement | Source review, reference tests, compiled shaders | Focused shadow target and tests | Passed; production shader compiled and deterministic dense-trace contract test passed |
| Temporal default compatibility | Preset/reference tests and history-state tests | Focused temporal target and tests | Passed for Full Quality, Reduced, Minimum, history reset, format fallback, and telemetry contracts |
| Fast temporal mode | Static work/tap/dispatch evidence and matched GPU timing when available | Shader inspection and Position-1 benchmark | Static contract passed; matched GPU timing not run, so no parity claim |
| Runtime packaging | Exact shader bundle tests | Runtime/production bundle tests | Passed for the 38-shader runtime and 77-shader production packages |
| Integrated renderer | Release application build and responsive smoke | CMake build and task-owned launch | Production and factory Release builds passed; runtime launch intentionally not attempted under the machine quiet/thermal protocol |
| Document compliance | All in-scope headings pass | `tools/check_document_title_case.py` | Self-test passed; 898 headings and lead-ins checked with zero violations |
| Independent safety review | No unresolved P0-P2 findings | Read-only reviewer handoff | Initial findings repaired; final re-review active |

For performance work, record:

- baseline and candidate commits or complete dirty-diff identities;
- GPU, scene, fixed camera, resolution, and settings preset;
- warmup and sample window/count;
- total frame time plus temporal pass costs;
- correctness and image-quality guardrails;
- before/after captures and raw measurement artifacts.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Use isolated canonical worktree at `3ee4ceb` | Root `main` contains a malformed generated-artifact commit and is six canonical commits behind; changing it would risk user work | All |
| 2026-07-30 | Rewrite the shadow implementation instead of cosmetically de-attributing it | Renaming copied code while removing required notices would be misleading and may violate retained licenses | Shadow |
| 2026-07-30 | Retain the Microsoft-derived temporal quality path under the full MIT notice | MIT permits generic product naming while requiring the copyright and permission notice; this preserves the mature quality path without misrepresenting authorship | Temporal |
| 2026-07-30 | Treat official Wicked Engine code as a behavioral reference, not a copy source | Allows algorithmic comparison while keeping the new compact fast path independently expressed | Temporal |
| 2026-07-30 | Exclude published Git history from removal scope | History rewriting is destructive, unauthorized, and unnecessary for the current product tree | Provenance |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | `/root` | Active | `3ee4ceb` isolated worktree | Repository/worktree/overlap preflight | Await audit handoffs |
| 2026-07-30 | `/root/taa_analysis` and `/root/shadow_analysis` | Complete | Read-only distilled handoffs | Source, binding, option, and test inspection | Findings integrated |
| 2026-07-30 | `/root` | Candidate | Dirty isolated worktree | Production 408-task shader build and Release application link passed | Run full deterministic suites |
| 2026-07-30 | `/root` | Build-Verified Candidate | Exact production and factory dirty candidate | Production compiled 408 shader tasks; factory compiled 60; both Release applications linked; both CTest suites passed 28/28 at bounded parallelism | No runtime or matched GPU timing evidence |
| 2026-07-30 | `/root` | Verification | Current maintained tree | Zero prohibited content/path hits; title-case self-test and 899-heading scan passed; staged license copies match source SHA-256 | Preserve explicit evidence limits |
| 2026-07-30 | `/root/independent_review` | Complete | Frozen final candidate | No actionable P0-P2 findings; shadow cache/barriers, DDA, interpolation, TAA layout ownership, resurrection pin, naming, licensing, and three-slot R8 ABI reviewed | Ownership released |

## Risks and Escalation Triggers

- A visually similar replacement can still regress thin-geometry shadow
  stability, disocclusion handling, or temporal ghosting without runtime review.
- Static instruction/tap reductions do not prove GPU-time parity.
- The active UI/command task may later require a semantic merge across
  `src/uvsr.cpp`, CMake, and UI tests; this task will not race integration.
- Required third-party notices must remain if any corresponding code or
  substantial expression remains.

Stop and ask the user if:

- compliance evidence shows a required attribution must remain in the current
  product tree;
- preserving the exact current visual default conflicts irreducibly with the
  requested cleanroom boundary;
- a material quality-versus-performance default cannot be preserved without a
  user preference;
- publication, history rewriting, or integration with another active task is
  requested but its authority or target remains unclear.

## Completion

- Final integrated commit: none; local implementation is not commit-authorized
- Verification summary: production and factory Release builds and both 28-test
  suites passed; shader package and license staging contracts passed; no
  runtime visual review or matched GPU benchmark was run
- Independent review: initial findings repaired; final re-review found no
  actionable P0-P2 findings
- Coming Soon/documentation update: candidate and evidence limits recorded
- Pushed/PR/merged, or intentionally local: intentionally local unless later
  authorized
- Remaining experiments or follow-ups: runtime visual review and matched
  Position-1 GPU measurements when the user provides an accepted testing window
- Active ownership released: yes
- Archived to completed/abandoned path: completed path
