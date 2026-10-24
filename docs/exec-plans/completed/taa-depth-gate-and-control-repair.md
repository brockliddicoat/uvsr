# TAA Depth Gate and Control Repair

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/taa-shadow-cleanroom`,
  `C:\Users\brock\OneDrive\Documents\uvsr\work\taa-shadow-cleanroom`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` plus the
  local, unstaged shadow/TAA cleanroom candidate
- Started: 2026-07-30
- Last updated: 2026-07-30
- Archived to:
  `docs/exec-plans/completed/taa-depth-gate-and-control-repair.md`

## Goal and Done Condition

Goal:

Explain why Moving Point / Stationary Bypass stabilizes thin edges, repair the
misleading Temporal Cost rows, and make Sample Resurrection a real production
control without weakening the optimized Reduced and Minimum topology contracts.

Done when:

- [x] Requested Temporal Cost is editable in Developer Options and Effective
  Temporal Cost is unmistakably read-only runtime status.
- [x] Sample Resurrection is selectable and functional in the production build
  for Full Quality while Reduced and Minimum truthfully retain their optimized
  no-resurrection effective paths.
- [x] Production and factory Release builds and test suites pass, resource and
  shader-bundle contracts are independently reviewed, and the depth-gate
  implications and default tradeoffs are reported.

## Scope

In scope:

- Temporal AA drawer semantics and production feature gating.
- Older-history resources, bindings, constants, shader permutations, resets,
  tests, and documentation required by production Sample Resurrection.
- Read-only diagnosis of Previous-Depth Validation and its default tradeoffs.

Non-goals:

- Changing the default Previous-Depth Validation policy without a separate
  product decision and dynamic ghosting/disocclusion comparison.
- Matched GPU benchmarking, publication, commit, push, pull request, merge, or
  release.
