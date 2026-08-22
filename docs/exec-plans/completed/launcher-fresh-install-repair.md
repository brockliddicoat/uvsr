# Launcher Fresh Install Repair

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/launcher-fresh-install-repair` in `work/launcher-reliability`
- Base commit: `50550da36a81efbb5c1cd6c0b85c2bd4ea9b24c7`
- Started: 2026-08-21
- Last updated: 2026-08-21
- Planned archive:
  `docs/exec-plans/completed/launcher-fresh-install-repair.md`

## Goal and Done Condition

Goal: make a fresh UVSR installation complete reliably on compatible Windows 11
computers and reduce the time spent resolving one exact public source revision.

Done when:

- [x] A clean checkout cannot fail because release asset size/hash constants were
  derived from checkout-dependent line endings or stale transitional files.
- [x] Exact public-main resolution retains fail-closed provenance while avoiding
  redundant remote work and reporting useful bounded progress.
- [x] Focused launcher/source-contract tests pass.
- [x] An isolated clean source/configure/build/package smoke passes from the
  exact candidate without using cached repository objects or prior build output.
- [x] Independent review finds no remaining P0-P2 fresh-install blocker.

## Scope

In scope:

- The CMake UI-asset contract that rejects the checked-in Geist notice.
- Launcher source-resolution network behavior and progress reporting.
- Deterministic regressions for clean checkout, moving `main`, and retry/recovery.
- Full isolated fresh-build and package evidence.

Non-goals:

- Weakening exact source, dependency, package, or update trust validation.
- Changing unsigned launcher self-update policy.
- Editing pinned Donut sources.
- Publishing, pushing, opening a pull request, merging, or releasing without a
  later explicit user instruction.

Affected subsystems and paths:

- `CMakeLists.txt`, checked-in font notices, and UI source-contract tests.
- `launcher/src/UVSR.Installer/SourceManager.cs` and related launcher tests.
- Launcher reliability documentation and this execution plan.

Shared hotspots reserved for the coordinator:

- Root `README.md`, `CMakeLists.txt`, Git state, integration, builds, and this
  execution plan.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`; live `main` and branch base
  are `50550da36a81efbb5c1cd6c0b85c2bd4ea9b24c7`.
- Local versus remote state: clean task branch equals `origin/main`; no open pull
  requests.
- Verified source/build: launcher 1.1.12 sequence 13 resolves exact public main,
  then fails during CMake configure before compiling UVSR.
- Known failure: clean checkout contains the transitional Geist notice at 4,383
  bytes while `CMakeLists.txt` expects 4,476 bytes.
- Known latency symptom: the launcher performs multiple public-main discovery or
  fetch operations while showing one broad source-resolution phase.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| ASSET-AUDIT | Exact Git-blob/runtime asset inventory and deterministic fix | Complete | Integration |
| RESOLVE-AUDIT | Current network-call graph and faster exact-resolution contract | Complete | Integration |
| INSTALL-AUDIT | Remaining clean-machine blockers and verification matrix | Complete | Integration |
| RESOLVE-WRITE | Filtered exact-resolution implementation and deterministic tests | Complete | Integration |

Public contracts:

- The selected renderer revision must be one exact commit reachable from public
  `main`, and every later checkout/build identity check must agree with it.
- Source resolution must remain safe if `main` advances during installation.
- Asset validation must use checkout-invariant bytes and reject missing,
  corrupted, partial, or mixed inventories.
- Failure must preserve the installed package and leave no candidate active.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| ASSET-AUDIT | `/root/bridge_identity_design` | Shared/read-only | `50550da` | None | None | Complete |
| RESOLVE-AUDIT | `/root/bridge_implementer` | Shared/read-only | `50550da` | None | None | Complete |
| INSTALL-AUDIT | `/root/bridge_patch_audit` | Shared/read-only | `50550da` | None | None | Complete |
| RESOLVE-WRITE | `/root/bridge_implementer` | Shared | `50550da` plus task diff | `SourceManager.cs`, launcher test `Program.cs` | RESOLVE-AUDIT | Complete |
| INTEGRATION | `/root` | Task branch | `50550da` | All task-owned paths | All audits | Complete |

