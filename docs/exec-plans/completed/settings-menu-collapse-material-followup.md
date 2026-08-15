# Settings Menu Collapse and Material Follow-Up

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/settings-menu-revamp` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\settings-menu-revamp`
- Base commit: `0224649055f2218dcf1dbab4af4a1ea8a6b894f9` plus the complete,
  uncommitted settings-menu-revamp candidate
- Started: 2026-08-14
- Last updated: 2026-08-14
- Planned archive:
  `docs/exec-plans/completed/settings-menu-collapse-material-followup.md`

## Goal and Done Condition

Goal: Correct the collapsed Settings retained row, remove the rejected viewport
shadow implementation, and finish the requested Interface, Material, and gated
alpha presentation details without disturbing renderer behavior.

Done when:

- [x] Collapsing Settings leaves the versioned code unchanged, removes every
  scrollbar remnant, and animates one correct outer outline into a retained
  hash surface with the same interior outline language as Performance and CLI.
- [x] The hardcoded Settings viewport shadows and their tests/docs are deleted.
- [x] Interface and Material labels, button removal, texture-toggle punctuation,
  and gated-alpha numeric visibility exactly match the user's corrections.
- [x] Focused and full tests, Release build, exact-artifact runtime inspection,
  diff hygiene, documentation validation, and independent review pass.

## Scope

In scope:

- Root Settings collapse geometry, scrollbar suppression, retained hash surface,
  outer outline, and snapshot composition.
- Deletion of the task-added Settings viewport edge-shadow implementation.
- Interface Background Color label.
- Material editor button/labels/texture-toggle punctuation.
- Gated color-picker alpha numeric component visibility while retaining the
  checkerboard-backed alpha slider.
- Tests, current documentation, decoder contract, and execution records.

Non-goals:

