# Launcher and Engine UI Consistency

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-engine-ui-consistency` in `work/launcher-reliability`
- Base commit: `42c2036dc5549eaca1c7a51d84e020018a6a089f`
- Started: 2026-08-20
- Last updated: 2026-08-20
- Planned archive: `docs/exec-plans/completed/launcher-engine-ui-consistency.md`

## Goal and Done Condition

Goal: Make the Windows launcher and the UVSR engine UI use an explicitly
sans-serif presentation while preserving every existing font weight, and make
the launcher's sizing, button states, labels, colors, and update choices
consistent and predictable.

Done when:

- [x] Launcher and renderer UI fonts are sans serif without changing regular,
  semibold, or bold roles.
- [x] The launcher main window is fixed-size and showing Details does not resize
  it.
- [x] Update choices read `UVSR Engine` and `UVSR Launcher`.
- [x] Progress, primary-button, destructive-error, button-size, installed, and
  in-progress states match the requested visual and interaction contracts.
- [x] Exact launcher and renderer candidates pass automated tests and visual
  inspection.

## Scope

In scope:

- Launcher main-window and update-dialog presentation and state behavior.
- Renderer UI font-family selection and packaged font inputs.
- Focused regression tests, release identity metadata, and durable UI
  documentation required by those changes.

Non-goals:

- Broader renderer UI redesign, new controls, rendering-quality changes, or
  public release/push actions.
- Changes to Donut source under `donut/`.

Affected subsystems and paths:

- `installer/src/UVSR.Installer/`
- `installer/tests/`
- `src/`, `assets/fonts/`, and relevant root build/package rules
- Launcher release identity and documentation files when required

Shared hotspots reserved for the coordinator:

- `README.md`, root build files, release identity metadata, this execution plan,
  and renderer/launcher integration.

## Baseline

- Canonical repository/remote: `origin/main`
- Local versus remote state: task branch was clean and equal to live remote main
  at task start.
- Verified source commit/build: `42c2036dc5549eaca1c7a51d84e020018a6a089f`;
  prior launcher release candidate 1.1.5 sequence 6 passed its GitHub workflow.
- GPU, scene, camera, resolution, and settings preset when relevant: no render
  benchmark is required; renderer UI visual inspection will use the default
  scene and settings.
- Known pre-existing failures: none accepted for this task.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Public source update path | Engine change must remain a normal public-source update, not mutate the retired exact-base compatibility bridge | Complete; frozen bridge unchanged | Launcher and renderer integration |
| Font assets | Sans-serif family must preserve regular, semibold, and bold roles and have valid distribution terms | Complete for local source builds; prebuilt redistribution remains unapproved | Renderer package |
| Launcher identity | Any launcher binary-input change must advance and verify its version and sequence | Complete at 1.1.8 sequence 9 | Update feed and installed launcher |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- No rendering ABI or shader binding changes are permitted.
- Existing installed-state JSON remains backward compatible.
- Launcher update identity remains monotonic and non-equivocating.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| UI-1 | `/root/bridge_implementer` | Shared task worktree | `42c2036` | Launcher source and launcher test paths listed below | Audit complete | Complete; paths released |
| UI-2 | `/root/bridge_identity_design` | Shared task worktree | `42c2036` | Renderer font source/test and frozen-bridge verifier paths listed below | Audit complete | Complete; paths released |
| UI-3 | `/root/bridge_patch_audit` | Shared/read-only | `42c2036` | None | None | Complete | Independent review complete |
| UI-4 | `/root` | Task worktree | `42c2036` | Integrated task scope | UI-1 through UI-3 | Complete |

## Assignment Contracts

### UI-1: Implement Launcher Presentation and State

- Owner/thread: `/root/bridge_implementer`
- Base commit/state: clean `42c2036dc5549eaca1c7a51d84e020018a6a089f`
- Branch/worktree: shared `codex/launcher-engine-ui-consistency` task worktree.
- Read scope: launcher source and tests.
- Write scope: `installer/src/UVSR.Installer/MainForm.cs`,
  `installer/src/UVSR.Installer/LauncherDialogs.cs`, one new launcher progress
  control source file if needed, and
  `installer/tests/UVSR.Installer.Tests/Program.cs`.
- No-touch scope: renderer source/tests, bridge scripts/resources, root build
  files, documentation, version/sequence metadata, lock files, Git state, and
  all unrelated work.
- Interface/invariant contract: map every requested launcher visual/state change
  to exact implementation and regression points.
- Deliverable: launcher implementation plus focused regression coverage and a
  distilled handoff.
- Done when: project compiles and focused launcher tests pass for fixed sizing,
  labels, colors, equal button dimensions, and install state transitions.
- Required verification: focused test build/run only in an isolated output
  path; no application launch.
- Allowed Git and external actions: scoped local writes and tests; no Git state
  changes or external actions.
- Stop and report if: an assigned file changes unexpectedly, a state/schema
  change becomes necessary, or the implementation would clip at supported DPI.

### UI-2: Implement Renderer Font Delivery

- Owner/thread: `/root/bridge_identity_design`
- Base commit/state: clean `42c2036dc5549eaca1c7a51d84e020018a6a089f`
- Branch/worktree: shared `codex/launcher-engine-ui-consistency` task worktree.
- Read scope: first-party renderer, font assets, build/package rules, and frozen
  compatibility-bridge machinery.
- Write scope: `src/uvsr.cpp`, `tests/ui_source_contract_tests.cpp`,
  `installer/generate-renderer-source-bridge.ps1`, and one focused PowerShell
  verifier test file if needed.
- No-touch scope: launcher C# source/tests, embedded bridge resource bytes,
  `RendererSourceBridge.cs`, root build files, documentation, version/sequence
  metadata, lock files, Git state, and unrelated work.
- Interface/invariant contract: select a distribution-safe sans-serif path that
  preserves current weights and remains a truthful public-source update.
- Deliverable: a regular 13 px packaged sans-serif stock/Ogg font path, retained
  16 px semibold body and 16 px bold headers, and a bridge verifier that proves
  the historical embedded patch from frozen identities instead of live source.
- Done when: source-contract tests and exact frozen bridge checks pass without
  changing bridge patch/hash/tree/commit constants.
- Required verification: focused native source-contract test and bridge
  generator `-Check`; no renderer launch or full build.
- Allowed Git and external actions: scoped local writes and tests; no Git state
  changes or external actions.
- Stop and report if: frozen bridge bytes/identity would change, CMake or Donut
  source changes become necessary, or a font-weight role would change.

### UI-3: Audit Tests and Ux Edge Cases

- Owner/thread: `/root/bridge_patch_audit`
- Base commit/state: clean `42c2036dc5549eaca1c7a51d84e020018a6a089f`
- Read scope: launcher tests, UI helpers, update workflow, and visual semantics.
- Write scope: none.
- Interface/invariant contract: verify literal labels, color-token equality,
  fixed-size behavior, uniform control sizing, and race-safe action states.
- Deliverable: prioritized regression matrix and independent design risks.
- Done when: each requested behavior has a testable acceptance rule.
- Allowed Git and external actions: read-only local inspection.

### UI-4: Implement and Integrate

- Owner/thread: `/root`
- Branch/worktree: `codex/launcher-engine-ui-consistency` in
  `work/launcher-reliability`
- Base commit/state: clean `42c2036dc5549eaca1c7a51d84e020018a6a089f`
- Read scope: repository-wide as needed.
- Write scope: task-owned launcher, renderer, tests, metadata, and documentation.
- No-touch scope: Donut source, unrelated dirty worktrees, unrelated active work.
- Build directory and runtime/GPU/resource lease: isolated task build/output;
  coordinator alone may launch the exact candidate for visual inspection.
- Dependencies already integrated: none beyond base.
- Interface/invariant contract: preserve font weights, renderer behavior, state
  schema, and update trust model.
- Deliverable: exact tested launcher and `uvsr.exe` candidates.
- Done when: every checklist item and verification row passes.
- Required verification: focused tests, full launcher suite, renderer build/tests,
  package validation, update/identity checks, and Windows visual smoke.
- Allowed Git and external actions: local edits/builds/tests only; no push,
  release, merge, or PR.
- Stop and report if: font licensing is unresolved, a bridge change would weaken
  source identity, or unrelated work overlaps task files.
- Handoff revision/artifact: pending.

## Integration Order

1. Close the three read-only audits and freeze exact UI/font contracts.
2. Implement launcher presentation and state changes with focused tests.
3. Implement renderer font delivery without weakening source identity.
4. Refresh release identity and documentation after writers are frozen.
5. Build, test, package, and visually inspect the exact integrated candidate.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Fixed launcher geometry | Main window cannot resize and Details preserves bounds | UI helper tests plus Windows visual smoke | Passed; 842x751 before/after Details and resisted resize drag |
| Exact labels and button states | Literal/state tests including installed and active operation | Launcher test suite | Passed in 93/93 tests; live update check disabled actions while active |
| Exact semantic colors and sizing | Token equality and control-layout assertions | Launcher UI tests plus screenshot | Passed; blue/white primary, blue progress, red error/Cancel, uniform buttons |
| Sans-serif renderer with preserved weights | Asset metadata, source assertions, package validation, and visual smoke | Native build/tests and launch | Passed; 49/49 tests, package validation, and exact renderer launch |
| Update-system eligibility | Engine source identity changes normally; launcher identity advances monotonically | Identity verifier and update tests | Passed at launcher 1.1.8 sequence 9; live feed/source URLs validated |
| Integrated correctness | Clean exact-snapshot builds and complete relevant suites | Launcher build and native build/test | Passed locally; launcher SHA-256 `ea958ab8...40835`, renderer SHA-256 `549e174e...b3abe` |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-20 | Treat the request as a coordinated launcher and renderer release-input change | It intentionally exercises both update channels; changing only launcher labels would not prove the engine update path | UI-1 through UI-4 |
| 2026-08-20 | Keep the retired exact-base bridge immutable unless audit proves a required compatibility repair | It represents a frozen, authenticated overlay for an older public commit and must not silently become the current renderer source | UI-2 and UI-4 |
| 2026-08-20 | Advance the final launcher to 1.1.8 sequence 9 | Post-review lifecycle and terminal-state repairs changed locked launcher inputs after earlier previews, so the anti-equivocation guard required a new identity | UI-1 and UI-4 |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-20 | UI-1 `/root/bridge_implementer` | Complete | Launcher UI and lifecycle source/tests | 93/93 launcher tests; isolated build clean | Paths released |
| 2026-08-20 | UI-2 `/root/bridge_identity_design` | Complete | Renderer fonts and frozen bridge verifier | 49/49 native tests; package validation; bridge exact | Paths released |
| 2026-08-20 | UI-3/UI-8 `/root/bridge_patch_audit` | Complete | Read-only combined review | No unresolved P0-P2 finding after repairs | Approved final source state |
| 2026-08-20 | UI-4 `/root` | Complete | Launcher 1.1.8 sequence 9 and renderer candidate | Exact builds, identity checks, live update check, Windows visual smoke | Keep local until publication is separately authorized |

## Risks and Escalation Triggers

- The current Windows font inputs are already sans serif, so the change must
  make the product contract explicit and visibly testable without pretending a
  no-op is an engine update.
- WinForms native progress bars do not reliably honor custom colors across all
  Windows themes; a first-party control may be required.
- Fixed geometry must remain usable under Windows DPI scaling and long failure
  messages.
- Renderer source files used to generate the retired bridge are intentionally
  frozen and cannot be regenerated casually from current source.

Stop and ask the user if:

- The only distribution-safe font choice would visibly change the requested
  weight hierarchy, or a materially different family choice requires a visual
  preference.

## Completion

- Final integrated commit: intentionally uncommitted local diff on base `42c2036dc5549eaca1c7a51d84e020018a6a089f`.
- Verification summary: 93/93 launcher tests, 49/49 renderer tests, exact renderer package validation, bridge and launcher identity checks, documentation validators, launcher health check, and Windows visual smoke passed.
- Independent review: renderer/bridge and combined launcher reviews found no unresolved P0-P2 issue after repairs.
- Coming Soon/documentation update: active branch entry and durable launcher/font documentation updated.
- Pushed/PR/merged, or intentionally local: intentionally local unless separately authorized.
- Remaining experiments or follow-ups: signing, signer pinning, canonical feed publication, and public source publication require separate authority; a same-DPI cross-monitor visual move was unavailable on this single-monitor host but has focused contract coverage.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/launcher-engine-ui-consistency.md`.
