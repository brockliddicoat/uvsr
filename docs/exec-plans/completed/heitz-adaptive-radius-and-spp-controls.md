# Heitz Adaptive Radius and Equal Sample Counts

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/heitz-ratio-estimator-repair` in
  `work/heitz-ratio-estimator-repair`
- Base commit: `5d15efd730c574e433bfc58ea4727010ff3a6619` plus the
  frozen local Heitz ratio-estimator repair candidate
- Started: 2026-08-17
- Last updated: 2026-08-17
- Planned archive:
  `docs/exec-plans/completed/heitz-adaptive-radius-and-spp-controls.md`
- Prior completed plan:
  `docs/exec-plans/completed/heitz-ratio-estimator-repair.md`

## Goal and Done Condition

Goal: make UVSR's estimator comparisons honest at equal samples per pixel,
explain and correct the sky-visibility control semantics, and add the Heitz
demo's adaptive spatial radius as a safe opt-in extension of the paired
filter-before-divide path.

Done when:

- [x] Directional-sun ratio on/off can be compared at the same configured SPP.
- [x] Sky visibility uses its configured SPP independently of the legacy
      ratio-named compatibility field, with accurate UI and documentation.
- [x] Adaptive paired filtering is available behind a default-off toggle and
      has bounded, finite, viewport-safe behavior plus deterministic tests.
- [x] Serialized settings, commands, snapshots, CPU/HLSL bindings, resource
      lifetime, sparse scheduling, MSAA, GI ownership, and denoiser fallback
      remain coherent.
- [x] All production shaders, focused and full CTest, documentation, packaging,
      and independent rendering review pass.
- [x] The exact final executable is handed to Brock with the runtime evidence
      boundary stated honestly.

## Scope

In scope:

- Directional-sun classic and Heitz ratio sampling at equal configured SPP.
- Ray-traced sky visibility sample-count semantics, UI copy, compatibility,
  hit-distance validity, and denoiser eligibility.
- Heitz-style adaptive radius for the existing 1x paired spatial filter behind
  a default-off control.
- Necessary settings, command, snapshot, CPU/HLSL, shader-package, tests, and
  durable documentation updates.
- User-provided provenance statement that Eric Heitz registered the downloaded
  demo code under the MIT License, while keeping copied scope narrow and
  recording any remaining provenance uncertainty accurately.

Non-goals:

- Changing UVSR's default visual output or enabling adaptive radius by default.
- Adding a new rectangular-area-light product model, scene schema, LTC asset
  dependency, or renderer-wide light transport redesign unless proven necessary
  for the requested adaptive radius.
- Editing Donut or the downloaded reference tree.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/heitz_ratio_estimator_shadows*`
- `src/heitz_ratio_estimator_filter_cs.hlsl`
- `src/ray_traced_sky_visibility*`
- `src/uvsr.cpp`, settings commands/snapshots, and shader packaging
- Estimator, sky, renderer, UI, packaging, and settings tests
- Ratio-estimation, PBR, advanced-settings, UI, and related documentation

Shared hotspots reserved for the coordinator:

- All writable paths in the shared task worktree
- CPU/HLSL interfaces, global settings/UI, `src/shaders.cfg`, CMake, and README
- Build trees, renderer windows, GPU work, integration, and plan closeout

## Baseline

- Canonical repository/remote: user-named main snapshot
  `5d15efd730c574e433bfc58ea4727010ff3a6619`; this follow-up remains on the
  existing local feature branch and does not claim live-main integration.
- Local versus remote state: task worktree is intentionally dirty with the
  completed first ratio-repair candidate; unrelated original-checkout work is
  untouched.
- Verified source commit/build: prior candidate compiled 314 production shaders,
  linked Release, and passed 47/47 CTest; exact executable SHA-256
  `7BA96351213A025308D32EFD5A9F5A6F8560F9E1181233D9A7B7043F6E178726`.
- GPU, scene, camera, resolution, and settings preset when relevant: PBR Sponza,
  Benchmark Position 1, 1920x1080; compare fixed/adaptive and classic/ratio at
  equal SPP only after the existing user-active UVSR process is no longer open.
- Known pre-existing failures: none in automated checks; exact prior-final
  runtime smoke remained unrun because the immediately preceding candidate was
  user-active and could not safely be closed or duplicated.

## Dependencies and Interfaces

| Dependency or Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior ratio repair | Frozen dirty candidate and completed plan | Integrated | Whole task |
| ADAPTIVE-AUDIT | Exact reference algorithm and safe resource contract | Complete | Implementation |
| SKY-SEMANTICS | Exact behavior delta and compatibility-safe UI model | Complete | Implementation |
| CLASSIC-SPP | Equal-SPP mode matrix and one-SPP semantics | Complete | Implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve `1.0` visible and `0.0` occluded at every lighting consumer.
- Ratio and classic modes must use the same requested SPP for a meaningful A/B.
- One-SPP ratio is permitted but must be documented as a degenerate
  control-variate case whose variance benefit is generally weak or absent.
