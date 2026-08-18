# Windows 11 Launcher Reliability and Self-Update

## Status

- State: locally complete; public release blocked on a permanent code-signing identity and signed launcher feed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/windows-installer` at `C:\Users\brock\OneDrive\Documents\uvsr\work\windows-installer`
- Base commit: `5d15efd730c574e433bfc58ea4727010ff3a6619`, plus the uncommitted locally verified installer candidate from the completed bootstrap plan
- Started: 2026-08-17
- Last updated: 2026-08-17
- Planned archive: `docs/exec-plans/completed/windows-11-launcher-reliability.md`

## Goal and Done Condition

Goal: turn the Windows 11 bootstrapper into the user-facing **UVSR Launcher**, add a durable launcher self-update path alongside UVSR updates, simplify install/launch behavior, redesign the Details surface, and make network transfers resilient to ordinary pauses and transient failures.

Done when:

- [x] Every current user-visible product, executable, shortcut, registration, workflow, and documentation surface says UVSR Launcher while existing owned installer state remains recoverable.
- [x] Update checks UVSR public `main` and the launcher feed, presents a styled component picker, and safely updates either or both. The production self-update path remains intentionally disabled until a permanently signed release and feed exist.
- [x] Reinstall is removed as a top-level action; Install offers Launch or Reinstall when UVSR is already present; Launch uses the requested short label.
- [x] The launcher desktop shortcut is selected by default and targets the installed launcher, including safe migration of owned legacy shortcuts.
- [x] The header and Details area are user-facing and visually consistent with the rest of the application at supported DPI settings.
- [x] Downloads tolerate pauses, retry transient HTTP/network failures with bounded backoff, resume safely where supported, and distinguish stalled progress from a definitive failure.
- [x] Deterministic tests cover update selection, feed validation, self-replacement boundaries, download retry/resume/stall behavior, legacy migration, cancellation, and sparse edge cases.
- [x] A final x64 single-file launcher and exact source-built `uvsr.exe` pass automated, UI, and independent safety reviews.

## Scope

In scope:

- Windows 11 x64 UVSR Launcher UI, lifecycle, download transport, launcher-update feed/client, self-replacement, shortcut/registration branding, compatibility migration, tests, workflow, and documentation.
- Read-only current-public-main and official GitHub/Microsoft protocol research needed to define a safe update contract.

Non-goals:

- Publishing a launcher feed or GitHub release, creating a signing identity, background/automatic updates, Windows 10, silent enterprise deployment, or installing GPU drivers.
- Renaming hidden ownership identifiers or state roots when doing so would strand existing owned installs; compatibility internals may retain legacy names while every visible surface changes.

Affected subsystems and paths:

- `installer/**`, `.github/workflows/windows-installer.yml`, `.gitignore`, `README.md`, and this execution plan.

Shared hotspots reserved for the coordinator:

- `MainForm.cs`, `Models.cs`, `InstallerEngine.cs`, `ProductConstants.cs`, `ShellIntegration.cs`, `SelfCleanup.cs`, build/workflow files, `README.md`, installer documentation, and this plan.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`, live `refs/heads/main` at `5d15efd730c574e433bfc58ea4727010ff3a6619` on 2026-08-17.
- Local versus remote state: feature branch equals public `main` and contains only the prior task's uncommitted launcher precursor; the original checkout and unrelated worktrees remain untouched.
- Verified source commit/build: prior candidate launcher precursor SHA-256 `36ae50c68f8d98df00c0b7de50cdcee05b037f44f8d60cf06fb76f5c052ef0cb`; isolated public-main `uvsr.exe` SHA-256 `f32dc3b3db4d46ccd944709921377c00b1cda561dd8512c52f072ca9c869adf6`.
- GPU, scene, camera, resolution, and settings preset when relevant: no rendering/performance claim; UI verification targets the launcher only.
- Known pre-existing failures: the user observed two transient download failures despite continued Wi-Fi connectivity; current `DownloadManager` has one request attempt, no resume, no inactivity/stall state, and treats transient `HttpRequestException`/I/O failures as terminal.
- Cross-task overlap: the legal/licensing branch also owns future `README.md` and `.github/**` integration; this isolated branch keeps its changes local and must be semantically reconciled by the eventual repository integrator.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| UI and naming audit | User-facing information architecture, dialog, DPI, and legacy-shortcut contract | Complete | Coordinator implementation |
| Launcher self-update contract | Feed schema, trust boundary, replacement phases, and old-launcher migration | Complete | Coordinator implementation |
| Network reliability audit | Retry/resume/stall classifications and deterministic fault fixtures | Complete | Coordinator implementation |
| Prior bootstrap lifecycle | Ownership, transaction, process containment, and exact-package invariants | Complete | All launcher changes |

Public contracts:

- The installed launcher remains per-user and never builds fetched source elevated. Only validated Microsoft prerequisite setup may request elevation.
- A launcher update must come from a fixed HTTPS feed, validate a bounded strict manifest and exact SHA-256, stage before activation, preserve a working current launcher on failure, and never overwrite a foreign path.
- Launcher self-update state is separate from the UVSR renderer package transaction and must recover after interruption without deleting `%LOCALAPPDATA%\UVSR` renderer data.
- Existing owned `UVSR Installer.exe`, shortcuts, registry entries, and hidden state remain migratable; unrelated collisions are preserved.
- Download retry is limited to transient failures and idempotent requests. Hash verification remains mandatory after all resumed or retried transfers.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| LUI-1 | `/root/ui_ux_audit` | Shared read-only | current dirty candidate | None | Prior bootstrap | Complete |
| LUP-1 | `/root/update_architecture` | Shared read-only | current dirty candidate | None | Prior bootstrap | Complete |
| LNET-1 | `/root/network_reliability` | Shared read-only | current dirty candidate | None | Prior bootstrap | Complete |
| LINT-1 | `/root` | `codex/windows-installer` | current dirty candidate | All task-owned paths | LUI-1, LUP-1, LNET-1 | Complete |

## Assignment Contracts

### Lui-1: Audit Launcher Naming and User Experience

- Owner/thread: `/root/ui_ux_audit`
- Branch/worktree: shared read-only
- Base commit/state: `5d15efd7` plus current uncommitted installer candidate
- Read scope: `MainForm.cs`, shell/path models, product constants, docs, current artifact UI where safely inspectable
- Write scope: none
- No-touch scope: all files, Git metadata, registry, shortcuts, and installed state
- Interface/invariant contract: visible branding becomes UVSR Launcher; Install handles installed state; Details and dialogs use one coherent visual language; launcher shortcut is default
- Deliverable: exact control/state changes, legacy migration hazards, DPI/accessibility bugs, and acceptance checks
- Done when: coordinator can implement without guessing visible behavior
- Required verification: read-only source/UI inspection only
- Allowed Git/external actions: no writes, installs, clicks that mutate state, or publication
- Stop and report if: requested UX conflicts with a safety/recovery invariant

### Lup-1: Define Safe Launcher Self-Update

- Owner/thread: `/root/update_architecture`
- Branch/worktree: shared read-only
- Base commit/state: `5d15efd7` plus current uncommitted installer candidate
- Read scope: lifecycle, process, state, shell, workflow, and official GitHub update-delivery documentation
- Write scope: none
- No-touch scope: all repository and external state
- Interface/invariant contract: strict bounded feed; exact artifact hash; separate launcher and UVSR choices; crash-safe replacement; legacy installed-manager migration; no elevation
- Deliverable: minimal schema/state machine, threat model, failure recovery, deterministic tests, and any unavoidable release prerequisites
- Done when: self-update can be implemented without weakening prior ownership/rollback guarantees
- Required verification: read-only inspection and primary-source research
- Allowed Git/external actions: browsing/read-only only; no GitHub changes
- Stop and report if: safe self-update requires a signing/publication choice not presently available

### Lnet-1: Diagnose and Harden Network Transfers

- Owner/thread: `/root/network_reliability`
- Branch/worktree: shared read-only
- Base commit/state: `5d15efd7` plus current uncommitted installer candidate
- Read scope: `DownloadManager`, HTTP callers, tool/source flows, process timeout/cancellation, logs, and tests
- Write scope: none
- No-touch scope: all files and external state
- Interface/invariant contract: transient retry with bounded jittered backoff; safe range resume; explicit stalled state; no hash/trust downgrade; bounded disk/memory/log behavior
- Deliverable: likely failure causes, comprehensive bug list by severity, retry/resume algorithm, fault-injection matrix, and sparse edge cases
- Done when: observed Wi-Fi symptom and adjacent network bugs have actionable fixes and tests
- Required verification: read-only code inspection and local/official protocol evidence
- Allowed Git/external actions: read-only only; no downloads beyond bounded research probes
- Stop and report if: a retry could duplicate a non-idempotent operation or corrupt a promoted archive

## Integration Order

1. Freeze the naming, feed, download, migration, and update-selection contracts from read-only findings.
2. Implement network primitives and their deterministic fault server tests.
3. Implement launcher feed discovery, staged self-update, recovery, and component selection.
4. Implement visible rename, shortcut migration, action simplification, and Details/dialog redesign.
5. Update build/workflow/docs, then run the full suite and build a new exact artifact.
6. Perform UI inspection, adversarial lifecycle review, source-build smoke proportional to affected code, and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Branding and simplified actions | UI tree, screenshots, state transition tests | exact 1.1.1 artifact inspection plus terminal-state/DPI contract tests | Passed; final artifact shows UVSR Launcher, Install/Update/Launch/Uninstall, checked launcher shortcut, and integrated Details surface |
| Launcher plus UVSR update choice | Feed and source fixtures covering none/one/both updates | deterministic update-discovery tests and modal selection checks | Passed; independent UVSR/Launcher rows and none/one/both selection paths verified |
| Crash-safe launcher replacement | phase faults before/after stage, parent exit, promotion, and relaunch | helper/state-machine tests plus exact-path process checks | Passed deterministic identity, continuation, recovery, operation-lock, and cleanup-watcher contracts |
| Connection tolerance | drop, timeout, truncated body, 429, 5xx, Range ignored, validator changed, cancellation | loopback HTTP/HTTPS fault-server tests | Passed retryable-status, TLS EOF/reset, resume/validator/416, redirect, stall, cancellation, and byte-cap matrix |
| Legacy migration | owned old manager/shortcut/registry fixtures and foreign collisions | deterministic ownership tests and local migration exercise | Passed; owned legacy manager/shortcuts migrated while foreign and unreadable collisions remain fail-safe |
| Full contract | no regressions in ownership, packaging, or process containment | `installer/build.ps1` with pinned .NET 10 SDK | Passed 71/71 tests; published x64 single-file launcher SHA-256 `2b5f092bdf80dcdabca46034f1334f6be374c712400e7bf8d6ae1e672f7a5b36` |
| Documentation headings | conventional Title Case everywhere in scope | `tools/check_document_title_case.cmd` | Passed at closeout |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-17 | Rename every visible surface and artifact to UVSR Launcher while retaining legacy hidden ownership paths where migration requires them. | Renaming hidden roots/IDs would make already-owned installs invisible to the new launcher and undermine the requested old-version continuity. | LUI-1, LUP-1, LINT-1 |
| 2026-08-17 | When both components have updates, present checkboxes with both selected by default; users may choose UVSR, Launcher, or both. | This satisfies component choice without forcing two consecutive update passes or withholding one available update. | LUI-1, LUP-1, LINT-1 |
| 2026-08-17 | Keep updates user-initiated and foreground. | Background updating adds persistence and concurrency scope the user did not request. | LUP-1, LINT-1 |
| 2026-08-17 | Continue on the existing isolated feature branch and uncommitted candidate. | The request is a direct refinement of that candidate; rebasing or replacing it would risk losing reviewed work and is unnecessary because live public `main` is unchanged. | All |
| 2026-08-17 | Require a permanent Authenticode signer pin before enabling public launcher self-update. | A hash supplied by the same mutable feed does not establish an independent publisher identity. The preview therefore fails closed instead of accepting an unsigned launcher. | LUP-1, LINT-1 |
| 2026-08-17 | Bump the final local candidate to launcher version 1.1.1, release sequence 2. | Reusing sequence 1 with a different executable hash correctly triggers the anti-equivocation guard for anyone who ran the earlier preview. | LUP-1, LINT-1 |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-17 | `/root` | Active | Prior candidate plus new active plan | Branch/worktree, live remote, dirty state, Coming Soon, active-plan overlap, and source/UI preflight recorded | Spawn bounded read-only audits |
| 2026-08-17 | `/root/ui_ux_audit` | Complete | `MainForm.cs`, `LauncherDialogs.cs`, and final read-only review | Branding/action model, terminal UI, bounded DPI layouts, accessibility, shortcut migration, and stale-window lifecycle reviewed | No remaining local UI P0/P1 after integration |
| 2026-08-17 | `/root/update_architecture` | Complete | `LauncherManager.cs`, `ShellIntegration.cs`, and final read-only review | Same-sequence identity, continuation, package binding, structural shortcut ownership, launch serialization, and cleanup flow reviewed | Permanent signer/feed remains the publication gate |
| 2026-08-17 | `/root/network_reliability` | Complete | Network/toolchain/process hardening and final read-only review | Retry/resume/stall/TLS/Git matrices, elevated-process recovery, and job containment reviewed | No concrete remaining network P0/P1/P2 |
| 2026-08-17 | `/root` | Complete | `UVSR-Launcher-Windows-11-x64.exe` 1.1.1 sequence 2 | 71/71 contract tests, exact-artifact visual inspection, checksum/provenance checks, and combined diff review | Keep local until permanent signing and feed publication are authorized |

## Risks and Escalation Triggers

- A launcher feed cannot make already-published binaries self-update retroactively; the first public UVSR Launcher release must contain this protocol.
- A checksum delivered by the same mutable feed proves transfer integrity, not publisher identity. Public release still requires Authenticode signing or an explicitly accepted repository-control trust boundary.
- Replacing a running single-file launcher and migrating an existing manager path are high-risk lifecycle operations requiring independent review.
- Visual Studio Build Tools remains a large first-run dependency, so network tolerance must not imply an unrealistically short guaranteed install time.

Stop and ask the user if:

- Safe self-update requires choosing a permanent signing identity/feed location that cannot be inferred from the existing public GitHub repository.
- A requested visible rename would require deleting or abandoning an existing owned installation rather than migrating it.
- Publication, release creation, signing, or pushing becomes necessary; this request authorizes local implementation and verification only.

## Completion

- Final integrated commit: none; commit was not authorized
- Verification summary: 71/71 launcher contract tests passed; exact x64 artifact SHA-256 `2b5f092bdf80dcdabca46034f1334f6be374c712400e7bf8d6ae1e672f7a5b36`; final 1.1.1 main and Details surfaces visually inspected; installed renderer package hash matches its 431-file manifest
- Independent review: UI/lifecycle, launcher-update architecture, and network/toolchain reviews completed with no remaining local P0/P1 code defect; the unsigned public-update gate remains intentional
- Coming Soon/documentation update: current README and installer documentation updated; no publication performed
- Pushed/PR/merged, or intentionally local: intentionally local and uncommitted
- Remaining experiments or follow-ups: obtain a permanent code-signing identity; pin its SPKI; sign and hash an immutable launcher release asset; publish `launcher-feed-v1.json` last; run a clean Windows 11 N-1 to N signed-update VM exercise
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/windows-11-launcher-reliability.md`
