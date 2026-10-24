# TAA Developer Options and Stability Diagnosis

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/taa-shadow-cleanroom`,
  `C:\Users\brock\OneDrive\Documents\uvsr\work\taa-shadow-cleanroom`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` plus the
  local, unstaged shadow/TAA cleanroom candidate
- Started: 2026-07-30
- Last updated: 2026-07-30
- Archived:
  `docs/exec-plans/completed/taa-developer-options-and-stability.md`

## Goal and Done Condition

Goal:

Expose every image-affecting choice and execution decision selected by Temporal
Cost under a TAA **Developer Options** section, make the strongest hidden
Minimum stability policies independently testable, and rename the visible
morphology control to **Morphology**.

Done when:

- [x] Cost-derived algorithm, history-storage, stability, sharpening, and
  topology state is visible in the TAA drawer.
- [x] Image-affecting overrides can isolate Minimum's edge-stability behavior
  without silently selecting an unsupported shader/resource combination.
- [x] Production and factory builds and tests pass, an independent shader
  review has no unresolved P0-P2 finding, and the exact updated build is ready
  for a user visual comparison.

## Scope

In scope:

- Temporal Cost resolution and reset semantics.
- Runtime-uniform stability policies for previous-depth validation, history
  weight, motion trust, rectification clipping, and blend domain.
- Explicit robust-versus-compact history storage.
- TAA drawer controls, telemetry, source-contract tests, and documentation.
- Visible **Morphology** label correction.

Non-goals:

- Changing the eight-phase jitter sequence.
- Claiming which stability policy is decisive without a controlled visual
  comparison.
- GPU performance benchmarking during this work item.
- Publication, commit, push, pull request, merge, or release.

Affected subsystems and paths:

- `src/temporal_aa_options.h`
- `src/temporal_aa_options_shared.h`
- `src/temporal_aa.cpp`
- `src/temporal_aa_blend_cs.hlsl`
- `src/temporal_aa_minimum_cs.hlsl`
- `src/uvsr.cpp`
- `tests/temporal_aa_tests.cpp`
- `tests/ui_source_contract_tests.cpp`
- `docs/temporal-aa-options.md`
- `docs/advanced-settings.md`

Shared hotspots reserved for the coordinator:

- `README.md`
- `CMakeLists.txt`
- shader registries and package configuration
- both sides of the Temporal AA CPU/HLSL constant-buffer contract

## Baseline

- Canonical repository/remote: `origin/main`
- Local versus remote state: feature worktree based on `3ee4ceb`; the existing
  cleanroom candidate is local, dirty, and intentionally unstaged.
- Verified source commit/build: the preceding candidate built in production and
  factory Release configurations and passed 28/28 tests in each configuration.
- GPU, scene, camera, resolution, and settings preset when relevant: no matched
  GPU measurement; the user's observation came from the launched production
  candidate and Minimum Temporal Cost.
- Known pre-existing failures: none in the two isolated build trees.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Shadow/TAA cleanroom candidate | Preserve current neutral names and three-slot visibility ABI | integrated locally | this task |
| Flashlight realism task | Serialize UVSR window ownership | released; this task launched PID 29808 | runtime smoke |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Keep the Temporal AA blend constant buffer at 128 bytes by replacing existing
  padding with one behavior-flags word and retained padding.
- Add no texture, UAV, sampler, or dispatch binding.
- Keep robust and compact history formats unchanged.
- Unsupported compact combinations must report a robust fallback and must not
  silently ignore the requested developer setting.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| cost-mode-root-cause | `/root/cost_mode_root_cause` | shared/read-only | dirty candidate | none | current TAA code | complete |
| developer-ui-contract | `/root/developer_ui_contract` | shared/read-only | dirty candidate | none | current UI/resolver | complete |
| developer-options-tests | `/root/developer_options_tests` | shared | dirty candidate | two AA test sources | implemented contracts | complete |
| developer-options-docs | `/root/developer_options_docs` | shared | dirty candidate | three AA/UI documents | implemented contracts | complete |
| developer-options-final-review | `/root/developer_options_final_review` | shared/read-only | final dirty candidate | none | complete builds/tests | complete |
| implementation/integration | `/root` | task worktree | dirty candidate | all in-scope files | both analyses | complete |

