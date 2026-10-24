# TAA Previous-Depth Validation Repair

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/taa-depth-validation-repair` in
  `work/taa-depth-validation-repair`
- Base commit: `402ebb042957eeba8149eee19e857b1e5452880a`
- Started: 2026-07-31
- Last updated: 2026-07-31
- Planned archive:
  `docs/exec-plans/completed/taa-depth-validation-repair.md`

## Goal and Done Condition

Goal: replace the unacceptable choice between stationary silhouette swimming
and moving-edge ghost highlights with one source-grounded previous-depth
validation policy that is stable at rest and safely rejects disoccluded history
under motion.

Done when:

- [x] UVSR's motion, jitter, previous-depth, and history-color coordinate
      contracts have been traced against MiniEngine, Wicked Engine, and at least
      three other primary-source temporal implementations.
- [x] The smallest evidence-backed repair is implemented without adding a
      speculative compatibility path or changing unrelated TAA behavior.
- [x] Focused reference tests cover stationary jitter, subpixel and integer
      motion, silhouettes, disocclusion, background, and viewport boundaries.
- [x] A task-local Release build and relevant automated tests pass.
- [ ] The exact candidate is smoke-tested and presented for the user's visual
      comparison, with any unverified dynamic-image claim stated explicitly.
- [x] A read-only independent rendering review finds no unresolved P0-P2 issue.

## Scope

In scope:

- Previous-depth sampling coordinates and footprint selection.
- Reverse-Z geometry/background classification and disocclusion confidence.
- Current-to-previous motion and jitter conversion used by depth validation.
- The exposed validation setting/default only if the implemented repair makes
  an old choice obsolete or misleading.
- Focused CPU/reference, source-contract, shader-package, documentation, build,
  and runtime checks required by the repair.

Non-goals:

- A wholesale TAA rewrite, new temporal upscaler, dynamic-resolution support,
  or unrelated sharpening/rectification/filter changes.
- Performance benchmarking unless the repair adds material GPU work.
- Push, pull request, merge, release, or canonical promotion.

Affected subsystems and paths:

- `src/temporal_aa_blend_cs.hlsl`
- `src/temporal_aa_common.hlsli`
- `src/temporal_aa_options.h` and `src/temporal_aa_options_shared.h` if needed
- `src/temporal_aa_reference.h`
- `src/uvsr.cpp` if exposed controls or guidance change
- `tests/temporal_aa_tests.cpp` and relevant shader/source-contract tests
- `docs/temporal-aa-options.md`, this plan, and `README.md`

Shared hotspots reserved for the coordinator:

- All first-party source and test edits.
- `README.md`, `src/uvsr.cpp`, shader contracts, task plan, build tree, and UVSR
  runtime/GPU control.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`; live `origin/main`
  `402ebb042957eeba8149eee19e857b1e5452880a`, confirmed by `git ls-remote` on
  2026-07-31.
- Local versus remote state: task branch starts equal to live `origin/main` in
  a clean isolated linked worktree. The primary checkout is unrelated, dirty,
  and diverged and will not be modified.
- Verified source commit/build: `402ebb0` has a clean detached verification
  worktree and task-adjacent production/factory artifacts; exact canonical
  verification evidence is being audited before reuse. The task will build its
  own candidate and will not call it Canonical verified.
- GPU, scene, camera, resolution, and settings preset when relevant: use the
  user's current reproduction settings when discoverable; otherwise use PBR
  Sponza Decorated, a fixed thin-silhouette view, 1920x1080, and compare a fully
  stationary camera against controlled camera motion with all other TAA
  settings fixed.
- Known pre-existing failures: none yet established for this task-local build.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `402ebb0` | Integrated as base | Whole repair |
| Upstream MiniEngine and Wicked audit | Exact source behavior and conventions | Complete | Root-cause decision |
| Other temporal implementation audit | Edge-safe validation patterns | Complete | Root-cause decision |
| UVSR forensic audit | Exact producer/consumer equations | Complete | Patch and tests |
| Independent review | Frozen candidate source snapshot | Complete | Final handoff |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Motion is expected to remain finite current-to-previous displacement in
  full-resolution pixels.
- History color remains on the resolved unjittered grid; raw previous depth
  remains on its producing jittered grid unless evidence proves that contract
  itself is the defect.
- Reverse-Z device depth zero remains background and must never authorize
  temporal geometry history.
- No resource or binding change is allowed unless a coordinate-only repair is
  proven insufficient.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `upstream-taa-audit` | `/root/upstream_taa_audit` | Read-only | `402ebb0` context | None | Public source | Complete |
