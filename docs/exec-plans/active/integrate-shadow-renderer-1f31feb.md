# Integrate Shadow Renderer 1f31feb

## Status

- State: integration
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/integrate-shadow-renderer-1f31feb` at
  `C:\Users\brock\OneDrive\Documents\uvsr`
- Base commit: `ec4c6f93029ae1d8106b80370bc9c24965993f98`
- Source tip: `1f31feb99e5400b63fd3c9a6e17151ff1f7988a0`
- Started: 2026-07-29 17:21 CDT
- Last updated: 2026-07-29 17:27 CDT
- Planned archive:
  `docs/exec-plans/completed/integrate-shadow-renderer-1f31feb.md`

## Goal and Done Condition

Goal:

Integrate the complete three-commit experimental shadow, IBL, and renderer
lineage ending at `1f31feb` onto live canonical `main`, preserve the accepted
AA, UI, visibility, and runtime behavior already on `main`, verify the composed
renderer, and launch the exact candidate for user review. Keep GitHub `main`
unchanged until the user accepts that launched artifact.

Done when:

- [ ] The exact source lineage is semantically merged without wholesale
  `ours`/`theirs` conflict resolution.
- [ ] Main's accepted AA, UI, fused visibility, runtime shader, and packaging
  contracts coexist with Bend, diagnostic CSM, SVSM, exact-light shadow
  composition, and IBL.
- [ ] Required source, shader, documentation, build, and full CTest checks pass
  from a fresh integration build directory.
- [ ] An independent reviewer clears high-risk renderer, shader-binding,
  resource-lifetime, and runtime-bundle composition.
- [ ] The exact verified candidate is launched with a lowercase experiment
  label and its executable identity is recorded.
- [ ] The user accepts the launched artifact before any update to remote
  `main`.
- [ ] After acceptance, live `main` is refreshed, the accepted commit is
  integrated without changing its tree, pushed through the authorized
  publication path, and reverified as the new Canonical checkpoint.

## Scope

In scope:

- Merge the complete source ancestry ending at `1f31feb`.
- Preserve and combine current-main AA, UI, screen-space visibility, shader
  packaging, tests, documentation, and launch behavior.
- Integrate Bend screen-space directional shadows, diagnostic CSM, SVSM,
  image-based lighting and sky behavior, exact-light visibility composition,
  command-line controls, benchmark hardening, and runtime shader-bundle
  synchronization from the source lineage.
- Resolve integration defects, build failures, contract failures, or runtime
  startup failures introduced by composition.
- Build, test, independently review, and launch locally.
- Publish to canonical `main` only after exact-artifact user acceptance.

Non-goals:

- Merge unrelated open PR #10 or PR #11.
- Redesign current accepted AA/UI behavior or revive retired tonemapper
  controls.
- Add new shadow algorithms beyond the source lineage.
- Weaken tests, hashes, runtime validation, cache invalidation, or shader
  packaging to make the merge pass.
- Create a GitHub release.

Affected subsystems and paths:

- Root build and launch: `CMakeLists.txt`, `tools/launch_uvsr.ps1`
- Renderer/UI integration: `src/uvsr.cpp`, `src/command_line_options.h`
- PBR, visibility, IBL, Bend, CSM, and SVSM first-party source and shaders
- Runtime shader catalog and exact synchronization
- Unit, source-contract, shader-bundle, and benchmark-validation tests
- `README.md`, PBR/visibility design documentation, execution plans, and
  technical reports

Shared hotspots reserved for the coordinator:

- `AGENTS.md`, `README.md`, `CMakeLists.txt`, `src/uvsr.cpp`,
  `src/shaders.cfg`, all shared CPU/HLSL binding contracts, runtime bundle
  catalogs, submodule state, Git index/history, build tree, and UVSR window

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main`
  `ec4c6f93029ae1d8106b80370bc9c24965993f98`
- Local versus remote state: local `main` and the candidate base exactly match
  `origin/main`; source is 3 commits ahead and 13 commits behind from merge base
  `a55e215e4bf0eddb20330283d9a4f8e853bda49f`
- Verified source commit/build: the newest Canonical renderer checkpoint is
  merge `64cd553e49f4b0818c634b6c6ddefa809daf8736`, recorded in
  `docs/exec-plans/completed/visibility-runtime-taa-canonical-verification.md`;
  later `main` commits are documentation-only
- GPU, scene, camera, resolution, and settings preset when relevant: launch at
  the repository default scene and settings; record the exact observed adapter,
  scene, camera, resolution, and settings at user acceptance
