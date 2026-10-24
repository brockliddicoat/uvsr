# visibility and shadow research

## status and authority

- coordinator: visibility research build handoff, task `01a073e5-3446-7970-bb5d-cd2763acf303`.
- candidate: `C:/Users/brock/.codex/worktrees/fbc8/uvsr`, branch `codex/visibility-hzb-research`.
- base: `bed36f951407e23d9e6b4a1a9462a96abc059e8a` plus the preserved visibility research source changes.
- the original research checkout and its binaries remain untouched. the starting patch, untracked source copies, and SHA-256 inventory are in `work/visibility-hzb-start/`. all 41 files with content changes match the original bytes; the original status also reports a README modification with no content diff.
- the task checkout originally pointed to clean detached `cf3e55c0c331e6d33e5771410762e4492f7c4890`. live remote main was `482e5ff46aa1abc97bf17c3696ea31a952024bc8` when inspected. this is a continuation of the named research lineage, not a canonical integration.
- local implementation and required verification are authorized by the user's feature request. no commit, push, PR, merge, or release is authorized. complete selected code changes before launching.
- current user-supplied instructions and the startup root policy `2026-07-30.1` take precedence over older instructions carried by this historical base (`2026-07-22.1`). UI reference: `2026-07-31.5`.

## outcome

- [x] add explicit aggressive distance mip sampling and directional off-screen fallback controls, following the applicable SSRT3 code and retaining its license.
- [x] put the Bend ray-coherent implementation behind a default-off wave toggle. the off path must not invoke its CPU planner or GPU tracer.
- [x] make shadow softness follow directional light angular size using binary visibility samples. remove the old hard/fade sample, contrast, edge, precision, and debug tuning from user controls and shader variants.
- [x] build conservative hierarchical depth bounds and consume them in visibility and shadow traversal. preserve valid depth/radiance association and distinguish aggressive mip approximation from conservative rejection.
- [ ] compare hierarchy construction and traversal with independent dense reference cases, including odd dimensions, empty depth, thin occluders, reverse depth, near-plane crossings, and edge exits.
- [ ] pass required CPU, shader, package, and runtime checks on the exact candidate. record unproven performance/visual claims honestly.
- [x] audit what still requires Bend attribution when the toggle is off.

## ownership and overlap

the coordinator owns every candidate edit, both sides of CPU/HLSL bindings, build configuration, UI, tests, and documentation. the `source_review` subagent is read-only and reviews external-source provenance and the rendering design. the historical Bend/SVSM plans and stale Coming Soon entries describe other work. open PR inventory was empty. the active cutdown debugging task uses another checkout and is not an integration target; no shared source writes are planned. GPU use must be serialized with any active renderer/testing session.

## design constraints

- a prefiltered average depth is not a conservative HZB bound. use full-precision nearest/farthest bounds for skipping and refine potential hits to source pixels.
- visibility is a horizon/sector integral, not a binary reflection ray. HZB rejection must preserve its sample ordering, radius, occluder ownership, and estimator equations.
- directional fallback consumes the remaining visible angular measure and replaces the old diffuse environment contribution once. it must not inject the environment again at every bounce or multiply cosine/PI twice.
- an off switch is not a distribution license exclusion. keep copyright, license, and provenance for any retained Bend code or shaders.
- screen depth cannot recover hidden/off-screen geometry. angular sampling improves the emitter model; it does not make screen-space shadows equivalent to scene ray tracing.

## UI intake

all new controls are session-only values with get/set/toggle/reset slash support, the existing animated checkbox and reset helper, and the existing SettingsBody scroll owner. no new top-level drawer or structural dropdown is required.

| control | owner and path | default/reset | consumers and cost |
| --- | --- | --- | --- |
| Aggressive Distance Mips | Visibility sampling, `visibility.sampling.aggressive-mips` | off | sample footprint and depth/radiance mip selection, history invalidation, configuration identity |
| Directional Off-Screen Fallback | Visibility indirect diffuse, `visibility.gi.directional-fallback` | off | remaining visible directions, environment radiance lookup, first-bounce integration and composite replacement, history invalidation |
| Wave Optimizations | Lights screen-space shadows, `shadows.screen-space-directional.wave-optimizations` | off | complete optional Bend planner/tracer selection; ordinary trace path otherwise; cached shaders and fixed result layout |

