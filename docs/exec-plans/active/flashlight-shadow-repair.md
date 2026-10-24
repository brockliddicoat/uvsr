# Flashlight Shadow Repair

## Status

- State: active
- Coordinator: `/root`
- Project branch and worktree: `codex/flashlight-ambient-fill` at
  `C:\Users\brock\OneDrive\Documents\uvsr\work\flashlight-ambient-fill`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`
- Resumed dirty candidate:
  - Tracked diff Git hash:
    `81397d748f72a84716a4a7103339b5def6fd9793`
  - `src/flashlight.h` SHA-256:
    `897F5469A43FA0D2145DE1925B45AF24B032261174E08B8C912D2F4AD6E21D7F`
  - `src/flashlight_shared.h` SHA-256:
    `0BA0A88C5F4ACDDFD361F1E6F887D0399C3BB2A1C7088DF9EFD9CAFB168337BA`
  - Rejected executable SHA-256:
    `4F4E4A65C05B5A57A5DCA08C57BDBEA5A40E61331C4D14347F00B2051DB8EE52`
- Prior plans:
  - `docs/exec-plans/completed/flashlight-ambient-fill.md`
  - `docs/exec-plans/completed/flashlight-realism-controls.md`
  - `docs/exec-plans/completed/flashlight-beam-shape.md`
- Started: 2026-07-30
- Last updated: 2026-07-30
- Planned archive:
  `docs/exec-plans/completed/flashlight-shadow-repair.md`

## Goal and Done Condition

Goal:

Determine why the camera flashlight's configured geometry shadow map does not
visibly attenuate the beam, repair the complete render-to-sample path, and
add a configurable camera offset for controlling shadow parallax. Produce a
durable report that explains the cause, fix, evidence, and remaining
constraints.

Done when:

- [ ] A controlled pre-fix reproduction proves the failure independently of
      beam brightness, Ambient Fill, AO, and sun lighting.
- [ ] CPU resource creation, shadow rendering, light submission, GPU binding,
      and PBR sampling are traced to one evidence-backed root cause.
- [ ] The smallest first-party fix produces stable hard geometry occlusion in
      simple and realistic flashlight modes without changing ordinary lights or
      the directional-visibility ABI.
- [ ] **Camera Offset** spans 0 to 40 cm, defaults to 20 cm along the retained
      two-to-one right/down mount direction, and keeps the shared beam/shadow
      pose converged on the camera aim.
- [ ] Bend, Sparse Virtual Shadow Maps, and Diagnostic Cascaded Shadow Maps live
      in a separate top-level **Shadows** drawer while selected-light shadow
      properties remain in **Lights**.
- [ ] Cast Shadows off/on, F fade endpoints, camera motion, rounded-square beam,
      scene recreation, resize, forward PBR, deferred PBR, and deferred PBR
      MSAA remain coherent.
- [ ] Focused tests, production and factory Release builds, both full CTest
      suites, independent rendering review, exact-build DX12 smoke, Title Case,
      README counts, diff checks, and a dedicated fix report pass.

## Scope

In scope:

- Flashlight shadow resource lifecycle, render ordering, light association,
  submission, GPU bindings, and PBR visibility sampling.
- Shadow-map view/projection, comparison convention, bias, and feature gates if
  evidence identifies one of them as causal.
- Focused helper/source contracts and an exact visual occluder matrix.
- Flashlight offset setting, sanitation, reset, UI, and camera-basis mapping.
- Top-level Settings ownership for the three directional shadow techniques.
- A durable Markdown report describing the observed failure and implemented
  repair.

Non-goals:

- General point-light shadows, a new shadow algorithm, PCSS/PCF redesign,
  volumetric shadows, directional-visibility changes, Donut edits, publication,
  or unrelated lighting changes.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/pbr_lighting.hlsli` and first-party shadow helpers if required
- `src/flashlight.h` if a focused contract helper is required
- Flashlight, renderer, UI, PBR, and shader-bundle tests
- `docs/flashlight-shadow-fix-report.md`
- Existing flashlight user/design documentation only where the verified
  behavior or implementation contract changes