## Assignment Contracts

### Asset-Audit: Reproduce the Clean-Checkout Contract Failure

- Owner/thread: `/root/bridge_identity_design`
- Base commit/state: `50550da`; clean public source tree
- Read scope: `CMakeLists.txt`, font assets, `.gitattributes`, package validation,
  source-contract tests, and relevant Git history
- Write scope: none
- No-touch scope: repository, Git state, build trees, and GitHub
- Deliverable: exact root cause, all adjacent checkout-dependent asset contracts,
  smallest durable fix, and focused tests
- Done when: the 4,383/4,476 mismatch is reproduced from committed bytes and no
  next deterministic asset failure is left hidden behind it
- Allowed Git and external actions: read-only
- Stop and report if: the correct fix requires changing a public package schema

### Resolve-Audit: Reduce Exact Source-Resolution Latency

- Owner/thread: `/root/bridge_implementer`
- Base commit/state: `50550da`; launcher 1.1.12 sequence 13
- Read scope: launcher source/update managers, Git command wrappers, progress UI,
  tests, and the two supplied logs
- Write scope: none
- No-touch scope: repository, Git state, caches, builds, and GitHub mutations
- Deliverable: exact remote/API/Git call graph, redundant work and timeout causes,
  faster fail-closed design, instrumentation, and tests
- Done when: the proposal preserves moving-main safety and update ancestry while
  materially reducing fresh-install round trips
- Allowed Git and external actions: read-only live endpoint timing is allowed
- Stop and report if: optimization would weaken exact commit provenance

### Install-Audit: Find Remaining Fresh-Machine Release Blockers

- Owner/thread: `/root/bridge_patch_audit`
- Base commit/state: `50550da`; supplied attempt logs
- Read scope: build/package/source contracts, workflows, current release evidence,
  and clean-install tests
- Write scope: none
- No-touch scope: repository, Git state, builds, and GitHub mutations
- Deliverable: prioritized P0-P3 findings and a clean-machine regression matrix
- Done when: independent review covers configure, build, package, transaction,
  recovery, hardware prerequisites, and latency/failure UX
- Allowed Git and external actions: read-only
- Stop and report if: another active task owns a required overlapping write

### Resolve-Write: Implement Filtered Exact Source Resolution

- Owner/thread: `/root/bridge_implementer`
- Base commit/state: `50550da` plus the coordinator-owned Geist/plan diff
- Write scope: `launcher/src/UVSR.Installer/SourceManager.cs` and
  `launcher/tests/UVSR.Installer.Tests/Program.cs` only
- No-touch scope: Git state, product version/sequence/lock, CMake, assets, docs,
  feeds, build scripts, renderer sources, and GitHub
- Required implementation: remove redundant remote/config subprocesses; use
  blobless depth-one and blobless unshallow fetches; validate the exact partial
  clone configuration; retry lazy checkout as a network operation; retain the
  first selected commit if `main` moves; report metadata, history, and source
  materialization as separate user-visible phases
- Tests: exact command/config/progress contracts, ignored/wrong filter rejection,
  moving-main pinning, and retry classification without live GitHub dependency
- Allowed checks: isolated pinned .NET launcher build/test only; no source-build
  smoke and no Git mutation
- Stop and report if: implementation would accept an unfiltered fallback,
  persist an untrusted checkout, or require a package/state schema change

## Integration Order

