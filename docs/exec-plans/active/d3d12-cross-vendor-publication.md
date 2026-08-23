# D3D12 Cross-Vendor Publication

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/d3d12-cross-vendor-startup` in `work/d3d12-cross-vendor-startup`
- Base commit: `13bd1f2ce9afb344cfe9e4b3b611ee8fe599bacd`
- Started: 2026-08-22
- Last updated: 2026-08-22
- Planned archive: `docs/exec-plans/completed/d3d12-cross-vendor-publication.md`

## Goal and Done Condition

Goal: publish the reviewed D3D12 portability repair and deliver the exact tested
Xe3 package to Grant without changing the immutable launcher release contract.

Done when:

- [ ] The task-owned source is committed and the feature branch is pushed.
- [ ] The exact `3D9B359F53F487D4307E6477A8F584F9902EA6F69BD3E7C0BEBE0A3CD5BF079E` executable is preserved in a validated portable ZIP.
- [ ] The ZIP is uploaded to Google Drive, shared with `grantliddi@gmail.com`, and its verified link is emailed with launch and log instructions.
- [ ] Public-`main` integration is completed through the protected pull-request workflow if separately authorized.

## Scope

In scope:

- Commit and push the reviewed renderer repair on its established feature branch.
- Package the exact tested executable with its required D3D12 runtime, shaders,
  notices, licenses, and media.
- Upload and share the test package, then email Grant.
- Preserve the existing immutable UVSR Launcher v1.1.14 download URL.

Non-goals:

- A new launcher binary, tag, release, feed revision, or download URL.
- Direct pushes to protected `main`.
- Rebuilding or changing the exact user-named test executable.

Affected subsystems and paths:

- Reviewed task-owned source and documentation already recorded in
  `docs/exec-plans/completed/d3d12-cross-vendor-startup.md`.
- Git-ignored publication artifacts under `work/xe3-test-3D9B359F/`.
- GitHub feature branch, Google Drive test artifact, and one Gmail message.

Shared hotspots reserved for the coordinator:

- Git index and refs, `README.md`, both execution plans, package staging, and all
  external publication actions.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`, protected `main`.
- Local versus remote state: task HEAD and live `origin/main` both
  `13bd1f2ce9afb344cfe9e4b3b611ee8fe599bacd`; the task branch has reviewed uncommitted changes and no remote branch.
- Verified source commit/build: exact reviewed dirty diff over the base; Release executable SHA-256 `3D9B359F53F487D4307E6477A8F584F9902EA6F69BD3E7C0BEBE0A3CD5BF079E`.
- Known pre-existing failures: no Xe3 hardware is available locally; Grant's Galaxy Book 6 is the external acceptance machine.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| UVSR Launcher | Existing immutable v1.1.14 release; renderer source follows public `main` | Confirmed | Fresh install/update |
| GitHub `main` | Protected pull request plus strict `Launcher README Download` check | Pending separate authorization | Launcher update path |
| Portable package | Installer payload layout from `PayloadPackager.cs` | In progress | Grant's Xe3 test |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Package root contains `bin/uvsr.exe`, `bin/D3D12`, `bin/shaders`,
  `bin/licenses`, `bin/third-party-notices.md`, and `media`.
- The existing launcher download link remains unchanged because this is an
  engine-source update, not a launcher release.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Publication coordination | `/root` | Current task worktree | `13bd1f2c` | Exact staging, commit, push, package, upload, email | Read-only audits | Active |
| GitHub provenance audit | `portability_research` | Shared, read-only | `13bd1f2c` | None | Live remote | Complete |
| Launcher/test audit | `test_audit` | Shared, read-only | `13bd1f2c` | None | Repository contracts | Complete |
| Package inventory audit | `startup_trace` | Shared, read-only | `13bd1f2c` | None | Build outputs | Active |

## Integration Order

1. Freeze and commit the reviewed task paths, then push the feature branch.
2. Preserve and validate the exact test runtime package.
3. Upload, share, and email the package.
4. If authorized, open and merge the protected pull request after its required check.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact executable | SHA-256 equals the user-named value before and after staging | `Get-FileHash` | Pending |
| Source publication | Remote feature ref equals the local commit | `git ls-remote` | Pending |
| Portable ZIP integrity | Required inventory present and archive test passes | package contract check plus `7z t` | Pending |
| Drive delivery | Uploaded-file metadata and sharing readback | Google Drive connector | Pending |
| Email delivery | Gmail send result | Gmail connector | Pending |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-22 | Keep the v1.1.14 launcher URL unchanged | The launcher already builds newest public `main`; replacing an immutable launcher link would create an unrelated release | GitHub publication |
| 2026-08-22 | Use Drive rather than a Gmail attachment | The complete runtime package is approximately 1.36 GB uncompressed, far above Gmail's attachment limit | Tester delivery |
| 2026-08-22 | Push the feature branch first | Protected `main` requires a pull request, and the current request does not grant a direct-push exception | GitHub publication |

## Risks and Escalation Triggers

- A partial archive that omits `media` can launch incorrectly or fail on a fresh machine.
- Any rebuild would change the exact executable hash requested by the user.
- Feature-branch publication alone does not make the launcher consume the repair.

Stop and ask the user if:

- Opening and merging a new protected pull request remains necessary after the
  authorized feature-branch push, because repository policy requires separate authorization.

## Completion

- Final integrated commit: pending
- Verification summary: pending
- Independent review: renderer diff already reviewed clean; publication audits complete
- Coming Soon/documentation update: renderer documentation already included in the reviewed diff
- Pushed/PR/merged, or intentionally local: pending
- Remaining experiments or follow-ups: external Xe3 acceptance
- Active ownership released: pending
- Archived to completed/abandoned path: pending
