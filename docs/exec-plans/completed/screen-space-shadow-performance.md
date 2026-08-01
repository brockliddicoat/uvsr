# Screen-Space Shadow Performance Restoration

## Status

- State: complete; locally verified candidate
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/screen-space-shadow-performance` in
  `work/screen-space-shadow-performance`
- Base commit: `402ebb042957eeba8149eee19e857b1e5452880a`
- Started: 2026-07-31
- Last updated: 2026-07-31
- Planned archive:
  `docs/exec-plans/completed/screen-space-shadow-performance.md`

## Goal and Done Condition

Goal: restore the retired ray-coherent screen-space directional-shadow
performance path behind UVSR's generic product interface, comply with the
upstream Apache-2.0 obligations using the smallest honest attribution surface,
and recover and rank earlier shadow-performance follow-ups.

Done when:

- [x] Default 60-pixel shadows use the ray-coherent shared-depth implementation
      without retaining the slower tiled dense-march path as dormant code.
- [x] Runtime product names, normal-output settings, `R8_UNORM` output,
      exact-light association, debug presentation surface, and deferred-lighting
      consumer remain compatible. Optional diagnostics return to the restored
      tracer's honest Edge, Thread, and Wave meanings, and Early Out returns to
      depth-bound receiver culling.
- [x] Required copyright notices and the complete Apache-2.0 license are
      retained and packaged; no unsupported notes-only attribution claim is
      made.
- [x] Focused tests, production shader packaging, Release build, complete CTest,
      document-title validation, diff checks, and independent rendering review
      pass.
- [x] Historical and newly identified optimization ideas are recorded with
      evidence, compatibility, and recommended order; runtime speed is claimed
      only if a matched accepted measurement is completed.

## Scope

In scope:

- The current `ScreenSpaceDirectionalShadowPass`, shader, settings, build and
  package registration, tests, attribution, and maintained documentation.
- Restoring the previously integrated Apache-2.0 implementation or a faithful
  derivative behind generic UVSR names.
- Low-risk performance improvements that preserve the current visible defaults
  and can be verified in the same work item.
- Read-only recovery of prior Hi-Z and other screen-space-shadow proposals.

Non-goals:

- Restoring Bend branding in the runtime UI.
- Removing or weakening required third-party license or copyright notices.
- Adding an unvalidated Hi-Z, temporal, reduced-resolution, stochastic, or
  quality-changing mode merely because it was discussed previously.
- Changing SVSM, diagnostic CSM, AO/GI visibility, TAA, lighting equations, or
  pinned Donut source.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `CMakeLists.txt`, `.gitattributes`, and shader packaging configuration.
- `src/screen_space_directional_shadows*`, `src/uvsr.cpp`, and focused tests.
- `third_party/` license/source records.
- `README.md`, `docs/advanced-settings.md`, `docs/pbr-foundation.md`, and this
  execution plan.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `.gitattributes`, `README.md`, `src/uvsr.cpp`, all shader
  registries, CPU/HLSL bindings, third-party source/license records, builds,
  tests, runtime control, and documentation integration.

## Baseline

- Canonical repository/remote: live `origin/main` at
  `402ebb042957eeba8149eee19e857b1e5452880a`.
- Local versus remote state: the task worktree is clean and equal to live main;
  the root checkout is diverged and contains unrelated user changes that remain
  untouched.
- Verified source commit/build: canonical verification records exist for
  `402ebb0`; establish task-local build evidence before completion.
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090
  Laptop GPU, Intel PBR Sponza Decorated, Benchmark Position 1, 1920x1080,
  60-pixel Default, four hard samples, eight fade samples, debug and optional
  modes off. Record exact live identity before any comparison.
- Known pre-existing failures: none recorded. Open PRs #10 and #11 affect AO/GI
  visibility helpers/tests and do not overlap the directional-shadow paths.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical main | `402ebb0` | Integrated as base | Whole task |
| Retired ray-coherent source | Historical `3ee4ceb` files and Apache-2.0 terms | Integrated and hash-pinned | Shadow implementation |
| Generic directional visibility ABI | Full-resolution linear `R8_UNORM` plus exact light pointer | Frozen | Deferred lighting |
| Historical optimization record | Prior plans, reports, commits, and docs | Recovered and documented | Follow-up ranking |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve `ScreenSpaceDirectionalShadowSettings`, its five trace reaches,
  three hard/fade axes, generic product labels, and default values.
- Preserve one single-sample depth input and one full-resolution `R8_UNORM`
  visibility output where one means unoccluded.
- Preserve pointer-exact directional-light association and the existing
  full-screen debug presentation contract; replace the slow tracer's
  Occlusion, Trace Progress, and Ray Bounds choices with the restored
  algorithm's native Edge, Thread, and Wave diagnostics.
- Licensed upstream or derivative source retains applicable Sony copyright and
  Apache-2.0 identification; distributed artifacts include the complete license.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `license-design` | `/root/license_design` | Read-only shared task worktree | `402ebb0` | None | Historical source and license | Complete |
| `optimization-history` | `/root/optimization_history` | Read-only repository history | `402ebb0` | None | Plans, reports, commits | Complete |
| `performance-audit` | `/root/performance_audit` | Read-only shared task worktree | `402ebb0` | None | Current and retired shaders | Complete |
| `integrate` | `/root` | Task worktree | `402ebb0` | All task paths | Audit handoffs | Complete |
| `independent-review` | `/root/performance_audit` | Frozen candidate | Candidate | None | Integrated candidate | Complete |

## Assignment Contracts

### License Design: Define the Minimum Compliant Restoration

- Owner/thread: `/root/license_design`
- Branch/worktree: read-only task worktree and historical Git objects
- Base commit/state: clean `402ebb0`
- Read scope: retired source, its archive record, Apache-2.0 text, current build
  and packaging rules, and maintained third-party notices
- Write scope: none
- No-touch scope: all files, refs, build trees, processes, and external actions
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: live base
- Interface/invariant contract: recommend only arrangements that retain all
  applicable copyright/license obligations and generic product naming
- Deliverable: exact required files/notices, allowed trimming, and integration
  risks with paths and source evidence
- Done when: the coordinator can implement without guessing at attribution
- Required verification: local source/license comparison plus official
  Apache-2.0 redistribution terms
- Allowed Git and external actions: read-only
- Stop and report if: source provenance or a required NOTICE file is ambiguous
- Handoff revision/artifact: exact upstream CPU/GPU and Apache-2.0 hash audit
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Optimization History: Recover Prior Proposals

- Owner/thread: `/root/optimization_history`
- Branch/worktree: read-only repository and Git history
- Base commit/state: `402ebb0` plus all local historical refs
- Read scope: shadow plans, reports, docs, commit messages, archived branches,
  and relevant source comments
- Write scope: none
- No-touch scope: all files, refs, builds, processes, and external actions
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: separate directional-shadow proposals from AO/GI
  visibility and label measured, estimated, rejected, or unimplemented evidence
- Deliverable: ranked list including Hi-Z and every other recovered proposal,
  with provenance and compatibility notes
- Done when: duplicate and superseded ideas are reconciled
- Required verification: repository-wide text and history search
- Allowed Git and external actions: read-only
- Stop and report if: a proposal's intended subsystem cannot be established
- Handoff revision/artifact: repository and session-history optimization audit
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Performance Audit: Compare Architectures and Find Safe Additions

- Owner/thread: `/root/performance_audit`
- Branch/worktree: read-only task worktree and historical Git objects
- Base commit/state: clean `402ebb0`
- Read scope: current and retired CPU/HLSL paths, settings, dispatch, timers,
  tests, and build permutations
- Write scope: none
- No-touch scope: all files, refs, builds, processes, and external actions
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: generic ABI and current defaults
- Interface/invariant contract: preserve visible default output and exclude
  speculative quality changes from the initial restoration
- Deliverable: minimal restoration design, additional exact optimizations,
  hazards, and required tests
- Done when: recommendations are ranked by likely value, risk, and evidence
- Required verification: source-level workload and resource analysis
- Allowed Git and external actions: read-only
- Stop and report if: a proposed optimization changes visible semantics
- Handoff revision/artifact: current-versus-restored workload and candidate review
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Integrate: Restore and Verify the Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/screen-space-shadow-performance` in the task worktree
- Base commit/state: clean `402ebb0`
- Read scope: full first-party repository, historical source, license evidence,
  pinned dependency APIs, and audit handoffs