## Integration Order

1. Define resolver and CPU/HLSL behavior contracts.
2. Implement shader policies and compact-path compatibility checks.
3. Rebuild the TAA drawer around Developer Options and rename Morphology.
4. Add tests and documentation.
5. Build both configurations, run tests, and obtain independent review.
6. Launch the exact updated production build after acquiring the GPU/UI lease.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Resolver behavior | Unit coverage for every cost default and override | focused/full CTest | production and factory 28/28 passed |
| CPU/HLSL ABI | Static asserts plus both shader bundles | production/factory Release builds | both Release builds passed |
| UI contract | Source-contract tests for Developer Options and Morphology | CTest | production and factory passed |
| Attribution/docs hygiene | prohibited-name and Title Case scans | repository tools/search | attribution and path scans clean; 914 headings pass |
| Shader safety | independent review | read-only reviewer | no remaining P0-P2 finding |
| Visual isolation | exact-build user comparison | launch after build | PID 29808 launched; user comparison pending |

## Decisions

| Date/time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Expose hidden stability policies, not only existing enums | Minimum's likely stabilization comes from depth acceptance and history confidence that the old UI could not represent | all |
| 2026-07-30 | Use runtime-uniform flags in existing constant-buffer padding | Avoids a shader-permutation explosion and adds no bindings or buffer growth | shaders/runtime |
| 2026-07-30 | Make robust/compact history an explicit override | It is required to separate precision/path effects from image-policy effects | resolver/runtime/UI |
| 2026-07-30 | Package one static-default and one configurable compact shader | The normal Minimum profile keeps its branches compile-time folded while custom diagnostic policies remain independently selectable | shaders/runtime/package |
| 2026-07-30 | Defer compact sharpening without requiring CMAA2 allocation | Compact history cannot sharpen in-pass, and Morphology Off must not make its Sharpness control a no-op | runtime/tests |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | cost-mode-root-cause | complete | read-only handoff | static trace | implement top-ranked controls |
| 2026-07-30 | developer-ui-contract | complete | read-only handoff | static trace | expose image policies and retain production shader coverage |
| 2026-07-30 | developer-options-tests | complete | dirty task worktree | resolver and UI source contracts | ownership released |
| 2026-07-30 | developer-options-docs | complete | dirty task worktree | `git diff --check` and terminology scan | ownership released |
| 2026-07-30 | developer-options-final-review | complete | read-only handoff | no remaining P0-P2 finding after four fixes | visual isolation remains |
| 2026-07-30 | implementation/integration | complete | production and factory Release candidates | both builds and both 28/28 CTest suites pass | exact production PID 29808 launched |

## Risks and Escalation Triggers

- A compact-path override may require data that compact history does not store.
  Such a choice must fall back visibly to robust history.
- Runtime branches are uniform but still need shader compilation and
  independent barrier/resource review.
- Changing an image policy must reset history; changing only execution topology
  must preserve it.

Stop and ask the user if:

- A requested diagnostic requires a new persistent GPU resource or a visible
  default-quality trade beyond the selected Temporal Cost profile.

## Completion

- Final integrated commit: intentionally uncommitted
- Verification summary: both Release builds and both 28/28 CTest suites pass;
  static audits pass; production SHA-256
  `258FF976D39BCC062D24AC37423D7E5541F849B7F371C14D201503F1377D999D`
  launched as PID 29808 at High priority
- Independent review: no remaining P0-P2 finding
- Coming Soon/documentation update: complete
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: controlled user visual isolation and
  matched GPU timing if requested
- Active ownership released: yes; the GPU/UI lease remains with the exact
  launched renderer until the user completes the comparison
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/taa-developer-options-and-stability.md`
