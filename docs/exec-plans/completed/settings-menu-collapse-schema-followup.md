# Settings Menu Collapse and Schema Coordination Follow-Up

## Status

- State: completed locally; product acceptance pending
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/settings-menu-revamp` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\settings-menu-revamp`
- Base commit: `0224649055f2218dcf1dbab4af4a1ea8a6b894f9` plus the complete,
  uncommitted settings-menu-revamp candidate
- Started: 2026-08-14
- Last updated: 2026-08-14
- Planned archive:
  `docs/exec-plans/completed/settings-menu-collapse-schema-followup.md`

## Goal and Done Condition

Goal: Correct settled Settings chrome and scrollbar layout behavior, add the
requested gated-alpha group outline, open Pathing when Path Tracing is selected,
and make four-character settings-schema allocation safe across concurrent or
temporarily disconnected branches.

Done when:

- [x] Settings retains width during both collapse directions, shows no visible
  scrollbar after settling, and keeps its outer and retained inner outlines.
- [x] The gated alpha numeric area has one continuous carved group outline with
  no interactive or separately outlined fourth component.
- [x] Switching to Path Tracing opens Pathing without preventing later manual
  collapse until the next solution switch.
- [x] A documented, mechanically checked schema reservation/integration system
  detects concurrent collisions and defines disconnected-agent resolution.
- [x] Focused and full checks, Release build, exact-build runtime inspection,
  documentation validation, and independent review pass.

## Scope

In scope:

- Settings root/child collapse geometry, scrollbar presentation, and outlines.
- Gated Material color-picker alpha numeric-group presentation.
- Solution-switch handling for the Pathing drawer.
- Snapshot schema reservation metadata, validation tooling, tests, and current
  agent/integration documentation.
- Task-owned tests, override patches, and execution records.

Non-goals:

