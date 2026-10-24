# MSAA Shadow Antialiasing Control

## Status

- State: complete local candidate
- Coordinator: `/root`
- Project branch/worktree: `codex/ratio-shadow-msaa-cmaa2-prep` in
  `work/ratio-shadow-msaa-cmaa2-prep`
- Base state: commit `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`
  plus the locally verified ratio/MSAA/CMAA2 diff recorded in
  `docs/exec-plans/completed/ratio-shadow-msaa-sparse-edge-repair.md`
- Starting tracked-diff blob SHA-1:
  `414c77e51def7e5bd21e461c9ca3403af52bc99a`
- Started: 2026-08-14
- Last updated: 2026-08-14
- Planned archive:
  `docs/exec-plans/completed/msaa-shadow-antialiasing-control.md`

## Goal and Done Condition

Goal:

Add a default-on Multisample Adaptive control that preserves the current
sample-frequency ray-traced sun shadows when enabled and traces only one
coherent closest receiver per pixel when disabled, reducing combined MSAA and
ray-tracing cost without changing the default image.

Done when:

- [x] The visible and command settings default on, reset with the Multisample
      quality recipe, and describe the quality/performance tradeoff precisely.
- [x] Disabled shadow antialiasing selects the same closest reverse-Z receiver
      as the coherent MSAA resolve and emits compatible final-direct and GI
      source modulation without tracing other receivers.
- [x] State changes invalidate affected temporal/accumulation history.
- [x] Focused shader, settings, UI, catalog, bundle, Release build, and full
      regression checks pass on an exact candidate.

## Scope

In scope:

- Multisample settings, UI, command catalog, reset/custom-state behavior, and
  documentation.
- Heitz ratio-estimator CPU/HLSL control and deterministic tests for 1x and
  2x/4x/8x/16x receiver topologies.
- Temporal and progressive-history invalidation for the image-policy change.

Non-goals:

- Changing flashlight or sky visibility, which already use a single coherent
  closest surface under MSAA.
- Claiming the lower-cost broadcast mode is sample-frequency correct at mixed
  primitive edges.
- Touching, integrating, publishing, or controlling the active settings-menu
  worktree or either running UVSR process.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, anti-aliasing settings/catalog, Heitz CPU/HLSL contracts,
  tests, README counts, and task documentation.

## Baseline

- Current repaired executable: `b/bin/uvsr.exe`, SHA-256
  `23F23079623F5B890D6F326157A6430DA1358C780E32D2279B033CE1F235CB65`.
- That baseline process exited without agent intervention; the replacement
  candidate was rebuilt at the same exact path.
- The separate settings-menu task is active in
  `work/settings-menu-revamp` from the same `0224649` base. Its coordinator has
  been notified of the overlapping future UI/catalog integration surface.
- The starting candidate passed Release `ALL_BUILD`, all 332 shader tasks, 47
  staged shader binaries, and 43 of 43 CTests.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Ratio/MSAA repair | Closest-owner and analytic-weighted two-output contract | Complete | New shadow budget mode |
| Settings-menu revamp | Serialize later UI/catalog composition | Active elsewhere | Eventual integration |
| Read-only math review | Closest-only response and sequence contract | Complete | Shader implementation |
| Read-only settings/test review | Complete visible and command surface | Complete | UI and tests |

Public contracts under review:

- Enabled preserves independent per-covered-receiver shadow ratios.
- Disabled chooses one closest valid receiver before tracing, uses one receiver
  sequence, broadcasts its total ratio to final direct lighting, and publishes
  its diffuse ratio to the closest-surface GI source.
- The new control is default on so existing quality and costs do not silently
  change.

## Assignment Summary

| Task ID | Owner | Base | Write Scope | Status |
| --- | --- | --- | --- | --- |
| SHADOW-MATH | `/root/msaa_repair_math_review` | Dirty candidate | None; read-only | Complete |
| SETTINGS-TESTS | `/root/msaa_repair_test_review` | Dirty candidate | None; read-only | Complete |
| SHADOW-CODE | `/root/msaa_repair_code_review` | Dirty candidate | None; read-only | Complete |
| IMPLEMENT-VERIFY | `/root` | Dirty candidate | Task-owned source/tests/docs | Complete |

## Integration Order

1. Fix the closest-only receiver, sequence, output, and history contract.
2. Add focused failing tests and the default-on settings surface.
3. Implement the shader/CPU route and durable documentation.
4. Wait for the user-owned candidate process to exit, then rebuild and verify.
5. Run an independent final review and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Defaults and recipes | Default-on, reset, custom-state reference tests | Temporal-AA and UI tests | Passed |
| Ray budget | Closest-only mode traces one receiver across 2x/4x/8x/16x | Heitz CPU/source contracts | Passed |
| Output ownership | Total final ratio and diffuse GI ratio use the selected owner | Heitz source/reference tests | Passed |
| Shader completeness | All required variants compile and package | Release build and bundle tests | 332 tasks; 47 files |
| Regression | Complete configured suite | Full CTest | 43 of 43 passed |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-14 | Default the control on | Preserve the repaired sample-frequency image unless the user deliberately chooses the cheaper approximation | All |
| 2026-08-14 | Reuse the closest coherent receiver when off | It guarantees one receiver budget and supplies the existing GI owner; pooled or random receivers have no stable single-surface owner | Shader and docs |
| 2026-08-14 | Preserve the physical MSAA receiver count and two-output ABI | Hit-distance/SIGMA availability and closest-source routing remain topology contracts even when only one receiver is traced | CPU and consumers |
| 2026-08-14 | Write owner total and diffuse factors directly | A second analytic response division can fail open in zero-response channels, and coverage scaling would attenuate sparse samples twice | Shader and tests |
| 2026-08-14 | Keep one coordinator writer | Settings, shader constants, output semantics, UI, and tests are coupled | All |

## Risks and Escalation Triggers

- Closest-only visibility is intentionally broadcast and can alias at mixed
  geometry/material edges; the UI and docs must not imply equal quality.
- A one-pass loop can trace an earlier receiver before discovering a closer
  one; disabled mode must determine ownership before any ray query.
- The settings-menu task overlaps `src/uvsr.cpp` and catalog tests in a separate
  worktree; neither task may integrate over the other without a semantic
  composition and combined verification.
- Do not build over PID 49908 or control either renderer window.

## Completion

- Final integrated commit: not created; commits are not authorized.
- Verification summary: Release `ALL_BUILD`, 332 shader tasks, 47 runtime shader files, and 43 of 43 CTests passed; executable SHA-256 is `9F716E1B6AADC591086719FD381387D9FE7CDBF0AF9C786B521EB3E45581CAB6`.
- Independent review: shader/CPU and math reviews found no runtime defect; both test-review gaps were repaired before final verification.
- Coming Soon/documentation update: no current Coming Soon section; durable
  settings documentation is in scope.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: live visual and performance comparison remains optional product evidence.
- Active ownership released: yes.
- Archived to completed/abandoned path: `docs/exec-plans/completed/msaa-shadow-antialiasing-control.md`.
