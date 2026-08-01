# Directional Shadow and CMAA2 Canon Candidate Integration

## Status

- State: complete; merged into live GitHub `main` and reverified at the
  integrated canonical snapshot
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/directional-shadows-aa-integration` in
  `work/directional-shadows-aa-integration`
- Base commit: `402ebb042957eeba8149eee19e857b1e5452880a`
- Started: 2026-07-31
- Last updated: 2026-07-31
- Planned archive:
  `docs/exec-plans/completed/directional-shadows-aa-integration.md`

## Goal and Done Condition

Goal: compose the exact technically verified directional screen-space-shadow
restoration and CMAA2 maximum-quality repair identified by the user's two
executable SHA-256 values, build one isolated candidate, verify their combined
CPU, HLSL, resource, packaging, and presentation contracts, and launch that
exact executable for product acceptance. Before promotion, make the camera
flashlight start disabled without changing its profile or runtime controls.

Done when:

- [x] Both input hashes are mapped to exact local artifacts, the same canonical
      base, and complete dirty source identities without importing unrelated
      work.
- [x] Both task diffs and their required untracked sources/licenses are composed
      semantically, with shared renderer, build, test, and documentation changes
      preserving both intents.
- [x] Release shaders and application build, focused tests, full CTest, source
      and package license checks, Title Case validation, and diff checks pass.
- [x] An independent read-only review finds no unresolved P0-P2 rendering,
      resource, packaging, or integration defect.
- [x] The exact combined `uvsr.exe` passes a bounded runtime smoke and is then
      launched for the user's visual acceptance.
- [x] The user accepts the combined behavior, requests the startup flashlight
      default be disabled, and explicitly waives a replacement runtime smoke
      for that constant-only tweak.
- [x] The replacement is committed, merged into live GitHub `main`, rebuilt,
      and reverified at the integrated canonical snapshot.

## Scope

In scope:

- Exact current dirty diff from `codex/screen-space-shadow-performance`, whose
  input executable is SHA-256
  `1079DDD81494FA61DC016446DFE3DDA7257A370405C490062BFB5E5B61981BC3`.
- Exact current dirty diff from `codex/cmaa2-maximum-quality`, whose input
  executable is SHA-256
  `177CB419B3BE0B3550BA7C3541D1B39BAF7C86A12C8A011A0E12FA47650AFB0F`.
- Semantic reconciliation of shared `CMakeLists.txt`, `README.md`,
  `docs/advanced-settings.md`, `src/uvsr.cpp`, and shared source-contract tests.
- Task-local build, deterministic checks, bounded runtime smoke, exact-artifact
  launch, provenance handoff, and later canonical promotion only after approval.
- Camera flashlight startup default, its direct contract test, and the durable
  setting documentation; runtime enable controls and profile values stay intact.

Non-goals:

- TAA depth-validation work, SVSM, diagnostic CSM, AO/GI visibility PRs,
  unrelated UI or scene changes, Donut edits, performance claims, or speculative
  shadow/AA changes.
- Release or deployment beyond the user-authorized GitHub `main` promotion.

Affected subsystems and paths:

- Directional screen-space-shadow CPU/HLSL integration, settings, tests,
  third-party Bend source, and Apache-2.0 packaging.
- CMAA2 CPU/HLSL integration, post-tonemap display output, shader packages,
  temporal-AA option contracts, tests, and settings documentation.
- Shared renderer sequencing in `src/uvsr.cpp`, root build registration, README,
  and advanced settings.

Shared hotspots reserved for the coordinator:

- All writable paths, Git state, build trees, shader packaging, renderer process,
  GPU/runtime control, shared CPU/HLSL contracts, and this plan.

## Baseline

- Canonical repository/remote: live `origin/main` at
  `402ebb042957eeba8149eee19e857b1e5452880a`, fetched on 2026-07-31.
- Local versus remote state: isolated integration worktree starts clean and equal
  to live `origin/main`; the root `main` checkout is divergent and remains
  untouched.
- Verified source commit/build: `402ebb0` is the current recorded Canonical
  verified checkpoint. Each input is a technically verified dirty candidate on
  that exact base and is not itself Canonical verified.
- GPU, scene, camera, resolution, and settings preset when relevant: bounded
  runtime smoke first; user visual review uses the launched combined candidate.
  Any performance work requires a separate clean matched Position-1 session.
- Known pre-existing failures: none recorded for the two input candidates. Open
  PRs #10 and #11 touch AO/GI visibility helpers/tests and are excluded.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `402ebb0` | Integrated as base | Whole task |
| Directional-shadow input | Exact `1079DDD8...BC3` source diff and license files | Frozen and composed | Integration |
| CMAA2 input | Exact `177CB419...AFB0F` source diff and display pipeline | Frozen and composed | Integration |
| Independent review | Frozen replacement candidate | Passed on final tree `1ad1a974` | Final handoff |
| Product acceptance | Exact launched artifact plus requested flashlight default | Accepted; replacement smoke waived | Canonical promotion |
| GitHub `main` promotion | PR #28 and merge commit `a159625c` | Complete and reverified | Canonical verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Directional visibility remains a full-resolution linear `R8_UNORM` texture,
  pointer-associated with the exact directional light and consumed only by the
  deferred directional-light contribution.
- The restored shadow implementation retains its generic runtime names, native
  diagnostics, existing settings defaults, exact vendored upstream bytes, and
  Apache-2.0 packaging.
- CMAA2 remains full-resolution single-frame spatial AA after AgX on an
  undithered display-linear intermediate, with Ultra defaults, full-color edge
  detection, and final transfer/dither after AA.
- Shared renderer sequencing must preserve temporal-AA behavior when selected,
  CMAA2 resource formats and barriers, post-processing timers, resize behavior,
  UI/CLI contracts, and shadow dispatch before deferred lighting.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `trace-shadow` | `/root/trace_shadow_build` | Read-only shared repository | `402ebb0` context | None | Shadow artifact | Complete |
| `trace-aa` | `/root/trace_aa_build` | Read-only shared repository | `402ebb0` context | None | AA artifact | Complete |
| `assess-integration` | `/root/assess_integration` | Read-only shared repository | `402ebb0` context | None | Both lineages | Complete |
| `integrate` | `/root` | Task worktree | `402ebb0` | All task paths | Trace handoffs | Complete |
| `independent-review` | `/root/independent_combined_review` | Frozen task worktree | Combined candidate | None | Integrated diff | Complete |

## Assignment Contracts

### Trace Shadow: Resolve Exact Directional-Shadow Provenance

- Owner/thread: `/root/trace_shadow_build`
- Branch/worktree: read-only shared repository and task worktrees
- Base commit/state: `402ebb0` context
- Read scope: shadow artifact, worktree, Git diff, manifest, tests, and plan
- Write scope: none
- No-touch scope: every file, ref, index, build tree, process, and remote
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: distinguish executable identity, base commit,
  tracked diff, untracked inputs, and verification evidence
- Deliverable: distilled provenance and minimal integration map
- Done when: exact source state behind the supplied hash is reproducible
- Required verification: read-only hashes, Git status/diff, and record inspection
- Allowed Git and external actions: read-only local Git only
- Stop and report if: provenance is ambiguous or source changes during review
- Handoff revision/artifact: frozen tree `5649710f676759bdd8704c0173ec870afe03eb22`
  and unreferenced snapshot commit `5dc683554993590eaa1a64d80e456d74bfaaf3ae`
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Trace AA: Resolve Exact CMAA2 Provenance

- Owner/thread: `/root/trace_aa_build`
- Branch/worktree: read-only shared repository and task worktrees
- Base commit/state: `402ebb0` context
- Read scope: AA artifact, worktree, Git diff, manifest, tests, and plan
- Write scope: none
- No-touch scope: every file, ref, index, build tree, process, and remote
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: distinguish executable identity, base commit,
  tracked diff, untracked inputs, and verification evidence
- Deliverable: distilled provenance and minimal integration map
- Done when: exact source state behind the supplied hash is reproducible
- Required verification: read-only hashes, Git status/diff, and record inspection
- Allowed Git and external actions: read-only local Git only
- Stop and report if: provenance is ambiguous or source changes during review
- Handoff revision/artifact: frozen tree `c2e4cc0ca505f0b3611b49f5268825ae6812ef4d`
  and unreferenced snapshot commit `5675ca4b86469665100c9c94ab1feff133617eaf`
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Assess Integration: Identify Ordering, Conflicts, and Checks

- Owner/thread: `/root/assess_integration`
- Branch/worktree: read-only shared repository and task worktrees
- Base commit/state: `402ebb0` context
- Read scope: both diffs, active plans, build/test scripts, and shared contracts
- Write scope: none
- No-touch scope: every file, ref, index, build tree, process, and remote
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: preserve both verified behaviors and identify
  semantic conflicts rather than selecting either side wholesale
- Deliverable: integration order, conflict map, verification commands, and risks
- Done when: coordinator can compose and validate without guessing
- Required verification: read-only source and Git comparison
- Allowed Git and external actions: read-only local Git only
- Stop and report if: active ownership overlaps the proposed task worktree
- Handoff revision/artifact: shared-path and verification audit; no P0-P2
  integration blocker found
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Integrate: Compose and Verify the Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/directional-shadows-aa-integration` in the task worktree
- Base commit/state: clean `402ebb0`
- Read scope: full first-party repository, both frozen input worktrees, completed
  plans, pinned dependency APIs, and worker handoffs