1. Reproduce and classify the asset failure and source-resolution call graph.
2. Freeze the deterministic asset and exact-source interfaces.
3. Implement disjoint fixes and focused regressions.
4. Run launcher tests and an isolated clean configure/build/package smoke.
5. Independently review the composed result and archive the evidence.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Checkout-invariant assets | Git blob and clean checkout agree with configured size/hash | Git attributes/hash audit and isolated configure | 4,383 bytes, 93 LF/0 CR, SHA-256 `f945605b...`; configure passed |
| Faster exact resolution | Fewer remote round trips with moving-main safety | Instrumented fixtures and live blobless fetch | 61 objects / 34,134 packed bytes; selected `50550da`; moving-main pin tests passed |
| Launcher correctness | Focused and full launcher contract suite | Isolated pinned .NET build/test | 101/101; 0 warnings/errors; health check 14/1.1.13 exit 0 |
| Fresh install payload | Clean configure/build/package succeeds | New isolated configure/build/output-validation tree | Full renderer build, package-input validation, and 50/50 native tests passed |
| Independent safety review | No P0-P2 remaining | Read-only final diff/evidence audit | Approved; no P0-P2 blocker |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-21 | Treat both reports as one deterministic release defect | Both clean attempts stop at the same exact committed notice-size mismatch on different selected `main` commits | ASSET-AUDIT, INTEGRATION |
| 2026-08-21 | Preserve exact-commit and package trust while optimizing latency | Speed cannot come from trusting a mutable branch or skipping integrity checks | RESOLVE-AUDIT, INTEGRATION |
| 2026-08-21 | Keep the sequence-4 transition output but pin its notice to LF | Removing the aliases would break launchers still allowed by the public minimum-sequence contract; accepting both hashes would preserve checkout ambiguity | ASSET-AUDIT, INTEGRATION |
| 2026-08-21 | Resolve and prove ancestry with blobless Git objects | Live depth-one main resolution fell from roughly 1.38 GB of blobs to 61 objects/about 34 KB while retaining Git commit provenance | RESOLVE-WRITE, INTEGRATION |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-21 | `/root` | Active | Branch `codex/launcher-fresh-install-repair` at `50550da` | Remote/worktree/PR/plan preflight complete | Run parallel read-only audits |
| 2026-08-21 | ASSET-AUDIT | Complete | Geist blob 4,383-byte LF / `f945605b...`; checkout CRLF 4,476 / `336949b8...` | All adjacent exact UI assets audited | Pin nested LF attributes and committed-byte CMake contract |
| 2026-08-21 | RESOLVE-AUDIT | Complete | Live blobless main fetch: 0.77 seconds, 61 objects, about 34 KB | Exact call graph and moving-main behavior audited | Implement filtered resolution and lazy materialization |
| 2026-08-21 | INSTALL-AUDIT | Complete | Base `50550da` | Configure/package/transaction/prerequisite matrix audited | Add clean managed-source build/stage gate and Win11 smoke |
| 2026-08-21 | RESOLVE-WRITE | Complete | Launcher 1.1.12 source before integration identity advance | Pinned .NET Release build 0 warnings/errors; full suite 101/101 passed twice | Coordinator advances identity and runs composed build/smoke |
| 2026-08-21 | `/root` integration | Final review | Launcher 1.1.13 sequence 14; lock `d3148fa8...`; launcher SHA `ee3b04e4...`; renderer SHA `131bcfa5...` | Identity uniquely advances from `50550da`; 101/101 launcher tests; clean configure/full renderer build; exact package validation; 50/50 native tests | Independent composed-diff review |
| 2026-08-21 | FINAL-INSTALL-REVIEW | Complete | Frozen local candidate | Independent checkout, provenance, retry, package, identity, and evidence audit | Approved with no P0-P2 blocker |

## Risks and Escalation Triggers

- Correcting only the first visible size may expose another stale asset constant;
  the audit must validate the full clean-checkout inventory.
- Reducing fetches must not select an unverified commit or hide a moving-main
  transition.
- A full native smoke is long-running and must use an isolated build/cache root.

Stop and ask the user if:

- A complete fix would require weakening integrity checks, changing unsigned
  self-update trust, or materially changing supported hardware.

## Completion

- Final integrated commit: intentionally uncommitted; publication was not authorized
- Verification summary: launcher 101/101; native 50/50; full renderer build;
  exact package validation; launcher health and identity passed
- Independent review: approved with no remaining P0-P2 blocker
- Coming Soon/documentation update: not required for this focused repair; maintained
  README counts were refreshed
- Pushed/PR/merged, or intentionally local: local unless later authorized
- Remaining experiments or follow-ups: rerun the launcher-managed source smoke
  after the source fix is public, then retest a clean Windows 11 client
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/launcher-fresh-install-repair.md`