Shared hotspots reserved for the coordinator:

- All source and test writes, HLSL/CPU contracts, CMake/shader registries,
  README, shared documentation, build trees, renderer process, GPU, this plan,
  and the final completion claim

## Baseline

- Canonical repository: local `main` is at `891f4f0`; remote freshness is not
  required because integration and publication are out of scope.
- Local versus remote state: local-only dirty feature branch; no commit, push,
  pull request, merge, release, or deployment is authorized.
- Rejected source/build: the prior candidate passed automated checks but the
  user observed that the flashlight does not cast shadows. Its earlier visual
  smoke is invalid as product evidence because it did not isolate an occluder.
- Runtime preset: production DX12 PBR Sponza Decorated, a fixed close occluder
  and receiver, Ambient Fill off, sun contribution minimized or held constant,
  fixed flashlight settings, and one serialized GPU/UI lease.
- Known pre-existing failures: none recorded outside the reported flashlight
  shadow failure.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Rejected flashlight candidate | Exact dirty identity above | Selected | Reproduction and repair |
| UI command-bar task | Plain F dispatches only after command-bar capture | Confirmed externally | Future semantic merge |
| Public light list | `flashlight_1` public; hotspot private | Confirmed | Runtime and future commands |
| Directional visibility | Three unchanged full-resolution `R8_UNORM` slots | Frozen | Deferred semantic compatibility |
| Shadow pipeline trace | Complete path; visibility failure is coaxial placement | Complete | Implementation |
| Regression design | Off-axis caster/receiver and lifecycle matrix | Complete | Verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- The public camera light remains exactly `flashlight_1`; the internal lens
  hotspot remains excluded from editable/public light discovery.
- Both flashlight lobes must continue to share one 2048-square geometry-shadow
  render and one camera-relative basis.
- `Cast Shadows` remains the only user-facing flashlight shadow gate and
  defaults on.
- `Camera Offset` is a flashlight-only float setting from 0 to 0.40 m, defaults
  to `0.20 m`, and scales the existing two-to-one right/down direction.
- **Shadows** follows **Lights**, starts collapsed, and owns only the three
  directional shadow-technique sections. Their nested sections remain
  default-open with their enable toggles off.
- Ordinary spotlights, all directional visibility producers/consumers, and
  existing G-buffer/resource layouts remain unchanged unless the root cause
  proves a narrowly scoped first-party binding correction is necessary.
- Exact off, PBR disabled, controlled experiments, and Cast Shadows disabled
  remain zero-work gates.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| shadow-pipeline-trace | `/root/shadow_pipeline_trace` | Shared read-only | Rejected dirty candidate | None | Current CPU/HLSL contracts | Complete |
| shadow-regression-design | `/root/shadow_regression_design` | Shared read-only | Rejected dirty candidate | None | Current tests/runtime tools | Complete |
| offset-40cm-audit | `/root/offset_40cm_audit` | Shared read-only | Current dirty candidate | None | Flashlight pose and narrowest beam | Complete |
| shadow-drawer-audit | `/root/shadow_drawer_audit` | Shared read-only | Current dirty candidate | None | Settings topology and UI contracts | Complete |
| reproduction-and-implementation | `/root` | Feature worktree | Rejected dirty candidate | All task paths and this plan | Explorer evidence | Active |
| independent-review | `/root/beam_shape_review` | Shared read-only | Final dirty candidate | None | Implementation and checks | Complete |

## Assignment Contracts

### Shadow Pipeline Trace: Find the Broken Render-to-Sample Link

- Owner/thread: `/root/shadow_pipeline_trace`
- Base commit/state: exact rejected dirty candidate recorded above
- Read scope: flashlight resource creation/render/submission, Donut planar
  shadow contracts, first-party forward/deferred/MSAA PBR bindings and HLSL,
  frame ordering, view/projection, and relevant tests
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, GPU, UI, and
  external state
- Interface contract: preserve ordinary lights, one geometry render, shared
  realistic lobes, exact-off gates, and the directional-visibility ABI