- Write scope: all task paths listed above
- No-touch scope: `donut/`, unrelated worktrees/branches, root dirty checkout,
  AO/GI visibility PR paths, remote refs, and unrelated renderer features
- Build directory and runtime/GPU/resource lease:
  `build-shadow-performance`; coordinator only
- Dependencies already integrated: audit decisions before source-sensitive edits
- Interface/invariant contract: frozen contracts listed above
- Deliverable: locally verified candidate, exact executable, completed plan, and
  performance/follow-up report
- Done when: every done condition has evidence or an explicit limitation
- Required verification: focused shadow tests, production shader build, Release
  application, full CTest, title-case validator, `git diff --check`, source and
  package license audit, runtime smoke, and matched timing only after clean
  benchmark preflight
- Allowed Git and external actions: local edits, configure/build/test, and
  task-owned runtime control; no commit, push, PR, merge, release, or deployment
- Stop and report if: compliance conflicts with the requested footprint, a
  visible quality/default change is required, uncertain code must be deleted,
  or a peer/user collision appears
- Handoff revision/artifact: Release candidate in `build-shadow-performance`
- Handoff acknowledged by/on: coordinator-owned completion, 2026-07-31

## Integration Order

1. Complete license, history, and performance audits and freeze the restoration
   boundary.
