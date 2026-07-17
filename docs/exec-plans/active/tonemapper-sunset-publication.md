# Tonemapper Sunset Publication

This plan resumes the completed implementation recorded in
`docs/exec-plans/completed/tonemapper-drawer-sunset.md` and tracks independent
review, GitHub integration, canonical reverification, and launch.

## Status

- State: integration
- Coordinator: primary Codex agent
- Project/integration branch and worktree:
  `codex/tonemapper-sunset` at
  `C:\Users\brock\Documents\Codex\2026-07-17\ive\work\uvsr-tonemapper-sunset`
- Base commit: `5f43205ecfe00e31fd64af34cad0f031472a224c`
- Started: 2026-07-17
- Last updated: 2026-07-17
- Planned archive:
  `docs/exec-plans/completed/tonemapper-sunset-publication.md`

## Goal and Done Condition

Goal: Publish the accepted tonemapper drawer and LUT sunset, integrate it into
live `main`, reverify the exact merged commit as the canonical contender, and
launch that build.

Done when:

- [ ] An independent reviewer finds no integration-blocking rendering,
  packaging, deletion, or restoration-contract defects.
- [ ] The exact task-owned diff is committed and published through a pull
  request to live `main`.
- [ ] The pull request is merged with the repository-required lowercase merge
  subject.
- [ ] The exact merged commit passes the relevant build, tests, documentation
  audit, and runtime smoke check.
- [ ] The exact merged build is left running with an identifiable experiment
  label.

## Scope

In scope:

- The already accepted sunset implementation and its durable restoration
  archive.
- Independent read-only review.
- Commit, push, pull request, merge, canonical build/test verification, and
  runtime launch.

Non-goals:

- Restoring the tonemapper drawer in this publication.
- Implementing the future bilateral-grid local tonemapper now.
- Modifying the separate ambient-occlusion experiment or its worktree.

Affected subsystems and paths:

- Renderer CPU and shader paths in `src/`.
- Runtime packaging and LUT assets in `CMakeLists.txt` and `assets/luts/kodak/`.
- Restoration, postmortem, roadmap, and agent guidance under `docs/`,
  `README.md`, and `AGENTS.md`.
- Documentation checker behavior in `tools/check_document_title_case.py`.

Shared hotspots reserved for the coordinator:

- Git index and branch history.
- GitHub pull request and merge state.
- Build directories and UVSR runtime windows.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, `origin/main`
- Local versus remote state: candidate base and live `origin/main` are both
  `5f43205ecfe00e31fd64af34cad0f031472a224c`; a separate
  `codex/ao-performance-optimization` worktree is out of scope.
- Verified source commit/build: the accepted uncommitted candidate on base
  `5f43205ecfe00e31fd64af34cad0f031472a224c` passed the Release build, all 11
  registered tests, documentation audits, restoration round-trip, and a
  renderer smoke test.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090, PBR Sponza Decorated default launch state.
- Known pre-existing failures: none in the relevant final candidate checks.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| User acceptance and publication authority | Exact accepted candidate plus explicit merge and launch instruction | complete | publication |
| Live canonical base | `origin/main` at `5f43205ecfe00e31fd64af34cad0f031472a224c` | complete | pull request |
| Independent review | Read-only review of the frozen candidate | complete with one packaging repair | merge |
| GitHub integration | Merged pull request commit | pending | canonical verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Active rendering keeps the neutral AgX display transform while removing the
  optional drawer, grading state, LUT bindings, discovery, and packaging.
- The restoration archive must remain hash-verifiable and apply cleanly.
- A future request to restore the drawer must restore the global drawer/LUT
  feature and implement the bilateral-grid local tonemapper in one candidate.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| REVIEW-1 | independent reviewer agent | shared candidate worktree | base plus frozen candidate diff | none | accepted candidate | complete |
| INTEGRATE-1 | primary Codex agent | `codex/tonemapper-sunset` | base plus repaired candidate diff | task-owned paths and Git/GitHub state | REVIEW-1 | active |
| VERIFY-1 | primary Codex agent | clean merged-commit worktree | merged `origin/main` | build products and runtime process only | INTEGRATE-1 | pending |

