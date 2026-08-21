# Unsigned Launcher Download

## Status

- State: complete
- Coordinator: `/root`
- Gate branch: `codex/unsigned-launcher-download`; merged by PR #36
- Publication branch: `codex/unsigned-launcher-link`; merged by PR #37
- Closeout branch: `codex/unsigned-launcher-closeout`
- Worktree: `work/launcher-reliability`
- Base commit: `29e9df8d8a6326240e1f7560c7a5ae0f59322cf8`
- Started: 2026-08-21
- Last updated: 2026-08-21
- Planned archive: `docs/exec-plans/completed/unsigned-launcher-download.md`

## Goal and Done Condition

Goal: make the root README offer a working, explicitly unsigned UVSR Launcher
download without describing it as signed or silently broadening unsigned trust
inside launcher self-update.

Done when:

- [x] A versioned unsigned launcher artifact is bound to an exact verified
  source commit and exposed through a durable GitHub Release URL.
- [x] The generated README block labels the executable as unsigned and points
  to the exact versioned release.
- [x] Launcher self-update remains governed by its existing feed and publisher
  verification unless the user separately requests that trust change.
- [x] Tooling, workflow validation, documentation, and focused regressions agree
  with the unsigned manual-download contract.
- [x] The integrated change and public release state pass independent review and
  exact artifact verification.

## Scope

In scope:

- Root launcher availability copy and generated download state.
- Deterministic README synchronization tooling and its GitHub validation.
- A versioned, exact-commit unsigned manual-bootstrap GitHub Release.
- Release documentation and agent instructions that distinguish manual unsigned
  download from signed self-update.

Non-goals:

- Accepting unsigned launcher self-updates.
- Restoring mutable `releases/latest` or `uvsr-launcher-latest` authority.
- Rebuilding or changing the renderer.
- Purchasing or provisioning a code-signing identity.

Affected subsystems and paths:

- `README.md`, `AGENTS.md`, `launcher/README.md`.
- `tools/sync_launcher_readme_download.py`.
- `.github/workflows/launcher-readme-download.yml`.
- `docs/exec-plans/completed/unsigned-launcher-download.md`.
- GitHub release/tag state for the exact verified launcher artifact.

Shared hotspots reserved for the coordinator:

