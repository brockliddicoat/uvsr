# Image-Based Lighting Sky Controls and Visibility

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/bend-screen-space-shadows` at
  `C:\Users\brock\Documents\Codex\2026-07-18\i-want-you-to-take-the\work\uvsr-bend-shadows`
- Base commit: `ea566bc67f744059e6f62e33c541c5b25bde9bd8` plus all preserved
  uncommitted renderer, shadow, lighting, test, documentation, and asset work
- Started: 2026-07-26
- Last updated: 2026-07-26
- Planned archive:
  `docs/exec-plans/completed/ibl-sky-controls-and-visibility.md`

## Goal and Done Condition

Goal:

Preserve the discovery that UVSR's former two-color `normal.y` ambient fallback
was hidden illumination, remove the remaining first-party procedural-sky
selection and fallback path, consolidate imported environment controls in the
Sky drawer, and add independent diffuse/specular IBL strengths without changing
the common source exposure or background.

Done when:

- [x] Durable renderer documentation makes the no-hidden-ambient invariant and
      its value to future UVSR-derived projects unmistakable.
- [x] Only the six imported, licensed environment sources remain selectable.
- [x] Missing or invalid imported sources fail safely without procedural
      illumination.
- [x] All environment selection, exposure, lobe, strength, background, and
      source-energy controls live in the Sky drawer.
- [x] Diffuse and specular strengths independently scale every matching
      lighting consumer while leaving the background/common exposure unchanged.
- [x] The Release renderer and PBR tests build; focused and full tests, document
      Title Case checks, diff checks, and a focused live control smoke pass.

## Scope

In scope:

- First-party imported IBL source catalog and loading path.
- Removal of first-party procedural environment generation and tests.
- Sky-drawer environment controls and factory reset state.
- Independent diffuse and specular IBL strengths.
- Documentation of the hidden-ambient discovery and the staged sky-visibility
  design.

Non-goals:

- Donut or NVRHI changes.
- A new sky-visibility renderer in this work item.
- Tonemapper, direct-BSDF, GI estimator, shadow, or scene-light changes.
- Commit, push, pull request, merge, main modification, submodule update, or
  destructive Git operation.

Affected subsystems and paths:

- `src/image_based_lighting_*`
- `src/diffuse_environment_math.h`
- `src/uvsr.cpp`
- `src/screen_space_visibility.*`
- `tests/pbr_reference_tests.cpp`
- `tests/diffuse_environment_asset_tests.cpp`
- `README.md`, `docs/pbr-foundation.md`,
  `docs/screen-space-visibility.md`, and `assets/environments/README.md`

Shared hotspots reserved for the coordinator:

- All writable paths in this plan
- The shared `build/` directory and renderer process
- Final integration, documentation, and verification

## Baseline

- Canonical repository/remote: not queried because this task neither selects
  latest nor publishes; canonical `main` is a separate worktree.
- Local versus remote state: current feature branch has extensive preserved
  tracked and untracked work; no history operation is authorized.
- Verified source commit/build: current dirty IBL candidate on `ea566bc`;
  earlier PBR build/tests passed, but all evidence becomes stale after edits.
- Known pre-existing failures: none recorded for the current IBL test suite.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Current imported IBL prototype | Preserve source orientation, SH9, GGX prefilter, BRDF LUT, and exact-source background | available | This task |
| Screen-space GI source | Diffuse probe scale must reach source radiance exactly once | confirmed | Strength controls |
| Existing shadow projects | No shadow files or behavior change | reserved | Final integration |

Public contracts:

- Common source scale remains `whiteWorldScale * exp2(exposureEV)`.
- Background uses only the common source scale.
- Diffuse/specular probe scales are the common source scale multiplied by their
  independently sanitized strengths when enabled, otherwise zero.
- Both lobes disabled or zero-strength never reveal a procedural or
  hemispherical fallback.
- Missing source data must not invent illumination.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| procedural-sky-audit | `/root/procedural_sky_dependency_audit` | shared, read-only | current dirty state | none | current source | complete |
| lobe-strength-audit | `/root/ibl_strength_test_design` | shared, read-only | current dirty state | none | current source | complete |
| test-update | `/root/ibl_test_update` | shared | current dirty state | two IBL test files | implementation | complete |
| documentation | `/root/ibl_docs_update` | shared | current dirty state | four durable docs | implementation | complete |
| implementation | `/root` | current worktree | current dirty state | all task paths | both audits | complete |
| independent-review | `/root/ibl_final_review` | shared, read-only | current dirty state | none | implementation and tests | complete |

## Integration Order

1. Freeze the imported-source and lobe-scale contracts.
2. Remove procedural source generation and its dead ambient data path.
3. Consolidate UI and add independent strengths.
4. Replace obsolete procedural tests with imported/synthetic projection and
   scale-contract coverage.
5. Reconcile durable documentation and run complete verification.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Renderer compiles | Release target succeeds | `cmake --build build --config Release --target uvsr` | passed |
| Scale and imported-source contracts | PBR targets and tests pass | build `uvsr_pbr_tests`; focused CTest | passed |
| Repository test compatibility | Full suite passes | `ctest --test-dir build -C Release --output-on-failure` | 16/16 passed |
| Documentation headings | Self-test and repository audit pass | `tools/check_document_title_case.cmd` | self-test and all 626 headings passed |
| Runtime controls and lighting | Sky control and diagnostic smoke review | `tools/launch_uvsr.ps1 -Experiment iblsky` | live Sky drawer and strength diagnostics verified; automation stopped when user input was detected |
| Preserved diff integrity | No whitespace errors or unrelated loss | `git diff --check`; status/diff review | passed; existing unrelated changes preserved |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-26 | Treat "on GitHub" as repository documentation only | Standing constraints prohibit commit/push; durable local docs are authorized | documentation |
| 2026-07-26 | Remove first-party procedural fallback rather than hide its UI | The user requested end-to-end removal and the fallback can silently reintroduce the exact hidden lighting problem being retired | implementation |
| 2026-07-26 | Keep strength separate from exposure | Exposure must continue matching source/background; lobe gain is an independent calibration/art direction control | implementation |
| 2026-07-26 | Remove Donut's unused `sky_ps` from UVSR runtime packaging | Independent review found no runtime `SkyPass`, but the stale packaged shader was the final nonhistorical procedural-sky artifact | implementation |

## Risks and Escalation Triggers

- Removing procedural fallback makes packaged environment assets required for
  IBL; direct lighting must remain available when loading fails.
- UI and renderer ownership meet in `src/uvsr.cpp`; no other writer may edit it.
- Forward and deferred paths must remain energy-consistent at non-unit gains.
- Fully disabled IBL still performs one cold-start import and prefilter; this is
  a bounded startup cost, not steady-state lighting work.
- The imported-environment GPU update state machine is source-reviewed but does
  not yet have a device-mocked success/failure/retry harness.
- Stop if an unexpected concurrent edit appears in any owned path.

## Completion

- Final integrated commit: intentionally none
- Verification summary: Release renderer built; 16/16 CTest cases, title checker
  self-test, 626-heading repository audit, packaged-shader audit, and
  `git diff --check` passed
- Independent review: no high-severity regression; its only concrete packaged
  artifact finding (`sky_ps`) was removed and reverified
- Coming Soon/documentation update: durable renderer docs updated; no roadmap
  entry because this is continuation of the uncommitted IBL candidate
- Pushed/PR/merged, or intentionally local: intentionally local
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ibl-sky-controls-and-visibility.md`