- Renderer-quality or performance changes.
- Changes to the separate ratio/MSAA/TAA edge-shake candidate.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`, `src/settings_snapshot.h`, settings catalog and tests.
- `overrides/donut-app-ui-polish.patch` and
  `overrides/imgui-tooltip-picker.patch` plus their reconstruction contracts.
- Current settings/material/UI documentation and this plan.

Shared hotspots reserved for the coordinator:

- All writable paths. Survey agents are read-only.

## Baseline

- Canonical repository/remote: exact requested base `0224649`.
- Local versus remote state: isolated local feature branch with only the prior
  task's uncommitted candidate changes; original checkout remains untouched.
- Verified source commit/build: prior exact candidate
  `build/settings-menu-revamp/bin/uvsr.exe`, SHA-256
  `326FC3D691BF8225EBA7A1004344204EFD93785D3B2A88C9A1D945CB3B7B3CD4`,
  passed a Release all-target build, 45-of-45 CTest, and runtime/UI smoke.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU, Sponza Decorated, Position 1, 1920 by 1080;
  visual interaction smoke only, not a performance benchmark.
- Known pre-existing failures: the user-provided collapsed Settings capture
  shows a changing hash, residual scrollbar thumb, stale/missing outer outline,
  and missing retained-row inner outline.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Collapse survey | Exact draw-list, animation, and snapshot causes | Complete | Coordinator |
| Material survey | Exact labels, button, and filename-adjacent toggles | Complete | Coordinator |
| Alpha survey | Exact ImGui gating seam and test contract | Complete | Coordinator |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Root Settings collapsed state is presentation state and must not participate
  in the copied settings snapshot.
- Removing that field changes extraction semantics, so the snapshot prefix and
  catalog contract advance from `0001` to `0002`.
- The retained code remains exactly 32 lowercase hexadecimal characters and
  the decoder resolves the complete represented setting set from its matching
  local catalog.
- No GPU, shader, material ABI, or renderer-default contract changes.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| COLLAPSE-SURVEY | `/root/collapse_followup_survey` | Shared isolated worktree | Dirty prior candidate | None | None | Complete |
| MATERIAL-SURVEY | `/root/material_followup_survey` | Shared isolated worktree | Dirty prior candidate | None | None | Complete |
| ALPHA-SURVEY | `/root/alpha_followup_survey` | Shared isolated worktree | Dirty prior candidate | None | None | Complete |
| IMPLEMENT | `/root` | `codex/settings-menu-revamp` | Dirty prior candidate | Task-owned source/tests/docs | All surveys | Complete |
| REVIEW | `/root/final_diff_review` | Shared isolated worktree | Final follow-up candidate | None | IMPLEMENT | Complete |

## Assignment Contracts

### Collapse Survey: Diagnose Retained Settings Geometry

- Owner/thread: `/root/collapse_followup_survey`
- Branch/worktree: shared isolated worktree
- Base commit/state: `0224649` plus prior task-owned dirty candidate
- Read scope: Settings/Performance draw paths, snapshot composition, tests/docs
- No-touch scope: all files, Git state, builds, renderer process
- Interface/invariant contract: distinguish animation, settled-collapse, and
  snapshot-state defects; remove rather than replace the rejected shadows
- Deliverable: exact source/test map and smallest complete correction
- Done when: all four collapse defects and shadow deletion are traced
- Required verification: source inspection only
- Allowed Git and external actions: read-only local commands
- Stop and report if: current files change unexpectedly during the survey
- Handoff revision/artifact: current dirty follow-up candidate; source audit only
- Handoff acknowledged by/on: `/root`, 2026-08-14

### Material Survey: Map Requested Labels and Removal

- Owner/thread: `/root/material_followup_survey`
- Branch/worktree: shared isolated worktree
- Base commit/state: `0224649` plus prior task-owned dirty candidate
- Read scope: Interface/Material UI, Donut override patch, tests/docs
- No-touch scope: all files, Git state, builds, renderer process
- Interface/invariant contract: enumerate every filename-adjacent texture toggle
  and preserve material mutation callbacks after removing the refresh button
- Deliverable: exact label/button/toggle source and test map
- Done when: every requested Material/Interface correction is unambiguous
- Required verification: source inspection only
- Allowed Git and external actions: read-only local commands
- Stop and report if: a requested label maps to multiple visible controls
- Handoff revision/artifact: current dirty follow-up candidate; source audit only
- Handoff acknowledged by/on: `/root`, 2026-08-14

### Alpha Survey: Diagnose Gated Fourth Numeric Component

- Owner/thread: `/root/alpha_followup_survey`
- Branch/worktree: shared isolated worktree
- Base commit/state: `0224649` plus prior task-owned dirty candidate
- Read scope: ImGui override, reconstructed source, picker tests/contracts
- No-touch scope: all files, Git state, builds, renderer process
- Interface/invariant contract: hide the gated fourth numeric component entirely
  while keeping a visible checkerboard-backed alpha slider
- Deliverable: exact patch seam and regression-test update
- Done when: numeric-input and alpha-bar paths are independently identified
- Required verification: source inspection only
- Allowed Git and external actions: read-only local commands
- Stop and report if: the component and slider cannot be gated independently
- Handoff revision/artifact: current dirty follow-up candidate; source audit only
- Handoff acknowledged by/on: `/root`, 2026-08-14

## Integration Order

1. Complete and reconcile the three read-only surveys.
2. Patch snapshot version/composition and collapse geometry; delete shadows.
3. Patch Interface/Material labels and remove dead refresh support.
4. Patch gated-alpha numeric visibility while retaining the checkerboard slider.
5. Update tests/docs, rebuild overrides and UVSR, then run visual and full checks.
6. Freeze writes, run independent review, repair findings, and archive this plan.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Stable collapsed code | Same expanded/collapsed code with valid `0002` prefix | Exact-build click/collapse/decode smoke | Expanded and collapsed code both `00026a2386b8f314ae556c313c7e9627`; schema and decoder tests passed |
| Correct collapse chrome | No scrollbar remnant; moving outer outline; inner retained outline | User-reference comparison and exact-build capture | Exact build showed no collapsed scrollbar remnant; independent geometry audit confirmed one current outer and one retained inner outline |
| No Settings shadows | Source/tests/docs absence | Focused source contract and `rg` | Source-contract test passed; rejected helper/call absent outside explicit absence contracts and history |
| Material/Interface corrections | Exact visible labels and no refresh button | Override reconstruction tests and runtime inspection | Override reconstruction and source contracts passed; exact labels and button absence covered |
| Gated alpha correction | Hidden fourth number plus checkerboard alpha slider | Picker lifecycle test and runtime inspection | ImGui lifecycle test passed the empty-slot, unchanged-canary, checker-lane, and noninteractive contracts |
| Combined correctness | Release all-target build and full CTest | Repository build/test commands | Release `ALL_BUILD` passed; 45 of 45 CTest passed |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-14 | Advance settings snapshots to version `0002` and omit root collapse state | A transient presentation toggle must not change the represented settings; retaining a constant or lying value would make decoded output inaccurate | COLLAPSE-SURVEY, IMPLEMENT |
| 2026-08-14 | Delete the rejected viewport-shadow code without replacement | The user explicitly rejected the hardcoded approach and requested removal | COLLAPSE-SURVEY, IMPLEMENT |
| 2026-08-14 | Keep the ratio/MSAA/TAA work isolated | It shares `src/uvsr.cpp` and catalog surfaces but has separate behavior and ownership | IMPLEMENT, future integration |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | `/root` | Active | User capture and correction list recorded | Prior candidate identity confirmed | Complete surveys and implement |
| 2026-08-14 | Survey agents | Complete | Read-only distilled handoffs | Collapse, Material, and alpha seams independently mapped | Findings implemented; ownership released |
| 2026-08-14 | `/root` | Verification complete | `build/settings-menu-revamp/bin/uvsr.exe`, SHA-256 `AD76057366754F08423DD8472D5FF2E15D6A20807FA6A28A340766A49023BDCB` | Release `ALL_BUILD`; 45 of 45 CTest; patch reconstruction; title-case and line-count checks; exact-build collapse smoke | Independent final review found no actionable P0-P2 issues |
| 2026-08-14 | `/root/final_diff_review` | Complete | Read-only final dirty-diff review | Snapshot, collapse geometry, override semantics, Material, alpha, tests, and docs reviewed | No actionable P0-P2 findings; ownership released |

## Risks and Escalation Triggers

- The outer outline belongs to the animated root window, while the retained hash
  surface must remain separately framed; drawing both on the wrong list can
  reproduce the stale-outline defect.
- Override patches must still reconstruct cleanly without modifying `donut/`.
- The fourth numeric component and alpha slider share ImGui alpha flags, so the
  patch must split presentation without exposing an interactive hidden value.
- The separate ratio/MSAA/TAA repair still requires serialized semantic
  composition after this local candidate is accepted.

Stop and ask the user if:

- Source inspection reveals two materially different controls that could be the
  requested fourth numeric component and visual evidence cannot disambiguate.

## Completion

- Final integrated commit: pending; no commit authorized
- Verification summary: Release `ALL_BUILD` and 45 of 45 CTest passed; exact
  runtime collapse retained `00026a2386b8f314ae556c313c7e9627` and showed no
  scrollbar remnant; patch reconstruction, Markdown Title Case, README line
  counts, and diff hygiene passed.
- Independent review: `/root/final_diff_review` found no actionable P0-P2
  findings; exact pixel contrast remains visual evidence.
- Coming Soon/documentation update: this base has no Coming Soon section
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: serialized semantic composition with the
  separate ratio/MSAA/TAA repair after both candidates are accepted
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/settings-menu-collapse-material-followup.md`