| `uvsr-taa-audit` | `/root/uvsr_taa_audit` | Read-only | `402ebb0` | None | Canonical source | Complete |
| `temporal-rejection-research` | `/root/temporal_rejection_research` | Read-only | `402ebb0` context | None | Public source | Complete |
| `integrate` | `/root` | This worktree | `402ebb0` | All task paths | Audit handoffs | Active |
| `independent-review` | `/root/independent_rendering_review` | Read-only frozen candidate | Candidate | None | Integrated patch | Complete |

## Assignment Contracts

### Upstream TAA Audit: Establish MiniEngine and Wicked Behavior

- Owner/thread: `/root/upstream_taa_audit`
- Branch/worktree: read-only shared repository context
- Base commit/state: `402ebb0` context plus official public sources
- Read scope: official MiniEngine and Wicked Engine TAA source and relevant UVSR
  comparison points
- Write scope: none
- No-touch scope: all files, Git state, builds, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: verify rather than assume motion, jitter, depth,
  and texel-center conventions
- Deliverable: cited behavior comparison, ranked bug hypotheses, and repair/test
  recommendations
- Done when: exact files/functions support every material comparison
- Required verification: primary-source inspection
- Allowed Git and external actions: read-only
- Stop and report if: upstream source or conventions cannot be established
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### UVSR TAA Audit: Trace the Complete Local Contract

- Owner/thread: `/root/uvsr_taa_audit`
- Branch/worktree: read-only canonical tree
- Base commit/state: `402ebb0`
- Read scope: TAA source, motion/depth producers, tests, documentation, and
  relevant history
- Write scope: none
- No-touch scope: all files, Git state, builds, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: trace exact pixel-center, jitter, reverse-Z,
  motion, footprint, and history-validity equations
- Deliverable: exact defect evidence, candidate patch design, and test gaps
- Done when: HLSL and CPU/reference contracts are reconciled end to end
- Required verification: read-only source, tests, and history inspection
- Allowed Git and external actions: read-only
- Stop and report if: canonical source identity is ambiguous
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Temporal Rejection Research: Compare Other Renderers

- Owner/thread: `/root/temporal_rejection_research`
- Branch/worktree: read-only public research
- Base commit/state: `402ebb0` context
- Read scope: official open temporal reconstruction implementations and vendor
  documentation beyond MiniEngine and Wicked Engine
- Write scope: none
- No-touch scope: workspace, Git state, builds, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: separate geometric disocclusion from color
  clipping/reactivity and report source fact versus inference
- Deliverable: at least three source-backed comparisons and ranked applicable
  validation patterns
- Done when: a concrete edge-safe repair direction is supported or rejected
- Required verification: primary sources only
- Allowed Git and external actions: read-only
- Stop and report if: access prevents useful source verification
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Integrate: Implement and Verify the Repair

- Owner/thread: `/root`
- Branch/worktree: `codex/taa-depth-validation-repair`,
  `work/taa-depth-validation-repair`
- Base commit/state: clean `402ebb0`
- Read scope: full first-party repository, pinned dependency APIs, completed TAA
  plans, and cited public sources
- Write scope: affected first-party source, focused tests, task documentation,
  this plan, and README Coming Soon
- No-touch scope: `donut/` source, other worktrees, primary dirty checkout,
  unrelated renderer features, generated build content, and remote state
- Build directory and runtime/GPU/resource lease:
  `build-taa-depth-validation-repair-v2`; coordinator alone controls it and any
  UVSR process. The original tree remains locked by the user's rejected-candidate
  comparison window and was not disturbed
- Dependencies already integrated: audit handoffs before final algorithm choice
- Interface/invariant contract: preserve established public contracts unless
  exact evidence identifies one as the bug; fail closed at disocclusion without
  converting stationary jitter into binary history flicker
- Deliverable: task-local verified candidate and concise root-cause report
- Done when: focused and integration checks pass, the candidate launches, and
  independent review clears the source
- Required verification: Release shader/application build, focused and full
  CTest as proportional, title-case validation, `git diff --check`, runtime
  smoke, and fixed-view stationary/motion comparison where safe
- Allowed Git and external actions: local edits, task-local build/tests/runtime;
  no commit, push, PR, merge, release, or deployment
- Stop and report if: the repair requires a broad resource redesign, unrelated
  renderer changes, uncertain deletion, or a user-visible quality/performance
  policy choice not resolved by evidence
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Independent Review: Audit the Frozen Candidate

- Owner/thread: `/root/independent_rendering_review`
- Branch/worktree: read-only task worktree
- Base commit/state: frozen candidate diff on `402ebb0`
- Read scope: full task diff and affected TAA producer/consumer contracts
- Write scope: none
- No-touch scope: all files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: completed implementation and tests
- Interface/invariant contract: review coordinate math, disocclusion safety,
  reverse-Z ordering, bounds, shader permutation coverage, and test strength
