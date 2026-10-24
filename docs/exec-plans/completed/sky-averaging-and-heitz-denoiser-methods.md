# Sky Averaging and Heitz Denoiser Methods

## Status

- State: complete
- Coordinator: `/root`
- Branch/worktree: `codex/heitz-ratio-estimator-repair` in
  `work/heitz-ratio-estimator-repair`
- Base: `5d15efd730c574e433bfc58ea4727010ff3a6619` plus the locally
  verified, uncommitted ratio-estimator candidate
- Started: 2026-08-17
- Planned archive:
  `docs/exec-plans/completed/sky-averaging-and-heitz-denoiser-methods.md`

## Goal and Done Condition

Restore an explicit, accurately named sky multi-ray convenience control and
provide a shadow denoising choice whose name and implementation clearly match
the locally supplied Heitz demo, without weakening sparse, MSAA, ratio-pair,
resource-lifetime, or compatibility guarantees.

Done when:

- [x] Sky exposes **Average Multiple Rays** without claiming to be a ratio
      estimator and preserves the selected SPP for quick one-ray versus N-ray
      comparisons.
- [x] The shadow denoising menu provides an accurately scoped Heitz-style
      paired method and does not misrepresent an unsafe unguided Gaussian blur
      as the reference filter.
- [x] Ratio-on filtering still applies identical weights to paired `S_N` and
      `U_N` and divides only after both spatial passes.
- [x] Default output, old commands/snapshots, sparse accumulation, MSAA,
      failure fallback, and optional resource release remain safe.
- [x] Production shaders, focused tests, full Release CTest, documentation,
      independent review, and an exact-build startup smoke pass.

## Scope

In scope:

- Sky multi-ray UI/settings/command compatibility and sampling semantics.
- Shadow denoising method naming and the smallest complete implementation that
  gives the user the requested Gaussian or accurately named Heitz behavior.
- Required CPU/HLSL routing, resources, settings schema, UI, commands, tests,
  shader packaging, and durable documentation.

Out of scope:

- New temporal shadow history, rectangular-area-light models, LTC assets, or
  changes to the ratio estimator's target quantity.
- Changing defaults without a demonstrated need.
- Commit, push, pull request, merge, release, or deployment.

## Assignments

| Task | Owner | Access | Deliverable | Status |
| --- | --- | --- | --- | --- |
| Reference kernel and implementation | `/root/adaptive_radius_audit` | `src/heitz_ratio_estimator_shadows.cpp`, `src/heitz_ratio_estimator_filter_cs.hlsl` | Three-state paired filter and reference-style safe guide | Complete; final review passed |
| Sky control and focused tests | `/root/sky_semantics_audit` | Sky settings and focused tests | Compatible toggle semantics and edge matrix | Complete; final review passed |
| Integration contract tests | `/root/classic_spp_audit` | Assigned disjoint test files | Method, UI, routing, command, and schema contracts | Complete; final review passed |
| Implementation and integration | `/root` | Write owner | Final source, tests, docs, build, and handoff | Complete |

## Initial Decisions

- The visible sky control, if restored, is **Average Multiple Rays**. It is not
  a sky ratio estimator because the producer computes only a binary visibility
  mean.
- Preserve the selected sky SPP while the convenience toggle is off so the
  user can return to the same N-ray configuration in one click.
- Do not call a plain unguided Gaussian blur the Heitz method: the demo's final
  filter includes Gaussian spatial weights, bilateral geometry/analytic guides,
  adaptive radius estimation, paired filtering, and post-filter division.
- Keep all new behavior default-neutral until the reference and integration
  audits determine the smallest truthful method contract.

## Final Design Decisions

- **Average Multiple Rays** reuses the serialized legacy bool. Off means one
  binary sky ray; on means the retained SPP arithmetic mean. The SPP control
  remains live, and only an effective-count change invalidates history.
- **Heitz-Style Paired Bilateral** is appended as a Shadows-only method. It is
  active only for soft, ratio-enabled, 1x, complete sun evidence, forces the
  existing adaptive radius passes, and otherwise stays Raw. An adaptive-only
  failure keeps the selected geometry guide with a fixed paired radius.
- The new method uses the reference depth, normal, and two-plane guide shapes
  with production finite and orientation guards. It omits the analytic-area-
  light guide because UVSR has no matching analytic area-integrated texture.
- No plain unguided Gaussian option is added. It is not the demo method and
  would cross unrelated silhouettes without a separate diagnostic-only use.

## Verification Plan

| Area | Required Evidence | Result |
| --- | --- | --- |
| Sky control | Settings/UI/command/snapshot/source tests for off=one and on=selected SPP | Passed across stored 1 through 64 SPP, effective-count-only history, and legacy command state |
| Denoiser math | Numeric fixtures and source contracts against reference equations | Passed shared reference guide, adaptive, nonfinite, opposite-normal, denormal, maximum-float, and randomized bounds |
| Routing | Ratio/classic, 1x/MSAA, sparse/complete, accumulation, fallback, and resource lifetime | Passed focused contracts plus three final independent reviews |
| Packaging | Every production shader permutation and runtime file contract | Passed 318 shader tasks and 52 packaged shader files |
| Integration | Release build, full CTest, title-case audit, and diff check | Passed Release link, 47/47 CTest, 2,220 maintained headings, README counts, and patch whitespace |
| Runtime | Exact executable startup and a clearly stated visual/performance boundary | Exact build remained alive for a 12-second hidden smoke; the task-owned PID was stopped and a follow-up confirmed no residual process |

## Completion

- Final commit: intentionally none unless separately authorized
- Verification: Release build, 318 shaders, 52 packaged shader files, schema
  `0006`, full 47/47 CTest, documentation checks, and exact-build startup smoke
- Independent review: three final read-only audits passed; two P3 documentation
  statements and one P3 extreme-value test gap were corrected
- Remaining visual experiments: user-facing comparison of Raw, Gaussian
  Bilateral, and Heitz-Style Paired Bilateral plus matched convergence and
  performance captures; no visual-quality or timing claim is made here
- Active ownership released: yes
- Archive state: completed
