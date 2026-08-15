# Ratio, MSAA, and TAA Main Integration

## Status

- State: completed local candidate
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ratio-shadow-msaa-cmaa2-integration` in `work/ratio-shadow-msaa-cmaa2-integration`
- Base commit: GitHub `main` at `3bc13fd3c170d746366f01404a7c4b726efdcab9`
- Started: 2026-08-15
- Last updated: 2026-08-15
- Archive: `docs/exec-plans/completed/ratio-msaa-taa-main-integration.md`

## Goal and Done Condition

Goal: create a local integration candidate that composes the completed Ratio-Estimator ray-traced shadows, MSAA/GI sparse-edge repairs and shadow sampling control, CMAA2 removal, and TAA stability work onto the settings-menu and denoising baseline now on GitHub `main`.

Done when:

- [x] Every old-candidate behavior is classified and ported or explicitly excluded without replacing new-main settings, denoising, UI, or snapshot-schema behavior.
- [x] The settings command registry is regenerated from the composed source, assigned a new immutable schema version/fingerprint, and proven by the registry probe and snapshot tests.
- [x] All registered shader tasks, the exact production runtime bundle, the Release executable, focused tests, and the complete CTest suite pass.
- [x] Independent reviews report no unresolved rendering, resource-lifetime, deletion, UI/schema, packaging, or documentation defect.
- [x] The exact local executable and remaining visual/runtime evidence are handed off without publishing or changing canonical `main`.

## Scope

In scope:

- Heitz ratio-estimator directional shadows with matched per-receiver MSAA evaluation, closest-source routing, GI energy correction, and the Per-Sample Shadows performance/quality control.
- Sparse-edge and descriptor fail-closed MSAA repairs.
- Complete CMAA2 source, shader, build, legal-sample, UI, and documentation removal while preserving MSAA, TAA, FXAA, AgX, output transfer, and dithering.
- TAA lifecycle, stationary/thin-outline coverage, moving-point depth, Halton-16, and Shared Primary adaptive-phase repairs.
- Semantic composition with the `3bc13fd` settings-menu, denoising, retained-row animation, settings snapshot schema, and registry probe.

Non-goals:

- Publishing, merging, pushing, opening a pull request, changing canonical `main`, reviving retired visibility-sample rotation, or redesigning the new settings menu.
- New denoising algorithms beyond preserving the mainline behavior and adapting the integrated controls to it.
- Performance claims without a later matched benchmark window.

Affected subsystems and paths:

- Rendering/shaders: ratio estimator, MSAA resolve/deferred lighting, TAA, path accumulation, shader manifest and bundle.
- Application/settings: `src/uvsr.cpp`, anti-aliasing settings, UI command catalog, settings snapshot schema/version registry.
- Build/legal/docs/tests: CMake, CMAA2 inventory, maintained renderer documentation, source/reference/schema/package tests.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `.gitattributes`, `src/shaders.cfg`, `src/uvsr.cpp`, `src/ui_settings_command_catalog.h`, all snapshot-schema files, global test catalogs, legal indexes, Git/index state, build tree, executable, and this plan.

## Baseline

- Canonical repository/remote: live `origin/main` and `git ls-remote` both resolve to `3bc13fd3c170d746366f01404a7c4b726efdcab9`.
- Local versus remote state: integration worktree is a clean branch from the exact live commit; the source candidate remains preserved and dirty in `work/ratio-shadow-msaa-cmaa2-prep` at base `0224649`.
- Verified source commit/build: main `3bc13fd` reports 46/46 post-push CTests; the old local candidate built 332 shaders, staged 47 runtime files, and passed 43/43 CTests before main composition.
- GPU, scene, camera, resolution, and settings preset when relevant: the external settings-menu renderer exited before closeout, but this candidate was not launched and no visual or performance result is inherited across integration.
- Known pre-existing failures: none in the two recorded input verification matrices; the primary expected integration conflict is UI/catalog/snapshot-schema composition.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| GitHub settings-menu baseline | `3bc13fd3c170d746366f01404a7c4b726efdcab9` | Integrated as branch base | Entire candidate |
| Old Ratio/MSAA/CMAA2/TAA candidate | Preserved dirty diff from `0224649` plus five completed plans | Frozen input | Semantic port |
| Settings snapshot registry | Version `0004`, fingerprint `1066f489a97640a0c9acaa2363b7fe3e` | Complete | Snapshot encoder/decoder/tests/docs |
| Runtime/GPU lease | No UVSR process remained at closeout | Released | Visual smoke remains user-facing |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Preserve mainline settings-menu hierarchy, row animation, denoising settings, and existing snapshot schema versions.
- Add the new anti-aliasing/shadow command only through the canonical command catalog and append a fresh immutable schema version generated from the composed registry.
- Preserve the ratio-estimator constant-buffer alignment, physical MSAA sample-count result contract, total-versus-diffuse output routing, and fail-closed closest-surface resolve.
- CMAA2 removal must leave no runtime/build/package/legal dependency and must not remove MSAA resolve, TAA, FXAA, AgX, display transfer, or dithering.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| INTEGRATE-UI-SCHEMA-AUDIT | `/root/integration_ui_schema_audit` | Shared read-only | Old candidate plus `3bc13fd` | None | None | Complete |
| INTEGRATE-RENDERING-AUDIT | `/root/integration_rendering_audit` | Shared read-only | Old candidate plus `3bc13fd` | None | None | Complete |
| INTEGRATE-INVENTORY-AUDIT | `/root/integration_inventory_audit` | Shared read-only | Old candidate plus `3bc13fd` | None | None | Complete |
| INTEGRATE-IMPLEMENT | `/root` | Integration worktree | `3bc13fd` | All in-scope integration paths | Audits inform conflict resolution | Complete |
| INTEGRATE-PBR-SOURCE | `/root/integration_rendering_audit` | Integration worktree | Frozen candidate on `3bc13fd` | PBR deferred-lighting source files and PBR tests only | Final rendering audit | Complete |
| INTEGRATE-REVIEW | `/root/final_ratio_pbr_review` and `/root/final_integration_hygiene_review` | Integration worktree read-only | Final repaired diff | None | Implementation | Complete |

## Assignment Contracts

### `INTEGRATE-IMPLEMENT`: Compose and Verify the Candidate

- Owner/thread: `/root`
- Branch/worktree: `codex/ratio-shadow-msaa-cmaa2-integration` in `work/ratio-shadow-msaa-cmaa2-integration`
- Base commit/state: clean `3bc13fd`
- Read scope: both input trees, mainline schema tooling, completed plans, relevant docs/tests
- Write scope: only the integration worktree's in-scope files and this plan
- No-touch scope: original dirty candidate, canonical checkout, other worktrees/branches, remotes, running UVSR process, and `donut/`
- Build directory and runtime/GPU/resource lease: `b`; coordinator-only; the short path avoids the Windows MSBuild path limit, and no runtime control occurs until the external PID exits or ownership is transferred
- Dependencies already integrated: settings-menu and denoising commit `3bc13fd`
- Interface/invariant contract: preserve both valid input behaviors; regenerate snapshot identity; no wholesale ours/theirs resolution
- Deliverable: local dirty integration branch, exact Release artifact, verification evidence, archived plan
- Done when: every goal criterion has evidence or a plainly reported runtime-only remainder
- Required verification: registry probe/schema checks; focused Ratio/MSAA/TAA/UI/settings tests; all shader tasks; exact runtime bundle; full CTest; legal, headings, line-count, and diff checks; independent review
- Allowed Git and external actions: local branch/worktree creation and edits/build/tests only; no stage, commit, push, PR, merge, release, or process control outside this task
- Stop and report if: an integration choice changes a visible default or quality/performance tradeoff, a peer writes an owned path, snapshot compatibility cannot be appended safely, or new authority is required
- Handoff revision/artifact: local dirty candidate based on `3bc13fd`; `b/bin/uvsr.exe` SHA-256 `8673815016D33351A6C2AA1D2F956F01B811452E5D697E9BE6C986A3E68CA923`
- Handoff acknowledged by/on: coordinator, 2026-08-15

### `INTEGRATE-PBR-SOURCE`: Separate Final and GI Sun Modulation

- Owner/thread: `/root/integration_rendering_audit`
- Branch/worktree: shared integration worktree, no Git ownership
- Base commit/state: `3bc13fd` plus tracked diff `01583ea484f8cf8c4352ed1710ba9c2524f3424e`
- Write scope: `src/pbr_deferred_lighting_cb.h`, `src/pbr_deferred_lighting_pass.h`, `src/pbr_deferred_lighting_pass.cpp`, `src/pbr_deferred_lighting_cs.hlsl`, `tests/pbr_lighting_source_contract_tests.cpp`, and `tests/pbr_reference_tests.cpp`
- No-touch scope: every other path, Git/index state, build trees, processes, remotes, and `donut/`
- Interface contract: add one final optional `DirectLightVisibility` argument for the write-source sun; preserve the public call sequence and CB size; final direct diffuse/specular use the ordinary total visibility, while source diffuse alone may use the alternate visibility; incomplete or incompatible alternate input falls back to the ordinary signal
- Required tests: source contract plus a numeric total-versus-diffuse composition oracle; no build or test execution by the worker
- Handoff: exact files, contract, assumptions, and released ownership

## Integration Order

1. Inventory both inputs, classify overlapping files, and mechanically port nonoverlapping candidate changes.
2. Resolve build/shader/legal changes, then rendering/resource contracts, then application/UI/settings composition.
3. Run the composed registry probe and append the new snapshot schema identity before adapting catalog/UI/schema tests and docs.
4. Run focused checks, freeze the diff for independent review, repair findings, then rebuild shaders/Release and run the full suite.
5. Reconcile README and maintained docs once, archive this plan, and hand off the exact local executable.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Exact live base | Remote/local identity | `git ls-remote`, `git rev-parse`, ancestry check | `3bc13fd`; complete |
| Complete semantic composition | Three-way diff plus focused source/reference tests | CMake target builds and focused CTest regex | Passed after final 1x source-routing repair |
| Snapshot compatibility | Probe-derived count/fingerprint and append-only schema tests | Registry probe plus snapshot tests | Schema `0004`; fingerprint `1066f489a97640a0c9acaa2363b7fe3e`; passed |
| Shader/package integrity | Registered task count and exact staged bundle | `uvsr_shaders`, bundle CTests | 311 of 311 tasks and 48 staged binaries passed |
| Combined regression safety | Complete configured matrix | `ctest -C Release --output-on-failure` | 46 of 46 CTests passed after final repairs |
| Repository hygiene | Legal, headings, README counts, diff/status | Repository tools | Legal, 2,165 headings, README 127,351/386,164/513,515 counts, and diff check passed |
| User inspection | Exact executable path/hash; no borrowed acceptance | Artifact handoff | Exact Release artifact recorded; visual inspection remains unrun |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-15 | Preserve the old dirty candidate and integrate in a fresh worktree | Rebasing or resetting the original would risk the verified, uncommitted source state. | All |
| 2026-08-15 | Use `3bc13fd` as the only integration base | It is the verified live GitHub main requested by the user; local canonical checkout state is unrelated. | All |
| 2026-08-15 | Regenerate rather than reuse snapshot identity | The command ordering and count changed on both inputs; an old fingerprint cannot represent the composed registry. | UI/schema/tests/docs |
| 2026-08-15 | Keep all sun denoising single-sample-only | The response-weighted MSAA shadow can span heterogeneous receivers while every available denoising guide owns one closest surface; third-party SIGMA also requires an actually produced matched hit distance. | Rendering/UI/docs/tests |
| 2026-08-15 | Use the short ignored `b` build directory | The longer task-specific directory exceeded Windows' 260-character MSBuild intermediate-file limit for source-contract targets. | Build/verification |
| 2026-08-15 | Export distinct 1x total and diffuse ratios | Final direct lighting needs the total-response factor, while screen-space GI transports diffuse radiance; one extra source permutation reuses the same rays and keeps physical hit distance exclusive to a matched one-ray signal. | Heitz/PBR/GI/tests/docs |
| 2026-08-15 | Preserve method-specific 1x sun denoising | Built-in bilateral methods need no hit distance and can filter hard or soft total visibility; third-party SIGMA still requires non-hard one-ray visibility plus its matched distance. | Rendering/UI/docs/tests |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-15 | Coordinator | Complete | Clean branch/worktree at `3bc13fd` | Remote, worktree, plans, Coming Soon, PRs, and process preflight complete | Composed the frozen old candidate |
| 2026-08-15 | Coordinator | Complete | Dirty integration candidate on `3bc13fd` | 311 of 311 shaders, 48 staged binaries, schema `0004`, Release build, and 46 of 46 CTests pass | User visual smoke |
| 2026-08-15 | Independent reviewers | Complete | Final repaired source snapshot | Rendering, resource lifecycle, UI/schema, package, legal, TAA/PT, and current documentation reviewed clean | Ownership released |

## Related Completed Work

- [MSAA Shadow Anti-Aliasing Control](../completed/msaa-shadow-antialiasing-control.md)
- [Ratio Shadow, MSAA, and CMAA2 Preparation](../completed/ratio-shadow-msaa-cmaa2-preparation.md)
- [Ratio Shadow MSAA Sparse-Edge Repair](../completed/ratio-shadow-msaa-sparse-edge-repair.md)
- [TAA Edge-Shake Repair](../completed/taa-edge-shake-repair.md)
- [TAA Thin-Outline Stability](../completed/taa-thin-outline-stability.md)

## Risks and Escalation Triggers

- Large overlapping `src/uvsr.cpp` and UI/source-contract diffs can merge textually while losing menu hierarchy, denoising behavior, or command ordering.
- CMAA2 deletion and new-main shader changes both affect package counts; counts must be measured after composition.
- Snapshot schemas are append-only compatibility records; a stale version/fingerprint would silently decode commands incorrectly.
- The old build and visual evidence are invalid after main composition.

Stop and ask the user if:

- Preserving both inputs requires choosing a materially different visible default, quality/performance policy, or deletion scope, or if publication/destructive authority is needed.

## Completion

- Final integrated commit: intentionally local and uncommitted on `codex/ratio-shadow-msaa-cmaa2-integration`; no publication authorized
- Verification summary: Release build, 311 shader tasks, 48-file runtime bundle, schema `0004`, focused tests, 46 of 46 CTests, legal inventory, title case, README counts, and diff checks passed
- Independent review: complete with no unresolved finding after the 1x diffuse-source and exact stochastic-threshold repairs
- Coming Soon/documentation update: no Coming Soon entry exists or is required; durable renderer, settings, TAA, Ratio/MSAA, PBR, legal, and UI references are updated
- Pushed/PR/merged, or intentionally local: intentionally local; no publication authorized
- Remaining experiments or follow-ups: user visual smoke across the documented 1x/MSAA, Ratio Estimator, Per-Sample Shadows, GI, and TAA matrix; no performance claim without a matched benchmark
- Active ownership released: complete; every worker, reviewer, and coordinator released ownership
- Archived to completed/abandoned path: `docs/exec-plans/completed/ratio-msaa-taa-main-integration.md`