shadow enablement and useful trace reach/thickness remain. angular size uses the existing directional light control. toggles retain identical row layout and reset only their owned field. resource allocation occurs in renderer execution, not UI composition. controlled-experiment locks and factory experiment restrictions apply to slash and visible changes alike.

## verification and evidence

research sources: SSRT3 (`cdrinmatane/SSRT3`, MIT), AMD FidelityFX SSSR hierarchical traversal and depth generation, McGuire and Mara's 2014 JCGT screen-space ray tracing paper, and the released Bend headers and Apache-2.0 license. exact revisions, adaptations, and comparisons will be recorded with the implementation.

required checks include the relevant mathematical reference tests, shadow and visibility feature contracts, production shader bundle and runtime package tests, `uvsr_ui_source_contract`, `uvsr_ui_animation_reference`, `uvsr_imgui_dropdown_roll_lifecycle`, `uvsr_renderer_source_contract`, Release compilation, and controlled DX12 smoke/visual exercise. no speedup is claimed without matched total-frame and pass measurements.

## current evidence and remaining work

- an earlier Release renderer build passed, `work/visibility-hzb-start/build-5.log`. candidate executable: `build-visibility-hzb/bin/uvsr.exe`, SHA-256 `37091D71D25A18325A41F0D4DFC6DE6D6072F57A041F9EBE99382EACF0585CBB`. it predates the final visibility shader corrections below and must be rebuilt. it is not technically verified or product accepted.
- all 34 CTest checks passed before those final corrections, `work/visibility-hzb-start/ctest-2.log`, including the independent CPU hierarchy/DDA reference, 723-permutation production core bundle, 81-file runtime shader bundle, UI contracts, and unchanged Bend upstream hashes. this evidence must be refreshed after rebuilding.
- the optional GPU reference target compiled, `work/visibility-hzb-start/gpu-test-build.log`. it executes the real pyramid shader and a test wrapper around the real traversal include. a subsequent source-only addition covers varying ray depth in both directions and requires rebuilding this test target.
- independent source review repaired the fixed traversal budget, shifted receiver-cell exit, projected shadow reach, wave resource rebinding, and intermediate accumulation precision. the final review found two further corrections: restrict world-depth-radius rejection to Horizon, since the mask estimators only limit projected reach; and return the unclaimed radius-falloff share of each Horizon wedge to directional environment fallback. both are edited, with a constant-environment mass regression added. these latest changes are not built or tested during the thermal pause.
- no renderer or GPU reference test has launched. the initial monitor setup required the pinned monitoring library and Windows PowerShell rather than PowerShell Core. the successful diagnostic mapped the RTX 4090 Laptop GPU and reported a 68.375 C hot spot, above the 55 C preflight limit. GPU checks are paused until the user provides another testing window; CPU temperature is unavailable and must remain recorded as unknown.
- the denoiser repair task has priority. stop only this task's renderer and capture processes when it requests the GPU. avoid routine cross-task status messages.
- remaining runtime coverage: actual GPU min/max and dense-DDA comparisons, DX12 debug smoke, cold and warmed toggle changes, all six visibility estimators, AO/GI combinations, angle-driven penumbra behavior, and affected resize/AA bindings. matched total-frame and shared-HZB/pass measurements are also outstanding. no fastest-HZB or image-equivalent wave-toggle claim is established.
- launch with the lowercase experiment token `visibilityhzb`; retain exact artifact, scene, settings, and process identity in the evidence. product acceptance has not been requested for an unseen candidate.
- the user's current lowercase instruction overrides this historical checkout's Title Case documentation rule. new prose and headings follow the current instruction; no broad historical heading rewrite is in scope.
- all work remains local and uncommitted. the original research build and other worktrees remain untouched.
