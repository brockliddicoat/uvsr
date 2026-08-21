# Launcher README Download Sync

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-readme-download-sync` in `work/launcher-reliability`
- Base commit: `76d76f384c835acb915ba62e5de83c6eac688123`
- Started: 2026-08-21
- Last updated: 2026-08-21
- Planned archive: `docs/exec-plans/completed/launcher-readme-download-sync.md`

## Goal and Done Condition

Goal: stop the root README from sending users to an incompatible mutable launcher release and make future compatible signed launcher publications update the README to the feed-derived immutable versioned asset.

Done when:

- [x] The current README exposes no executable link while no compatible signed public launcher exists.
- [x] Deterministic tooling owns the download block and rejects mutable `releases/latest` links.
- [x] The release agent derives the block from the strict canonical feed, and GitHub Actions rejects publication unless the versioned release, asset bytes, signature, metadata, health check, attestation, and legacy feed mirror agree.
- [x] Agent and launcher release documentation require this synchronization.
- [x] Focused tests, documentation validators, workflow review, and an independent safety review pass.

## Scope

In scope:

- Root README launcher availability block.
- Deterministic README download-block tooling and tests.
- Release/feed-triggered GitHub Actions enforcement.
- Agent and launcher release instructions.

Non-goals:

- Publishing, signing, tagging, or modifying a GitHub Release.
- Linking the unsigned 14-day Actions artifact.
- Changing launcher binaries, release identity, feed contents, or update trust.

Affected subsystems and paths:

- `README.md`
- `AGENTS.md`
- `launcher/README.md`
- `tools/sync_launcher_readme_download.py`
- `.github/workflows/launcher-readme-download.yml`
- `.gitignore`

Shared hotspots reserved for the coordinator:

- `README.md`, `AGENTS.md`, launcher documentation, workflows, and this plan.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, `main`
- Local versus remote state: equal at `76d76f38` before task edits.
- Verified source commit/build: launcher and renderer checks passed on the preceding publication.
- Known pre-existing failures: `/releases/latest/download/...` serves mutable tag `uvsr-launcher-latest` bytes that disagree with the canonical sequence-2 feed; the feed-derived versioned v1.1.1 executable is not publicly available.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical launcher feed | Version, sequence, artifact name, size, and SHA-256 remain authoritative | Confirmed | Sync workflow |
| Versioned GitHub Release | Non-draft, non-prerelease `uvsr-launcher-v<version>` with exact assets | Not currently available | Sync workflow |
| Permanent signer | Valid Authenticode chain and compiled SPKI pin | Not currently configured | Sync workflow |

Public contracts:

- Human downloads use an immutable feed-derived versioned Release URL, never `releases/latest` or an Actions artifact.
- The README contains exactly one marked generated block.
- Without a fully validated compatible release, that block is visibly unavailable and has no executable link.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| README-LINK-AUDIT | `/root/bridge_identity_design` | Shared/read-only | `76d76f38` | None | Live GitHub | Complete |
| README-LINK-SCRIPT | `/root/bridge_implementer` | Shared | `76d76f38` | `tools/sync_launcher_readme_download.py` | Fixed marker/URL contract | Complete |
| README-LINK-REVIEW | `/root/bridge_patch_audit` | Shared/read-only | `76d76f38` | None | Integrated candidate | Complete |
| WORKFLOW-REVIEW | `/root/bridge_identity_design` | Shared/read-only | `76d76f38` | None | Integrated candidate | Complete |
| INTEGRATION | `/root` | Shared | `76d76f38` | All shared hotspots and integration | Audit and script handoff | Complete |

## Assignment Contracts

### README-Link-Script: Deterministic Download Block Tool

- Owner/thread: `/root/bridge_implementer`
- Branch/worktree: shared `work/launcher-reliability`
- Base commit/state: clean `76d76f38`
- Read scope: README and existing tools.
- Write scope: only `tools/sync_launcher_readme_download.py`.
- No-touch scope: all documentation, workflows, launcher source/feed/identity, Git state, releases, and renderer files.
- Build directory and runtime/GPU/resource lease: none.
- Interface/invariant contract: exact markers; exact immutable URL; unavailable mode has no executable link; outside bytes/newline style preserved.
- Deliverable: script with `--check`, `--set-version`, `--set-unavailable`, and `--self-test`.
- Done when: deterministic positive and negative self-tests pass.
- Required verification: Python compile and self-test.
- Allowed Git and external actions: none.
- Stop and report if: current README structure cannot support one exact generated block.
- Handoff revision/artifact: owned-file diff and test output.
- Handoff acknowledged by/on: `/root`, 2026-08-21.

## Integration Order

1. Integrate the deterministic block tool.
2. Replace the stale README link with the unavailable generated block.
3. Add release/feed synchronization and documentation rules.
4. Run focused and repository documentation checks.
5. Complete independent review and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| No stale public link | Exact unavailable block and no mutable latest route | Script `--check` plus source search | Passed |
| Deterministic updates | Positive/negative marker and version cases | Script `--self-test` | Passed |
| Safe publication gate | Agent derives from strict feed; read-only workflow validates release attestation, source identity, bytes, signature, x64 metadata, and health before merge | Official `actionlint`, PowerShell parsing, unavailable/no-op/omission simulations, and release-source worktree verification | Passed locally and independently reviewed |
| Documentation integrity | Title Case, link syntax, line counts, and diff checks | Repository tools | Passed: 2,352 headings; 130,478 first-party lines; no whitespace errors |
| Launcher identity unchanged | Documentation/workflow/tool changes do not alter locked launcher binary inputs | `verify-launcher-identity.ps1 -BaseCommit 76d76f38...` | Passed: 1.1.12 sequence 13 remains valid |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-21 | Remove the active executable link until a valid release exists | The dynamic latest route currently serves bytes that disagree with the feed; the Actions artifact is unsigned and temporary | All |
| 2026-08-21 | Derive the future link from the canonical feed's immutable versioned tag | The feed is the launcher update authority; GitHub's mutable latest pointer is not | All |
| 2026-08-21 | Keep GitHub validation read-only and make the release agent update the link in the feed pull request | A bot write after merge leaves a public mismatch window and can race branch protection; a required pre-merge check is fail-closed | All |
| 2026-08-21 | Require branch protection before release publication | Live `main` currently has no protection or ruleset, so a push failure alone cannot retract a bad public link | INTEGRATION |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-21 | `/root` | Active | Live release/feed audit | Exact size/hash mismatch proven | Integrate tooling and workflow |
| 2026-08-21 | `/root/bridge_implementer` | Complete | `tools/sync_launcher_readme_download.py` | Compile, self-test, and current README check passed | Ownership released |
| 2026-08-21 | `/root` | Integration | README, instructions, and workflow | Script checks and official Action lint passed | Run final review and documentation checks |
| 2026-08-21 | `/root` | Integration | Strict feed parsing, immutable release/tag/source/asset attestation, x64 and health gates | Focused workflow simulations and exact release-source verifier passed | Await final independent review |
| 2026-08-21 | `/root/bridge_patch_audit` | Complete | Final integrated candidate | No remaining P0-P2; block ownership, workflow naming, docs, counts, and diff approved | Ownership released |

## Risks and Escalation Triggers

- Never make an unsigned, draft, prerelease, mutable-tag, temporary Actions, or feed-mismatched artifact the human download.
- The validation workflow is read-only; the release agent must update `README.md` in the same feed publication change.
- Live `main` has no branch protection or ruleset today. The release agent must stop before publication until Launcher README Download is configured as a required pull-request check.
- Transport/API uncertainty must preserve the existing README rather than rewrite it.

Stop and ask the user if:

- Completing the request would require publishing or signing a launcher release.

## Completion

- Final integrated commit: intentionally uncommitted local candidate based on `76d76f384c835acb915ba62e5de83c6eac688123`
- Verification summary: synchronization self-test/check, strict feed parse, feed alias tests, launcher identity, official Action lint, PowerShell parsing and simulations, release-source worktree verification, README counts, Title Case, remote freshness, and diff checks passed
- Independent review: approved with no remaining P0-P2 local blocker
- Coming Soon/documentation update: not required for this release-tooling correction; root and launcher release documentation updated directly
- Pushed/PR/merged, or intentionally local: intentionally local unless separately authorized
- Remaining experiments or follow-ups: enable protected `main` with Launcher README Download required, then publish the first compatible signed immutable versioned launcher release through the documented pull-request flow
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/launcher-readme-download-sync.md`