- Known pre-existing failures: none recorded for Canonical automated checks;
  two user-owned root `.obj` files predate checkout and remain untouched and
  untracked
- Repository policy version recorded before integration: `2026-07-22.1`
- UI reference version recorded before integration: `2026-07-29.1`
- Visible-task overlap: the source lineage was produced and published by the
  idle Shadow Richness Fix task; this task owns the fresh integration lease.
  Other visible shadow/SVSM tasks are inactive or historical.
- Roadmap/PR overlap: open PR #10 and PR #11 touch visibility helpers/tests but
  are not part of this integration and remain unmerged.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical base | `origin/main` at `ec4c6f9` | Complete | Integration branch |
| Source lineage | Exact tip `1f31feb`, including parents `94b8597` and `ea566bc` | Complete | Semantic merge |
| Donut submodule | Preserve the source-required pinned Gitlink after ancestry composition | Pending | Configure/build |
| UI reference | `2026-07-29.1` and current helper contracts | Recorded | `src/uvsr.cpp` integration |
| Runtime shader bundle | Union main AA/fused shaders with Bend/CSM/SVSM/IBL catalogs and exact sync | Pending | Release runtime |
| User product acceptance | Exact launched candidate and observed settings | Pending | Canonical publication |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve the current CPU/HLSL resource-slot contract for forward, deferred,
  MSAA, fused visibility, and screen-space paths while adding the three
  pointer-identical directional-light visibility producers.
- Bend, CSM, and SVSM resolve to independent single-sample visibility inputs;
  missing, invalid, disabled, unsupported, or out-of-range shadow visibility
  remains white.
- Final directional-light visibility preserves the source contract while main's
  AO/GI visibility and AA paths remain functional.
- Runtime shader synchronization must copy every declared shader, reject stale
  or undeclared DXIL, preserve main's production-bundle exclusions, and avoid a
  build-order deletion race.
- Render-target, pass, binding-cache, temporal-history, and benchmark state
  transitions preserve current narrow-refresh and full-rebuild preconditions.
- `tools/launch_uvsr.ps1` remains the only accepted launch path and embeds the
  source revision and lowercase experiment label in the window title.

## Incoming UI Intake

The incoming UI surface is an integration of existing experimental controls,
not authority to redesign the accepted Settings system. Every item below must
use the destination's current drawer, nested-section, deferred-dropdown,
animated-toggle, reset, tooltip, and Statistics presentation contracts.

### Visibility and Shadow Controls

- Bend screen-space directional shadow enablement, profile, trace/filter
  controls, diagnostics, and timings remain owned by Visibility/Statistics.
- Diagnostic CSM enablement, comparison/profile controls, cascade coverage,
  resolution, distance, filtering, Developer Options, Unabstracted controls,
  Diagnostics, benchmark actions, and timings remain owned by
  Visibility/Statistics.
- SVSM enablement, profile, dense/sparse/cached behavior, clipmap/page/pool
  controls, marking, filtering, resolution bias, Bend-assisted optimizations,
  Developer Options, Unabstracted controls, Diagnostics, benchmark actions,
  counters, and timings remain owned by Visibility/Statistics.
- Toggle-owned dependent rows use balanced animated regions. Profile and mode
  dropdowns that change visible structure use the shared staged structural
  presentation and global commit barrier. Renderer-facing changes occur only
  after stable presentation.
- Defaults and reset behavior come from the source presets unless they conflict
  with current Canonical product defaults; individual resets restore their
  owning preset and complete profile resets restore the named profile.

### Sky, Image-Based Lighting, and Statistics Controls

- Procedural sky, environment/IBL selection or enablement, sky visibility, and
  related diagnostics remain in Sky or Statistics and preserve current
  Canonical layout, Title Case, tooltips, and reset semantics.
- Shadow and IBL telemetry remains in the shared Statistics effect selector and
  striped tables. Idle state does not show readiness prose or an inert Cancel
  action; running actions use the accepted animated region.
- Any selector that recreates resources or changes renderer topology must stage
  presentation, inventory every consumer, show `--` for incompatible pending
  telemetry, and commit after popup, layout, scrolling, Settings, zoom, and
  input are idle for the required frame barrier.

### Current UI Contracts to Preserve

- Settings launches hidden; General alone opens on the first Escape.
- Drawer order, body IDs, sibling spacing, shared scrollbar, surfaces, blur,
  outlines, shadows, popup roll, deferred commits, and current AA structural
  transaction remain intact.