- Deliverable: ranked hypotheses with exact source evidence, the most likely
  root cause, minimum safe repair, and regression checks
- Done when: the coordinator can implement without guessing which resource,
  transform, or shader branch is wrong
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: the fix requires Donut edits, a general light ABI change,
  an additional shadow render, or conflicts with another active writer

### Shadow Regression Design: Define Proof That Shadows Work

- Owner/thread: `/root/shadow_regression_design`
- Base commit/state: exact rejected dirty candidate recorded above
- Read scope: focused tests, renderer source contracts, launch/runtime helpers,
  scene/camera controls, flashlight settings, and documentation conventions
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, GPU, UI, and
  external state
- Interface contract: distinguish an actual occluder shadow from brightness,
  AO, Ambient Fill, sun, or beam falloff; cover both flashlight modes and all
  active PBR paths
- Deliverable: focused unit/source tests, exact visual matrix, report outline,
  and likely false-positive traps
- Done when: each acceptance criterion has an objective evidence path
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: reliable proof requires a new debug/capture interface or
  a material product preference

## Integration Order

1. Reproduce the rejected build with a fixed occluder/receiver view and record
   exact settings.
2. Trace CPU shadow rendering and GPU PBR sampling in parallel.
3. Freeze the root cause and minimum compatible repair.
4. Add regression tests, implement the fix, and write the report.
5. Run focused checks, production/factory builds, and both full suites.
6. Freeze writes for independent high-risk rendering review.
7. Run an exact-build Cast Shadows off/on visual matrix, repository checks, and
   archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Pre-fix failure | Same receiver with Cast Shadows on and off shows no occluder difference | Rejected executable DX12 capture | Pending |
| Root cause | Complete render-to-sample trace with one broken invariant | Source review and focused instrumentation if required | Complete: coaxial placement hid receiver shadows |
| Fixed geometry shadow | Occluder silhouette appears only with Cast Shadows on | Exact candidate DX12 capture | Complete at the 20 cm default in simple mode |
| Mode coherence | Simple and realistic spill/hotspot share the same occlusion | Runtime mode comparison and source contracts | Source contracts complete; expanded realistic runtime remains |
| PBR parity | Forward, deferred, and deferred MSAA consume correct shadow visibility | Focused source/reference tests and exact smoke | Source and packaging complete; default deferred smoke passed; expanded parity runtime remains |
| Lifecycle safety | F, resize, recreate, off, PBR, benchmark, and shadow gates remain coherent | Helper/renderer tests and runtime toggles | F on/off, offset limit/reset, and shadow gate passed; expanded lifecycle runtime remains |
| Packaging and quality | Production/factory build topology, full CTest, docs, counts, diff audit | Repository commands | Complete: final documentation corrections were followed by both 29/29 suites and repository document checks |
| Independent review | No unresolved P0-P2 rendering/lifetime findings | Read-only frozen-candidate review | Complete: no implementation P0-P2; two documentation P2s corrected |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Reopen shadow correctness despite prior passing smoke. | The user supplied stronger product evidence. A lit wall/column view can look plausible without proving visibility attenuation, so the prior smoke cannot certify shadows. | Whole repair |
| 2026-07-30 | Require an isolated occluder/receiver comparison before changing code. | Source plausibility and Cast Shadows state alone cannot distinguish a missing sample from bias, frustum, render-order, or lighting-confound failures. | Reproduction and verification |
| 2026-07-30 | Preserve the existing render-to-sample pipeline. | The complete CPU, Donut, binding, and HLSL trace is coherent. Missing bindings, reverse-Z, bias, stale transforms, and shadow-channel collisions were ruled out. | Source and shader contracts |
| 2026-07-30 | Move the shared emitter 4 cm forward/right and 2 cm down, converging at 5 m. | The prior 5 cm axial-only origin projected shadows behind their casters. The 6 cm mount plus the 3 cm axial distance to the near-plane center remains inside the 10 cm collision envelope and creates visible receiver parallax without another light or shadow render. | Motion, tests, docs, runtime |
| 2026-07-30 | Expose 0-to-20 cm lateral **Camera Offset** while keeping forward spacing and convergence fixed. | The user needs direct control over cast-shadow parallax. Scaling the tested two-to-one right/down mount preserves the accepted default and keeps the maximum toe-in inside the narrowest supported beam. | Settings, UI, motion, tests, docs, future commands |
| 2026-07-30 | Supersede the initial mount range with a 20 cm default, 40 cm maximum, and 6 m fixed convergence. | The user requested shoulder-scale parallax. Six meters keeps the 40 cm endpoint's 3.84-degree toe-in below the narrowest beam's four-degree half-angle; a 5 m target would not. The larger mount is intentionally documented as lacking hand, body, and wall collision. | Settings, motion, tests, docs, runtime |
| 2026-07-30 | Move directional shadow techniques from **Lights** into a sibling **Shadows** drawer. | Light selection and flashlight **Cast Shadows** remain light properties. The three producer techniques are global directional-shadow methods and are easier to inspect in one dedicated, collapsed-at-launch drawer. | Settings UI, source contracts, docs, future semantic merge |
| 2026-07-30 | Order the flashlight's primary controls as **Enabled (F)**, **Cast Shadows**, then **Realistic Flashlight**. | Shadow and realism state now sit directly beneath the feature's master control. The complete animated realistic-only region remains attached to its toggle and ends before **Brightness**, while only **Enabled (F)** remains inside the PBR-disabled scope. | Settings UI, source contracts, docs, future semantic merge |