- Deliverable: prioritized findings with exact evidence
- Done when: no P0-P2 issue remains or every issue is repaired and re-reviewed
- Required verification: read-only diff/source/test inspection
- Allowed Git and external actions: read-only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

## Integration Order

1. Freeze `402ebb0` as the clean task base and reproduce the complete current
   equations from producer through history validation.
2. Reconcile UVSR behavior with MiniEngine, Wicked Engine, and other cited
   temporal implementations.
3. Choose one smallest repair and encode its invariants in CPU/reference tests.
4. Patch the shader and only the control/documentation surface made obsolete by
   that repair.
5. Build and run focused then integration checks.
6. Smoke-test stationary and moving cases, freeze the candidate, and obtain an
   independent rendering review.
7. Repair any task-introduced issue, rerun invalidated checks, and archive this
   plan with exact evidence.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Exact canonical base | Live remote SHA and clean task worktree | `git ls-remote`, `git status`, `git rev-parse` | `402ebb0`, clean at creation |
| Coordinate contract | End-to-end equations with source citations | Source audit and focused fixtures | Complete: MiniEngine, Wicked, FSR2/3, Playdead, Filament, Falcor, Intel, and Unreal cross-check |
| Stationary stability | Validation result does not alternate solely from the jitter phase | CPU/reference jitter-phase fixture and fixed-camera smoke | Eight-phase interior and viewport-edge fixtures pass; visual replacement pending |
| Moving silhouette safety | Background/foreground disocclusion cannot retain unrelated bright history | Motion/disocclusion fixtures and moving-camera smoke | Center/selected-motion and one-sided near/far depth fixtures pass; visual replacement pending |
| Bounds and invalid data | No clamped synthetic history or NaN authorization | Edge/background/non-finite fixtures | Point, Gather, stationary, background, and non-finite fixtures pass |
| Shader coverage | Every affected permutation compiles and packages | Task-local Release build and shader tests | 601/601 production tasks plus 54 diagnostic CSM, 2 SSD, and 105 SVSM tasks pass |
| Integration health | Relevant/full tests, clean diff, title-case docs | CTest, document checker, `git diff --check` | 33/33 CTest pass; 965 headings pass; diff check clean |
| Runtime viability | Exact candidate launches and remains responsive | Task-local launcher/smoke | Pending |
| Independent review | No unresolved P0-P2 finding | Frozen diff audit | Complete on tracked patch `80309777983fc52ffdeeae78e39b7ce225dcb471` |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-31 | Start from live `origin/main` `402ebb0` in a new linked worktree | It contains the exact validation modes the user observed; the primary checkout is stale, dirty, and diverged | Whole repair |
| 2026-07-31 | Use one coordinator as the only writer and runtime operator | The shader, CPU reference, options, UI, and tests are a tightly coupled contract | Integration and verification |
| 2026-07-31 | Treat both reported artifacts as evidence of one validation-policy defect until disproven | Binary full-footprint rejection can shimmer under jitter, while a stationary bypass can retain disoccluded high-contrast history; tuning either side alone would preserve the tradeoff | Research and patch choice |
| 2026-07-31 | Preserve MiniEngine's coordinate sign and farthest-depth ordering | Official source confirms UVSR's resolved-color coordinate, raw-depth jitter offset, and reverse-Z farthest reduction are correct; changing them would introduce a new bug | Shader and reference contract |
| 2026-07-31 | Reject contributor-weighted raw previous depth after visual review | At a stationary silhouette, the resolved history-color coordinate is stable but the raw previous-depth coordinate follows the Halton jitter delta. The weighted support therefore changed by 3-6x across the eight jitter phases and made edges shake. FSR2's superficially similar weighting operates on a separately reconstructed previous-nearest-depth field and does not validate weighting UVSR's raw prior raster | Shader, defaults, tests, and UI guidance |
| 2026-07-31 | Keep Stationary Bypass and repair center ownership during edge dilation | The visually improved path borrows only a foreground neighbor's XY reprojection. It retains center depth, depth delta, validity, and output ownership, so a neighboring foreground cannot authorize its history on a farther or invalid center. Stationary pixels remain independent of raw-depth jitter phase; moving pixels keep the jitter-correct point test | Shader, defaults, tests, and UI guidance |
| 2026-07-31 | Retain R32 depth in the Minimum path and move current-sample containment before resurrection | Reverse-Z depth remains a geometry signal across cost modes, while resurrection must not clip an older accepted sample against bounds that exclude the current sample eventually blended | Shader, resource format, tests, and documentation |
| 2026-07-31 | Validate moving point depth in linear view space with one-sided ownership | The old symmetric device-depth floor became extremely permissive at far reverse-Z depth. A 1% relative view-depth allowance preserves slope tolerance while rejecting stale foreground consistently; farther history remains conservative and is constrained by color rectification | Shared shader helper, both TAA paths, and reference tests |
| 2026-07-31 | Require center and selected XY motion to be stationary | Edge dilation intentionally borrows foreground XY while retaining center geometry. Checking only the borrowed vector let a stationary neighbor hide a moving center and bypass depth, so both vectors must remain below the 0.01-pixel threshold | Quality shader, CPU reference, and tests |
| 2026-07-31 | Make history bounds follow the sampling policy | Stationary Bypass reads no previous depth and therefore requires only color bounds. Moving samples require the single point texel; Legacy Four-Texel requires the full linear/Gather footprint. This prevents unused jittered depth coordinates from phase-toggling viewport-edge history | Both shader paths, high-order history filtering, and tests |