## Assignment Contracts

### Review-1: Review the Frozen Sunset Candidate

- Owner/thread: independent reviewer agent
- Branch/worktree: shared `codex/tonemapper-sunset` worktree
- Base commit/state: `5f43205ecfe00e31fd64af34cad0f031472a224c`
  plus the frozen unstaged candidate diff
- Read scope: all candidate changes, adjacent renderer/shader/build contracts,
  restoration manifest, patch, and postmortem
- Write scope: none
- No-touch scope: all files, Git index/history, submodules, build products,
  GitHub state, and runtime windows
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: accepted implementation candidate
- Interface/invariant contract: neutral AgX remains active; removed global
  controls and LUT paths leave no mismatched CPU/HLSL/package bindings; the
  restoration archive is coherent; future restoration is paired with the local
  tonemapper
- Deliverable: findings ordered by severity with file/line evidence, or an
  explicit no-findings conclusion plus residual risks
- Done when: the full candidate and high-risk contracts have been reviewed
- Required verification: read-only diff and targeted source/package/archive
  inspection; no build required
- Allowed Git and external actions: read-only local Git commands only
- Stop and report if: the candidate changes during review, an unexpected
  unrelated diff is found, or verification would require mutation
- Handoff revision/artifact: read-only findings report against tracked diff hash
  `ef5b544b7a6277145540be8eb1ba47ec5f04acd3`
- Handoff acknowledged by/on: primary Codex agent on 2026-07-17

### Integrate-1: Publish and Merge the Accepted Candidate

- Owner/thread: primary Codex agent
- Branch/worktree: `codex/tonemapper-sunset`
- Base commit/state: frozen candidate after REVIEW-1
- Read scope: repository and GitHub state
- Write scope: task-owned candidate files, Git index/history, feature branch,
  and its pull request
- No-touch scope: separate ambient-occlusion worktree and unrelated pull
  requests
- Build directory and runtime/GPU/resource lease: existing candidate build tree
  only until publication
- Dependencies already integrated: accepted candidate
- Interface/invariant contract: preserve the reviewed diff exactly apart from
  this coordinator-owned plan lifecycle record
- Deliverable: committed, pushed, reviewed, and merged pull request
- Done when: live `main` contains the accepted candidate through the required
  merge commit
- Required verification: staged-diff audit, relevant local checks, pull-request
  checks, and live-target identity refresh
- Allowed Git and external actions: explicit commit, push, pull request, and
  merge authority from the user
- Stop and report if: live `main` changes incompatibly, review reports a
  blocking defect, or GitHub checks fail for a candidate-caused reason
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Verify-1: Verify and Launch the Merged Canonical Contender

- Owner/thread: primary Codex agent
- Branch/worktree: clean worktree created at merged `origin/main`
- Base commit/state: pending merge commit
- Read scope: exact merged source and runtime output
- Write scope: isolated build products and the launched contender process
- No-touch scope: every other worktree, build directory, and UVSR runtime window
- Build directory and runtime/GPU/resource lease: isolated contender build and
  one `canon`-labeled UVSR window
- Dependencies already integrated: INTEGRATE-1
- Interface/invariant contract: verification and launch must use the exact merge
  commit, not the pre-merge feature commit
- Deliverable: passing evidence and a running identifiable UVSR window
- Done when: build, tests, audits, runtime smoke, and launch succeed
- Required verification: Release renderer build, all registered tests,
  documentation audits, clean source status, and visual runtime inspection
- Allowed Git and external actions: read-only canonical checkout plus local
  build and launch
- Stop and report if: the merged source differs from the accepted candidate,
  build/tests fail, or another process owns the intended build/window
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

## Integration Order