## Progress and Handoffs

| Date | Task Or Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | `/root` | Active | Plan and repository preflight | Dirty identity, Coming Soon, worktrees, branches, active plans, open PRs, and visible tasks inspected | Reproduce and await read-only traces |
| 2026-07-30 | `/root/shadow_pipeline_trace` | Complete | Read-only end-to-end path audit | Resource creation, render order, constants, bindings, forward, deferred, and MSAA sampling traced | Implement off-axis mount |
| 2026-07-30 | `/root/shadow_regression_design` | Complete | Read-only regression design | Receiver-only silhouette, confound controls, mode and lifecycle matrix defined | Apply focused tests and runtime matrix |
| 2026-07-30 | `/root` | Superseded | `build-flashlight-shadow-repair` | Clean Release build plus post-review rebuild; 516 first-party production shader tasks and 29/29 CTest passed; executable SHA-256 `7757C2A201C9E9999A57E3284982304160FB0682281984DF5A500C474D36D60A` | Replaced by Camera Offset extension |
| 2026-07-30 | `/root` | Superseded | `build-flashlight-shadow-repair-factory` | Clean Release build plus post-review rebuild; 51 experiment shader tasks and 29/29 CTest passed | Replaced by Camera Offset extension |
| 2026-07-30 | `/root/beam_shape_review` | Complete | Frozen source/test candidate | No P0-P2 rendering, ABI, binding, registration, or lifetime findings; documentation concerns resolved and the repository Title Case validator passes | Exact runtime remains |
| 2026-07-30 | `/root` | Superseded | `build-flashlight-camera-offset-production` | Fresh Release build plus post-review rebuild; 516 first-party shader tasks and 29/29 CTest passed; executable SHA-256 `E79CE04EAF79CB3DB2648416BACAB540AD393C2E3FD04080514C0ABF46F81CAC` | Replaced by 20-to-40 cm and Shadows-drawer extension |
| 2026-07-30 | `/root` | Superseded | `build-flashlight-camera-offset-factory` | Fresh Release build; 51 experiment shader tasks and 29/29 CTest passed | Replaced by 20-to-40 cm and Shadows-drawer extension |
| 2026-07-30 | `/root/beam_shape_review` | Complete | Final Camera Offset delta | No P0-P2 source, ABI, binding, lifetime, or performance findings; one report heading corrected | Rebuild and exact runtime |
| 2026-07-30 | `/root` | Waiting | GPU/UI lease | PID 4840 is an Explorer-launched older flashlight build that this turn did not start | Do not touch; run exact Camera Offset smoke after it exits |
| 2026-07-30 | `/root/offset_slider_contract` | Complete | Read-only offset contract | 0-to-20 cm range, 4.5 cm default, fixed forward/convergence, two-to-one lateral mapping, command and regression impacts defined | Implement and reverify |
| 2026-07-30 | `/root/offset_40cm_audit` | Complete | Read-only 40 cm safety contract | 20 cm default, 40 cm maximum, 6 m convergence, parallax and near-axis limits quantified | Implemented; rebuild and runtime remain |
| 2026-07-30 | `/root/shadow_drawer_audit` | Complete | Read-only current-file topology audit | Sibling boundary, IDs, disabled scope, nested disclosure, and remaining test/document impacts verified | Rebuild and future semantic merge remain |
| 2026-07-30 | `/root` | Superseded | `build-flashlight-camera-offset-production` | 516 first-party shader tasks and 29/29 CTest passed; executable SHA-256 `208EABF61E9F48F5CF19342ACD774E962C6C5E8E5E85F91E2A31824DF476F59C` | Replaced by flashlight control-order extension |
| 2026-07-30 | `/root` | Superseded | `build-flashlight-camera-offset-factory` | 51 experiment shader tasks and 29/29 CTest passed | Replaced by flashlight control-order extension |
| 2026-07-30 | `/root/offset_shadows_final_review` | Complete | Frozen 20-to-40 cm and Shadows-drawer candidate | No implementation P0-P2; superseded-hash and Bend-profile documentation findings corrected | Rerun affected checks; exact runtime remains |
| 2026-07-30 | `/root` | Waiting | GPU/UI lease | Two Explorer-launched renderers from other worktrees were present after build verification; this task owns neither | Do not touch; runtime smoke remains deferred |
| 2026-07-30 | `/root` | Complete | Exact production delta smoke, title `main-3ee4ceb-2159` | Shadows disclosure, three nested disabled techniques, 20-to-40 cm slider, 20 cm reset, F on/off, and Cast Shadows receiver difference passed | Broader mode/lifecycle matrix remains; user input stopped cleanup and the exact candidate remains open |
| 2026-07-30 | `/root/flashlight_control_order_audit` | Complete | Initial and frozen-candidate read-only control audits | No P0-P3 in the final candidate; verified PBR scope, primary order, attached realistic-only region, tooltip accuracy, source-contract coverage, both 29/29 suites, binaries, and production hash | Exact UI smoke remains lease-blocked |
| 2026-07-30 | `/root` | Candidate | `build-flashlight-control-order-production` | 516 first-party shader tasks and 29/29 CTest passed; executable SHA-256 `4A96BB5EFC73AEC4CFA586228C2F345B328EACB0ECFA77748D1E4D4BEE2EDB89` | Exact control-order runtime smoke remains |
| 2026-07-30 | `/root` | Candidate | `build-flashlight-control-order-factory` | 51 experiment shader tasks and 29/29 CTest passed | Exact control-order runtime smoke remains |

## Risks and Escalation Triggers

- Shadow depth may be rendered correctly but sampled through an unbound or
  incompatible path; a screenshot alone cannot identify which stage failed.
- Bias or reverse-Z comparison changes can create acne, peter-panning, or
  all-lit/all-shadowed failure, so any convention change needs source and visual
  evidence.
- Flashlight beam-shape metadata reuses private light-constant fields; a repair
  must not overwrite `shadowChannel.x` or ordinary spotlight behavior.
- Source/HLSL overlap with unpublished UI and shadow/TAA tasks requires a future
  semantic merge, not textual assumptions.

Stop and ask the user if:

- A credible repair requires editing Donut, adding a second geometry render,
  accepting a material shadow-quality/performance tradeoff, or choosing a new
  default softness/bias presentation.
- Completion requires a commit, push, pull request, merge, release, or
  deployment.

## Completion

- Final integrated commit: intentionally none unless separately requested
- Verification summary: pending
- Independent review: pending
- Coming Soon and documentation update: pending
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: pending
- Active ownership released: no
- Archived to completed or abandoned path: pending