- Adaptive radius is opt-in, finite, viewport bounded, and falls back to the
  existing fixed radius when required evidence or resources are unavailable.
- The legacy sky field/command must not silently retain misleading visible
  semantics; compatibility treatment must be explicit and tested.
- Any new represented setting follows the snapshot schema/version registry.

## Assignment Summary

| Task ID | Owner | Branch or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| ADAPTIVE-AUDIT | `/root/adaptive_radius_audit` | Shared read-only | Frozen candidate plus demo | None | Prior repair | Complete; released |
| SKY-SEMANTICS | `/root/sky_semantics_audit` | Shared read-only | Frozen candidate | None | Prior repair | Complete; released |
| CLASSIC-SPP | `/root/classic_spp_audit` | Shared read-only | Frozen candidate | None | Prior repair | Complete; released |
| IMPLEMENT | `/root` | Task worktree | Reconciled audit contract | All final files | Three audits | Complete |
| REVIEW | Three read-only reviewers | Task worktree | Frozen final candidate | None | IMPLEMENT | Complete; released |

## Assignment Contracts

### Adaptive-Audit: Map the Reference Radius

- Owner/thread: `/root/adaptive_radius_audit`
- Branch/worktree: shared task worktree, read-only
- Base commit/state: `5d15efd` plus frozen first repair candidate
- Read scope: downloaded reference denoiser and current paired filter/settings
- Write scope: none
- No-touch scope: files, Git, builds, processes, and GPU
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: prior ratio-repair candidate
- Interface/invariant contract: separate the essential adaptive-radius math from
  rectangular-light/LTC-only inputs and retain UVSR's safe bounds/fallbacks
- Deliverable: source-cited algorithm, resource/ABI design, tests, and risks
- Done when: every derivative, prefilter, guide, threshold, and pass dependency
  required by the radius decision is accounted for
- Required verification: cross-check reference CPU dispatch and all denoiser
  shaders against current UVSR passes
- Allowed Git and external actions: read-only local inspection
- Stop and report if: source changes during audit or implementation requires an
  undecided renderer-wide lighting contract
- Handoff revision/artifact: final frozen math/resource review passed with one
  accepted low-energy-threshold fidelity note
- Handoff acknowledged by/on: `/root`, 2026-08-17

### Sky-Semantics: Prove and Repair the Control Meaning

- Owner/thread: `/root/sky_semantics_audit`
- Branch/worktree: shared task worktree, read-only
- Base commit/state: `5d15efd` plus frozen first repair candidate
- Read scope: sky settings/pass/shader/UI/commands/snapshots/docs/tests/history
- Write scope: none
- No-touch scope: files, Git, builds, processes, and GPU
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: prior ratio-repair candidate
- Interface/invariant contract: distinguish a binary multi-ray mean from a
  Heitz response ratio and preserve compatibility without misleading UI
- Deliverable: exact base/current behavior matrix and recommended final model
- Done when: sample count, hit distance, denoising, serialization, and UI effects
  are traced end to end
- Required verification: source and history cross-checks
- Allowed Git and external actions: read-only local inspection
- Stop and report if: source changes or a schema decision has two materially
  different user-visible outcomes
- Handoff revision/artifact: final sky and equal-sample semantics review passed
- Handoff acknowledged by/on: `/root`, 2026-08-17

### Classic Samples per Pixel: Design Equal-Sample Sun Comparisons

- Owner/thread: `/root/classic_spp_audit`
- Branch/worktree: shared task worktree, read-only
- Base commit/state: `5d15efd` plus frozen first repair candidate
- Read scope: sun settings/pass/shader/UI/scheduling/MSAA/GI/tests/docs/reference
- Write scope: none
- No-touch scope: files, Git, builds, processes, and GPU
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: prior ratio-repair candidate
- Interface/invariant contract: classic and ratio modes use matched SPP without
  pooled MSAA evidence, stale sparse reads, or changed closest-owner GI
- Deliverable: complete mode matrix, one-SPP explanation, minimal safe changes,
  and test list
- Done when: all hard/soft, 1x/MSAA, sparse, denoise, and estimator combinations
  have explicit behavior
- Required verification: static source and test/history audit
- Allowed Git and external actions: read-only local inspection
- Stop and report if: source changes or equal-SPP comparison requires a new
  product-quality default
- Handoff revision/artifact: final adversarial integration review passed after
  its paired-resource accumulation edge was repaired and re-reviewed
