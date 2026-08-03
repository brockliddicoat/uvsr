# Canonicalize Engine Core Cleanup

## Status

- State: completed
- Coordinator: `/root`
- Project branch and worktree: `codex/engine-core-cleanup` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\engine-core-cleanup`
- Integration branch and worktree: `codex/engine-core-cleanup`, rooted directly
  at live `origin/main`; publication targets `origin/main`
- Live remote base: `f7c0c87d8cba6880428fbc34400eb2882fb5182e`
- Started: 2026-08-03
- Last updated: 2026-08-03
- Completed: 2026-08-03
- Archived path:
  `docs/exec-plans/completed/canonicalize-engine-core-cleanup.md`

## Goal and Done Condition

Goal: publish the user-accepted engine-core cleanup and restored front end to
GitHub `main`, preserve valid local policy history, reverify the clean integrated
renderer, and establish the resulting renderer checkpoint as UVSR's newest
Canonical verified build.

Done when:

- [x] The accepted dirty source is captured in a focused feature commit without
      generated build output or unrelated worktree state.
- [x] Valid local policy semantics are transplanted without publishing the
      contaminated local `main` ancestry, importing superseded planning bloat,
      or reviving superseded README behavior.
- [x] The integrated renderer receives a fresh clean Release build, complete
      shader-package verification, all registered tests, document validation,
      and independent review.
- [x] The exact integrated checkpoint and executable are recorded in a durable
      Canonical-verification record.
- [x] `main` is pushed without history rewriting and a fresh remote query proves
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
- Final pre-commit counted source: 64,615 first-party, 388,207 third-party, and
  452,822 total nonblank lines; `src/uvsr.cpp` has 17,306 physical lines. The
  current build-input diff is
  `04bc8b8ad81f823290258b82bfd1870f9617a12b`, with zero untracked build
  inputs.
- The frozen-tree renderer review found one priority-two composition defect:
  Conservative Morphological Anti-Aliasing incorrectly forced the Minimum
  Temporal Reconstructive path onto its robust fallback even though the two
  techniques now consume separate history and display-linear resources. The
  compatibility gate is removed and a source-contract test prevents its return.
- Final Canonical artifact: commit
  `b4dc24128e4f38effdeaf5a2dbc33cae107e9134`, tree
  `8a3fbc4848a65e057bdabc669d0f6e73f074207f`, executable SHA-256
  `5F3779B0EFDE94B9C9B202BEB5A069265869D111ACE32D1132CA3D16F9430362`.
  The user verified Temporal Reconstructive, Conservative Morphological, and
  Multisample Reference running together on this exact executable and then
  renewed authorization to publish it.
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
| Independent deletion review | No unresolved priority-zero through priority-two renderer or packaging issue | Complete | Publication |

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
| Commit, integration, verification, and publication | `/root` | Feature and isolated verification worktree | `f7c0c87` plus accepted diff | All integration resources | Audit handoffs | Complete |

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
| Exact candidate captured | Clean focused commit, tree and build-input identities | Git status, staged diff, tree and hash audit | Passed: renderer commit `b4dc241`, tree `8a3fbc4`, build-input diff `04bc8b8...`, zero untracked build inputs |
| Integration preserves intent | Parentage, semantic conflict review, accepted renderer-path comparison | Git graph, diff, source contract review | Passed: three-commit fast-forward from `f7c0c87`; contaminated local `main` excluded |
| Release build | Fresh configure and all-target x64 Release build | CMake and MSBuild in isolated canonical tree | Passed in `canonical-engine-core-cleanup-final/build-canonical`; DirectX 12 on, DirectX 11 and Vulkan off |
| Shader package | 268 core, 46 retained shadow, 76 Donut tasks; exact 39 files | Build logs and package contract | Passed: 390 tasks and 39 files split 7 framework root, 10 framework passes, and 22 UVSR |
| Registered tests | Every CTest target passes | `ctest --output-on-failure` | Passed: 30 of 30 in 73.79 seconds |
| Runtime smoke | Exact integrated executable starts on D3D12 and remains responsive without disturbing another UVSR owner | Isolated launch and process/window identity | Passed: PID 7268, exact path and title `UVSR Renderer D3D12 (b4dc241)`, responsive at High priority; user verified all three aliasing techniques together |
| Documentation policy | Line-count self-test/check, Title Case self-test/full scan, diff check | Repository tools | Passed: counts current, 1,121 headings and bold lead-ins with zero violations, clean patch |
| Independent review | No unresolved priority-zero through priority-two finding | Read-only frozen integration review | Passed on commit `b4dc241` and tree `8a3fbc4`; no unresolved P0–P2 finding |
| Publication | Live `origin/main` equals the expected renderer and final documentation commits | Fresh fetch and `ls-remote` | Renderer checkpoint `b4dc241` confirmed live; README Line Counts and Document Title Case workflows passed; this documentation-only closing commit records but does not supersede it |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-03 | Bind product acceptance to executable SHA-256 `194D1637...` | It is the exact most recent build the user verified; no older candidate may substitute | All |
| 2026-08-03 | Exclude local `main` ancestry and transplant only valid semantics | `891f4f0` contains generated build trees and a 116.9 MB blob that cannot be published; leaving the root worktree untouched preserves recovery while the exact-build-link rule is copied safely and its redundant historical plan stays local | Integration |
| 2026-08-03 | Archive five superseded plans with their complete evidence | Their done conditions remain unmet, but deleting failure, benchmark, and recovery evidence would make future restoration less safe; archiving does not revive runtime code | Documentation |
| 2026-08-03 | Leave PRs #10 and #11 outside this publication | They have separate owners and are not required by the accepted renderer | Integration |
| 2026-08-03 | Treat performance benchmarking as out of scope | The cleanup claims reduced code and permutations, not a matched frame-time improvement | Verification |
| 2026-08-03 | Repair the frozen-review anti-aliasing composition finding before publication | Minimum Temporal Reconstructive and Conservative Morphological Anti-Aliasing are user-composable techniques; retaining a stale pre-tonemapping resource gate would silently change the selected Cost preset | Renderer checkpoint |

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
| 2026-08-03 | `/root` final pre-commit hygiene | Complete | Build-input diff `04bc8b8...`; 64,615 first-party lines | README self-test/check, 1,121-heading Title Case scan, launcher audit, and diff check pass | Stage exact task paths and commit |
| 2026-08-03 | Frozen renderer review and `/root` repair | Complete | Commit `b4dc241`, tree `8a3fbc4` | Exact review found no unresolved P0–P2 finding; regression contract present | Fresh canonical build and acceptance |
| 2026-08-03 | `/root` exact canonical build | Complete | SHA-256 `5F3779B0...`, 2,516,480 bytes | Fresh all-target Release build, 390 shader tasks, exact 39-file package, 30 of 30 tests, clean source and document checks | Runtime verification |
| 2026-08-03 | `/root` runtime and product acceptance | Complete | `UVSR Renderer D3D12 (b4dc241)`, PID 7268 | Responsive at High priority; user verified all three aliasing techniques enabled together | Publish renderer checkpoint |
| 2026-08-03 | `/root` renderer publication | Complete | Live `origin/main` `b4dc241` | Fresh fetch and `ls-remote`; README Line Counts and Document Title Case workflows passed | Publish documentation-only closing record |

## Completion

- Final integrated commit: `b4dc24128e4f38effdeaf5a2dbc33cae107e9134`
- Verification summary: fresh DirectX 12 Release build; 268 core, 46 retained
  shadow, and 76 Donut shader tasks; exact 39-file runtime package; 30 of 30
  registered tests; responsive exact-commit runtime; user-verified simultaneous
  aliasing composition; repository and remote document checks passed
- Independent review: complete on tree
  `8a3fbc4848a65e057bdabc669d0f6e73f074207f`, with no unresolved P0–P2 finding
- Coming Soon and documentation update: shipped cleanup entry removed; durable
  reports and this completed plan retained
- Publication: renderer checkpoint confirmed live on `origin/main`; this
  documentation-only closing commit does not supersede the renderer checkpoint
- Remaining external work: PRs #10 and #11 remain separate; Bend, Emissive,
  SVSM, and diagnostic CSM plan evidence is archived as superseded
- Active ownership released: yes
- Archived path:
  `docs/exec-plans/completed/canonicalize-engine-core-cleanup.md`

## Artifact Identity

| Field | Canonical Renderer Artifact |
| --- | --- |
| Renderer commit | `b4dc24128e4f38effdeaf5a2dbc33cae107e9134` |
| Renderer tree | `8a3fbc4848a65e057bdabc669d0f6e73f074207f` |
| Verification worktree | `C:\Users\brock\OneDrive\Documents\uvsr\work\canonical-engine-core-cleanup-final` |
| Executable | `build-canonical/bin/uvsr.exe` |
| SHA-256 | `5F3779B0EFDE94B9C9B202BEB5A069265869D111ACE32D1132CA3D16F9430362` |
| Size | 2,516,480 bytes |
| UTC write time | `2026-08-03T09:24:15.2747310Z` |
| Runtime identity | PID 7268; `UVSR Renderer D3D12 (b4dc241)`; responsive; High priority |
| Product acceptance | The user verified all three aliasing techniques enabled together on this exact runtime and authorized publication |

The earlier SHA-256 `194D1637...` executable remains the historical artifact
for the accepted interface restoration. It is not the Canonical renderer
artifact because the independent-review repair changed Temporal Reconstructive
composition afterward.

## Publication Lineage

The renderer checkpoint is a three-commit fast-forward from the refreshed live
base: `f7c0c87` → `b9287b0` → `e6f1591` → `b4dc241`. A fresh remote query after
the push returned `b4dc241` for `refs/heads/main` and tree `8a3fbc4`. The
documentation-only commit containing this completed plan closes the record but
does not change, rebuild, or supersede that renderer checkpoint.

The contaminated root-worktree `main` remains preserved locally at `5e300c9`
and was never merged, rebased, reset, or published. Its generated-build parent
`891f4f0` and 116.9 MB shader blob are absent from the Canonical lineage.

## Verification Evidence

- A fresh Visual Studio 2022 x64 tree configured DirectX 12 on and DirectX 11
  and Vulkan off, then completed the unqualified all-target Release build with
  MSBuild node reuse disabled.
- ShaderMake completed 268 UVSR core, 46 Screen-Space Directional Shadow, and
  76 Donut tasks. The synchronized runtime package contains exactly 39 files.
- CTest passed all 30 registered tests in 73.79 seconds, including production
  and runtime shader contracts, scene provenance, interface source contracts,
  and the new simultaneous-aliasing regression guard.
- README line-count self-test/check, Document Title Case self-test and complete
  1,121-item scan, patch-whitespace checks, clean worktree/index checks, and
  recursive pinned-submodule checks all passed.
- The independent frozen-tree reviewer traced compact temporal history through
  sharpening, AgX, and conservative morphological filtering and reported no
  unresolved priority-zero through priority-two finding.
- The exact executable launched through the repaired build-directory launcher,
  exposed the expected D3D12 commit title, remained responsive at High process
  priority, and was then verified by the user with all three aliasing techniques
  enabled together.
- GitHub's README Line Counts run `30802049605` and Document Title Case run
  `30802051077` both passed on live renderer commit `b4dc241`.

This evidence supports correctness and packaging claims, not a controlled
frame-time improvement. No thermal benchmark or matched performance comparison
was performed.

## Canonical Resolution

`b4dc24128e4f38effdeaf5a2dbc33cae107e9134` is UVSR's Canonical renderer
checkpoint for the engine-core cleanup and front-end restoration. Its exact
executable is the artifact recorded above. The documentation-only closing
commit is provenance, not a replacement renderer build. The evidence-weighted
average confidence in the remaining configurable feature set is 96.3 percent.