- Write scope: all task paths and this plan
- No-touch scope: Donut, source candidate worktrees, root checkout, other active
  worktrees/plans, remote refs beyond fetch, and unrelated features
- Build directory and runtime/GPU/resource lease:
  `build-directional-shadows-aa`; coordinator exclusively owns it and the UVSR
  window
- Dependencies already integrated: trace and assessment handoffs before final
  conflict resolution
- Interface/invariant contract: contracts listed above
- Deliverable: technically verified combined candidate and exact executable
- Done when: every done condition except user acceptance and promotion has exact
  evidence
- Required verification: Release shader/application and all-target builds,
  focused/full CTest, title checker, diff checks, license/source/package hashes,
  initial runtime smoke, independent review, and post-integration automated
  checks; replacement runtime smoke is explicitly waived by the user
- Allowed Git and external actions: local branch/worktree edits, build, tests,
  runtime smoke, launch, commit, push, ready PR, and merge into GitHub `main`;
  no release or deployment
- Stop and report if: a visible product/default tradeoff, uncertain deletion,
  provenance mismatch, unrelated diff, or user-controlled runtime collision
  appears
- Handoff revision/artifact: technically verified executable SHA-256
  `CB0289E45935B58378D17C7A0079554EE86F60522E3852EE334FB255C5AAE2E0`
