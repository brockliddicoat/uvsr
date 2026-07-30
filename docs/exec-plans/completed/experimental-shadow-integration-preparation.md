# Experimental Shadow Integration Preparation

## Status

- State: complete
- Coordinator: `/root`
- Project branch and worktree:
  `experimental/shadow-maps-ibl-20260726` at
  `C:\Users\brock\Documents\Codex\2026-07-18\i-want-you-to-take-the\work\uvsr-bend-shadows`
- Base commit: `94b8597b79d13e5522fe16d49afc1a6f5768a82a`
- Started: 2026-07-28
- Completed: 2026-07-28
- Archived to:
  `docs/exec-plans/completed/experimental-shadow-integration-preparation.md`

## Goal and Done Condition

Goal:

Prepare the published experimental shadow and IBL branch for later semantic
integration with canonical `main` without merging either lineage. Repair
branch-local packaging and benchmark-evidence defects, add mechanical lighting
contract guards, and produce a clean local Release candidate while preserving
all existing tracked and untracked work.

Done when:

- [x] HLSL-only rebuilds deterministically refresh runtime shader binaries and
      component shader targets cannot race through a shared destructive staging
      operation.
- [x] Environment asset staging follows source deletion and remains usable when
      UVSR is included below another CMake project.
- [x] SVSM acceptance evidence fingerprints the complete relevant configuration,
      rejects invalid GPU timings, writes the latest result atomically, and
      rejects unknown command-line options.
- [x] CSM labels raw evidence authoritative only when its frame/query accounting
      is complete.
- [x] PBR debug-view changes invalidate TAA history.
- [x] Source-contract tests protect the three exact-light visibility inputs and
      forbid the retired hemispherical ambient in the experimental production
      composition paths.
- [x] Documentation records the branch-local repairs and the MSAA/fused work
      that must remain deferred to the future integration branch.
- [x] A clean isolated Release build, full CTest suite, Bend hashes, title-case
      audit, static checks, and independent source review pass.
- [x] No commit, push, pull request, merge, `main` change, submodule update, or
      destructive Git operation occurs.

## Scope

In scope:

- Branch-local build and runtime shader staging.
- Environment asset staging and source-root portability.
- SVSM and CSM benchmark evidence integrity.
- PBR debug-history invalidation.
- Source-contract and deterministic helper tests.
- Integration-preparation documentation.

Non-goals:

- Merging or copying canonical `main` into this branch.
- Porting main-only MSAA, CMAA2, fused visibility, the canonical production
  shader-bundle implementation, or newer TAA implementations. The branch-local
  exact runtime-staging verifier remains in scope as an integration repair.
- Changing rendered lighting, authored colors, shadow quality, resource
  layouts, defaults, presets, or UI organization.
- Claiming runtime or performance acceptance without the full runtime matrix.

Affected subsystems and paths:

- `CMakeLists.txt`
- `cmake/SyncRuntimeShaderBundle.cmake`
- `src/command_line_options.h`
- `src/diagnostic_csm_benchmark.h`
- `src/uvsr.cpp`
- `src/svsm_motion_benchmark.h`
- `src/gpu_performance_monitor.h` only if a focused helper contract requires it
- `tests/`
- `docs/exec-plans/active/experimental-shadow-integration-preparation.md`
- `tools/launch_uvsr.ps1`
- Existing branch documentation with stale integration contracts

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- `AGENTS.md`
- `src/uvsr.cpp`
- `src/shaders.cfg`
- All tracked documentation and tests

## Baseline

- Canonical remote: `origin/main` at
  `4104018842371546c59fb09017ff964b82965eba`
- Experimental local and remote:
  `94b8597b79d13e5522fe16d49afc1a6f5768a82a`, equal before edits
- Relationship: experimental and canonical are diverged; no integration is
  authorized in this task
- Verified source/build: the base commit previously passed an isolated
  warning-free Release build and 16 CTests; its required runtime matrix remains
  incomplete
- Known pre-existing state: numerous preserved untracked plans, reports,
  telemetry captures, benchmark artifacts, and local tools
- Open canonical work: PR #10 shared visibility shader helpers and PR #11
  visibility degenerate-path tests; neither is copied or modified here

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Experimental base | Exact `94b8597` tracked tree | Ready | All work |
| Canonical staging reference | Read-only inspection of `origin/main` | Ready | Build design |
| Directional visibility contract | Three pointer-identical producer slots, white fail-open | Frozen | Tests |
| Runtime lighting contract | No hidden hemispherical ambient fallback | Frozen | Tests |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve the experimental renderer's three `R8_UNORM` directional visibility
  slots and exact-light identity mapping.
