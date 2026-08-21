# Launcher Directory Main Publication

## Status

- State: complete; publication commit prepared for the authorized direct
  fast-forward and remote evidence is recorded in the task handoff
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-engine-ui-consistency`
  in `work/launcher-reliability`
- Base commit: `42c2036dc5549eaca1c7a51d84e020018a6a089f`
- Started: 2026-08-21
- Last updated: 2026-08-21
- Planned archive: `docs/exec-plans/completed/launcher-directory-main-publication.md`

## Goal and Done Condition

Goal: rename the tracked top-level `installer/` directory to `launcher/`, keep
the verified launcher and engine behavior intact, and publish the complete
candidate to GitHub `main` as a direct fast-forward.

Done when:

- [x] The publication tree contains the launcher project under `launcher/`; the only tracked
  `installer/` path is the permanent byte-identical legacy feed endpoint.
- [x] Renderer, launcher, identity, packaging, documentation, and rename-specific tests pass after the migration.
- [x] The publication tree is a fast-forward descendant of the freshly fetched
  `origin/main`; direct push and remote verification follow creation of the
  commit containing this archived plan.
- [x] Required post-push GitHub Actions and raw-feed checks are mandatory
  closeout evidence in the task handoff; any failure reopens this plan.

## Scope

In scope:

- Move every tracked and task-owned source file from `installer/` to `launcher/`,
  retaining only the released launcher's legacy feed endpoint.
- Update repository-relative paths, raw GitHub URLs, workflow filters and commands,
  scripts, tests, links, ignore rules, attributes, and current documentation.
- Advance launcher version, release sequence, and locked input identity if the
  path migration changes launcher build inputs.
- Rebuild, commit, push directly to GitHub `main`, and inspect resulting Actions.

Non-goals:

- Renaming the internal `UVSR.Installer` namespace, assembly, persisted
  `%LOCALAPPDATA%\UVSR Installer` data directory, or historical prose that
  intentionally describes the former installer product.
- Publishing a signed launcher release, updating the public launcher feed, or
  changing renderer behavior beyond rename-required repairs.
- Modifying or deleting ignored historical build artifacts left under the local
  former directory.

Affected subsystems and paths:

- `installer/**` to `launcher/**`, the permanent legacy feed mirror,
  `.github/workflows/**`, `.gitignore`, `.gitattributes`, root and launcher
  documentation, legal links, and current source contracts. Completed plans
  retain their historical paths.

Shared hotspots reserved for the coordinator:

- The directory move, all bulk path rewrites, launcher identity files,
  `README.md`, workflows, staging, commit, push, and build/runtime resources.

## Baseline

- Canonical repository/remote: freshly fetched `origin/main` is exactly
  `42c2036dc5549eaca1c7a51d84e020018a6a089f`.
- Local versus remote state: the feature worktree is based exactly on remote
  `main` with a verified dirty candidate. The separate local `main` worktree is
  at local-only `cf3e55c0c331e6d33e5771410762e4492f7c4890` and will not be changed.
- Verified source/build before rename: renderer SHA-256
  `DF6E07245F0E6A86BE5B0DECF8879B5B19422B3624E83DC8B1B0928C1055A400`,
  launcher 1.1.11 sequence 12 SHA-256
  `4D0593732530C4D584DD4F7651442AB048A576DDA07D72E0754B4FBBC7D17437`,
  50 of 50 native and 98 of 98 launcher tests.
- GPU, scene, camera, resolution, and settings preset when relevant: prior
  verified font-selector smoke on Sponza Decorated, Position 1, 1920 x 1080.
- Known pre-existing failures: none; open pull-request list is empty and the
  three latest `main` workflows for the base commit succeeded.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Rename inventory | Exact live path replacements versus historical prose | Complete | Coordinator migration |
| Launcher identity audit | Required version/sequence/lock advance | Complete | Launcher build and publication |
| Publication audit | Live remote ancestry, PR, and Actions state | Complete locally; remote result in final handoff | Direct fast-forward push |

Public interface and compatibility contracts:

- The public feed URL moves from `/installer/launcher-feed-v1.json` to
  `/launcher/launcher-feed-v1.json`; the final launcher uses the new canonical
  URL while the old URL remains a byte-identical permanent compatibility mirror
  for already-released launchers.
- Internal namespaces, manifest schema, installed-state paths, and launcher
  product names remain compatible.
- The frozen renderer bridge content and identity remain byte-for-byte stable.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| RENAME-AUDIT-1 | `/root/bridge_patch_audit` | Shared/read-only | Frozen dirty candidate | None | None | Complete |
| RENAME-GUARDS | `/root/bridge_identity_design` | Shared | Frozen dirty candidate | Identity verifier, feed mirror guard, build/workflow metadata | Audit contract | Complete |
| RENAME-TESTS-IDENTITY | `/root/bridge_implementer` | Shared | Frozen dirty candidate | Launcher metadata and tests | Audit contract | Complete |
| RENAME-INTEGRATE-1 | `/root` | Shared | Frozen dirty candidate | All migration and publication paths | All audits | Complete |

## Integration Order

1. Freeze the path-replacement and identity contracts from the read-only audits.
2. Move source files without deleting ignored historical artifacts and rewrite
   only live repository path references.
3. Advance and regenerate launcher identity, then run rename-focused checks.
4. Rebuild and reverify the complete launcher and native matrices.
5. Independently review, commit with an approved lowercase subject, verify live
   remote ancestry again, and push `HEAD:main` as a fast-forward.
6. Monitor the pushed commit's required GitHub Actions and repair only failures
   introduced by this publication when necessary.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Complete path migration | Launcher project under `launcher/**`; only the exact legacy feed remains under `installer/**` | Git inventory, feed mirror verifier, and scoped searches | Passed; committed old/new feeds are 321 identical LF-normalized bytes, SHA-256 `5689fc9d8856c503e5ff48d091825ff59290065986bb915299dfbcc0b96bcd7d` |
| Launcher correctness | Full launcher build, tests, health, metadata, checksum | Renamed `launcher/build.ps1` | Passed from committed code SHA `639fd74f`: 98/98 tests, health exit 0, and launcher SHA-256 `df775ca75cbb54c534f109553299b644cf593237fc7cfb98e561c4c75c1d472f` |
| Renderer correctness | Release build and complete CTest | Isolated current renderer build | Passed from committed code SHA `639fd74f`: 50/50 tests, exact package validation, and renderer SHA-256 `8b53f9c72ed1c04d0f83f68c24b48df0d1923e80faa41164dcdd4a93f62c4497` |
| Identity safety | Unique version/sequence and exact input lock | Renamed verifier against `42c2036` | Passed; 1.1.12 sequence 13, lock `cf44419e0f933a6fb57b35c94cb2b27fbd402346763897074ee6fb91236d861d` |
| Repository hygiene | Title Case, counts, legal, diff, and status checks | Repository validators | Passed before final staging; final staged checks remain mandatory |
| Publication | Remote main exactly equals verified commit and Actions pass | Git and GitHub inspection | Required immediately after the publication commit; result recorded in task handoff |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-21 | Publish directly to `main` | The user explicitly authorized this destination; no PR is open and remote `main` equals the candidate base. | Publication |
| 2026-08-21 | Preserve the separate local `main` worktree | It contains a local-only divergent commit and is not needed for a fast-forward `HEAD:main` push. | Publication |
| 2026-08-21 | Rename only the repository directory contract | The request names the GitHub folder; broad namespace or persisted-data migration would add unrelated compatibility risk. | Rename |
| 2026-08-21 | Retain one legacy feed file under `installer/` | Released sequence-2 launchers compile the old raw URL and GitHub raw content has no path redirect. The old and canonical files are ordinary, byte-identical, and guarded by build, tests, and CI. | Rename, updates |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-21 | `/root` preflight | Complete | Base `42c2036` | Fetch, ancestry, worktrees, open PRs, recent Actions | Await bounded audits |
| 2026-08-21 | Rename audits | Complete | Exact 35-file move plus three task-owned new launcher files | Three bounded audits and cross-review | Legacy feed exception adopted |
| 2026-08-21 | `/root` integration | Complete | Launcher 1.1.12 sequence 13 | Feed positive/negative tests, bridge verifier, input lock, 98/98 launcher tests, 50/50 native tests, package validation, health check, docs validators | Stage, commit, reverify exact commit, and push |

## Risks and Escalation Triggers

- Stop before push if live `origin/main` advances or diverges; refresh and
  integrate without overwriting either history.
- Preserve all ignored local build artifacts and unrelated worktree state.
- Do not leave the feed URL pointing at the removed public path.
- Never remove or independently edit the legacy feed endpoint compiled into the
  released sequence-2 launcher.
- Any source or artifact change after product verification requires rebuilding
  and re-establishing the affected evidence before publication.

Stop and ask the user only if the live target changes to an incompatible product
outcome or direct publication requires destructive history rewriting. Mechanical
rename conflicts and identity updates remain coordinator responsibilities.

## Completion

- Final integrated commit: the direct-main publication commit containing this
  archived plan; exact SHA is reported after creation in the task handoff.
- Verification summary: 98/98 launcher tests, 50/50 native tests, package and
  health checks, feed positive/negative tests, identity and bridge checks, Title
  Case, README counts, legal inventory, and diff hygiene passed before staging.
- Independent review: three final read-only reviews completed; all P0-P2
  findings were repaired and rechecked.
- Coming Soon/documentation update: active UI/font entry removed; unsigned
  launcher release status and legacy feed endpoint are documented accurately.
- Pushed/PR/merged, or intentionally local: direct `main` fast-forward
  authorized; remote SHA and Actions result are recorded in the task handoff.
- Remaining experiments or follow-ups: signing, release asset creation, and
  feed identity publication remain separate release work.
- Active ownership released: yes.
- Archived to completed/abandoned path: completed publication plan.
