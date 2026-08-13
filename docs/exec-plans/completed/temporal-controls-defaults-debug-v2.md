# Temporal Controls, Defaults, and Debug Views V2

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/temporal-accumulation-ui-repair` at `C:/Users/brock/OneDrive/Documents/uvsr/work/temporal-accumulation-ui-repair`
- Base commit: `ae7112f4557365e91ce80169c346c47e0f95a2fc` plus the completed local temporal-accumulation candidate recorded in `docs/exec-plans/completed/temporal-accumulation-ui-repair.md`
- Starting working-diff identity: Git tracked-diff hash `5d5b8b6fffdf525e8c1b26ef350d6b6ed0e665fe`; prior candidate executable SHA-256 `FDFBB1059AB82C1AD42B04F01BDE444808D74AFE48B421912C1843EB40B3373F`
- Started: 2026-08-13
- Last updated: 2026-08-13
- Planned archive: `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`

## Goal and Done Condition

Goal: refine the current temporal-accumulation candidate so animated Ray Marching noise remains visibly animated during camera motion, variance-guided accumulation is the fully exposed default tuning model, temporal-motion ownership is configurable without corrupting screen-space means, requested lighting and jitter defaults are applied, RESTIR naming is uniform, and both render paths expose useful transport and sky-visibility debug views.

Done when:

- [x] Ray Marching honors each producer's Animate Samples setting during movement while retained stationary samples use a deterministic per-pixel sequence.
- [x] Ray-traced directional shadows and sky visibility default enabled at 2 SPP; TAA defaults to Halton 8; Stationary When Idle defaults on; Variance Guided is the default accumulation preset.
- [x] All accumulation tuning controls are exposed whenever accumulation is enabled, preset edits display `Preset (Custom)`, and convergence controls visibly determine when expensive sampling eases to bounded revisits.
- [x] Motion-history policy is explicit and preserves only revalidated proposal history; screen-space radiance means and RESTIR GI checkpoints never survive camera movement.
- [x] RESTIR spelling is uniform in first-party source, UI, tests, and documentation.
- [x] Path Tracing exposes Primary and Indirect Transport debug options, and Sky Visibility is inspectable in Ray Marching. Path Tracing intentionally omits a fabricated sky scalar because none exists without new estimator rays.
- [x] Focused tests, all shaders, the Release renderer, full functional CTest, documentation checks, independent review, and bounded runtime UI/render inspection pass for the exact replacement executable.

## Scope

In scope:

- Sampling-phase ownership during motion and stationary accumulation.
- Accumulation presets, custom-state derivation, adaptive convergence controls, UI commands, defaults, documentation, and tests.
- Motion-reset policy split between non-reprojectable means and reprojectable RESTIR reservoirs.
- Ray-traced shadow/sky-visibility, TAA jitter, and flashlight defaults.
- RESTIR casing in first-party files.
- Path Tracing transport debug controls and ray/path sky-visibility debug output where supported by existing signals.

Non-goals:

- Retaining non-reprojected screen-space radiance means across camera motion.
- New denoisers, a general temporal upscaler, unrelated UI redesign, edits under `donut/`, or speculative transport buffers solely to manufacture a Path Tracing sky signal.
- Commit, push, pull request, merge, release, or deployment without later authorization.

Affected subsystems and paths:

- `src/uvsr.cpp`, sampling/accumulation settings and shaders, path-tracing settings/pass/shader contracts, ray-traced shadow and sky-visibility settings, temporal-AA defaults, flashlight defaults, command catalog, focused tests, and renderer documentation.

Shared hotspots reserved for the coordinator:

- All writable paths, especially `src/uvsr.cpp`, `CMakeLists.txt`, `README.md`, shared CPU/HLSL contracts, build trees, packaged shaders, UVSR processes, and this execution plan.

## Baseline

- Canonical repository/remote: not refreshed because this is a local continuation of the exact user-named `ae7112f` lineage and no publication or latest-remote operation is authorized.
- Local versus remote state: local feature branch with the completed first-pass dirty candidate; unrelated worktrees and active plans remain untouched.
- Verified source commit/build: prior first-pass candidate `b-ta/bin/uvsr.exe`, SHA-256 `FDFBB1059AB82C1AD42B04F01BDE444808D74AFE48B421912C1843EB40B3373F`, passed 327 shaders, Release link, and 43/43 CTest before this resumed scope.
- GPU, scene, camera, resolution, and settings preset when relevant: previous Sponza runtime smoke; replacement runtime matrix will explicitly exercise moving/static noise, adaptive convergence, defaults, and debug views.
- Known pre-existing failures: user-reported stuck Ray Marching noise during motion; hidden accumulation tuning outside Custom; missing Path Tracing transport debug choices; missing sky-visibility debug; defaults and RESTIR casing do not match requested behavior.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Motion And Accumulation Audit | Distinguish animated frame noise, stationary sample sequence, raw mean reset, and revalidated proposal policy | Complete | Coordinator implementation |
| Debug Signal Audit | Map existing transport and sky-visibility signals to debug output without inventing expensive buffers | Complete | Coordinator implementation |
| Defaults And Naming Audit | Exact default owners, preset/custom precedent, RESTIR scope, and focused tests | Complete | Coordinator implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Animate Samples controls changing stochastic phases while the camera moves; stationary retained accumulation indexes decorrelated samples by each pixel's successful-sample count.
- A preset is a complete named parameter vector. Editing any exposed parameter retains the selected preset as provenance and displays `<Preset> (Custom)` until that origin recipe is explicitly reapplied or all fields are returned to it.
- Variance Guided warms every eligible pixel, then reduces ray work according to target error and bounded revisit controls. No pixel may starve.
- Camera motion always resets screen-space radiance means and RESTIR GI radiance checkpoints. A separate toggle allows only re-evaluated direct proposals and RESTIR PT path seeds to survive camera-only movement.
- New debug modes must be fail-safe and must not alter the Final Image path.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| MOTION-2 | `/root/temporal_audit` | Shared read-only checkout | Current dirty candidate | None | None | Complete |
| DEBUG-2 | `/root/ui_spacing_audit` | Shared read-only checkout | Current dirty candidate | None | None | Complete |
| DEFAULTS-2 | `/root/flashlight_audit` | Shared read-only checkout | Current dirty candidate | None | None | Complete |
| INT-2 | `/root` | Feature worktree | Current dirty candidate | All task-owned files | All audits | Complete |

## Assignment Contracts

### Motion-2: Audit Sampling Motion, Adaptive Work, and Temporal Ownership

- Owner/thread: `/root/temporal_audit`
- Branch/worktree: shared read-only feature worktree
- Base commit/state: `ae7112f` plus current dirty first-pass candidate and this plan
- Read scope: sampling phases, lighting accumulation, path-tracing reservoirs/means, invalidation signatures, settings/tests/docs.
- Write scope: none.
- No-touch scope: all filesystem and Git/index state.
- Build directory and runtime/GPU/resource lease: none.
- Interface/invariant contract: animated movement noise, per-success stationary sequences, bounded adaptive revisits, raw means reset on motion, optionally reprojectable RESTIR reuse.
- Deliverable: exact root cause, smallest safe CPU/HLSL contract, toggle semantics, tests, and risks.
- Done when: every temporal owner has an explicit reset/reuse decision and the moving-noise regression is reproducible from source.
- Required verification: read-only source/test inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: valid motion reuse would require unimplemented reprojection for the raw mean.
- Handoff revision/artifact: final read-only review found no remaining P0/P1 correctness defect after the per-pixel invalid-attempt retry salt landed.
- Handoff acknowledged by/on: `/root`, 2026-08-13.

### Debug-2: Audit Accumulation UI and Debug Signal Integration

- Owner/thread: `/root/ui_spacing_audit`
- Branch/worktree: shared read-only feature worktree
- Base commit/state: current dirty candidate and this plan
- Read scope: accumulation UI/preset precedents, Ray and Path debug menus, debug-enum routing, sky-visibility/transport buffers, UI contracts/tests/docs.
- Write scope: none.
- No-touch scope: all filesystem and Git/index state.
- Build directory and runtime/GPU/resource lease: none.
- Interface/invariant contract: always-visible controls, deferred dropdown mutations, stable animated layout, `<Preset> (Custom)` provenance, debug modes with unchanged Final Image.
- Deliverable: file/line map, feasible Ray/Path modes, minimal UI design, and focused tests.
- Done when: transport and sky visibility each have a traced producer-to-debug-output route or a bounded reason Path support is not inexpensive.
- Required verification: read-only source/test inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: a Path sky view requires a new persistent transport buffer or materially changes integrator cost.
- Handoff revision/artifact: Ray Sky Visibility and Path Primary/Indirect Transport routes implemented; a truthful Path sky scalar would require new estimator work and was rejected under the stop condition.
- Handoff acknowledged by/on: `/root`, 2026-08-13.

### Defaults-2: Audit Defaults, Naming, and Regression Coverage

- Owner/thread: `/root/flashlight_audit`
- Branch/worktree: shared read-only feature worktree
- Base commit/state: current dirty candidate and this plan
- Read scope: shadow/sky/flashlight/TAA/sample defaults, RESTIR occurrences, command catalog, tests/docs, and Aliasing/Diffuse custom-preset precedent.
- Write scope: none.
- No-touch scope: all filesystem and Git/index state.
- Build directory and runtime/GPU/resource lease: none.
- Interface/invariant contract: requested factory/reset defaults, exact `RESTIR` spelling, and no silent domain mismatch.
- Deliverable: exact default owners, occurrence inventory, precedent recommendation, and test update list.
- Done when: all requested default/reset surfaces and exact casing occurrences are accounted for.
- Required verification: read-only source/test inspection.
- Allowed Git and external actions: read-only only.
- Stop and report if: the old mixed-case spelling is required verbatim by a third-party/public serialized identifier.
- Handoff revision/artifact: requested factory/reset defaults, three-preset provenance, catalog values, and RESTIR casing verified; external sky-plan contradiction recorded without taking its ownership.
- Handoff acknowledged by/on: `/root`, 2026-08-13.

## Integration Order

1. Complete the three read-only audits and freeze the motion-reuse and debug-signal contracts.
2. Add or update focused failing tests for defaults, moving sample phases, preset customization, adaptive scheduling, motion policy, casing, and debug routing.
3. Implement the shared settings/UI/CPU/HLSL changes, then build and run focused tests.
4. Run an independent high-risk temporal/shader review and repair substantiated findings.
5. Run all shaders, Release link, full CTest, document/heading/diff checks, and bounded runtime visual inspection.
6. Record the replacement executable identity and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Animated movement noise | Source/unit contract plus live moving-camera A/B for Animate off/on | Focused renderer contracts and runtime capture | Passed; movement resets use live animated frame phases while stationary accepted samples use per-pixel success counts |
| Requested defaults | Unit/default/reset/catalog/UI assertions | Focused settings and source-contract tests | Passed; shadows/sky 2 SPP on, Halton 8, Stationary When Idle on, Variance Guided |
| Adaptive tuning and preset customization | Round-trip/custom-detection/scheduling tests plus UI inspection | Sample settings and UI tests | Passed; six visible fields, origin-based resets, bounded revisits, `Preset (Custom)` |
| Safe motion-history policy | Temporal-owner reset/reuse contracts and moving-scene smoke | Path/Ray source tests and runtime | Passed; only re-evaluated proposals/seeds may survive camera-only motion |
| RESTIR casing | Repository occurrence scan and affected tests | `rg` plus focused/full suite | Passed; the legacy mixed-case product spelling is absent |
| Transport and sky debug | Enum/routing/shader contracts plus runtime dropdown/output inspection | Path/Ray debug tests and runtime | Passed for Ray Sky Visibility and both Path transport views; Path sky stopped as intentionally infeasible in narrow scope |
| Integrated candidate | All shaders, Release target, full CTest, heading/diff audit, executable SHA-256 | Isolated `b-ta` rebuild and final audit | 327/327 shaders, final runtime shader bundle verification, Release link, focused post-review checks, and final 43/43 CTest passed; executable SHA-256 `F54659CD3F01FDBDDF99CA7099A995473CF8893E99F5620BF53B7D4A8C9F98D8` |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-13 | Continue in the exact first-pass worktree and start a new active plan. | The user is refining the current local candidate; the archived plan is historical and must not be reopened. | All |
| 2026-08-13 | Keep the coordinator as sole writer and use read-only parallel audits. | The requested settings, UI, renderer integration, and CPU/HLSL contracts overlap heavily in `src/uvsr.cpp` and shared temporal interfaces. | All |
| 2026-08-13 | Split motion policy by temporal owner. | A global preserve-history switch would smear non-reprojected means; RESTIR reservoirs have distinct reprojection and validation semantics. | MOTION-2, INT-2 |
| 2026-08-13 | Treat the existing active sky-visibility plan as external lineage context, not writable ownership. | This branch already contains the integrated sky feature from `ae7112f`; the requested debug/default refinements remain local and must be reconciled by a future publisher. | DEBUG-2, INT-2 |
| 2026-08-13 | Preserve only revalidated direct proposals and RESTIR PT seeds during camera-only movement. | Direct proposals are re-evaluated and visibility-retraced at the current surface, and PT seeds are reintegrated; screen-space means and RESTIR GI store radiance and therefore always reset. | MOTION-2, INT-2 |
| 2026-08-13 | Do not add a Path Tracing Sky Visibility debug view. | Path transport has no truthful cosine-hemisphere sky-visibility scalar; adding one would require a new estimator or extra rays and violates the narrow-debug stop condition. | DEBUG-2, INT-2 |
| 2026-08-13 | Use the existing Path variance texture alpha as an invalid-attempt retry salt. | It avoids a new resource/binding while guaranteeing that a rejected stationary attempt does not replay the same seed; skips preserve it and success clears it. | MOTION-2, INT-2 |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-13 | `/root` preflight | Complete | `ae7112f` plus tracked diff hash `5d5b8b6f...` | Branch/status/worktrees/active plans/prior plan/runtime process checked; no UVSR process running | Dispatch read-only audits |
| 2026-08-13 | `/root` implementation | Complete | Dirty local feature candidate | Requested defaults, phase ownership, adaptive tuning, provenance UI, selective motion reuse, naming, and debug routes implemented | Independent review and verification |
| 2026-08-13 | `/root` verification | Complete | `b-ta/bin/uvsr.exe`, SHA-256 `F54659CD3F01FDBDDF99CA7099A995473CF8893E99F5620BF53B7D4A8C9F98D8` | 327 shader tasks, exact bundle verification, Release link, focused tests, runtime Sponza UI/motion/Ray Sky/Path transport/reuse smoke, heading audit, diff check, and final 43/43 CTest passed | Local handoff |

## Risks and Escalation Triggers

- Changing sample phases during movement must not double-advance stationary retained sequences or make Animate Samples silently ineffective.
- Preserving RESTIR reservoirs through motion may still require existing motion-vector/depth validation; fail closed if that path is absent.
- An adaptive scheduler can save traced work only if skipped pixels do not trigger equivalent producer/denoiser work elsewhere.
- Debug output must not create resource lifetime or binding hazards and must leave Final Image bit-identical.
- The separate sky-visibility branch has an active publication owner; no Git integration or external action is part of this local task.

Stop and ask the user if:

- A requested Path Tracing sky view requires a substantial new transport decomposition instead of exposing an existing signal.
- A motion-history option can only be implemented by preserving non-reprojected radiance and accepting visible ghosting.
- Verification would require stopping or reconfiguring unrelated applications, or any publication/destructive action becomes necessary.

## Completion

- Final integrated commit: none; local implementation only.
- Verification summary: 327 shaders, exact bundle verification, Release link, focused contracts, bounded Sponza runtime checks, heading/diff checks, generated README count, and final 43/43 CTest passed.
- Independent review: temporal/shader review found no remaining P0/P1; defaults/naming review found implementation clean and recorded one external-plan documentation inconsistency; final UI review complete after origin-reset parity and dropdown-focus repairs.
- Coming Soon/documentation update: README has no Coming Soon section; durable docs and this plan will be updated.
- Pushed/PR/merged, or intentionally local: intentionally local unless later authorized.
- Remaining experiments or follow-ups: a truthful Path Tracing sky-visibility scalar would be a separate estimator feature; the externally owned active sky-visibility plan still records old defaults and must be reconciled by its owner.
- Active ownership released: all subagents and coordinator released.
- Archived to completed/abandoned path: `docs/exec-plans/completed/temporal-controls-defaults-debug-v2.md`.