- Handoff acknowledged by/on: coordinator-owned

### Independent Review: Audit the Frozen Combined Candidate

- Owner/thread: `/root/independent_combined_review`
- Branch/worktree: read-only task worktree
- Base commit/state: frozen combined dirty identity on `402ebb0`
- Read scope: complete combined diff plus CPU/HLSL/resource/package contracts
- Write scope: none
- No-touch scope: every file, Git state, build tree, process, and remote
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: final composed candidate
- Interface/invariant contract: review shadow light association, CMAA2 pipeline
  order and color space, barriers/formats, shader coverage, resize/lifetime,
  licensing, documentation, and test gaps
- Deliverable: prioritized findings with exact paths and evidence
- Done when: no unresolved P0-P2 finding remains
- Required verification: read-only diff and source/test inspection
- Allowed Git and external actions: read-only local inspection
- Stop and report if: candidate changes during review
- Handoff revision/artifact: initial tree
  `444a8a528ab07cba7f3eb2e8c45336e953b46ee5` reported one P1; replacement
  tree `160c957ac929bb6cf2eb9f068e8a5c8ca42c5fec` and final flashlight-default
  tree `1ad1a9740c87a8a86d304efdff2df4de27169af7` passed with no unresolved
  P0-P2 findings
- Handoff acknowledged by/on: `/root`, 2026-07-31

## Integration Order

1. Freeze and record both input artifacts and dirty source identities.
2. Apply the CMAA2 repair, including its display-output and global shader changes.
3. Apply the directional-shadow restoration and vendored license/source inputs.
4. Reconcile shared renderer sequencing, root build rules, tests, and maintained
   documentation so both verified contracts remain explicit.
