# Canonicalize Engine Core Cleanup

## Status

- State: integration
- Coordinator: `/root`
- Project branch and worktree: `codex/engine-core-cleanup` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\engine-core-cleanup`
- Integration branch and worktree: `codex/engine-core-cleanup`, rooted directly
  at live `origin/main`; publication targets `origin/main`
- Live remote base: `f7c0c87d8cba6880428fbc34400eb2882fb5182e`
- Started: 2026-08-03
- Last updated: 2026-08-03
- Planned archive:
  `docs/exec-plans/completed/canonicalize-engine-core-cleanup.md`

## Goal and Done Condition

Goal: publish the user-accepted engine-core cleanup and restored front end to
GitHub `main`, preserve valid local policy history, reverify the clean integrated
renderer, and establish the resulting renderer checkpoint as UVSR's newest
Canonical verified build.

Done when:

- [ ] The accepted dirty source is captured in a focused feature commit without
      generated build output or unrelated worktree state.
- [ ] Valid local policy semantics are transplanted without publishing the
      contaminated local `main` ancestry, importing superseded planning bloat,
      or reviving superseded README behavior.
- [ ] The integrated renderer receives a fresh clean Release build, complete
      shader-package verification, all registered tests, document validation,
      and independent review.
- [ ] The exact integrated checkpoint and executable are recorded in a durable
      Canonical-verification record.
- [ ] `main` is pushed without history rewriting and a fresh remote query proves
      the expected SHA is live.

## Scope

In scope:

- The complete accepted `codex/engine-core-cleanup` source, shader, build, test,
  UI, command, documentation, and dated-report diff.
- The user-accepted executable at
  `build-custom-default-copy\bin\uvsr.exe`, SHA-256
  `194D1637CC271330D3CC3C800F723639D7A17ABEBB97DE93DE018C79752D11EF`.
- Semantic transplantation of the valid exact-build-link policy from local
  commit `5e300c9`; its superseded historical plan, the generated-build ancestry
  of `891f4f0`, and that commit's superseded README remain excluded.
- README Coming Soon closeout, execution-plan lifecycle, exact artifact records,
  and Canonical-verification documentation.
- Direct publication to `origin/main`, explicitly authorized by the user.

Non-goals:

- Merging open pull requests #10 or #11.
- Reviving or modifying the superseded Bend Screen-Space Shadows or Emissive
  Motion Lights experiments; only their stale plan lifecycle is reconciled.
- Reviving sparse virtual shadows, diagnostic cascaded shadows, forward PBR,
  visibility history, recursive diffuse, sample resurrection, benchmark
  planners, or removed shader families.
- Performance claims or a new controlled GPU benchmark.
- Deleting unrelated worktrees, branches, builds, or media.

Affected subsystems and paths:

- `AGENTS.md`, `CMakeLists.txt`, `README.md`
- retained first-party renderer, shader, settings, and command paths under
  `src/`
- focused and package contracts under `tests/`
- current renderer guidance, completed plans, and dated cutdown reports under
  `docs/`

Shared hotspots reserved for the coordinator:

- Git index and refs, `main`, the feature branch, README, AGENTS policy, root
  build files, shader manifests, execution plans, reports, build trees, and any
  UVSR process.

## Baseline

- Live GitHub `main`: `f7c0c87d8cba6880428fbc34400eb2882fb5182e`.
- Feature base: exactly equal to live `origin/main`.
- Local `main`: `5e300c91e253441edceecd527cdf0161e2b97820`, two
  local commits ahead of merge base `a4f9bf0` and fifteen live commits behind.
- Local commit `891f4f0` changes 6,427 paths and contains generated build trees,
  including a `116916088`-byte shader binary. Neither local commit exists on a
  remote ref. Publishing that ancestry would violate repository policy and the
  GitHub file-size limit, so the root `main` worktree remains untouched and
  recoverable.
- Accepted candidate artifact: `2516480` bytes; SHA-256
  `194D1637CC271330D3CC3C800F723639D7A17ABEBB97DE93DE018C79752D11EF`.
- Accepted artifact-producing build-input diff:
  `8a2a64d412d19828567b159dca64363f7dfa843e`, with zero untracked build
  inputs. The post-acceptance launcher cleanup supersedes the full working-tree
  identity but does not change renderer inputs or executable bytes.
- Accepted candidate verification: clean Release compilation in its isolated
  build tree, exact 39-file runtime shader package, 30 of 30 tests, README count
  checks, 1,034 Title Case headings and bold lead-ins with zero violations, and
  `git diff --check` with line-ending warnings only.
- Product acceptance: the user stated that this exact most recent build is
  verified and explicitly requested publication to `main` as the new Canonical
  verified build. This acceptance is scoped to the requested cleanup and UI
  behavior. The observed scene, camera, resolution, skin, and settings were not
  recorded for this artifact, so no broader exact-scene visual-quality or
  performance claim is attached to it.
- Post-acceptance non-binary cleanup removed the launcher's unused experiment
  label. Only `LaunchUVSR.cmd`, `tools/launch_uvsr.ps1`, policy, and restoration
  guidance changed; renderer source and executable bytes remain unchanged. A
  new full-source identity and clean canonical build will own final evidence.
- Final pre-commit counted source: 64,611 first-party, 388,207 third-party, and
  452,818 total nonblank lines; `src/uvsr.cpp` has 17,306 physical lines. The
  current build-input diff is
  `d0b7279b3242493ebbdb1b4fbaa1e7f72ae4b77e`, with zero untracked build
  inputs.
- Known pre-existing failures: none in the accepted candidate's registered
  suite.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live remote base | `origin/main` at `f7c0c87` | Confirmed | Integration |
| Accepted candidate | Exact dirty source that produced SHA-256 `194D1637...` | Frozen for commit | Integration |
| Local main policy | Transplant exact-build-link policy and retired experiment-build guidance removal without its ancestry | Complete | Feature commit |
| Retired launcher label | Remove unused `UVSR_EXPERIMENT` interface while preserving exact build-directory and commit identity | Complete | Canonical smoke |
| Superseded local README | Preserve compact intent only where the accepted README already implements it | Excluded by audit | Feature commit |
| Independent deletion review | No unresolved priority-zero through priority-two renderer or packaging issue | Pending final handoff | Publication |

Public contracts:

- DirectX 12 remains the only product backend.
- The retained renderer is deferred PBR, current-frame screen-space visibility,
  Screen-Space Directional Shadows, and independently composable Temporal
  Reconstructive, Conservative Morphological, and Multisample Reference paths.
- Visibility, PBR, debug views, shadows, and aliasing ownership remain decoupled.
- Quality, Temporal Cost, and Visibility profile Custom/reset behavior remains
  exactly as accepted; dropdown option lists contain no provenance duplicates.
- The runtime shader package remains the exact 39-file contract produced by 268
  core and 46 Screen-Space Directional Shadow shader tasks plus 76 Donut tasks.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Integration topology audit | `/root/taa_ui_cleanup_review` | Read-only repository | Current refs | None | Live remote | Complete |
| Cleanup diff ownership audit | `/root/taa_ui_copy_audit` | Read-only feature worktree | Dirty accepted state | None | Candidate freeze | Complete |
| Canonical protocol audit | `/root/fidelity_report_review` | Read-only repository | Current docs and refs | None | Prior records | Complete |
| Plan lifecycle audit | `/root/plan_lifecycle_audit` | Read-only feature worktree | Current plans and deleted base records | None | Ownership audit | Complete |
| Commit, integration, verification, and publication | `/root` | Feature and isolated verification worktree | `f7c0c87` plus accepted diff | All integration resources | Audit handoffs | In progress |

## Integration Order

1. Freeze and audit the accepted diff, untracked documents, build-input identity,
   and current remote state.
2. Transplant the valid local policy, archive superseded plans without importing
   redundant local history, then commit the complete accepted feature in its
   owning worktree.
3. Reconcile Coming Soon and durable publication documentation without changing
   renderer behavior.
4. Verify the clean integrated renderer from a new isolated worktree and build
   tree.
5. Push the renderer checkpoint to `origin/main`, confirm the live SHA, publish
   its documentation-only Canonical record, and confirm the final live remote.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Exact candidate captured | Clean focused commit, tree and build-input identities | Git status, staged diff, tree and hash audit | Pending |
| Integration preserves intent | Parentage, semantic conflict review, accepted renderer-path comparison | Git graph, diff, source contract review | Pending |
| Release build | Fresh configure and all-target x64 Release build | CMake and MSBuild in isolated canonical tree | Pending |
| Shader package | 268 core, 46 retained shadow, 76 Donut tasks; exact 39 files | Build logs and package contract | Pending |
| Registered tests | Every CTest target passes | `ctest --output-on-failure` | Pending |
| Runtime smoke | Exact integrated executable starts on D3D12 and remains responsive without disturbing another UVSR owner | Isolated launch and process/window identity | Pending |
| Documentation policy | Line-count self-test/check, Title Case self-test/full scan, diff check | Repository tools | Pending |
| Independent review | No unresolved priority-zero through priority-two finding | Read-only frozen integration review | Pending |
| Publication | Live `origin/main` equals the expected renderer and final documentation commits | Fresh fetch and `ls-remote` | Pending |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-03 | Bind product acceptance to executable SHA-256 `194D1637...` | It is the exact most recent build the user verified; no older candidate may substitute | All |
| 2026-08-03 | Exclude local `main` ancestry and transplant only valid semantics | `891f4f0` contains generated build trees and a 116.9 MB blob that cannot be published; leaving the root worktree untouched preserves recovery while the exact-build-link rule is copied safely and its redundant historical plan stays local | Integration |
| 2026-08-03 | Archive five superseded plans with their complete evidence | Their done conditions remain unmet, but deleting failure, benchmark, and recovery evidence would make future restoration less safe; archiving does not revive runtime code | Documentation |
| 2026-08-03 | Leave PRs #10 and #11 outside this publication | They have separate owners and are not required by the accepted renderer | Integration |
| 2026-08-03 | Treat performance benchmarking as out of scope | The cleanup claims reduced code and permutations, not a matched frame-time improvement | Verification |

## Risks and Escalation Triggers

- Stop if live `origin/main` moves after the freeze; refresh and reintegrate
  before any push.
- Stop if the ownership audit identifies an unrelated user or peer modification
  inside the candidate diff.
- Stop if semantic integration changes renderer, shader, settings, asset, test,
  or runtime UI content from the accepted feature without a documented repair
  and renewed product acceptance.
- Stop if another coordinator claims the `main` publication lease.
- Stop if the clean integrated build, package contract, tests, runtime smoke, or
  independent review fails.

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-03 | `/root` candidate acceptance | Complete | SHA-256 `194D1637...` | 30 of 30 tests and full candidate hygiene passed | Freeze source and commit |
| 2026-08-03 | `/root` live-target audit | Complete | `origin/main` `f7c0c87`; local `main` `5e300c9` | Fresh fetch, graph, worktrees, PRs, and Coming Soon inspected | Await ownership reviews |
| 2026-08-03 | Read-only publication audits | Complete | Topology, ownership, protocol, launcher, and plan-lifecycle handoffs | No cleanup-diff P0; launcher label retired; stale plans archived; contaminated local main excluded | Freeze final source and commit |
| 2026-08-03 | `/root` final pre-commit hygiene | Complete | Build-input diff `d0b7279...`; 64,611 first-party lines | README self-test/check, 1,121-heading Title Case scan, launcher audit, and diff check pass | Stage exact task paths and commit |

## Completion

- Final integrated commit: pending
- Verification summary: pending
- Independent review: pending
- Coming Soon and documentation update: pending
- Publication: explicitly authorized; pending
- Remaining external work: PRs #10 and #11 remain separate; Bend, Emissive,
  SVSM, and diagnostic CSM plan evidence is archived as superseded
- Active ownership released: no
- Archived path: pending