## Progress and Handoffs

| Date/Time | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-31 | `integrate` `/root` | Active | Branch/worktree at clean `402ebb0` | Live remote and worktree identity confirmed | Await audits while tracing source and preparing fixtures |
| 2026-07-31 | `upstream-taa-audit` `/root/upstream_taa_audit` | Complete | Official MiniEngine `a19ee605`, Wicked `3a800b7`, and FSR2 `1680d1e` source audit | Motion, jitter, depth, filtering, and reconstructed-depth contracts cross-checked | Revised recommendation after visual evidence: center-owned Stationary Bypass plus moving point; reject weighted raw depth |
| 2026-07-31 | `uvsr-taa-audit` `/root/uvsr_taa_audit` | Complete | Read-only `402ebb0` forensic audit | Producer/consumer equations and option paths traced | Found borrowed foreground-depth/center-color mismatch and unequal footprints/tolerances |
| 2026-07-31 | `temporal-rejection-research` `/root/temporal_rejection_research` | Complete | Official FSR2/3, Cauldron, Playdead, Filament, Falcor, Intel, and Unreal sources | Primary-source comparison completed | Recommended contributor-aware geometry confidence and confidence-tightened rectification |
| 2026-07-31 | `integrate` `/root` | Rejected candidate | Dirty candidate diff `fafbf3eca2798b058a8bbffbdca9bcf7cbeee973` | 601/601 production shader tasks, application build, 33/33 CTest, and heading validation passed before visual review | User found severe stationary edge shaking; weighted raw-depth support and surface-owned bounds removed |
| 2026-07-31 | `integrate` `/root` | Candidate | Tracked patch `80309777983fc52ffdeeae78e39b7ce225dcb471`; `build-taa-depth-validation-repair-v2/bin/uvsr.exe`; SHA-256 `63433D5449C9273CECF09232A9A8399752F6361A981C274EC101BCC60AC7B666` | 601/601 production shader tasks, Release application build, focused test, and 33/33 CTest pass | Run final title/diff checks; exact visual smoke waits for the old rejected-candidate window to close |
| 2026-07-31 | `independent-review` `/root/independent_rendering_review` | Complete, read-only | Frozen tracked patch `80309777983fc52ffdeeae78e39b7ce225dcb471` on `402ebb0` | No unresolved P0-P2 findings; center/selected stationarity, policy-specific bounds, high-order bypass, Minimum parity, UI, and documentation confirmed | Source ownership released |

## Risks and Escalation Triggers

- A luminance highlight can also come from an incorrectly weighted Catmull-Rom
  history tap or color-space/clipping defect; do not force a depth-only theory
  if the exact shader path contradicts it.
- A soft depth confidence can leak history across real disocclusions unless the
  geometry/background and foreground-occlusion cases remain hard rejects.
- Previous depth is raw jittered depth. Sampling it at the unjittered history
  coordinate or applying jitter twice can produce the exact stationary phase
  instability under investigation.
- Runtime image evidence requires exclusive UVSR/GPU control and must not race
  the user's interaction or another experiment.

Stop and ask the user if:

- evidence leaves two materially different visible quality policies with no
  defensible default;
- required runtime reproduction depends on settings or a scene that cannot be
  recovered and substituting a scene would make the result misleading;
- a fix would require unrelated rendering changes or publication authority.

## Completion

- Final integrated commit: intentionally uncommitted unless the user later asks
  to save it
- Verification summary: Release candidate built; 601 production shader tasks and
  33/33 CTest pass; visual smoke remains pending because the user is still
  inspecting the rejected executable
- Independent review: no unresolved P0-P2 findings on frozen tracked patch
  `80309777983fc52ffdeeae78e39b7ce225dcb471`
- Coming Soon/documentation update: complete for the active local candidate
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: pending
- Active ownership released: no
- Archived to completed/abandoned path: pending