- All affected writable paths, Git state, GitHub release/tag state, and final
  integration/publication.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`; live `main` and this task
  base both equal `29e9df8d8a6326240e1f7560c7a5ae0f59322cf8`.
- Local versus remote state: clean task worktree; no open pull request; new task
  branch is local-only at the base.
- Verified source commit/build: Windows 11 Launcher run `32522216529` passed at
  the exact base and retained artifact `9461188459` until 2026-09-04.
- Known pre-existing failures: the public sequence-2 launcher is unsigned,
  incompatible with current renderer source, and does not match the checked-in
  feed; the exact versioned v1.1.1 executable is absent.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| ARTIFACT-AUDIT | Exact archive inventory, EXE identity, checksum, health, and unsigned status | Complete | Integration and release |
| UNSIGNED-DESIGN | Manual-download state contract separated from self-update feed | Complete | Coordinator implementation |
| RELEASE-PREFLIGHT | Publication authority, live tag/release conflicts, and ordering | Complete | Coordinator publication |

Public contracts:

- A manual unsigned download must be labeled unsigned at the link and in the
  surrounding copy.
- The URL must be an immutable versioned release asset, never a mutable latest
  alias or expiring Actions artifact.
- Release source, executable metadata, checksum, health identity, and exact
  commit must agree.
- The canonical and legacy self-update feeds remain unchanged for this task.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| ARTIFACT-AUDIT | `/root/bridge_implementer` | Shared/read-only plus disposable temp | `29e9df8` | None | None | Complete |
| UNSIGNED-DESIGN | `/root/bridge_identity_design` | Shared/read-only | `29e9df8` | None | None | Complete |
| RELEASE-PREFLIGHT | `/root/bridge_patch_audit` | Shared/read-only | `29e9df8` | None | None | Complete |
| UNSIGNED-IMPLEMENT | `/root/bridge_implementer` | Shared writer | `29e9df8` | README synchronization tool and launcher download workflow | All audits | Complete |
| INTEGRATION | `/root` | Gate and publication branches | `29e9df8` | All task-owned paths and external publication | All audits | Complete |

## Assignment Contracts

### Artifact-Audit: Verify the Exact Unsigned Candidate

- Owner/thread: `/root/bridge_implementer`
- Base commit/state: `29e9df8`; successful run `32522216529`, artifact
  `9461188459`
- Read scope: Actions artifact, launcher metadata, checksums, health mode, and
  Authenticode status
- Write scope: disposable temporary download only
- No-touch scope: repository, Git state, releases, tags, feeds, and user installs
- Interface/invariant contract: verification may execute only launcher health
  mode, never the UI or installation path
- Deliverable: exact archive/EXE/checksum identity and blockers
- Done when: x64, product version, sequence/version health, hash, size, and
  unsigned state are proven
- Allowed Git and external actions: read-only download/API access
- Stop and report if: the artifact is missing, expired, signed unexpectedly, or
  disagrees with source identity

### Unsigned-Design: Define the Manual Download Contract

- Owner/thread: `/root/bridge_identity_design`
- Base commit/state: `29e9df8`
- Read scope: README sync tool, workflow, docs, feed/updater verification
- Write scope: none
- No-touch scope: repository, GitHub, artifact, and build state
- Interface/invariant contract: manual unsigned download is distinct from the
  launcher self-update feed and signature policy
- Deliverable: exact state model, paths, regression matrix, and risk findings
- Done when: the proposed contract cannot be confused with signed update
  authority and retains deterministic generated-block ownership
- Allowed Git and external actions: read-only
- Stop and report if: the contract requires an unsigned updater change or a
  launcher identity bump

### Release-Preflight: Audit Publication Scope and Ordering

- Owner/thread: `/root/bridge_patch_audit`
- Base commit/state: live GitHub at `29e9df8`
- Read scope: releases, tags, Actions, rules, pull requests, active work, and
  publication policy
- Write scope: none
- No-touch scope: repository and all GitHub mutations
- Deliverable: exact safe ordering, conflicts, and authority classification
- Done when: every intended external mutation is enumerated and scoped
- Allowed Git and external actions: read-only
- Stop and report if: a release/tag collision, incompatible active task, or
  missing user authority prevents publication

## Integration Order

1. Verify the exact unsigned Actions candidate and settle the manual-download
   state contract.
2. Implement and test tooling, workflow, and documentation locally.
3. Independently review the composed candidate.
4. Commit and run exact-commit checks.
5. Create and verify the versioned unsigned release only if publication
   authority is confirmed.
6. Publish the README contract through the authorized GitHub path and reverify
   public URL, bytes, and checks.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact unsigned binary | x64/ProductVersion/health/hash/size/NotSigned | Artifact audit and Windows metadata checks | Passed: EXE SHA-256 `2c393a2d...`, 59,054,520 bytes, x64, `1.1.12+29e9df8...`, health exit 0, `NotSigned` |
| Deterministic block | Valid unsigned/manual, unavailable, and future signed states; malformed links rejected | Sync-tool self-test and focused fixtures | Passed: Python compile, self-test, unavailable check/state JSON, strict feed and renderer-contract parse |
| Workflow enforcement | Action accepts the exact unsigned versioned release and rejects mutable/mismatched/signed-label errors | Action lint, PowerShell parse, and protected PR/main validation | Passed: PR #37 run `32531046996` and post-merge main run `32531695128` validated the public unsigned release |
| Update trust isolation | Feed files and launcher signature-verification source are unchanged | Exact Git diff assertions | Passed: both feeds, ProductConstants, NativeMethods, and input lock have no diff; feed alias and identity verifiers pass |
| Documentation correctness | Title Case, line counts, and no stale signed-only claim | Repository documentation checks | Passed: 2,369 headings with zero violations; line counts `130,763 / 386,164 / 516,927`; diff check clean |
| Public availability | Anonymous exact URL returns the verified bytes | Post-publication download and hash check | Passed: EXE 59,054,520 bytes/SHA-256 `2c393a2d...8d2`; checksum 99 bytes/SHA-256 `c8d3c4e7...ba63`; x64, metadata, `NotSigned`, and health exit 0 reverified |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-21 | Publish only a versioned unsigned manual-bootstrap link and leave self-update trust unchanged | The user explicitly requested unsigned executables; accepting unsigned updater payloads is a broader security change that was not requested | All |
| 2026-08-21 | Do not use the Actions artifact as the public URL | It expires after 14 days and requires GitHub authentication; a versioned immutable Release is durable and auditable | All |
| 2026-08-21 | Publish the unsigned bootstrap as a GitHub prerelease | The prerelease marker truthfully distinguishes the unsigned manual bootstrap from a signed stable self-update release | All |
| 2026-08-21 | Prepare locally before requesting publication authority | The user selected unsigned product behavior, but tag, immutable Release, protection, push, PR, and merge mutations remain separately irreversible | INTEGRATION |
| 2026-08-21 | Proceed with the complete protected GitHub publication chain | The user explicitly authorized branch protection, push, pull requests, merges, immutable unsigned prerelease publication, link activation, and public verification | INTEGRATION |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-21 | `/root` | Complete | Branch `codex/unsigned-launcher-download` at `29e9df8` | Remote/worktree/PR/plan preflight completed | Audit handoffs received |
| 2026-08-21 | `/root/bridge_implementer` | Complete | Actions artifact `9461188459` | ZIP/executable/checksum digests, x64 metadata, `NotSigned`, and health 13/1.1.12 passed | Evidence retained in isolated Temp directory |
| 2026-08-21 | `/root/bridge_identity_design` | Complete | Separate manual-bootstrap contract | State, workflow, compatibility, disclosure, and regression matrix reviewed | Updater feed and signature policy remain untouched |
| 2026-08-21 | `/root/bridge_patch_audit` | Complete | Live release/publication preflight | Main, PRs, workflows, immutability, releases, and authority classified | Prepare locally; request exact publication authorization afterward |
| 2026-08-21 | `/root/bridge_implementer` | Complete | Three-state README generator and signed/unsigned workflow validation | Python compile/self-test, live unavailable check, renderer contract parse, PowerShell AST parse, and actionlint passed | Coordinator composition and independent review |
| 2026-08-21 | `/root` | Complete | `work/unsigned-launcher-release-v1.1.12-29e9df8d` | Copied only the audited EXE and checksum; hashes remain `2c393a2d...` and `c8d3c4e7...` | Exact files published after explicit authorization |
| 2026-08-21 | `/root/bridge_patch_audit` | Complete | Final composed candidate | No P0-P3 findings; parser, workflow syntax, exact checksum, docs, feed/identity isolation, and artifact hashes rechecked | External publication only |
| 2026-08-21 | `/root` | Complete | User-authorized publication | Live `main` was `29e9df8`; no open PR or v1.1.12 tag collision existed | Protected gate and immutable prerelease publication completed |
| 2026-08-21 | `/root` | Complete | PR #36, gate commit `c37dac73` on `main` | All six checks passed, including required README validation and two Windows launcher builds; post-merge run `32530279332` passed | Gate is canonical |
| 2026-08-21 | `/root` | Complete | Release `uvsr-launcher-v1.1.12`, ID `374692496`, source `29e9df8` | Immutable prerelease; exact two assets and attestations passed; anonymous downloads, hashes, checksum, x64 metadata, `NotSigned`, and health exit 0 passed | Manual bootstrap is public; self-update feeds remain unchanged |
| 2026-08-21 | `/root` | Complete | PR #37, publication commit `f6f75f4a` on `main` | PR runs `32531047005`, `32531046996`, `32531046978`, and `32531046884` passed; post-merge runs `32531695190`, `32531695128`, `32531695152`, and `32531695181` passed | README link is canonical and public |

## Risks and Escalation Triggers

- Windows will present unsigned-download reputation warnings.
- The manual download cannot become authority for launcher self-update.
- GitHub Release creation becomes difficult to undo after immutability locks the
  release; every byte and source identity must be verified first.
- The separate legal-and-licensing work was kept isolated and publication was
  serialized through this coordinator.

Stop and ask the user if:

- The only feasible implementation would require accepting unsigned self-update
  payloads, using a mutable or expiring download, or replacing an existing
  immutable release.
- Publication scope is not actually covered by the user's request to make the
  unsigned EXE link work.

## Completion

- Final integrated publication commit: `f6f75f4a4f0642814da150656ba95576eb008573`.
- Verification summary: exact unsigned artifact, immutable release/tag/source, attestations, anonymous downloads, checksum, PE x64 metadata, ProductVersion, `NotSigned`, health identity, generated README state, and required GitHub checks all passed.
- Independent review: passed with no P0-P3 findings.
- Coming Soon/documentation update: complete; the finished launcher item was removed.
- Pushed/PR/merged, or intentionally local: gate PR #36 and publication PR #37 merged through protected `main`; closeout record uses the same protected path.
- Remaining experiments or follow-ups: permanent signing and signed self-update remain separate future work.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/unsigned-launcher-download.md`.