- Handoff acknowledged by/on: `/root`, 2026-08-17

## Integration Order

1. Reconcile all three read-only audits into one settings and shader contract.
2. Implement settings/UI and CPU/HLSL changes serially under coordinator
   ownership.
3. Add production-coupled math, source, snapshot, sparse/MSAA/GI, and packaging
   coverage.
4. Freeze writes, build and test in a dedicated follow-up tree, then request an
   independent high-risk rendering review.
5. Repair confirmed findings, rerun invalidated checks, archive the plan, and
   hand off the exact local executable.

## UI Intake

- Policy versions: `AGENTS.md` `2026-07-30.1`; UI reference
  `2026-08-17.1`.
- State owner/default: `HeitzRatioEstimatorShadowSettings` owns new
  `adaptiveFilterRadius = false`; the existing sun and sky `sampleRateLog2`
  values remain authoritative and default to two SPP.
- Control class/range/commands: one boolean **Adaptive Radius** control for the
  Heitz paired Gaussian filter; existing logarithmic 1/2/4/8/16/32/64 SPP
  sliders become independent of estimator choice. Add the matching represented
  command and allocate its provisional snapshot schema through the probe.
- Reset/group behavior: the control reset restores false; existing group/footer
  resets restore the owning settings default. Stored values remain retained
  while the feature is inactive.
- Direct and derived consumers: paired-filter resource eligibility, two radius
  prepasses, filter Gaussian width, binding caches, settings fingerprint,
  commands/snapshots, renderer history reset, and explanatory UI state.
- Mutation class: constants, optional filter pipeline dispatches, and two
  optional full-resolution radius textures; no scene, process, popup, focus, or
  animation topology change.
- Unavailable state: adaptive radius is active only for 1x soft-sun ratio
  evidence, Gaussian Bilateral, non-sparse complete frames, and supported
  optional resources; UI remains visible but accurately gated and retained.
- Sky compatibility: remove the visible redundant checkbox, always obey the SPP
  slider, and retain the legacy field/command as documented compatibility state
  without consulting it for rendering.
- Focused/live evidence: settings math, command/catalog/snapshot, UI order and
  gating, CPU/HLSL ABI, shader topology, optional-resource fallback, full
  Release shaders/CTest, and bounded exact-build exercise when safe.
- Unchanged visible behavior: adaptive radius defaults off; hard shadows remain
  one center ray; sky still averages cosine-hemisphere binary visibility; all
  existing drawers, order, reset lanes, and control ranges remain.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command or Experiment | Result or Artifact |
| --- | --- | --- | --- |
| Equal-SPP comparison | Ratio on/off trace the same configured sample count | Production-shared unit tests and HLSL/CPU contracts | Passed for 1 through 64 SPP; hard and zero-extent shadows remain one center sample |
| Sky semantics | Configured SPP is independent and UI makes no ratio claim | Settings, source, UI, command, and snapshot tests | Passed; legacy command remains accepted but is ignored by rendering |
| Adaptive radius | Reference-derived radius is finite, bounded, opt-in, and changes filter reach | Unit/source contracts plus compiled shader variants | Passed with default off, explicit fixed/adaptive blobs, and optional-resource fallback |
| Edge safety | Zero gradients, invalid depths/normals, borders, sparse frames, resize, and failure fallback | Deterministic tests and independent review | Passed; accumulation-owned history now retires paired/adaptive evidence before allocation |
| Integration | All production shaders/package and full Release suite pass | Dedicated build and CTest | Passed: 318 shader tasks, 52 packaged shader files, 47/47 CTest |
| Runtime boundary | Exact candidate startup and equal-SPP mode exercise when the GPU lease is available | Repository launcher and bounded visual smoke | Exact executable remained alive for a 12-second hidden startup smoke and was stopped; visual mode matrix remains a user-facing follow-up |

## Decisions

| Date and Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-17 | Resume through a new active plan on the existing branch | The prior plan is archived historical evidence; the user explicitly requested a coupled follow-up | All |
| 2026-08-17 | Treat the user's MIT statement as provided provenance, not an independently verified public license notice | It removes the user's copying concern while preserving an accurate repository record | ADAPTIVE-AUDIT, IMPLEMENT |
| 2026-08-17 | Keep adaptive radius default off | The user requested a toggle and did not authorize a default quality/performance change | IMPLEMENT |
| 2026-08-17 | Require equal configured SPP for classic-versus-ratio comparisons | Forced one-SPP classic behavior confounds estimator quality with sample count | CLASSIC-SPP, IMPLEMENT |
| 2026-08-17 | Retire the visible sky averaging checkbox but retain its legacy storage/command | Sky has only one binary-mean estimator; SPP is the single real degree of freedom, while compatibility state avoids an unrelated migration | SKY-SEMANTICS, IMPLEMENT |
| 2026-08-17 | Implement the demo's two-stage adaptive-radius estimate only for Gaussian paired filtering | The reference derives one radius map from raw `S_N/U_N`, box-filters it, and reuses it for both Gaussian passes; applying it to the flat Joint kernel would be a different algorithm | ADAPTIVE-AUDIT, IMPLEMENT |
| 2026-08-17 | Keep the analytic-`U` bilateral term out of this toggle | It is separate from adaptive radius and UVSR has no matching area-integrated analytic `U`; substituting noisy `U_N` or center-sun response would be unfaithful | ADAPTIVE-AUDIT, IMPLEMENT |

