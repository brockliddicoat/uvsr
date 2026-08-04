# Fast Approximate Anti-Aliasing

## Status

- State: completed
- Coordinator: `/root`
- Project branch and worktree: `codex/fast-approximate-aa` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\fast-approximate-aa`
- Base commit: `ff5adbd1f4cd9592cfac0fd1ab0e91720027772e`
- Canonical renderer checkpoint: `b4dc24128e4f38effdeaf5a2dbc33cae107e9134`
- Started: 2026-08-03
- Last updated: 2026-08-03
- Completed: 2026-08-03
- Archived path:
  `docs/exec-plans/completed/fast-approximate-aa.md`

## Goal and Done Condition

Goal: add an independently composable **Fast Approximate** anti-aliasing
technique based on Google Filament's FXAA path, and make all four Aliasing
techniques use a consistent visible Quality recipe plus a collapsed-by-default
**Advanced** disclosure for detailed overrides.

Done when:

- [x] Fast Approximate defaults off, runs after tone mapping and before CMAA2,
      preserves UVSR's display-linear presentation contract, and remains safe
      in every composition of MSAA, TAA, FXAA, and CMAA2.
- [x] Temporal Reconstructive, Fast Approximate, Conservative Morphological,
      and Multisample Adaptive each expose visible Low, Medium, High, and Ultra
      Quality recipes while enabled, followed by an Advanced disclosure that
      starts collapsed without erasing stored settings while disabled.
- [x] Temporal Reconstructive labels its independent budget selector **Cost**;
      CMAA2 Advanced exposes edge threshold and luma/full-color detection;
      Fast Approximate Quality owns all three Filament controls; and
      Multisample Adaptive Quality maps deterministically to 2x/4x/8x/16x.
- [x] The command interface, shader/package manifests, statistics, maintained
      documentation, and focused source/reference contracts cover the feature.
- [x] A clean isolated Release build, complete registered tests, shader-package
      checks, document checks, patch checks, and independent rendering review
      pass for the final source snapshot.
- [x] The exact candidate `uvsr.exe` and its source/build identity are recorded
      for handoff; no currently running user-owned UVSR process is disturbed.

## Scope

In scope:

- A first-party DirectX 12 fullscreen FXAA pass adapted from the pinned Google
  Filament implementation and its G3D/NVIDIA FXAA 3.11 lineage.
- Perceptual edge classification over UVSR's tone-mapped display-linear RGBA16F
  source while keeping transfer encoding and dithering in the final output pass.
- Low-through-Ultra Fast Approximate recipes plus runtime controls for edge
  sharpness, relative edge threshold, and minimum edge threshold, with
  Filament defaults and bounded UI/command validation.
- Visible CMAA2 Quality recipes backed by an Advanced runtime edge threshold
  and luma/full-color detector override.
- A user-facing **Multisample Adaptive** name with Quality recipes mapped to
  2x, 4x, 8x, and 16x coverage samples.
- A Fast Approximate timing stage/statistics view and deterministic composition
  before Conservative Morphological.
- Collapsed Advanced disclosures for all four Aliasing techniques.
- Source, UI, command, shader-package, renderer, documentation, and license or
  provenance contracts required by the implementation.

Non-goals:

- Editing pinned Donut, reviving retired SMAA or HDR CMAA2, adding another
  backend, changing default anti-aliasing enablement, or claiming performance.
- Exposing CMAA2's experimental line-search, precision, UAV-layout, or
  scheduling constants without measured product value.
- Integrating the dirty `taa-depth-validation-repair`, `taa-shadow-cleanroom`,
  or `cmaa2-maximum-quality` experiments.
- Commit, push, pull request, merge, release, or deployment.
- Closing, replacing, or interacting with the currently running Canonical UVSR
  process.

Affected subsystems and paths:

- `CMakeLists.txt`, `README.md`, `.gitattributes` only if provenance requires it
- new first-party FXAA pass and shader under `src/`
- `src/temporal_aa_options.h`, `src/uvsr.cpp`, `src/shaders.cfg`
- UI command catalog and focused tests under `tests/`
- `docs/advanced-settings.md` and this execution plan
- third-party notice/license files only if required by the final adaptation

Shared hotspots reserved for the coordinator:

- root build and shader manifests, `src/uvsr.cpp`, AA CPU/HLSL contracts,
  README, maintained documentation, Git state, build trees, and renderer/GPU
  runtime ownership.

## Baseline

- Canonical repository and remote: live `origin/main` is
  `ff5adbd1f4cd9592cfac0fd1ab0e91720027772e`, confirmed by `git ls-remote`.
- Local versus remote state: the root `main` worktree is clean but diverged at
  `5e300c9` (two local commits ahead and nineteen canonical commits behind); it
  remains untouched. This feature worktree starts exactly at live canonical.
- Verified source commit/build: Canonical renderer checkpoint `b4dc241`; exact
  executable SHA-256 `5F3779B0EFDE94B9C9B202BEB5A069265869D111ACE32D1132CA3D16F9430362`.
- Existing AA contract: independently composable deferred MSAA, TAA, tone
  mapping, then display-linear CMAA2; all three techniques default off.
- Runtime lease: Canonical `uvsr.exe` PID 25068 was already running at task
  start. It is user-owned for this task and will not be closed or overwritten.
- Known pre-existing failures: none in Canonical's recorded 30-of-30 suite.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical base | `ff5adbd` documentation tip over renderer `b4dc241` | Confirmed | Whole feature |
| Filament implementation | Official pinned source, algorithm, defaults, color-space and license audit | Complete | FXAA shader/pass |
| Existing AA architecture | Resource, composition, UI, command, tests, and package map | Complete | Integration contract |
| Branch overlap audit | Preserve older dirty AA worktrees and active ownership | Confirmed | Coordinator |

Public interface, binding, resource, and settings contracts:

- `AntiAliasingSettings` gains one independent Fast Approximate settings group;
  `ResolvedAntiAliasingSettings` carries sanitized runtime values.
- Fast Approximate consumes and produces matching single-sample RGBA16F display-
  linear textures at the active render extent.
- Edge luminance is computed in a perceptual display encoding because Filament's
  FXAA contract explicitly rejects linear-light edge classification; sampled RGB
  remains display-linear for correct downstream transfer and dithering.
- Runtime execution order is deferred MSAA, TAA, AgX tone mapping, Fast
  Approximate, Conservative Morphological, then final transfer/dither output.
- Every new setting has the same UI/command mutation boundary and reset behavior
  as existing AA settings.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `AA-FILAMENT` | `/root/filament_fxaa_research` | Read-only | External pinned source | None | None | Complete |
| `AA-EXPLORE` | `/root/aa_repo_map` | Read-only repository | `5e300c9` plus refs | None | None | Complete |
| `AA-LINEAGE` | `/root/aa_lineage_audit` | Read-only repository | `5e300c9` plus refs | None | None | Complete |
| `AA-IMPLEMENT` | `/root` | `codex/fast-approximate-aa` | `ff5adbd` | All scoped files | Research and architecture contract | Complete |
| `AA-REVIEW` | `/root/aa_quality_final_review` | Read-only final tree | Final dirty snapshot | None | Implementation freeze | Complete; no open P0-P2 |
| `AA-FXAA-TIERS` | `/root/fxaa_quality_tiers` | Read-only primary sources | Pinned Filament plus current dirty snapshot | None | None | Complete |
| `AA-QUALITY-MAP` | `/root/aa_quality_architecture` | Read-only repository | Current dirty snapshot | None | None | Complete |
| `AA-CMAA2-AUDIT` | `/root/cmaa2_controls_audit` | Read-only repository | Current dirty snapshot | None | None | Complete |

## Assignment Contracts

### AA-Filament: Audit Filament Fxaa

- Owner/thread: `/root/filament_fxaa_research`
- Base commit/state: official Google Filament primary sources
- Read scope: Filament FXAA shader/material/API and applicable license records
- Write scope: none
- No-touch scope: UVSR files, Git, builds, and renderer
- Interface/invariant contract: report exact URLs/revision, runtime constants,
  color-space requirements, and redistribution obligations
- Deliverable: implementation-ready primary-source handoff
- Done when: the coordinator can adapt the shader without guessing
- Allowed Git and external actions: read-only research only
- Stop and report if: provenance is unclear

### AA-Explore: Map Canonical AA Integration

- Owner/thread: `/root/aa_repo_map`
- Base commit/state: shared repository refs at task start
- Read scope: canonical renderer, settings, UI, commands, tests, shaders, docs
- Write scope: none
- No-touch scope: files, Git state, build trees, and renderer
- Interface/invariant contract: map the exact CPU/HLSL/UI/package seams
- Deliverable: concise path, contract, test, and risk handoff
- Done when: the coordinator can implement without an ownership collision
- Allowed Git and external actions: read-only only
- Stop and report if: active conflicting writes are discovered

### AA-Lineage: Resolve the Base and Overlap

- Owner/thread: `/root/aa_lineage_audit`
- Base commit/state: root `main` at `5e300c9` and all local/remote refs
- Read scope: worktrees, plans, histories, verification records, and open PRs
- Write scope: none
- No-touch scope: files, refs, builds, and processes
- Deliverable: exact base recommendation and collision inventory
- Done when: a safe canonical base is proven
- Handoff: `ff5adbd` selected; older dirty AA worktrees and PID 25068 preserved
- Ownership released: yes

### AA-Implement: Implement and Verify the Feature

- Owner/thread: `/root`
- Branch/worktree: `codex/fast-approximate-aa` in the isolated worktree
- Base commit/state: clean `ff5adbd`
- Read scope: full canonical repository and completed read-only handoffs
- Write scope: scoped implementation, tests, manifests, docs, and this plan
- No-touch scope: Donut, other worktrees/branches, root `main`, remotes,
  generated build output in Git, and running PID 25068
- Build directory and resource lease: worktree-local `build-fast-approximate-aa`;
  no renderer launch while another UVSR process owns the runtime lease
- Dependencies already integrated: canonical base only
- Interface/invariant contract: preserve the contracts stated above
- Deliverable: technically verified local candidate and exact executable
- Done when: every checked done-condition item has evidence
- Required verification: focused tests, clean Release all-target build, complete
  CTest, shader package audit, documentation and patch checks, independent review
- Allowed Git and external actions: local files/branch/build only; no commit or
  publication
- Stop and report if: color-space correctness, license requirements, or active
  ownership cannot be resolved safely

### AA-Review: Review the Frozen Rendering Integration

- Owner/thread: independent read-only reviewer assigned after write freeze
- Base commit/state: final dirty feature snapshot and exact diff identity
- Read scope: all changed source, shader, tests, package and documentation files
- Write scope: none
- No-touch scope: Git state, build tree, processes, and files
- Interface/invariant contract: review resource lifetime, CPU/HLSL layout,
  composability, color space, bounds, UI reset/disclosure behavior, provenance,
  and whether tests exercise the new path
- Deliverable: prioritized findings and explicit release of review ownership
- Done when: no unresolved priority-zero through priority-two finding remains
- Allowed Git and external actions: read-only only

## Integration Order

1. Freeze the Filament algorithm/default/color-space/provenance contract.
2. Extend settings and deterministic reference tests.
3. Add the isolated fullscreen pass and shader, then register the package.
4. Integrate pass lifetime, render order, timing/statistics, UI, and commands.
5. Update maintained documentation and package/provenance records.
6. Build and run focused then complete checks.
7. Freeze the diff, complete independent review, repair findings, and rerun all
   affected verification.
8. Archive this plan and hand off the exact local executable.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Settings/defaults | Deterministic reference and source contracts | Focused AA/UI tests | Passed; all four recipes, sanitization, Custom state, and same-tier reapplication covered |
| Shader fidelity | DXC compile, package membership, pinned-source comparison | Shader target and package tests | Passed; 258 core, 304 first-party, and 380 integrated tasks; 40 staged blobs |
| Color/resource safety | CPU/HLSL review and rendering-source contracts | Focused renderer tests and independent review | Passed; no open P0-P2 after repair and re-review |
| Collapsed Advanced UI | Exact disclosure count/order/default-state contract | UI source-contract test | Passed; eight technique/disclosure trees and four default-closed Advanced initializers |
| Command parity | Catalog count/section and dispatch validation | Command catalog/UI tests | Passed; 130 entries, 126 values, four actions; same-tier Custom recipe reset repaired |
| Complete integration | Clean all-target Release build and full CTest | Isolated build tree | Passed; Release all-target build and 30 of 30 CTest tests |
| Documentation | README counts, Title Case self-test/full scan, patch check | Repository tools | Passed; README 66,303 first-party lines; 1,155 headings and bold lead-ins with zero violations |
| Runtime handoff | Exact executable identity and safe runtime status | Hash/path/process audit | Passed; exact rebuilt UI smoke, task window closed, zero UVSR processes |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-03 | Base on live Canonical `ff5adbd` in a new isolated worktree | Root `main` is diverged and older AA worktrees contain unrelated dirty experiments | All |
| 2026-08-03 | Keep Fast Approximate independently composable and default off | Canonical Aliasing uses independent checkboxes, and the request does not authorize replacing an accepted technique or default | Settings, UI, render order |
| 2026-08-03 | Classify edges perceptually while blending display-linear RGB | Filament requires post-tonemap perceptual luma, while UVSR deliberately defers transfer encoding and dithering until after presentation AA | Shader |
| 2026-08-03 | Use Filament's narrow documented control ranges | Sharpness 2 through 8, relative threshold 0.08 through 0.25, and minimum threshold 0.04 through 0.06 avoid inventing unsupported quality extensions | Settings, UI, commands |
| 2026-08-03 | Ship explicit Filament/G3D/NVIDIA provenance | The HLSL is a modified translation, so its header, packaged attribution, shared Apache-2.0 text, and a BSD-2-Clause text retain the applicable source lineage without copying Filament's large shader verbatim | Shader, package, documentation |
| 2026-08-03 | Do not launch over the existing Canonical UVSR process | Runtime ownership is already occupied and must not be inferred or revoked | Verification |
| 2026-08-03 | Make CMAA2 threshold runtime and compile only detector variants | A 16-byte constant buffer preserves Intel's exact Low-through-Ultra thresholds while two edge-detector PSOs plus three shared pipelines avoid redundant quality permutations | CMAA2 runtime and packaging |
| 2026-08-03 | Preserve direct sample count as a Multisample Advanced override | The visible Quality row supplies the requested 2x/4x/8x/16x recipes while the existing direct setting remains a useful explicit override | Multisample UI and commands |
| 2026-08-03 | Permit same-tier recipe commands only for Custom groups | Reapplying or resetting the selected tier must clear Advanced overrides like the UI; unchanged non-Custom commands retain the established no-op rejection | Aliasing command dispatcher |

## Progress and Handoffs

| Date | Task and Owner | Status | Revision or Artifact | Checks | Risks or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-03 | `AA-LINEAGE` | Complete | Live `ff5adbd`; renderer `b4dc241` | Remote, refs, plans, worktrees, PRs, process audit | Preserve isolated dirty experiments |
| 2026-08-03 | `AA-FILAMENT` | Complete | Filament `47c86ee`, `fxaa.fs` and `fxaa.mat` | Primary-source algorithm, color-space, defaults, ranges, and license audit | Implement modified HLSL adapter |
| 2026-08-03 | `AA-EXPLORE` | Complete | Canonical AA path map | UI, renderer, settings, commands, tests, package, and docs mapped | Implement against `ff5adbd` only |
| 2026-08-03 | `AA-IMPLEMENT` setup | Complete | `codex/fast-approximate-aa` at `ff5adbd` | Clean isolated worktree and frozen contracts | Implemented only in isolated worktree |
| 2026-08-03 | `AA-IMPLEMENT` feature | Complete | Dirty local candidate over `ff5adbd` | FXAA pass, four visible Quality recipes, collapsed Advanced controls, CMAA2 runtime controls, command and package integration | No commit or publication requested |
| 2026-08-03 | `AA-REVIEW` final | Complete | Frozen dirty task diff | Found and repaired same-tier recipe-command P2; repair re-review found no P0-P2 | Ownership released |
| 2026-08-03 | `AA-VERIFY` automated | Complete | `build-fast-approximate-aa` Release | 258 core shader tasks, all-target build, 30 of 30 CTest tests, README/title/patch checks | Technically verified local candidate |
| 2026-08-03 | `AA-VERIFY` runtime | Complete | `uvsr.exe` SHA-256 `77E922DE7968E9BFF6212D36323432D3915C2B1E2E83F1E235DF0D3009966938` | Exact rebuilt UI smoke verified all four cards, Cost, CMAA2 Advanced controls, and collapsed disclosures | Task-owned process closed; zero UVSR processes |

## Risks and Escalation Triggers

- Filament's shader traces through G3D and NVIDIA FXAA notices; the final source
  and package must carry whatever attribution or license text the adaptation
  actually requires.
- FXAA conventionally expects perceptual post-tone-map input, while UVSR's
  presentation intermediate is display-linear. A direct green/alpha-as-luma
  port would be visibly and contractually wrong.
- All four techniques are composable; pass binding, lifetime, and tests must
  cover chained FXAA-to-CMAA2 execution, not only isolated enablement.
- Existing source-contract tests assert exact disclosure counts and shader
  package members; they must be updated semantically rather than weakened.
- The final rebuilt candidate received a task-owned UI smoke only after the
  original user-owned renderer was no longer present; the task-owned window was
  then closed and the handoff process audit found zero UVSR processes.

Stop and ask the user if:

- faithful licensing requires a distribution change the repository cannot
  accommodate without a product decision;
- a material visual default or quality-versus-performance choice cannot be
  derived from Filament's implementation;
- runtime verification requires closing or taking control of the existing UVSR
  process.

## Completion

- Final integrated commit: intentionally uncommitted unless separately requested
- Verification summary: Release all-target build, 258 core and 304 first-party
  shader tasks, 30 of 30 CTest tests, package/source contracts, README line-count
  check, document Title Case self-test and full scan, and patch checks passed
- Independent review: complete; the one P2 command-reset finding was repaired,
  covered, and re-reviewed with no remaining P0-P2 finding
- Coming Soon and documentation update: complete for the local candidate
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: performance characterization is not
  claimed and remains optional future work
- Exact candidate: `build-fast-approximate-aa/bin/uvsr.exe`, 2,535,424 bytes,
  SHA-256 `77E922DE7968E9BFF6212D36323432D3915C2B1E2E83F1E235DF0D3009966938`
- Active ownership released: yes
- Archived path: `docs/exec-plans/completed/fast-approximate-aa.md`