5. Run focused checks, then Release shader/application and complete test builds.
6. Freeze the candidate for independent review; repair and reverify any finding.
7. Run a bounded exact-artifact smoke, then launch that artifact for the user.
8. After explicit acceptance of that artifact/settings, run the required final
   canonical promotion chain and reverify the integrated canonical snapshot.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Exact inputs | SHA-256, base, tracked diff, and untracked file identity | Hash and Git audit | Passed; shadow tree `5649710f`, AA tree `c2e4cc0c`, both on `402ebb0` |
| Shadow contract | Focused tests, shader/package compile, license hashes | Shadow test and package audit | Passed; exact Bend headers and Apache-2.0 package hashes |
| CMAA2 contract | Focused tests and all LDR/HDR shader permutations | CMAA2 and shader-bundle tests | Passed after odd-extent capacity repair |
| Renderer composition | Source contracts and runtime bundle tests | Focused CTest selection | Passed |
| Build health | Release shaders, `uvsr`, and all targets | CMake build | Passed with MSBuild node reuse disabled |
| Suite health | Complete task-local tests | CTest | Passed, 33/33 production and 2/2 factory bundle tests |
| Documentation health | All in-scope headings use Title Case | Repository heading validator | Passed, 1,003 headings and bold lead-ins |
| Source health | No whitespace/conflict/unexpected diff defects | `git diff --check` and status audit | Passed before final freeze |
| Rendering safety | Independent frozen-diff review | Read-only review | Passed on replacement tree `160c957a`; no unresolved P0-P2 findings |
| Runtime viability | Responsive High-priority D3D12 window | Bounded smoke | Passed; PID 4804, exact path, one renderer, 10/10 responsive High-priority samples |
| Product acceptance | User accepts exact launched artifact/settings | Interactive review | Passed; flashlight-default replacement smoke explicitly waived |
| Flashlight startup default | Shared constant, direct test, and durable docs | Focused build and CTest | Passed; full 33/33 production suite also passes |
| Canonical integration | Live `main` merge plus rebuilt exact snapshot | GitHub PR/merge and post-merge checks | Passed at `a159625c`; production 33/33 and factory 2/2 tests passed |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-31 | Use live `origin/main` `402ebb0` in a new isolated worktree. | Both verified inputs use that base, while root `main` is divergent and unrelated worktrees remain user-owned. | Whole task |
| 2026-07-31 | Preserve each input as a complete source diff, not by executable-file copying or inferred feature snippets. | The executable hashes identify technically verified source states whose CPU/HLSL/build/test contracts must remain coupled. | Integration |
| 2026-07-31 | Keep promotion conditional on acceptance of the final exact artifact and settings. | Integration and any repair change artifact identity and invalidate acceptance of either input build by itself. | Runtime and promotion |
| 2026-07-31 | Size the CMAA2 deferred-location list as independently rounded-up 2x2 dimensions. | The item-head texture addresses `ceil(width / 2) * ceil(height / 2)` quads; `ceil(width * height / 4)` is smaller for odd extents and permits out-of-range appends. | CMAA2 repair and test coverage |
| 2026-07-31 | Disable the camera flashlight startup default through its shared constant and skip a replacement runtime smoke. | The user accepted the combined candidate, requested this exact constant-only behavior change, and explicitly judged a smoke unnecessary; automated pre- and post-merge verification remains required. | Flashlight default and canonical promotion |

## Progress and Handoffs

