# Settings Retained Row Animation Follow-Up

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/settings-menu-revamp` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\settings-menu-revamp`
- Base commit: `0224649055f2218dcf1dbab4af4a1ea8a6b894f9` plus the complete
  uncommitted settings-menu candidate
- Started: 2026-08-14
- Last updated: 2026-08-14
- Planned archive:
  `docs/exec-plans/completed/settings-retained-row-animation-followup.md`

## Goal and Done Condition

Goal: Make the Performance summary and Settings hash retain a stable solid
surface, text baseline, and continuously compacting inner outline throughout
root collapse and opening.

Done when:

- [x] Neither retained row changes opacity or text position at the settled
  endpoint.
- [x] Performance has a permanently opaque retained surface behind its stat
  line in expanded, animated, and compact states.
- [x] Settings uses one inner content outline whose bottom visibly compacts
  around the hash instead of swapping to a separately appearing rectangle.
- [x] Focused checks, Release build, exact-build visual inspection, full CTest,
  documentation validation, and independent review pass.

## Scope

In scope:

- Performance and Settings retained-row surface, text, chrome geometry, and
  transition tests/documentation.

Non-goals:

- Renderer, denoiser, snapshot schema, or unrelated menu behavior changes.
- Changes to or integration of the isolated ratio/MSAA/TAA candidate.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`, UI source/lifecycle tests, and relevant UI documentation.

Shared hotspots reserved for the coordinator:

- All writable paths, the build tree, and the UVSR runtime window. Survey
  agents remain read-only.

## Baseline

- Canonical repository/remote: exact requested base `0224649`; remote freshness
  is not required for this local continuation.
- Local versus remote state: dirty local feature branch with no staged changes.
- Verified source commit/build: prior candidate
  `build/settings-menu-revamp/bin/uvsr.exe`, SHA-256
  `B9F1F7F4B496930578B566248C678C454A6BD42EDCA3653EC528721E7B0785B4`,
  passed Release `ALL_BUILD` and 46 of 46 CTest.
- GPU, scene, camera, resolution, and settings preset when relevant: visual UI
  transition smoke only; no performance benchmark.
- Known pre-existing failures: retained background opacity and text baseline
  change at the endpoint; Settings swaps inner outlines during motion.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Retained-row geometry surveys | Exact shared geometry and draw-order seam | Complete | Coordinator |
| Prior collapse repair | Logical-collapse body gating and fixed scrollbar reservation | Integrated | This follow-up |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- No public, GPU, shader, snapshot-schema, or serialized-setting change.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| CHROME-SURVEY | `/root/collapse_followup_survey` | Shared worktree | Dirty candidate | None | None | Complete |
| GEOMETRY-REVIEW | `/root/alpha_followup_survey` | Shared worktree | Dirty candidate | None | None | Complete |
| TEST-SURVEY | `/root/schema_pathing_survey` | Shared worktree | Dirty candidate | None | None | Complete |
| IMPLEMENT | `/root` | `codex/settings-menu-revamp` | Dirty candidate | Task-owned source/tests/docs | Surveys | Complete |

## Assignment Contracts

### Read-Only Retained-Row Surveys

- Owner/thread: the three survey agents listed above
- Base commit/state: `0224649` plus current dirty candidate
- Read scope: retained Performance/Settings draw paths, ImGui override, focused
  tests, and related documentation
- Write scope: none
- No-touch scope: all files, Git state, build tree, and renderer process
- Build directory and runtime/GPU/resource lease: coordinator only
- Dependencies already integrated: prior logical-collapse repair
- Interface/invariant contract: one stable retained-row baseline and one
  continuously shrinking content rectangle per root
- Deliverable: exact source cause, minimal implementation map, and regression
  assertions
- Done when: endpoint discontinuities and draw-list risks are fully mapped
- Required verification: read-only source inspection
- Allowed Git and external actions: none
- Stop and report if: inspected files change unexpectedly
- Handoff revision/artifact: current dirty candidate, read-only source reviews
- Handoff acknowledged by/on: `/root`, 2026-08-14

## Integration Order

1. Reconcile the read-only surveys and freeze shared geometry.
2. Implement source and focused tests through the coordinator.
3. Rebuild, visually inspect both directions, run full checks, review, archive.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Stable retained surface | Constant full-alpha row in all states | Draw-data/source test and exact-build smoke | Passed in expanded, in-flight, and compact states |
| Stable text baseline | Equal expanded, in-flight, and compact Y | Lifecycle geometry assertion and visual smoke | Passed with the fixed padded top baseline |
| Continuous Settings outline | One content rectangle whose bottom tracks root height | Source/draw-data contract and visual smoke | Passed; no opacity or endpoint geometry swap |
| Combined correctness | Release build, full CTest, heading and diff checks | Repository commands | Passed: 46 of 46 CTests, 2,074 headings, current counts, and clean diff |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-14 | Keep one coordinator as the only writer/runtime owner | UI source, tests, and the renderer window are shared resources | All |
| 2026-08-14 | Preserve ratio/MSAA/TAA isolation | Its overlapping UI/catalog work requires later serialized semantic composition | IMPLEMENT |
| 2026-08-14 | Use exact root-body bounds and let the outline helper apply its own half-pixel inset | Pre-insetting both bounds caused a final-frame width and height handoff | IMPLEMENT |
| 2026-08-14 | Use the same one-line Performance summary for every skin | Expanded two-line Ogg content could not compact without changing text layout | IMPLEMENT |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | `/root` | Active | User feedback and exact prior build recorded | Worktree, branch, active plans, and source paths inspected | Complete surveys and implement |
| 2026-08-14 | Survey agents | Complete | Read-only current-candidate reviews | Duplicate hash drawing, centered endpoint text, binary outline swap, half-pixel bounds, and coverage gaps identified | Corrections integrated by coordinator |
| 2026-08-14 | `/root` | Complete | `src/uvsr.cpp` and focused UI tests/docs | Focused tests passed; Release renderer rebuilt | Run exact-build visual and full repository checks |
| 2026-08-14 | `/root` | Complete | `uvsr.exe` SHA-256 `0E741E316C52CB86804D0908EEC805E8293ADA8A328AD0E644E515617921A7BA` | Expanded, genuine in-flight, and settled compact states visually inspected; both retained rows and outlines remain stable | Refresh documentation counts and rerun full matrix |

## Risks and Escalation Triggers

- Root and child draw-list ordering can hide late chrome if the final visible
  owner is selected incorrectly.
- A separate endpoint-only path can reintroduce a one-frame geometry change.

Stop and ask the user if:

- A fix would require changing the requested compact dimensions or visible
  information rather than only stabilizing the animation.

## Completion

- Final integrated commit: none; no commit authorized
- Verification summary: Release renderer, focused tests, and all 46 CTests
  passed; exact-build expanded, in-flight, and compact visual states passed;
  2,074 headings and current README line counts passed documentation checks.
- Independent review: three read-only reviews completed; reported draw order,
  half-pixel geometry, Ogg parity, and regression-coverage findings were fixed
  before the final review pass.
- Coming Soon/documentation update: no roadmap change required
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual acceptance of this exact
  candidate; later semantic composition with the isolated ratio/MSAA/TAA branch
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/settings-retained-row-animation-followup.md`
