# Heitz Shadow Follow-Up

## Status

- State: completed and technically verified; final Release build, full tests,
  shader packaging, independent review, and exact-build live smoke pass. The
  user's exact failing-camera visual acceptance remains a documented follow-up.
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/heitz-stochastic-shadows` in
  `C:/Users/brock/Documents/Codex/2026-08-04/https-casual-effects-com-research-heitz2018shadow`
- Base commit/state: `578496047c4753077e36f39a68df8c2c323cee58` plus the
  technically verified, local uncommitted Heitz-shadow candidate recorded in
  `docs/exec-plans/completed/heitz-ratio-estimator-shadows.md`
- Started: 2026-08-05
- Last updated: 2026-08-05
- Planned archive: `docs/exec-plans/completed/heitz-shadow-follow-up.md`

## Goal and Done Condition

Goal:

Repair the rejected Heitz-shadow candidate while preserving independent shadow
producers, complete denoiser removal, hard/soft control, statistics, physical
sun extent, and the shared Representation drawer. Remove nonfunctional
fractional rates and every private S/U history surface, make global final-color
TAA the sole temporal accumulator, replace the regressed `TMin` policy with a
true raster triangle-normal origin offset that works at the lower prototype
scale, remove Hashed White Noise from Visibility and ray-traced shadows, and
place Animate Samples directly above Samples Per Pixel.

Done when:

- [x] Screen-space and ratio-estimator shadows each have an independent enable
  control; both-off, either-only, and both-on states render intentionally.
- [x] Both-on combines visibility conservatively without double-darkening an
  occluder detected by both producers.
- [ ] The ineffective Origin Safety control remains removed. Ray Bias uses a
  `0.002` triangle-normal factory default, and the supplied Sponza view is free
  of the reported acne without shortening ray reach or detaching contacts.
- [x] Statistics reports the complete ratio-estimator shadow GPU cost.
- [x] The spatial denoiser is removed end to end, including its shaders,
  intermediate resources, settings, controls, packaging, tests, and stale docs.
- [x] A dedicated Hard Shadows toggle forces the center direction, takes a
  receiver-gated one-ray path, and makes the soft sample control irrelevant
  without changing the authored directional-light angle; directional smoke
  confirms the reported inversion is gone, while no formal score is claimed.
- [x] Every loaded primary sun defaults to a `0.53` degree full angular diameter
  unless it already authors a positive extent.
- [x] A logarithmic slider exposes only functional integer rates `1` through
  `64` samples per pixel.
- [x] Soft-shadow samples expose Permutated White Noise and Void Cluster Blue
  Noise, and advance through time independently of global TAA when animation is
  enabled.
- [x] Ray Bias has a documented geometric meaning, its changed effective scale
  is diagnosed, and any performance effect is measured on matched settings.
- [x] Fractional rates, duty scheduling, motion reprojection, and all private
  numerator/denominator histories are removed end to end.
- [x] Noise sampling exposes only distinct runtime behavior, and Animate Samples
  is directly above the sample-count slider.
- [x] The hard-shadow path is measured against a matched one-sample soft path
  and is materially cheaper after any warranted large optimization.
- [x] Release builds, targeted and full tests, shader packaging, independent
  rendering review, and exact-build live smoke coverage pass.

## Scope

In scope:

- Independent UI and command state for both directional shadow producers.
- Centralized two-producer visibility composition at the PBR consumer.
- Practical world-space ray bias with a known-working factory default and no
  far-end shortening.
- Ratio-estimator GPU timing and Statistics presentation.
- Complete removal of the spatial denoiser and its resources/shaders/contracts.
- Dedicated hard-ray optimization, a logarithmic integer 1-to-64 sample budget,
  directional-light angular-size defaults, blue-noise sampling, source
  contracts, and runtime validation.
- Previously identified contribution-transition TAA resets and inactive timing
  gating where they directly affect the revised temporal/statistics contract.
- Ray-bias scale diagnosis, private-history removal, temporal sample evolution,
  minimal noise-sequence controls, and hard-versus-soft performance evidence.

Non-goals:

- Reflection rendering, new light types, transparency-aware ray candidates,
  a replacement denoiser, small speculative optimizations, Vulkan, DirectX 11,
  a pull request, merge commit, release, or deployment.

Affected subsystems and paths:

- Directional shadow settings/visibility, Heitz pass and shader, deferred PBR
  bindings/shaders, `src/uvsr.cpp`, command catalog, tests, and documentation.

Shared hotspots reserved for the coordinator:

- All writable paths, build output, packaged shaders, Git state, documentation,
  and the sole UVSR/GPU runtime lease.

## Baseline

- Canonical repository/remote: previous feature base was exact fetched
  `origin/main` at `5784960`; this follow-up intentionally continues its dirty,
  technically verified task candidate rather than replacing it with another
  lineage.
- Local versus remote state at follow-up start: local-only feature work with no
  commit or push; the pre-existing task diff was preserved as the follow-up
  base.
- Verified source commit/build: prior candidate passed a full Release build,
  35/35 CTest, shader packaging, and live RTX 4090 Laptop GPU smoke; executable
  SHA-256 `C955813418DD4404BA007F53AA05D5AFBD87F2E0676AEF0D6EAE8305B2FE9626`.
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090
  Laptop GPU, Sponza Decorated, the supplied 1920x1080 captures, selected
  `sun_1`, ray bias 0.002 versus 0.05, angular size 0 degrees, one sample, and
  denoiser radius 0.
- Known pre-existing failures: the second supplied Sponza capture still shows
  widespread acne with Origin Safety at 8; that control produces no useful
  visual correction. The capture also has a zero-degree light, so the current
  one-ray collapse cannot vary from frame to frame.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior Heitz candidate | Preserve correlated RGB estimator and reusable representation | Integrated baseline | Follow-up implementation |
| Visibility composition | Two optional fixed producer slots, componentwise minimum for both-on | Implemented and tested | Deferred PBR |
| Ray-origin policy | View-facing raster triangle-normal displacement by the maximum of Ray Bias and one internal depth-step distance, followed by a representable normal nudge and `TMin = 0` | Implemented and source-tested; exact Sponza visual pending | G-buffer and ray query shader |
| Hard-shadow policy | Explicit toggle; exact direct-light receiver gate, then one center ray with no material/emitter/ratio work | Implemented, shader-compiled, and exercised in matched directional smoke; no formal benchmark claimed | Pass, shader, UI |
| Softness contract | Positive sun extent defaults to `0.53` degrees; selectable emitter noise and TAA-independent sample phase | Implemented and tested | UI, shader, docs |
| Sampling rates | Functional integer powers of two from 1 through 64; no duty scheduling or private history | Implemented and source-tested | Settings, pass, shader, UI |
| Integer sample animation | Frame-local phase regardless of TAA; final-color TAA is the only temporal accumulator | Implemented and source-tested | Pass, shader, UI |
| Denoiser removal | Delete all four reconstruction stages and intermediate resources rather than bypassing them | Removed end to end; build, package, and tests pass | Pass, shaders, build, UI, docs |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Replace the singular directional-shadow technique state with independent
  screen-space and ratio-estimator enabled states.
- Deferred PBR must consume zero, one, or two exact-light visibility inputs and
  define their combination explicitly.
- Heitz timing data becomes a frame-local renderer/statistics contract.
- The Heitz pass becomes one traced-estimate dispatch with no spatial
  reconstruction shader or intermediate denoiser textures.
- The ray-budget setting is a log2 rate in `[0, 6]`, mapping exactly to one
  through 64 current-frame samples.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Bias, sampling, and paper analysis | `/root/paper_analysis` | Shared, read-only | Current dirty candidate | None | Paper, Demofox article, and shader | Completed |
| Ray-rate and runtime architecture | `/root/renderer_architecture` | Shared, read-only | Current dirty candidate | None | Renderer and TAA contracts | Completed |
| Verification and regression audit | `/root/test_risk_audit` | Shared, read-only | Current dirty candidate | None | Tests and screenshots | Completed |
| Ray-origin and scale audit | `/root/paper_analysis` | Shared, read-only | Current dirty candidate | None | Current shader and prior candidate | Completed |
| Fractional and temporal sampling audit | `/root/renderer_architecture` | Shared, read-only | Current dirty candidate | None | Settings, pass, shader, TAA, UI | Completed |
| Hard-path performance audit | `/root/test_risk_audit` | Shared, read-only | Current dirty candidate | None | Pass/shader permutations and timing | Completed |
| Independent implementation review | `/root/test_risk_audit` | Shared, read-only | Replacement candidate | None | Implemented settings/pass/shader/UI/tests | Completed; no P1/P2 findings |
| Implementation and integration | `/root` | Task worktree | Current dirty candidate | All task-owned edits | Read-only handoffs | Technically verified; runtime smoke complete; publication authorized |

## Assignment Contracts

### Bias and Softness Analysis

- Owner/thread: `/root/paper_analysis`
- Base commit/state: `5784960` plus the current dirty Heitz candidate
- Read scope: supplied captures, paper/talk notes, directional-light data,
  Heitz shader, ratio settings, and relevant documentation
- Write scope: none
- No-touch scope: all files, Git state, build/runtime resources, and subagents
- Deliverable: evidence-backed diagnosis and exact corrective math/parameter
  recommendations for origin bias and penumbra behavior
- Done when: shader lines and product-setting causes are identified, with
  fidelity and detachment risks stated
- Allowed Git and external actions: read-only only
- Stop and report if: the paper and current light-size semantics conflict

### Visibility and Timing Architecture

- Owner/thread: `/root/renderer_architecture`
- Base commit/state: `5784960` plus the current dirty Heitz candidate
- Read scope: visibility contracts, deferred PBR pass/shaders, Heitz pass,
  renderer routing, UI state, commands, and Statistics timing patterns
- Write scope: none
- No-touch scope: all files, Git state, build/runtime resources, and subagents
- Interface/invariant contract: independent producers, both-off neutral, both-on
  componentwise minimum unless source evidence requires another operation
- Deliverable: smallest coherent binding/lifecycle/timing design and risk list
- Done when: all producer combinations and binding-cache invalidation paths are
  mapped to concrete files and tests
- Allowed Git and external actions: read-only only
- Stop and report if: the proposed two-slot PBR contract would conflict with an
  existing shared binding or MSAA layout

### Verification and Regression Audit

- Owner/thread: `/root/test_risk_audit`
- Base commit/state: `5784960` plus the current dirty Heitz candidate
- Read scope: tests, command catalog, UI contracts, Statistics, shader bundle,
  supplied captures, and runtime procedures
- Write scope: none
- No-touch scope: all files, Git state, build/runtime resources, and subagents
- Deliverable: targeted test matrix, live state matrix, and artifact/softness
  acceptance criteria
- Done when: every user request maps to automated and runtime evidence
- Allowed Git and external actions: read-only only
- Stop and report if: a claimed result needs a controlled performance benchmark
  rather than a correctness smoke test

### Ray-Origin and Scale Audit

- Owner/thread: `/root/paper_analysis`
- Base commit/state: `5784960` plus the current dirty Heitz candidate
- Read scope: current and prior ray-origin math, view reconstruction, scene-scale
  inputs, acceleration-structure geometry, tests, and relevant technique docs
- Write scope: none
- No-touch scope: all files, Git state, build/runtime resources, and subagents
- Deliverable: exact geometric meaning of Ray Bias, why `0.093` is now needed,
  correctness risks, and source-grounded performance expectations
- Done when: the changed effective scale is traced to concrete math/lines and a
  corrective recommendation distinguishes artifact suppression from detachment
- Allowed Git and external actions: read-only only
- Stop and report if: fixing the scale requires a materially new origin policy

### Fractional and Temporal Sampling Audit

- Owner/thread: `/root/renderer_architecture`
- Base commit/state: `5784960` plus the current dirty Heitz candidate
- Read scope: ratio settings, CPU pass, HLSL scheduling/history, TAA phase,
  command catalog, UI, and numerical/source tests
- Write scope: none
- No-touch scope: all files, Git state, build/runtime resources, and subagents
- Deliverable: diagnosis of broken sub-one rates and TAA-only evolution plus the
  smallest coherent noise-control interface and history policy
- Done when: every fractional rate and TAA on/off transition has defined sample,
  output, and history behavior with concrete files/tests identified
- Allowed Git and external actions: read-only only
- Stop and report if: a useful fractional result fundamentally requires a new
  spatial reconstruction pass

### Hard-Path Performance Audit

- Owner/thread: `/root/test_risk_audit`
- Base commit/state: `5784960` plus the current dirty Heitz candidate
- Read scope: pass/shader permutations, dispatch dimensions, ratio-estimator
  branches, timing instrumentation, benchmark controls, and tests
- Write scope: none
- No-touch scope: all files, Git state, build/runtime resources, and subagents
- Deliverable: source-backed cause of the hard-versus-soft inversion, any large
  optimization worth implementing, and a matched benchmark/test protocol
- Done when: cost differences are tied to dispatch/permutation behavior rather
  than inferred from the toggle label
- Allowed Git and external actions: read-only only
- Stop and report if: only small speculative micro-optimizations remain

## Integration Order

1. Remove fractional/private-history and hashed-noise surfaces while preserving
   the hard path, frame-local phase, and ratio helper.
2. Correct the packed geometric-normal contract and apply Ray Bias once as
   triangle-normal origin clearance.
3. Reconcile UI order, commands, numerical/source coverage, and documentation.
4. Build, run targeted/full tests, obtain independent review, and live-smoke
   every producer combination plus bias, hard/soft, temporal, rate, timing, and
   MSAA behavior.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Independent controls and combination | CPU/source/UI tests plus live four-state matrix | Release tests and exact executable | Automated contracts and prior four-state smoke pass; exact replacement enables RT with factory-disabled TAA without a modal |
| Bias repair | Shader reference test plus low-bias Sponza comparison | Exact camera/settings capture at 0, 0.002, and 0.05 | Source/math contracts pass; the `0.002` default shows no prior catastrophic wall occlusion at Sponza Benchmark Position 1, while the user's exact failing-camera acceptance remains pending |
| Denoiser removal | No dead shader, setting, resource, package, test, command, or documentation references | Repository-wide search plus shader bundle tests | Passed |
| Softness | Finite angular-size near/far penumbra capture; zero-size explanation | Exact executable with sufficient samples and optional global TAA | Sampling/default contracts pass; the exact replacement renders the default `0.53`-degree sun with soft stochastic sampling |
| Statistics | Timing source contract and visible Statistics row | Release tests and live drawer inspection | Timing/epoch contracts pass; exact replacement visibly reports `Ratio-Estimator Ray Dispatch` |
| Existing rendering/package contracts | Full Release build and CTest | CMake build and CTest | Exact replacement Release app and shader bundle built; full 35/35 CTest passed |
| Documentation integrity | Heading, line-count, and whitespace checks | Repository scripts and `git diff --check` | README counts regenerated; 1,235 headings/lead-ins and whitespace passed before this evidence-only plan update |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-05 | Start a new follow-up plan on the existing dirty candidate | The prior plan was archived and this feedback changes product behavior; replacing the lineage would discard the implementation under review | All |
| 2026-08-05 | Default both-on composition to componentwise minimum | It preserves the strongest occlusion without multiplying two estimates of the same event and double-darkening overlaps | PBR, tests, docs |
| 2026-08-05 | Remove the spatial denoiser completely | The user measured it as substantially more expensive than tracing and explicitly prioritized its removal; leaving dormant stages would violate product YAGNI | Heitz pass, shaders, UI, build, tests, docs |
| 2026-08-05 | Reject Origin Safety as the product control | The supplied default-build capture remains visibly corrupted even at eight steps; the dimensionless control has no useful corrective range on the active scene | Settings, shader, UI, docs |
| 2026-08-05 | Limit hard-mode optimization to a materially distinct path | A center-direction hard shadow needs exactly one visibility ray and no stochastic emitter, BRDF, or ratio work; small speculative micro-optimizations remain out of scope | Pass, shader, package, tests |
| 2026-08-05 | Superseded: store normalized additive numerator and denominator histories | This was mathematically preferable to filtering divided visibility, but the private history caused unacceptable double-temporal smear and is now removed | Shader, tests, docs |
| 2026-08-05 | Superseded: use the raw jitter-adjusted coordinate for S/U and depth | The private histories and their reprojection coordinates are now removed | Shader, tests |
| 2026-08-05 | Preflight the exact TAA permutation and commit phases only after an actual dispatch | Pipeline-ready did not prove a lazily selected permutation could render; failed preflight now retires stale TAA history | TAA, renderer, tests |
| 2026-08-05 | Superseded: track world-representation content revisions separately | Its only observer retired private history, so the unused revision is removed under YAGNI | Representation, renderer, tests |
| 2026-08-05 | Superseded: keep `0.05` as the Ray Bias default and expose `0.1` headroom | The `TMin` implementation was rejected; triangle-normal displacement restores the lower `0.002` prototype scale while retaining diagnostic headroom | Settings, UI, docs |
| 2026-08-05 | Treat disabled motion vectors as absent at the caller and again when temporal accumulation is inactive | Donut preserves a non-null 1x1 placeholder when motion generation is disabled; accepting that sentinel as a full-resolution input caused the reported first-enable resource mismatch | Renderer, Heitz pass, source contracts |
| 2026-08-05 | Superseded: make Ray Bias exclusively the ray-query `TMin` | A same-plane hit occurs at `e / dot(N,L)`, so no finite `TMin` guarantees clearance at grazing light angles | Shader, settings, docs |
| 2026-08-05 | Superseded: decouple shadow duty and emitter phases from global TAA | Fractional duty and all private S/U histories are removed; the phase remains frame-local | Renderer, pass, shader, UI, tests |
| 2026-08-05 | Superseded: keep fractional duty on a dedicated void-cluster rank field | Fractional rates and duty scheduling are removed | Settings, shader, tests, docs |
| 2026-08-05 | Gate hard queries with the same prepared-surface test as deferred PBR | The previous hard path traced every valid G-buffer surface while one-sample soft skipped zero-response receivers, causing the reported inversion; this removes a large class of provably irrelevant queries without a speculative shader split | Shader, tests, performance validation |
| 2026-08-05 | Make final-color TAA the sole temporal accumulator | The private 64-weight S/U history retained only `N/64` new evidence per frame and was then filtered again by TAA, exactly explaining animation-dependent smear | Pass, shader, renderer, tests, docs |
| 2026-08-05 | Store a true raster triangle normal in the existing geometric-normal field | The old field contained an interpolated vertex normal and could weaken or reverse origin clearance; world-position derivatives match the represented triangle plane without a new target | G-buffer, PBR, Heitz, tests, docs |
| 2026-08-05 | Apply Ray Bias once as triangle-normal origin clearance and use `TMin = 0` | Normal displacement guarantees exterior-plane separation for valid outgoing rays; dual use would compound contact detachment | Shader, UI, tests, docs |
| 2026-08-05 | Remove fractional rates and Hashed White Noise | The user rejected nonfunctional fractional sampling and requested both hashed options be removed; retained choices have distinct behavior | Settings, shaders, UI, commands, tests, docs |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-05 | `/root` | Active | User captures and prior verified artifact recorded | Repository/task preflight | Complete source investigation before patching |
| 2026-08-05 | `/root` | Active | Second Sponza capture: 4 requested samples, Origin Safety 8, zero-degree sun | Visual failure reproduced by user; Demofox source reviewed | Finalize fractional-rate contract, then patch |
| 2026-08-05 | All reviewers | Completed | Current dirty candidate | Independent rendering/lifetime and regression reviews found no remaining code-level P0/P1 | Build and exact-artifact evidence remained |
| 2026-08-05 | `/root` | Candidate | `build-follow-up/bin/uvsr.exe`, SHA-256 `464B95E14B8BAE5C5D355EE4B25C099B8C51226F636F6B210CA2CE6609075D22` | Release app/shaders built; full 35/35 CTest; bundle, line-count, 1,233-heading, and diff checks passed | Do not launch a duplicate while the prior user-owned UVSR PID 30120 is open; exact-build live matrix and visual acceptance remain |
| 2026-08-05 | `/root` | Candidate | `build-follow-up/bin/uvsr.exe`, SHA-256 `65DEEC20FBE010453B40857B1CD9B9036E0866FBF71ADC223842DD2F24D125A6` | Rebuilt after gating the disabled-motion sentinel; targeted contracts and full 35/35 CTest passed; live `/set shadows.ratio-estimator.enabled on` with factory-disabled TAA reported success, rendered, and produced no error modal | Exact build is open as PID 21596 for user review; broader visual matrix and product acceptance remain |
| 2026-08-05 | `/root` | Active | Third user-feedback round: Ray Bias needs about `0.093`; sub-one rates appear inert; temporal changes appear only with TAA; hard appears slower than soft; noise controls requested | Complex-task preflight complete; three disjoint read-only audits assigned | Stabilize contracts before coordinator-only implementation |
| 2026-08-05 | Read-only auditors | Completed | Current third-feedback source state | Diagnosed interpolated-normal bias scaling, TAA-gated fractional/phase policy, and unconditional hard receiver queries | Coordinator implemented the agreed contracts; independent post-change review active |
| 2026-08-05 | `/root` | Candidate | `build-feedback3/bin/uvsr.exe` | Release app and shader bundle built; focused Heitz/renderer/UI tests passed; full CTest 34/35 with only stale README line counts | Regenerate documentation counts, rerun full tests, then exact-build live smoke without disturbing the user-owned prior candidate |
| 2026-08-05 | Independent reviewer | Completed | Post-fix replacement source | Re-review found no blocking correctness, lifecycle, API, or futureproofing issue; material mutations reset private history and both shadow paths share the G-buffer normal decoder | Ownership released |
| 2026-08-05 | `/root` | Technically Verified Candidate | `build-feedback3/bin/uvsr.exe`, SHA-256 `17EEF0C8FBEB2AA1586113DA4DA201B5658A84D8A9F506F223E264AA015B3DF8` | Full Release build, 35/35 CTest, runtime/production shader bundles, README counts, 1,238-heading audit, diff check, and exact-build live smoke passed; RT enable, `1/16` with TAA off, all exposed sampling controls, hard/soft switching, `0.050` bias, and the Statistics row were exercised | Exact build remains open at Sponza Benchmark Position 1 for product review; performance observations are directional smoke, not a controlled benchmark |
| 2026-08-05 | `/root` | Active | Fourth-feedback source in `build-feedback4` worktree state | Seven focused tests and all 259 first-party shader tasks pass; preliminary Release app compiled before the requested UI reorder | Rebuild exact source, full test/package/doc checks, independent review, and live smoke |
| 2026-08-05 | Independent reviewer | Completed | Final fourth-feedback source | Read-only review found no P1/P2 rendering, resource-lifetime, binding, UI-order, hard-path, or statistics issue and approved integration | Residual visual risks are grazing/thin geometry, conservative contact clearance, and opaque alpha-test treatment |
| 2026-08-05 | `/root` | Technically Verified Candidate | `build-feedback4/bin/uvsr.exe`, SHA-256 `45EED59A66B50ADA482E7FD288E06810116F035E962D21D993532AAF1A089872` | Full Release build and 35/35 CTest passed, including production/runtime shader bundles; documentation and diff checks passed; exact-process live smoke enabled RT without a modal, showed the requested control order and two retained noise modes, exercised animated 2-spp shadows with TAA off, and visibly reported `Ratio-Estimator Ray Dispatch` | Exact build remains open at Sponza Benchmark Position 1 for product review; the visible `0.295 ms` timing is informal smoke evidence, not a controlled benchmark |

## Risks and Escalation Triggers

- A large world-space offset can hide self-intersection while causing detached
  contact shadows; acceptance requires both artifact removal and contact checks.
- A finite light alone yields noisy Monte Carlo penumbrae at one sample; the
  estimator and temporal accumulation must not be confused with geometric
  softness, and removing spatial reconstruction increases convergence time.
- Adding a second PBR visibility input changes CPU/HLSL binding and MSAA
  contracts and requires full package/lifecycle coverage.
- Timing must cover the remaining traced-estimate dispatch without adding
  measurement work when Statistics is closed if existing UVSR timing policy
  avoids it.

Stop and ask the user if:

- Correct both-on semantics require a visibly different blend policy than the
  conservative strongest-occlusion behavior stated here.
- Fixing softness would require silently changing authored light values rather
  than an explicit user-facing default or warning.

## Completion

- Final integrated commit: `feat: add ratio-estimator ray-traced shadows`;
  exact SHA is recorded by Git history and the final handoff
- Verification summary: fourth-feedback replacement is technically verified by
  the full Release build, 35/35 CTest, shader-bundle checks, documentation
  validators, independent review, and exact-artifact live smoke
- Independent review: complete with no P1/P2 findings; integration approved
- Coming Soon/documentation update: current-facing documentation is reconciled;
  the roadmap has no stale active entry for this directly published feature
- Pushed/PR/merged, or intentionally local: direct `origin/main` publication
  explicitly authorized; exact push outcome is recorded in the final handoff
- Remaining experiments or follow-ups: exact failing-view bias and motion
  acceptance; any formal performance score requires a controlled benchmark
  window
- Active ownership released: yes; implementation, review, build, tests, and
  runtime ownership are complete
- Archived to completed/abandoned path: completed in this file