- Preserve independent Bend, CSM, and SVSM producer behavior.
- Preserve all existing shader register and GPU resource layouts.
- Packaging changes may alter build dependencies and staging locations but not
  runtime virtual paths or shader filenames.
- Benchmark evidence changes may make previously accepted output fail closed;
  they must not change the rendered workload.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| EIP-1 | `/root` | Shared experimental worktree | `94b8597` | All task-owned edits and integration | Reviewer findings | Complete |
| EIP-R1 | Read-only reviewer | Shared experimental worktree | `94b8597` | None | None | Complete |
| EIP-R2 | Read-only reviewer | Shared experimental worktree | `94b8597` | Focused benchmark helper/tests after review | EIP-R2 review | Complete |
| EIP-R3 | Read-only reviewer | Shared experimental worktree | `94b8597` | Lighting contract test/docs after review | EIP-R3 review | Complete |
| EIP-R5 | Read-only reviewer | Shared experimental worktree | Current task-owned diff | None | Integrated candidate | Complete |
| EIP-R6 | Read-only reviewer | Shared experimental worktree | Current task-owned diff | None | Integrated candidate | Complete |
| EIP-R7 | Read-only reviewer | Shared experimental worktree | Current task-owned diff | None | Integrated candidate | Complete |

## Assignment Contracts

### Build and Asset Staging Design Review (`EIP-R1`)

- Owner/thread: assigned read-only reviewer
- Branch/worktree: shared experimental worktree
- Base commit/state: exact tracked `94b8597`
- Read scope: root CMake, Donut shader CMake helpers, canonical main's staging
  implementation, environment and scene staging
- Write scope: none
- No-touch scope: entire filesystem and Git state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: runtime shader paths and filenames remain stable
- Deliverable: exact defect analysis and smallest safe patch design
- Done when: destructive/race/staleness/deletion and portability edge cases are
  accounted for with recommended tests
- Required verification: source inspection only
- Allowed Git and external actions: read-only Git commands; no fetch, checkout,
  branch, index, history, or external writes
- Stop and report if: tracked state differs from the base or another writer
  touches reviewed paths
- Handoff revision/artifact: read-only findings at exact `94b8597`
- Handoff acknowledged by/on: `/root`, 2026-07-28

### Benchmark Evidence Contract Review (`EIP-R2`)

- Owner/thread: assigned read-only reviewer
- Branch/worktree: shared experimental worktree
- Base commit/state: exact tracked `94b8597`
- Read scope: SVSM/CSM benchmark helpers, renderer argument parsing, output
  writers, timing-query accounting, and focused tests
- Write scope: none
- No-touch scope: entire filesystem and Git state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: evidence changes fail closed without changing
  renderer workload or presets
- Deliverable: complete settings identity, invalid-sample, atomic-write, CLI,
  and authoritative-evidence patch/test recommendations
- Done when: every previously identified evidence defect has a deterministic
  acceptance criterion
- Required verification: source inspection only
- Allowed Git and external actions: read-only Git commands only
- Stop and report if: tracked state differs from the base or reviewed files
  change
- Handoff revision/artifact: read-only findings at exact `94b8597`; focused
  helper/test implementation is assigned without renderer or CMake writes
- Handoff acknowledged by/on: `/root`, 2026-07-28

### Lighting Contract and Debug-History Review (`EIP-R3`)

- Owner/thread: assigned read-only reviewer
- Branch/worktree: shared experimental worktree
- Base commit/state: exact tracked `94b8597`
- Read scope: PBR/deferred lighting shaders and bindings, directional visibility
  seam, TAA history control, existing source-contract tests, and branch
  documentation
- Write scope: none
- No-touch scope: entire filesystem and Git state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: no visible lighting change; tests protect
  existing correct behavior
- Deliverable: exact TAA reset site, contract-test design, and stale
  documentation inventory
- Done when: branch-local fixes are separated from main-only MSAA/fused work
- Required verification: source inspection only
- Allowed Git and external actions: read-only Git commands only
- Stop and report if: tracked state differs from the base or reviewed files
  change
- Handoff revision/artifact: read-only findings at exact `94b8597`; focused
  source-contract test and documentation implementation is assigned without
  CMake or renderer writes
- Handoff acknowledged by/on: `/root`, 2026-07-28

## Integration Order

1. Record reviewer findings and freeze task-owned contracts.
2. Implement build and environment staging.
3. Implement deterministic evidence helpers and renderer integration.
4. Add contract tests and update documentation.
5. Run targeted tests, isolated clean build, full suite, and independent review.
6. Archive this plan only after the local candidate and its remaining runtime
   limits are recorded.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Shader staging is dependency-driven | HLSL/config dependency graph and staged binary freshness test | Clean and incremental CMake builds | Same-size corruption, missing shader, unexpected shader, and no-op verifier checks passed in the preliminary isolated build |