2. Restore the ray-coherent implementation behind the generic interface.
3. Apply only independently justified exact optimizations.
4. Build and run deterministic checks.
5. Freeze the candidate for independent rendering/license review.
6. Repair task-introduced findings, rerun affected checks, and complete the
   runtime/performance evidence that the machine state safely allows.
7. Reconcile maintained docs and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Generic interface preserved | Source/API/resource comparison and focused tests | Shadow reference test and source audit | Passed; focused test and full suite cover settings, planner, adapter, shader, and package contracts |
| Ray-coherent implementation active | Shader/source and packaged-permutation inspection | Production shader build and manifest test | Passed; 45 compiled variants plus debug shader, and runtime/production bundle tests passed |
| License obligations met | Copyright/header, full license, and package audit | Hash/text/package inspection | Passed; both exact upstream hashes and the source/package Apache-2.0 hashes match their CMake pins |
| Renderer health | Release build and complete deterministic suite | CMake build and CTest | Passed; Release `uvsr`, all targets, and 33 of 33 CTests passed |
| Documentation health | All visible headings in Title Case | Repository title-case checker | Passed; 31-case self-test and 965 headings or bold lead-ins with zero violations |
| Rendering safety | Independent source review and responsive runtime smoke | Frozen-candidate review and task-owned launch | Passed; no remaining P0-P2 finding, and exact candidate opened a responsive D3D12 window at High priority |
| Performance | Matched pass and total-frame timing under clean preflight | Position-1 60-pixel A/B | Not run; no current timing claim. Historical restored-path observations are labeled unmatched |
| Follow-up roadmap | Ranked sourced list of prior and new ideas | History audit and final documentation | Complete in `docs/pbr-foundation.md` |

For performance work, record:

- baseline and candidate commits or complete dirty identities;
- GPU, scene, fixed camera, resolution, and settings preset;
- warmup and sample window/count;
- total frame time plus directional-shadow trace cost;
- correctness and image-quality guardrails;
- before/after captures and raw measurement artifact.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-31 | Base the task on live `origin/main` `402ebb0` in an isolated worktree. | The root checkout is diverged and dirty; the live target is current and clean. | Whole task |
| 2026-07-31 | Preserve generic runtime naming while retaining mandatory Apache-2.0 source/license attribution. | Notes-only attribution would not satisfy the license's redistribution conditions; restoring Bend branding in product UI is unnecessary. | License, source, UI, docs |
| 2026-07-31 | Use one coordinator as the only writer and build/GPU operator. | CPU/HLSL, build, packaging, licensing, and UI contracts are tightly coupled shared hotspots. | Integration and verification |
| 2026-07-31 | Restore the two upstream headers byte-for-byte, omit the former dedicated README, keep one maintained provenance paragraph, and package one shared Apache-2.0 license. | This is the smallest conservative redistribution layout: notes alone are insufficient, while runtime branding and a dedicated credit file are not required. | License, source, package, docs |
| 2026-07-31 | Restore native Edge, Thread, and Wave diagnostics plus depth-bound Early Out semantics. | Preserving the replacement tracer's optional meanings would require a licensed algorithm derivative or a second tracer. All affected options default off, so normal output and default performance remain unchanged. | Settings, UI, CLI, tests, docs |
| 2026-07-31 | Defer push constants, default-only specialization, integer loads/gathers, clear removal, Hi-Z, reduced resolution, temporal reuse, stochastic sampling, and async compute. | The direct restoration already removes the dominant regression. The exact ideas need matched measurements; the remaining ideas add correctness or quality risk and belong in isolated follow-ups. | Implementation and roadmap |
| 2026-07-31 | Preserve the Apache-2.0 license as LF bytes with an explicit `-text` rule. | Independent review found that a hash-pinned file could otherwise be rewritten on checkout; the attribute and focused test now protect the packaged source identity. | License and verification |
| 2026-07-31 | Do not report a current speedup without a matched Position-1 A/B session. | The bounded candidate launch proved responsiveness only. Historical 0.098-0.106 ms data establishes provenance, not current performance. | Performance report |