1. Freeze and independently review the candidate.
2. Reinspect, stage, commit, push, and open the pull request.
3. Pass pull-request checks and merge.
4. Refresh live `main`, create an isolated merged-commit worktree, and reverify.
5. Launch and leave open the exact verified merged build.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Candidate is integration-safe | independent findings report | read-only reviewer handoff | complete after retaining remove-only stale-LUT cleanup and reverifying the repaired state |
| Task-only commit | staged status and cached diff inspection | `git status`; `git diff --cached` | pending |
| Repository tests pass | all registered tests pass | CMake build plus CTest Release | repaired candidate Release build passed; CTest 11/11; merged rerun pending |
| Documentation remains conformant | self-test and full audit pass | `tools/check_document_title_case.py` | repaired candidate self-test passed; 294 headings and bold lead-ins passed; merged rerun pending |
| Restoration remains durable | manifest hash, apply check, and contract inspection pass | PowerShell hash plus `git apply --check` | repaired archive SHA-256 `e5e85b69d5cf8a5f6ab5cf3933f63b98d765e113df68d4606b4f72f774e5e93a`; all eight pre-sunset and three post-sunset blobs matched in the full round trip |
| Legacy LUT packages are scrubbed | a pre-existing stale directory is absent after an incremental build | seed `build/media/luts/kodak`; build Release `uvsr` | pass; post-build cleanup removed the seeded directory |
| Integrated state is canonical | live `origin/main` equals merge commit and worktree is clean | Git remote and status inspection | pending |
| Exact contender launches | labeled window loads expected renderer without the drawer | `tools/launch_uvsr.ps1 -Experiment canon` | repaired pre-merge smoke `tonerepair-5f43205-0222` passed; exact merged launch pending |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-17 | Treat “merge and make canon contender then launch” as acceptance and full publication authority for the exact candidate | The repository short-follow-up agreement explicitly binds “merge it” to the accepted candidate and authorizes the push/PR/merge chain | all |
| 2026-07-17 | Use a clean merged-commit worktree for canonical reverification | The feature worktree will not contain the GitHub merge commit, and the ambient-occlusion worktree is active and out of scope | VERIFY-1 |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-17 | implementation/primary Codex agent | complete | frozen candidate on `5f43205ecfe00e31fd64af34cad0f031472a224c` | build, 11/11 tests, docs, archive round-trip, runtime smoke | independent review |
| 2026-07-17 | publication preflight/primary Codex agent | complete | live `origin/main` at `5f43205ecfe00e31fd64af34cad0f031472a224c` | GitHub auth, worktrees, open pull requests, base identity | start REVIEW-1 |
| 2026-07-17 | REVIEW-1/independent reviewer agent | complete | findings against tracked diff hash `ef5b544b7a6277145540be8eb1ba47ec5f04acd3` | CPU/HLSL bindings coherent; archive blobs and hash matched; found stale-package cleanup blocker and coordinator Title Case defects | retain remove-only LUT cleanup, regenerate archive, correct headings, and reverify |
| 2026-07-17 | review repair/primary Codex agent | complete | repaired candidate | seeded stale LUT package removed by incremental build; Release build and CTest 11/11; archive full round trip; 294-heading audit; `tonerepair-5f43205-0222` visual smoke | stage exact task-owned diff |

## Risks and Escalation Triggers

- Removing a shader/resource binding on only one side could leave a latent
  renderer contract mismatch.
- Runtime LUT files could remain packaged despite source removal.
- The archival patch could drift from its recorded manifest or restoration
  instructions.
- Live `main` could move while publication is in progress.

Stop and ask the user if:

- A live canonical change creates a material product conflict that cannot be
  mechanically integrated.
- The independent review finds a product-level defect requiring an
  artifact-changing repair after acceptance.
- Publication or merge authority is rejected by GitHub and requires a new user
  decision.

## Completion

- Final integrated commit: pending
- Verification summary: pending
- Independent review: REVIEW-1 complete; one stale-package cleanup blocker was
  repaired and the repaired state was reverified
- Coming Soon/documentation update: already included in the accepted candidate;
  final reconciliation pending
- Pushed/PR/merged, or intentionally local: pending
- Remaining experiments or follow-ups: future paired global drawer/LUT and
  bilateral-grid local tonemapper restoration only when explicitly requested
- Active ownership released: pending
- Archived to completed/abandoned path: pending