| Environment deletion is represented | Generated manifest and staging test | Configure/build staging target | Generated exact source/runtime manifests and the runtime bundle contract passed |
| SVSM evidence fails closed | Deterministic helper tests | Focused benchmark test executable | Full settings identity, invalid timing, nested timing, and atomic publication tests passed |
| CSM authority fails closed | Deterministic helper tests | Focused CSM test executable | Exhaustive 90-field workload identity and fail-closed frame/query/timing tests passed |
| Lighting contract remains intact | Source-contract tests | CTest | Three slots, exact producer pairs, white fallback, multiplication, and retired ambient absence passed |
| Candidate compiles | Warning-free isolated Release build | CMake configure/build | `work/f213748` Release candidate built successfully; executable SHA-256 `0438006E283AC0FD6EF2051970A694BED7C29F9653691F27BF082134311C139F` |
| Repository policy passes | Hash, title, parser, diff, and status checks | Repository tools | 19/19 CTests, Bend hashes, clean submodule trees, 644-heading audit, and `git diff --check` passed |
| Runtime smoke | Correctly labeled experiment launch | `tools/launch_uvsr.ps1` equivalent for isolated build | PID 19104 launched as `integrationprep-94b8597-2146`; two visible captures showed a responsive Sponza frame with no error modal |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-07-28 | Keep this preparation local and uncommitted | User requested a build, not publication; preserving both remote parents avoids integration risk | All |
| 2026-07-28 | Defer main-only MSAA and fused paths | Backporting canonical implementations would duplicate work and increase conflicts | EIP-1, EIP-R3 |
| 2026-07-28 | Use coordinator-only writes with read-only reviewers | CMake, renderer, tests, and documentation are shared hotspots | All |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-28 | EIP-1 `/root` | Complete | Local uncommitted Release candidate `work/f213748` | Build, 19/19 CTests, repository policy, mutation checks, incremental shader rebuild, and runtime smoke passed | Preserve local candidate; full runtime matrix remains deferred |
| 2026-07-28 | EIP-R1 | Complete | Read-only staging review | Exact branch and canonical graph inspected | Implement isolated component outputs and dependency-driven runtime staging |
| 2026-07-28 | EIP-R2 | Complete | Evidence helpers, renderer integration, and deterministic tests | Focused and full SVSM/CSM tests passed | Final evidence review complete |
| 2026-07-28 | EIP-R3 | Complete | Lighting source contract and documentation corrections | Receiver slots, exact producer pairs, white fallback, multiplication, and retired ambient absence covered | Full suite passed |
| 2026-07-28 | EIP-R5 | Complete | Final staging review | Stale-content and recursive-delete blockers repaired; final review found no blocker or high-risk defect | Complete |
| 2026-07-28 | EIP-R6 | Complete | Final evidence review | GPU/CPU nested timing and exhaustive CSM identity gaps repaired; final review found no blocker or medium gap | Complete |
| 2026-07-28 | EIP-R7 | Complete | Final lighting review | No rendering blocker; producer-pair coverage gap repaired | Full suite passed |

## Risks and Escalation Triggers

- CMake staging changes can silently omit shaders or create cross-target races.
- Benchmark fail-closed changes can invalidate earlier reports by design.
- An in-place renderer edit can collide with preserved user or peer work.
- A running UVSR process can lock the candidate executable.
- Canonical main may advance, but this task does not integrate it.

Stop and ask the user if:

- A required fix changes rendered lighting, user-visible defaults, shader
  resource layouts, or benchmark workload.
- Safe progress requires deleting or overwriting uncertain assets or work.
- Publication, integration, or canonical changes become necessary.

## Completion

- Final integrated commit: intentionally none unless separately requested
- Verification summary: isolated Release build, 19/19 CTests, shader-bundle
  mutation tests, HLSL-only incremental rebuild, title-case audit, Bend hashes,
  submodule cleanliness, static checks, and visible runtime smoke passed
- Independent review: three final read-only reviews found no remaining staging,
  benchmark-evidence, or lighting-contract blocker in the task-owned diff
- Coming Soon/documentation update: branch-local repairs and deferred canonical
  MSAA/fused integration are recorded
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: main-only MSAA/fused integration and full
  runtime matrix
- Active ownership released: 2026-07-28; the smoke-tested renderer remains open
  for user inspection
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/experimental-shadow-integration-preparation.md`