- No raw ImGui primitive substitutes for an established UVSR helper without an
  explicitly documented and tested exception.
- UI composition performs no normalization, resource mutation, file I/O, scene
  mutation, or benchmark mutation.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Build/launch audit | `/root/build_launch_audit` | Shared read-only checkout | Unborn shell plus remote metadata | None | Remote commit | Complete |
| Git integration audit | `/root/git_integration_audit` | Shared read-only checkout | Unborn shell plus remote metadata | None | Remote commit | Complete |
| Semantic integration | `/root` | Candidate branch/current worktree | `ec4c6f9` | All conflict and integration paths | Completed audits | Active |
| Independent renderer review | Pending reviewer | Shared read-only checkout after write freeze | Integrated candidate | None | Completed integration | Pending |
| Build/runtime operation | `/root` | Candidate branch/current worktree | Integrated candidate | `build-integration` and one UVSR process | Review-ready tree | Pending |

## Assignment Contracts

### Build and Launch Audit

- Owner/thread: `/root/build_launch_audit`
- Base commit/state: remote metadata before checkout
- Read scope: build scripts, README, CMake, launch tooling, machine prerequisites
- Write scope: none
- No-touch scope: repository, refs, submodules, build trees, processes
- Deliverable: exact fresh configure/build/test/launch commands and pitfalls
- Done when: the coordinator has a reproducible full-suite and labeled-launch
  path
- Required verification: read-only toolchain and target inspection
- Allowed Git and external actions: read-only
- Handoff revision/artifact: distilled handoff received 2026-07-29
- Handoff acknowledged by/on: `/root`, 2026-07-29

### Git Integration Audit

- Owner/thread: `/root/git_integration_audit`
- Base commit/state: live remote metadata before checkout
- Read scope: remote refs, commit graph, changed paths, overlap, PR/check state
- Write scope: none
- No-touch scope: repository, refs, branches, index, files
- Deliverable: exact ancestry classification, conflict risk, and integration
  strategy
- Done when: the coordinator has a safe lineage-preserving merge approach
- Required verification: live remote and compare inspection
- Allowed Git and external actions: read-only
- Handoff revision/artifact: distilled handoff received 2026-07-29
- Handoff acknowledged by/on: `/root`, 2026-07-29

### Semantic Integration

- Owner/thread: `/root`
- Branch/worktree: `codex/integrate-shadow-renderer-1f31feb`, current worktree
- Base commit/state: `ec4c6f9`
- Read scope: both complete lineages and every conflicting/overlapping contract
- Write scope: conflict resolution, integration repairs, plan, and reconciled
  documentation
- No-touch scope: the two pre-existing untracked `.obj` files, unrelated open
  PRs, user-owned external worktrees/processes
- Build directory and runtime/GPU/resource lease: `build-integration`; `/root`
  owns the only build and UVSR window
- Dependencies already integrated: none until the no-commit semantic merge
- Interface/invariant contract: preserve both lineages' valid behavior and the
  contracts listed above; never take a hotspot side wholesale
- Deliverable: one reviewable committed merge candidate
- Done when: conflicts are resolved semantically, source/contracts pass, and
  the diff is ready for independent review
- Required verification: targeted source and shader contracts, documentation
  checks, line-count checks, `git diff --check`
- Allowed Git and external actions: local fetch, branch, merge, stage, and
  commit; no push or remote-main update before user acceptance
- Stop and report if: integration requires a material product tradeoff,
  unrelated work appears, source identity changes unexpectedly, or a valid
  contract cannot be preserved

### Independent Renderer Review

- Owner/thread: assigned after semantic integration
- Branch/worktree: shared read-only candidate checkout under write freeze
- Base commit/state: completed local integration candidate
- Read scope: renderer, PBR, visibility, shadow, shader binding, CMake/runtime
  bundle, tests, and semantic diff against both parents
- Write scope: none
- No-touch scope: files, Git state, submodules, build tree, processes
- Deliverable: severity-ranked findings with exact paths and evidence
- Done when: high-risk rendering, resource-lifetime, binding, and packaging
  composition is independently cleared or actionable findings are returned
- Required verification: source/diff inspection and test-coverage review
- Allowed Git and external actions: read-only
- Stop and report if: a correctness or silent-packaging defect is found

## Integration Order