## Progress and Handoffs

| Date and Time | Task and Owner | Status | Revision or Artifact | Checks | Risks or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-17 | Coordinator `/root` | Active | Prior candidate at `5d15efd` plus dirty diff | Status, worktree, active-plan, branch, and prior evidence inspected | Reconcile read-only audits before writing |
| 2026-08-17 | ADAPTIVE-AUDIT | Complete; acknowledged and released | Downloaded demo and current candidate | Exact four-axis derivative, 3x3 box prefilter, Gaussian-width use, resources, fallback, and edge matrix | Implement two optional radius prepasses and reuse one map for H/V |
| 2026-08-17 | SKY-SEMANTICS | Complete; acknowledged and released | Current sky path and history | Proved rename-only semantics plus Halton, hit-distance, and TMax changes | Remove redundant UI gate; always resolve stored SPP |
| 2026-08-17 | CLASSIC-SPP | Complete; acknowledged and released | Current sun path and history | Complete hard/soft, 1x/MSAA, sparse, denoise, and one-SPP matrix | Loop and average classic soft rays at the same configured SPP |
| 2026-08-17 | Coordinator implementation | Complete | Settings, shaders, pass resources, UI, commands, schema 0005, tests, and documentation | Release `ALL_BUILD` passed; 318 shaders and `uvsr.exe` linked | Freeze for independent review |
| 2026-08-17 | Three final read-only reviews | Complete; acknowledged and released | Frozen integrated candidate | Sky/equal-SPP passed; adaptive math/resources passed; adversarial integration found one P2 resource edge | Added the accumulation-ownership gate and two regression contracts |
| 2026-08-17 | Narrow repair re-review | Complete; acknowledged and released | Final paired-evidence request predicate | Confirmed allocation exclusion, inherited adaptive exclusion, and resource retirement | No remaining source blocker |
| 2026-08-17 | Final verification | Complete | `b-final/bin/uvsr.exe`, SHA-256 `F95BBB17ED9AC141E6646EA6C5D37154B8FCEFC820DF247580C15EBC5B9244FB` | Release build passed; 47/47 CTest; 188 scoped headings; diff check clean; 12-second startup smoke passed | Hand off exact executable and state the visual/performance boundary |

## Risks and Escalation Triggers

- Adaptive reach based on noisy derivatives can create halos, overblur, or large
  filter cost unless bounded and edge-aware.
- Equal-SPP classic mode may affect hit-distance semantics or denoiser routing if
  sample aggregation is not kept separate from the Heitz ratio.
- Hiding, removing, or repurposing the legacy sky field can break commands or
  snapshots unless compatibility behavior is explicit.
- The old candidate remains user-active, so build replacement and exact runtime
  smoke must not close it or launch a competing renderer.

Stop and ask the user if:

- the only faithful adaptive implementation requires a new area-light model,
  new licensed binary assets, or a default visual/performance change;
- compatibility and accurate UI semantics require choosing between two
  materially different visible control models that cannot be inferred from the
  requested equal-SPP comparison;
- destructive cleanup, publication, or external legal representation becomes
  necessary.

## Completion

- Final integrated commit: intentionally none unless separately authorized
- Verification summary: 318 shader tasks compiled, 52 packaged shader files
  validated, Release executable linked, 47/47 CTest passed, scoped title-case
  and diff checks passed, and a 12-second exact-build startup smoke passed
- Independent review: three final read-only reviews passed; one paired-resource
  accumulation edge was repaired, regression-tested, and narrowly re-reviewed
- Coming Soon and documentation update: no README Coming Soon section exists;
  the README feature summary and durable estimator/settings/PBR/UI documentation
  are updated
- Pushed, PR, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user-visible equal-SPP scene comparison,
  adaptive-radius image/performance validation, and an optional later experiment
  lowering only adaptive evidence's low-energy guard from `1e-4` to the demo's
  `1e-6`
- Active ownership released: yes
- Archived to completed or abandoned path:
  `docs/exec-plans/completed/heitz-adaptive-radius-and-spp-controls.md`