- Touching any renderer process whose ownership is not proven to this task.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/temporal_aa.h`
- `src/temporal_aa.cpp`
- `src/temporal_aa_options.h`
- `src/temporal_aa_blend_cs.hlsl`
- `src/shaders_production.cfg`
- `src/shaders_experiment_defaults.cfg`
- `tests/temporal_aa_tests.cpp`
- TAA shader-bundle, UI source-contract, and documentation tests
- `docs/temporal-aa-options.md`
- `docs/advanced-settings.md`

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- all shader registries and package configuration
- both sides of the Temporal AA CPU/HLSL resource and constant-buffer contract

## Baseline

- Canonical repository/remote: `origin/main`
- Local versus remote state: isolated feature worktree based on `3ee4ceb`; the
  candidate is local, dirty, intentionally unstaged, and uncommitted.
- Verified source commit/build: the preceding dirty candidate passed production
  and developer Release builds plus 28/28 tests in both configurations.
- GPU, scene, camera, resolution, and settings preset when relevant: the user
  observed a large thin-edge stability improvement by changing only
  Previous-Depth Validation to Moving Point / Stationary Bypass.
- Known pre-existing failures: none in the preceding two isolated build trees.

## Dependencies and Interfaces

| Dependency or Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Depth-gate audit | Identify the exact rejection mechanism and input conventions | Completed | Final diagnosis |
| Moving-point tradeoff audit | Assess safe default scope and failure modes | Completed | Default recommendation |
| Cost/resurrection audit | Define the smallest complete production promotion | Completed | Implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Keep Requested Cost as stored user intent and Effective Cost as observed
  runtime execution.
- Full Quality may use one or two older persistent history samples; Reduced and
  Minimum pin their effective resurrection mode off without erasing the stored
  Full Quality choice.
- Production shader-package counts and runtime permutation indexing must match
  exactly.
- Resource allocation, binding layouts, constant-buffer size, invalidation, and
  history-memory telemetry must include production resurrection consistently.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `depth-gate-audit` | `/root/depth_gate_audit` | Shared/read-only | Dirty candidate | None | Current TAA source | Completed |
| `moving-point-tradeoff` | `/root/moving_point_tradeoff` | Shared/read-only | Dirty candidate | None | Current TAA source | Completed |
| `cost-resurrection-ui-audit` | `/root/cost_resurrection_ui_audit` | Shared/read-only | Dirty candidate | None | Current UI/runtime/package | Completed |
| `implementation-integration` | `/root` | Task worktree | Dirty candidate | All in-scope paths | Three audits | Completed |

## Integration Order

1. Freeze the requested/effective setting and production resurrection contract.
2. Promote only the required resurrection resources and shader permutations.
3. Repair Developer Options UI semantics and source-contract tests.
4. Update documentation, build both configurations, and run all tests.
5. Obtain an independent resource/shader review before completion.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command or Experiment | Result or Artifact |
| --- | --- | --- | --- |
| Requested and effective controls are truthful | UI source-contract tests and exact-build smoke | CTest and runtime inspection | Passed; Requested menu applied Minimum, Effective reported Minimum, and Full Quality resurrection opened and applied 1x |
| Production resurrection is complete | CPU/HLSL static assertions and production shader bundle | Release build and CTest | Passed; 601 shader permutations and 28/28 tests |
| Factory build remains stripped and complete | Factory shader bundle and full tests | Release build and CTest | Passed; 61 shader permutations and 28/28 tests |
| No stale feature gating remains | Focused source scan | `rg` | Passed; no prohibited maintained-tree terminology hits |
| High-risk resource changes are sound | Independent read-only review | Frozen candidate audit | Passed after continuity invalidation repair; no P0-P2 findings remain |
| Documentation headings and claims conform | Title Case validator and terminology scan | Repository tools | Passed; self-test and 925 headings/lead-ins with zero violations |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Keep Effective Temporal Cost read-only but stop rendering it as a disabled dropdown | It is measured execution state, not stored input; a functional dropdown would misrepresent the runtime fallback contract | UI |
| 2026-07-30 | Promote Sample Resurrection into production rather than expose an inert selector | The current production binary strips the implementation, so a UI-only repair would remain nonfunctional | Runtime, shaders, package, UI |
| 2026-07-30 | Do not silently change the global depth-validation default during diagnosis | Stationary bypass trades rejection shimmer for stale-history and disocclusion risk that needs a dynamic comparison | Resolver, final recommendation |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision or Artifact | Checks | Risks or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | `/root` | Diagnosis active | Static shader trace plus official reference comparison | Coordinate conventions consistent so far | Complete three audits |
| 2026-07-30 | `/root` | Implementation complete | Production resurrection resources, bindings, permutations, controls, tests, and documentation | Production and factory Release builds passed | Run exact-build UI smoke |
| 2026-07-30 | `/root/resurrection_control_review` | Review complete | Continuity invalidation re-review | No remaining P0-P2 findings | Ownership released |
| 2026-07-30 | `/root` | Runtime smoke complete | Production window 73074768, `main-3ee4ceb-1936` | Requested cost, effective status, Minimum gate, and Full Quality 1x resurrection verified | Renderer closed and GPU/UI lease released |

## Risks and Escalation Triggers

- Production resurrection widens persistent resource lifetime and bindings; any
  partial compile-time gate would create a permutation, binding, or buffer-size
  mismatch.
- Turning Stationary Bypass on globally may replace visible shimmer with
  ghosting on newly visible or incorrectly zero-velocity samples.
- An unverified `uvsr.exe` process may belong to the user; leave it untouched
  and defer the exact-build visual smoke until ownership and lease are clear.

Stop and ask the user if:

- A safe production resurrection implementation requires changing its visible
  image behavior or discarding an existing saved override.

## Completion

- Final integrated commit: intentionally uncommitted
- Verification summary: production and factory Release builds passed; 28/28
  tests passed in each; the exact production UI smoke passed; production
  executable SHA-256 is
  `950D69D1847489E80323F5B29F303A94C7FB488EF099DF778D3058CBE234DD81`.
- Independent review: no remaining P0-P2 findings after the persistent-history
  continuity fix.
- Coming Soon/documentation update: durable TAA control and cost behavior
  documented in the renderer and advanced-settings documentation.
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: matched ghosting/stability comparison
- Active ownership released: yes
- Archived to completed/abandoned path: completed