## Progress and Handoffs

| Date/Time | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-31 | `/root` | Started | Clean `402ebb0` task worktree | Live remote, worktree, branch, plan, PR, and Coming Soon preflight | Complete audits before editing source |
| 2026-07-31 | `/root/license_design` | Complete | Read-only audit of historical and live upstream sources | Official archive/header/license hashes and Apache-2.0 redistribution audit | Use untouched headers, shared license, and one provenance paragraph |
| 2026-07-31 | `/root/performance_audit` | Complete | Read-only current-versus-historical workload audit | CPU/HLSL, settings, resources, permutations, and retired timing evidence | Restore native optional semantics and preserve current finite-light validation |
| 2026-07-31 | `/root/optimization_history` | Complete | Read-only repository/session history audit | Plans, reports, commits, and July 18 design discussion | Report measured versus proposed ideas in final handoff |
| 2026-07-31 | `/root` integration | Complete | Dirty local candidate based on `402ebb0` | 46 shader tasks, full Release application and all-target builds, focused test, and 33 of 33 CTests passed | Keep candidate local unless publication is separately authorized |
| 2026-07-31 | `/root/performance_audit` independent review | Complete after repair | Frozen candidate plus `.gitattributes` repair | Initial license-byte P1 resolved; focused re-review found no remaining P0-P2 issue | No rendering-review blocker remains |
| 2026-07-31 | `/root` documentation and package audit | Complete | Maintained docs, exact upstream sources, shared license, and package | README counts current at 136,151 first-party, 388,222 third-party, and 524,373 total; title scan 965/0; hash and diff checks passed | Archive this plan after final status audit |
| 2026-07-31 | `/root` runtime smoke | Complete | `build-shadow-performance/bin/uvsr.exe` | Responsive D3D12 window, experiment title `shadowrestore-402ebb0-1421`, High priority; exact task-owned PID closed afterward | This is not matched performance evidence |

## Risks and Escalation Triggers

- Restored source may require more attribution than the user's preferred notes
  footprint; mandatory notices take precedence over cosmetic minimization.
- The prior implementation was measured only historically, not against the
  current canonical renderer under a matched accepted session.
- Shader/package changes are high-risk and require independent review.
- Hi-Z, reduced resolution, temporal reuse, and stochastic sampling can change
  image behavior and remain follow-ups unless separately justified and verified.

Stop and ask the user if:

- required attribution is unacceptable even when kept out of the runtime UI;
- restoring performance requires a visible shadow-quality/default tradeoff;
- an external publication action is requested without a clear destination.

## Completion

- Final integrated commit: none; no commit was authorized
- Verification summary: Release application and all-target builds passed; 33 of
  33 CTests passed; focused shadow, runtime bundle, production bundle, source
  hashes, packaged license, README counts, Title Case, diff, and responsive
  launch checks passed
- Independent review: complete; the only P1 finding was repaired and the
  focused re-review found no remaining P0-P2 issue
- Coming Soon/documentation update: complete
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: matched current A/B measurement, then
  default specialization, wave-offset push constant, load/gather experiments,
  proven clear removal, chunked LDS for long rays, and only afterward the
  separately validated Hi-Z/far-field research path
- Active ownership released: yes
- Archived to completed/abandoned path: completed path
