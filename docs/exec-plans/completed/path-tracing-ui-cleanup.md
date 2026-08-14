# UI-Clean: Path Tracing UI Cleanup

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/path-tracing-resampling-controls` in `work/path-tracing-resampling-controls`
- Base commit: `54a57b08a462ad83979ccc8912570f2c6cc7ea03` plus the technically verified local path-tracing candidate
- Started: 2026-08-14
- Completed: 2026-08-14
- Archived as: `docs/exec-plans/completed/path-tracing-ui-cleanup.md`

## Goal and Done Condition

Goal: make the Pathing, Denoising, Debug, Sky, Noise, and Representation drawers visually consistent and remove redundant status copy without changing rendering behavior.

Done:

- [x] Requested labels, control grouping, folding, dropdown geometry, and drawer order are implemented.
- [x] UI/source contracts and user documentation match the new presentation.
- [x] The exact candidate passed the full Release build, shaders, tests, documentation checks, independent review, and a bounded visual smoke.

## Scope

In scope:

- Compact side-labeled dropdowns in Pathing, path Denoising, path Debug Transport, and Sky.
- Pathing label and control-order cleanup, Raw denoiser folding, accumulation-summary removal, Sky ordering, and Representation status cleanup.
- Focused UI documentation and source-contract updates.

Non-goals:

- Renderer, estimator, resource, shader, command-ID, or persistence behavior changes.
- Work in `work/ratio-shadow-msaa-cmaa2-prep` or publication actions.

Affected subsystems and paths:

- `src/uvsr.cpp`, `src/path_tracing_settings.h`
- `tests/ui_source_contract_tests.cpp`, focused dependent tests
- `docs/advanced-settings.md`, `docs/path-tracing-transport.md`, and README wording/counts

## Baseline

- Canonical repository/remote: local feature worktree; no remote action authorized.
- Local versus remote state: dirty task-owned path-tracing candidate at exact base `54a57b0`; expected build trees and new shader preserved.
- Prior verified source/build: the pre-cleanup path-tracing candidate passed Release, 333 shader tasks, 43 CTests, documentation checks, and Sponza smoke.
- Current verification scope: bounded UI behavior and layout smoke; no performance claim.
- Known pre-existing failures: none in the selected in-scope checks.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Final Status | Consumer |
| --- | --- | --- | --- |
| Path-tracing candidate | Preserve renderer behavior and exact settings/command IDs | Preserved | UI cleanup |
| Ratio/MSAA/CMAA2 preparation | Remain isolated in its own worktree | Coordinated and untouched | Later integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Visible labels and layout changed; command IDs, settings fields, shader bindings, and renderer behavior remain unchanged.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Final Status |
| --- | --- | --- | --- | --- | --- |
| UI-CLEAN-1 | `/root/uvsr_pt2_architecture_audit` | shared read-only | current candidate | none | Completed; no P0/P1 or requested mismatch |
| UI-CLEAN-2 | `/root/zeta_primary_taa_research` | shared read-only | current candidate | none | Completed; source/tests/docs matched |
| UI-CLEAN-3 | `/root/spatial_gi_reconnection_research` | shared read-only | current candidate | none | Completed; final stale labels identified and repaired |
| UI-CLEAN-4 | `/root` | feature worktree | current candidate | implementation/integration | Completed |

## Integration Order

1. Froze the compact-row pattern and requested visible strings from read-only audits.
2. Patched source, tests, and documentation as one coordinator-owned change.
3. Ran focused checks, full verification, independent frozen review, and exact-artifact visual smoke.
4. Recorded the source and artifact identities and archived this plan.

## Verification Evidence

| Acceptance Criterion | Evidence | Result |
| --- | --- | --- |
| Requested source layout | Focused settings and UI source-contract tests | Passed |
| Release renderer and shader bundle | Release `ALL_BUILD`; complete shader catalog | Passed; 333 shader tasks |
| Complete local suite | `ctest --test-dir b-pt2 -C Release --output-on-failure` | Passed; 43/43 |
| Documentation truthfulness | README count check and Title Case validator | Passed; 0 violations |
| Patch hygiene | `git diff --check` | Passed; only expected LF-to-CRLF notices |
| Independent review | Three frozen read-only reviews | No P0/P1 or remaining requested mismatch |
| Visual consistency | Exact 1080p Sponza candidate | Pathing grouping/names, Raw folding, compact Debug/Sky rows, Sky order, and Representation status were confirmed |

Exact candidate identity:

- Source base: `54a57b08a462ad83979ccc8912570f2c6cc7ea03`
- Working-tree content identity: SHA-256 `4F4CE28BF4945811D2C6ABA03C43D6A28B5DF404B41ED08FB8A4943B2C979DAD` over the sorted status/path/blob manifest for 28 task source files, excluding build trees and this plan
- Executable: `b-pt2/bin/uvsr.exe`
- Executable SHA-256: `530091172875E6BE731290BD74E2D4AB62FBFDC7A1D46F63BB410A406C4A012B`
- Executable size: 2,900,992 bytes
- Executable write time: `2026-08-14T07:35:30.9956499Z`

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives |
| --- | --- | --- |
| 2026-08-14 | Keep all writes with the coordinator and use read-only reviewers. | The request touched shared UI, tests, and documentation in one hot file; a single writer avoided conflicting layout edits. |
| 2026-08-14 | Use `Light Candidates` for the former `NEE Candidates` slider. | It describes the proposals controlled by the slider and aligns with `Light Reservoir` without renaming the Next Event Estimation picker. |
| 2026-08-14 | Keep operational path availability/status disclosures visible. | Static solver explanations moved into bounded tooltips; current runtime failure and fallback state remains discoverable. |
| 2026-08-14 | Leave the exact candidate open at handoff. | The user was actively inspecting that window; closing it would interrupt their visual review. |

## Completion

- Final integrated commit: the single publication commit containing this plan; its exact SHA and push result are recorded in the final task handoff.
- Verification summary: Release build, 333 shaders, 43/43 tests, documentation checks, independent review, and bounded visual smoke passed.
- Independent review: three frozen read-only reviewers found no P0/P1 or remaining requested mismatch after the final label repair.
- Coming Soon/documentation update: no roadmap entry was required; durable UI documentation was updated.
- Pushed/PR/merged, or intentionally local: the user later authorized a direct fast-forward publication to GitHub `main`; the outcome is recorded in the final task handoff.
- Remaining experiments or follow-ups: none for this UI cleanup.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/path-tracing-ui-cleanup.md`.