1. Refresh and record live `main` and exact source tip.
2. Merge `1f31feb` with `--no-ff --no-commit`.
3. Resolve root policy/documentation and build/runtime bundle contracts.
4. Resolve shared renderer, AA/UI, PBR, visibility, and shader bindings.
5. Reconcile tests and execution-plan/documentation state.
6. Run source hygiene, line-count, heading, and targeted contract checks.
7. Freeze writes and complete independent high-risk review.
8. Configure a fresh build, build every Release target, and run full CTest.
9. Record executable identity and launch the exact candidate as `shadowmerge`.
10. Await user product acceptance.
11. On acceptance, refresh live `main`, preserve the accepted tree, complete
    authorized publication, reverify the canonical commit, and archive this
    plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact ancestry | Two-parent merge with exact base/source identity | `git show`, `git merge-base`, `git rev-list` | Pending |
| Runtime shader union and exact sync | Bundle contracts plus staged file inventory | Full Release build and shader-bundle CTests | Pending |
| Current AA/UI preserved | Focused source, animation, dropdown, renderer, and TAA contracts | Focused build/CTest regex from UI reference | Pending |
| Shadow/IBL behavior composes | Bend hash, CSM/SVSM/PBR/source-contract tests | Full CTest and independent review | Pending |
| Documentation policy | Self-tests and complete scans pass | Title Case and README line-count tools | Pending |
| Build reproducibility | Fresh Release configuration builds every target | CMake configure and unqualified Release build | Pending |
| Full automated verification | Every registered test passes | `ctest --test-dir build-integration -C Release --output-on-failure` | Pending |
| Runtime startup | Exact labeled executable remains running and renders | `tools/launch_uvsr.ps1 -Experiment shadowmerge -BuildDirectory build-integration` | Pending |
| Product acceptance | User accepts exact hash/settings | Manual review of launched build | Pending |
| Canonical publication | Remote `main` contains accepted tree and checks pass | Refresh, publish, fetch, clean reverify | Blocked on acceptance |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-29 17:22 CDT | Use a fresh candidate branch from exact live `origin/main` | The workspace began as an unborn Git shell; configuring it in place preserves the two unrelated `.obj` files while establishing canonical ancestry | All |
| 2026-07-29 17:24 CDT | Merge the complete source lineage instead of cherry-picking only `1f31feb` | The tip depends on its two preceding feature commits; a tip-only cherry-pick would omit required source and ancestry | Integration |
| 2026-07-29 17:26 CDT | Require semantic hotspot resolution and a fresh all-target build | Both lineages changed renderer, visibility, UI, PBR, shaders, and packaging; a textual merge or target-only build cannot prove composition | Integration and verification |
| 2026-07-29 17:27 CDT | Keep remote `main` untouched until exact-artifact approval | The user explicitly conditioned canonical upload on approval of the launched build | Publication |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-29 17:24 CDT | Git audit | Complete | `ec4c6f9...1f31feb` graph | Live remote and overlap audit | High semantic-conflict risk |
| 2026-07-29 17:25 CDT | Build audit | Complete | Recommended `build-integration` workflow | Toolchain/prerequisite inspection | First configure downloads pinned dependencies |
| 2026-07-29 17:27 CDT | Coordinator preflight | Active | This plan | Worktrees, tasks, PRs, roadmap, policies reviewed | Refresh remote, then begin no-commit merge |

## Risks and Escalation Triggers

- `src/uvsr.cpp`, PBR, visibility, CMake, shader catalogs, and documentation
  encode behavior from both lineages and can compile while silently dropping a
  binding, shader, UI transaction, or resource invalidation path.
- Source runtime evidence predates composition and is stale after the merge.
- An incomplete source execution plan may expose runtime matrices that remain
  unverified after automated tests.
- The full shader build is large and must not overlap another build or UVSR
  process.
- User acceptance binds the exact executable, scene, settings, and observed
  window. Any artifact- or settings-changing repair invalidates acceptance.

Stop and ask the user if:

- Preserving both lineages requires choosing between materially incompatible
  visible behavior or default settings.
- Publication cannot preserve the exact accepted tree because live `main`
  changes after acceptance.
- The requested destination or authority for the final canonical update becomes
  ambiguous.

## Completion

- Final integrated commit: pending
- Verification summary: pending
- Independent review: pending
- Coming Soon/documentation update: pending reconciliation
- Pushed/PR/merged, or intentionally local: intentionally local until user
  acceptance
- Remaining experiments or follow-ups: pending
- Active ownership released: pending
- Archived to completed/abandoned path: pending