- New settings payload semantics or a schema bump for presentation-only fixes.
- Renderer-quality or performance changes.
- Changes to the separate ratio/MSAA/TAA candidate.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/uvsr.cpp`, ImGui overrides, UI lifecycle/source-contract tests.
- Snapshot schema header, reservation metadata/tooling, catalog/decoder tests,
  and collaboration documentation.

Shared hotspots reserved for the coordinator:

- All writable paths, the build tree, and the renderer window. Survey agents
  remain read-only.

## Baseline

- Canonical repository/remote: exact requested base `0224649`; live canonical
  state is not the integration target for this local follow-up.
- Local versus remote state: local dirty feature branch; no staged changes.
- Verified source commit/build: current local candidate
  `build/settings-menu-revamp/bin/uvsr.exe`, SHA-256
  `AD76057366754F08423DD8472D5FF2E15D6A20807FA6A28A340766A49023BDCB`,
  passed Release `ALL_BUILD` and 45 of 45 CTest before this follow-up.
- GPU, scene, camera, resolution, and settings preset when relevant: visual UI
  smoke only; no performance benchmark.
- Known pre-existing failures: settled collapsed outlines disappear; scrollbar
  removal changes child width during both transition directions; gated alpha has
  no requested blank-area group outline; Pathing does not open on solution switch.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Collapse survey | Exact endpoint and layout-preserving scrollbar seam | Complete | Coordinator |
| Alpha survey | Exact continuous-outline geometry and style seam | Complete | Coordinator |
| Schema/Pathing survey | Enforceable allocation contract and state transition | Complete | Coordinator |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- The shown `0002` schema remains registered; excluding Material drawer
  presentation changes represented membership and allocates version `0003`.
- Schema reservations must be committed branch metadata to be discoverable;
  uncommitted or unreachable work cannot reserve globally and must be treated as
  a candidate requiring collision validation at integration.
- No GPU, shader, material ABI, or renderer-default contract changes.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| COLLAPSE-SURVEY | `/root/collapse_followup_survey` | Shared isolated worktree | Dirty candidate | None | None | Complete |
| ALPHA-SURVEY | `/root/alpha_followup_survey` | Shared isolated worktree | Dirty candidate | None | None | Complete |
| SCHEMA-PATHING-SURVEY | `/root/schema_pathing_survey` | Shared isolated worktree | Dirty candidate | None | None | Complete |
| IMPLEMENT | `/root` | `codex/settings-menu-revamp` | Dirty candidate | Task-owned source/tests/docs | All surveys | Complete |
| REVIEW | `/root/collapse_followup_survey` and focused survey reviewers | Shared isolated worktree | Final candidate | None | IMPLEMENT | Complete |

## Assignment Contracts

### Collapse Survey: Endpoint and Scrollbar Geometry

- Owner/thread: `/root/collapse_followup_survey`
- Branch/worktree: shared isolated worktree
- Base commit/state: `0224649` plus current dirty candidate
- Read scope: Settings root/child draw paths, ImGui override, lifecycle tests
- Write scope: none
- No-touch scope: all files, Git, builds, and renderer process
- Build directory and runtime/GPU/resource lease: coordinator only
- Dependencies already integrated: prior collapse correction
- Interface/invariant contract: preserve child width through both transition
  directions while hiding the scrollbar only visually when required
- Deliverable: exact cause, patch seam, and regression map
- Done when: endpoint outlines and width changes are fully traced
- Required verification: source inspection only
- Allowed Git and external actions: read-only local inspection
- Stop and report if: another writer changes the inspected paths
- Handoff revision/artifact: final dirty candidate reviewed 2026-08-14
- Handoff acknowledged by/on: `/root`, 2026-08-14

### Alpha Survey: Continuous Blank-Area Outline

- Owner/thread: `/root/alpha_followup_survey`
- Branch/worktree: shared isolated worktree
- Base commit/state: `0224649` plus current dirty candidate
- Read scope: color-picker override and lifecycle/source tests
- Write scope: none
- No-touch scope: all files, Git, builds, and renderer process
- Build directory and runtime/GPU/resource lease: coordinator only
- Dependencies already integrated: hidden fourth numeric widget correction
- Interface/invariant contract: one continuous 3D outline around both blank input
  rows, no middle gap, no per-row outline, no interaction
- Deliverable: exact geometry/style/test map
- Done when: a stable draw rect and ordering are identified
- Required verification: source inspection only
- Allowed Git and external actions: read-only local inspection
- Stop and report if: the two rows do not share a stable parent/draw list
- Handoff revision/artifact: final dirty candidate reviewed 2026-08-14
- Handoff acknowledged by/on: `/root`, 2026-08-14

### Schema and Pathing Survey: Concurrent Allocation and Drawer State

- Owner/thread: `/root/schema_pathing_survey`
- Branch/worktree: shared isolated worktree
- Base commit/state: `0224649` plus current dirty candidate
- Read scope: snapshot/decoder/catalog tooling and solution/drawer state
- Write scope: none
- No-touch scope: all files, Git, builds, and renderer process
- Build directory and runtime/GPU/resource lease: coordinator only
- Dependencies already integrated: shown snapshot schema `0002`; this follow-up
  registers revised represented membership as `0003`
- Interface/invariant contract: detect rather than silently accept a duplicated
  schema claim; retain manual Pathing collapse until a new Path Tracing switch
- Deliverable: concrete near-YAGNI design plus exact implementation/test map
- Done when: connected and disconnected branch cases have deterministic rules
- Required verification: source inspection only
- Allowed Git and external actions: read-only local inspection
- Stop and report if: a four-character schema cannot support the required rule
- Handoff revision/artifact: final dirty candidate reviewed 2026-08-14
- Handoff acknowledged by/on: `/root`, 2026-08-14

## Integration Order

1. Reconcile all read-only surveys and freeze public contracts.
2. Correct collapse geometry and alpha outline.
3. Implement Pathing transition and schema reservation validation.
4. Update tests/docs and reconstruct overrides.
5. Build, test, inspect the exact runtime, independently review, and archive.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Stable collapse geometry | Constant child width, endpoint outlines, invisible settled scrollbar | Lifecycle draw-data checks plus exact-build visual smoke | Passed; stable hash, no endpoint scrollbar nub, both outlines present, and opening scrollbar width stable |
| Gated alpha group outline | One continuous carved rect, no widget or interaction | ImGui lifecycle and source-contract tests | Passed; RGB, HSV, inter-row gap, storage, canary, and RGBA-negative coverage |
| Pathing opens on switch | Transition-only state test and exact-build smoke | Focused source/state tests | Passed source/state contracts; prior runtime smoke remains applicable because the final repair did not alter this path |
| Schema concurrency safety | Reservation validation and synthetic collision tests | Tool/unit/source-contract tests | Passed; live fingerprint `93b446de2e16385efdec6b0c50fb52fe` resolves uniquely to `0003` |
| Combined correctness | Release build and full CTest | Repository build/test commands | Passed Release `ALL_BUILD` with 311 shader tasks and 46 of 46 CTest tests |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-14 | Interpret settings numbers as the four-character snapshot schema prefix | It is the only new settings number whose concurrent allocation can collide across branches | SCHEMA-PATHING-SURVEY |
| 2026-08-14 | Keep every writer on the coordinator | The affected UI and schema files are shared hotspots; parallel work is read-only | All |
| 2026-08-14 | Preserve the separate ratio/MSAA/TAA worktree | Its overlapping UI/catalog edits require later serialized semantic composition | IMPLEMENT |
| 2026-08-14 | Gate root-panel bodies on logical collapse, not `Begin()` alone | A continuously rearmed zero-height auto-fit can make `Begin()` return true after logical collapse; the wrongly submitted Settings child redirected scrollbar geometry into the root while its late chrome went to a hidden child | IMPLEMENT |
| 2026-08-14 | Use one compact-root renderer for Performance and Settings | The requested endpoint is exact visual parity apart from text and Settings click behavior, so duplicated look-alike paths are not an adequate contract | IMPLEMENT |
| 2026-08-14 | Reserve the scrollbar channel on every submitted Settings-body frame | Removing the scrollbar flag during motion changed the child work width and visibly resized every drawer header | IMPLEMENT |
| 2026-08-14 | Preserve shown schema `0002` and append `0003` | Material drawer visibility was still changing the hash during root collapse; the revised membership is a new represented schema, while the already shown `0002` artifact remains identifiable | IMPLEMENT |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | `/root` | Active | User correction list and current candidate recorded | Worktree/process identity inspected | Complete surveys and implement |
| 2026-08-14 | Survey agents | Complete | Distilled collapse, alpha, schema, and Pathing handoffs | Read-only source inspection | Coordinator acknowledged; final review in progress |
| 2026-08-14 | `/root` | Active | Logical-collapse root lifecycle and shared compact chrome implemented | Focused source-contract and ImGui lifecycle tests passed 2 of 2 | Rebuild exact app and visually inspect the settled endpoint |
| 2026-08-14 | `/root` | Active | Schema registry/probe, generic decoder, Pathing one-shot, and hidden-alpha perimeter implemented | Focused tests passed before final combined run | Complete full Release verification after visual repair |
| 2026-08-14 | `/root` | Complete | `build/settings-menu-revamp/bin/uvsr.exe`, SHA-256 `B9F1F7F4B496930578B566248C678C454A6BD42EDCA3653EC528721E7B0785B4` | Release `ALL_BUILD`, 311 shaders, 46 of 46 CTest, 2,059 document headings with zero violations, exact-build collapse/open visual inspection | Await user product acceptance; no publication authorized |
| 2026-08-14 | Review agents | Complete | Final dirty candidate | Independent collapse, alpha, schema, and Pathing source review passed; requested hardening incorporated | Ownership released to coordinator |

## Risks and Escalation Triggers

- ImGui scrollbar flags change content width, so opacity must be separated from
  reservation and hit testing.
- Settled root chrome may be clipped or submitted to the wrong draw list.
- A branch-local reservation cannot provide global mutual exclusion when work is
  uncommitted or the branch is not discoverable; integration must fail closed.
- The separate ratio/MSAA/TAA branch overlaps `src/uvsr.cpp` and catalog surfaces.

Stop and ask the user if:

- Schema allocation would require a centralized external service or publication
  action beyond the authorized local repository change.

## Completion

- Final integrated commit: none; no commit authorized
- Verification summary: Release `ALL_BUILD` passed with 311 shader tasks; full
  CTest passed 46 of 46; exact-build visual inspection proved a stable settings
  hash, no settled scrollbar nub, retained outer and inner outlines, and stable
  scrollbar/header width during opening; documentation heading validation found
  2,059 headings and zero violations.
- Independent review: passed read-only collapse, alpha, schema, and Pathing audits
- Coming Soon/documentation update: this base has no Coming Soon section
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user product acceptance is pending for
  the exact candidate. The isolated ratio/MSAA/TAA branch must be composed later
  by one integrator; its overlapping UI/catalog edits require rerunning the
  schema probe and assigning the integrated schema by fingerprint.
- Active ownership released: yes; all survey writers were read-only and complete
- Archived to completed/abandoned path: completed on 2026-08-14