| Date/Time | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-31 | `/root` | Started | Clean task worktree at `402ebb0` | Live remote, worktrees, open PRs, active plans, Coming Soon, and visible tasks inspected | Await exact trace handoffs, then compose |
| 2026-07-31 | Trace workers | Complete | Shadow tree `5649710f`; AA tree `c2e4cc0c` | Both executable hashes rechecked; source mtimes and dirty manifests audited | Preserve complete trees, not inferred snippets |
| 2026-07-31 | `/root/assess_integration` | Complete | Read-only shared-path audit | Five shared paths; implementation hunks disjoint; exact build and test matrix returned | Preserve debug bypass and post-AgX ordering |
| 2026-07-31 | `/root` integration | Composed | Automatic merge tree `eab62b69df0637113410fd236d8849c5261fafe6` | Clean three-way merge; all 37 input paths materialized; shared invariants inspected | Reconcile roadmap, build, test, and review |
| 2026-07-31 | `/root/independent_combined_review` | P1 found | Reviewed tree `444a8a528ab07cba7f3eb2e8c45336e953b46ee5` | Complete read-only CPU/HLSL/resource/package audit; no other P0-P2 findings | Repair odd-extent deferred-location capacity |
| 2026-07-31 | `/root` repair | Built and tested | `uvsr.exe` SHA-256 `CB0289E45935B58378D17C7A0079554EE86F60522E3852EE334FB255C5AAE2E0` | Full Release build; 33/33 production and 2/2 factory tests; exact package hashes; README and Title Case checks | Freeze replacement for independent review, then smoke and launch |
| 2026-07-31 | `/root/independent_combined_review` | Complete | Replacement tree `160c957ac929bb6cf2eb9f068e8a5c8ca42c5fec` | Odd-extent capacity repair correct; frozen before and after; no unresolved P0-P2 findings | Ready for runtime smoke |
| 2026-07-31 | `/root` runtime | Launched for acceptance | Exact executable SHA-256 `CB0289E45935B58378D17C7A0079554EE86F60522E3852EE334FB255C5AAE2E0`, PID 4804 | PBR Sponza Decorated at 1920x1000; CMAA2 enabled at Ultra; directional shadows enabled on Default 60 px profile; debug view Off; one exact-path renderer and 10/10 responsive High-priority samples | Await user acceptance; any artifact or setting repair invalidates it |
| 2026-07-31 | `/root` acceptance and publication | Pre-merge verified | Replacement `uvsr.exe` SHA-256 `585ECBB1FEAA41871A2C2AF07B5634BF334394880EDC472AD6E0CBB39BDDB1F0` | Live `origin/main` remains `402ebb0`; focused flashlight test and 33/33 production suite pass; exact dependency/package hashes, README counts, Title Case, and diff checks pass; replacement smoke waived | Publish through a ready PR, merge, and reverify canonical main |
| 2026-07-31 | `/root` GitHub promotion | Merged | Feature commit `18236ffb`; PR #28; canonical merge `a159625c` | Both required GitHub workflows passed; exact merge subject, parents, and tree verified | Rebuild and verify live canonical snapshot |
| 2026-07-31 | `/root` canonical verification | Complete | `uvsr.exe` SHA-256 `AF1EA0DE66B2DE4ED54BFE5889D64294CE2FBD899CB414F6E216F56951266AF8` from `a159625c` | Production build passed; CTest 33/33; factory bundles 2/2; exact package hashes; README counts and 1,003 Title Case headings passed; worktree clean; replacement runtime smoke remained waived | Remove merged roadmap entry and archive this plan |

## Risks and Escalation Triggers

- Both inputs change `src/uvsr.cpp`; textual success is not proof that shadow
  dispatch, deferred lighting, tonemapping, temporal AA, CMAA2, transfer, dither,
  and presentation remain in the intended order.
- Combined shader/package permutations may expose a build-only gap absent from
  either isolated candidate.
- Runtime visual acceptance must bind to the combined executable, scene, and
  settings rather than either supplied input hash.
- Active older Bend, SVSM, CSM, and TAA lineages remain separate and must not be
  imported merely because they touch similar files.

Stop and ask the user if:

- the only correct resolution requires a materially different visible shadow or
  AA default, destruction of uncertain work, or broader feature integration;
- launch would require closing a user-controlled process whose ownership cannot
  be established;
- canonical publication destination or authority becomes ambiguous after
  product acceptance.

## Completion

- Final integrated commit: `a159625c8e84b8ae1cf308550fa9cc2ac75ce79c`
- Verification summary: the integrated production build, all 33 production
  tests, both factory shader-bundle contracts, exact dependency/package hashes,
  README counts, Title Case validation, and diff/status checks pass; the user
  explicitly waived a replacement runtime smoke for the flashlight constant
- Independent review: final tree `1ad1a974` passed with no unresolved P0-P2
  findings
- Coming Soon/documentation update: merged candidate entry removed and this
  plan archived
- Pushed/PR/merged, or intentionally local: feature commit `18236ffb` pushed
  and merged by PR #28 as canonical merge `a159625c`
- Remaining experiments or follow-ups: none for this task
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/directional-shadows-aa-integration.md`
