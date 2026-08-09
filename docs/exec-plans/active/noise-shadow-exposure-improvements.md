# Noise, Shadow, and Exposure Improvements

## Status

- State: second follow-up repair in progress after product rejection
- Coordinator: `/root`
- Project branch and worktree: `codex/main-lighting-denoising-controls` at
  `C:/Users/brock/OneDrive/Documents/uvsr/work/main-lighting-denoising-controls`
- Canonical base commit: `51bad6a7fccb88afd46078dcd95ef4bbdb509d7a`
- Starting NRD candidate diff identity:
  `76e30804819711edfc1c69d26e10654c33b6354c`
- Started: 2026-08-08
- Planned archive:
  `docs/exec-plans/completed/noise-shadow-exposure-improvements.md`
- Predecessor plan:
  `docs/exec-plans/completed/main-lighting-denoising-controls.md`

## Goal and Done Condition

Goal: extend the technically verified NRD production candidate with the
requested terminology and UI corrections, material-aware ray traced shadows,
a unified precomputed noise system implementing the published spatiotemporal
blue-noise objective with first-party assets, and NVIDIA-inspired sky auto
exposure while keeping the newer specular
reflections experiment out of the lineage.

Done when:

- [x] Every requested rename, default, tooltip, collapsible effect section,
      statistics split, capture label, flashlight control, and loading-time
      presentation is implemented and covered.
- [x] Every shipping ray-query visibility path respects alpha-tested geometry,
      and the related traversal audit has no unresolved P0 through P2 issue.
- [x] One Noise drawer owns the global pattern and resolution; each eligible
      effect inherits it unless Specify Noise is enabled, with centered tiling
      and the requested `64`, `128`, `256`, and `512` resolutions.
- [x] Spatiotemporal Blue uses a reproducible first-party `128x128x64` `R8`
      default asset based on the published objective, with correct slice
      progression and runtime packaging and no copied NVIDIA code or textures.
- [x] Sky auto exposure follows the applicable NVIDIA PTRTX or RTX Remix
      source contract, exposes its accompanying target slider, resets history
      correctly, and composes coherently with HDR, AgX, and manual exposure.
- [x] Standard and NRD Release builds, focused tests, full CTest matrices,
      shader and asset packaging checks, document Title Case validation,
      independent high-risk review, and the available runtime smoke pass for
      the exact replacement artifacts.
- [ ] Flashlight Angular Size produces a visibly wider, distance-dependent
      penumbra rather than only changing analytical-light metadata.
- [ ] A flashlight collision volume reuses the production scene-collision
      representation and prevents the emitter from crossing nearby walls, so
      its projected beam does not collapse as the camera approaches a surface.
- [x] Auto Exposure uses robust metering and color-safe display composition,
      exposes an Adjustment Period control, and passes bright, dark, saturated,
      and neutral-color comparisons against primary-source behavior.
- [x] The loading line has no parenthesized tick annotation, Flashlight Enable
      has no shortcut suffix, and Sun Irradiance defaults to `8` everywhere.
- [x] Auto Exposure is an independent technique submenu, defaults disabled,
      exposes separate maximum brightening and darkening controls, and leaves
      the established AgX/manual presentation path exact while disabled.
- [x] Pressing `V` begins camera roll reset while a stationary trackpad gesture
      remains held, without suppressing real subsequent camera movement.
- [x] Near-wall flashlight collision retracts the complete forward, horizontal,
      and vertical mount offset toward the camera instead of sliding the lateral
      components along the wall.

## Scope

In scope:

- First-party renderer, UI, settings, shaders, tests, runtime assets, build
  metadata, notices, and durable documentation required by the requested work.
- Alpha-tested triangle handling for every current first-party inline ray-query
  visibility consumer, including matched hit-distance behavior.
- Global and per-effect noise selection, precomputed texture creation or
  packaging, centered tiling, temporal addressing, and reset/command behavior.
- Sky auto exposure and practical advice about automatic AO and GI strength.

Non-goals:

- The dirty `codex/specular-ratio-estimator` experiment or its reflection files.
- Editing `donut/`, reviving retired features, unrelated renderer redesign,
  performance claims, publication, push, pull request, merge, or release.
- Implementing automatic AO or GI strength without a later explicit request.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `README.md`, `src/shaders.cfg`, `src/uvsr.cpp`, global
  settings, shared CPU/HLSL bindings, dependency and asset manifests, notices,
  execution plans, Git state, builds, tests, runtime, and GPU control.

## Baseline and Constraints

- Live `origin/main` remains `51bad6a`; it does not contain the NRD candidate.
- The starting candidate has standard and NRD Release builds with `39/39` tests
  recorded for each, but its deferred runtime smoke becomes stale after this
  task's first modification.
- The newer specular experiment begins at the same Git commit but contains the
  NRD candidate plus unrelated reflection work and is read-only and excluded.
- All prior dirty and untracked NRD work is user/task-owned and must be
  preserved. The user authorized a focused commit and feature-branch push after
  the final flashlight proximity repair passes; direct `main`, PR, merge, and
  release actions remain unauthorized.
- Runtime and GPU work is serialized and occurs only after a clean process and
  thermal preflight in an available user testing window.

## Assignments

| Task | Owner | Write Scope | State | Deliverable |
| --- | --- | --- | --- | --- |
| Ray alpha and fail-open implementation review | `/root/ray_alpha_audit` | Four focused test files, then read-only renderer review | Complete | Material-aware traversal, lifetime, auto-exposure fallback, timing, and tests |
| Noise implementation and timing tests | `/root/noise_core` | Focused noise consumer and Visibility timing tests | Complete | Settings, assets, centered sampling, phase, and atomic timing contracts |
| UI and exposure source tests | `/root/auto_exposure_tests` | Focused UI/catalog/renderer tests | Complete | Exact labels, commands, exposure, loading, and statistics contracts |
| Integration and verification | `/root` | Task-owned first-party files | Complete | Composed locally verified candidate and exact artifacts |
| Flashlight softness and collision audit | `/root/flashlight_followup_audit` | Read-only flashlight, ray-shadow, collision, and denoiser paths | Complete | Exact failure cause and finite-emitter/collision implementation contract |
| Exposure behavior audit | `/root/auto_exposure_tests` | Read-only exposure, AgX, UI, tests, and official primary sources | Complete | Saturation diagnosis and Adjustment Period contract |
| Follow-up integration and verification | `/root` | Task-owned first-party files and shared hotspots | Complete | Repaired source, tests, docs, builds, reviews, and exact candidate artifacts |
| Second follow-up camera and flashlight helpers | `/root/camera_flashlight_helpers` | Camera controller, collision, flashlight helpers, and focused tests | Complete | Trackpad-safe leveling and uniformly bounded mount extension |
| Second follow-up exposure and UI tests | `/root/exposure_ui_tests` | Three focused exposure, catalog, and UI test files | Complete | Directional limits, exact off path, submenu, commands, and color goldens |
| Second follow-up documentation | `/root/followup_docs` | Three renderer and UI documents | Complete | Current exposure, camera, and collision behavior |
| Second follow-up independent review | `/root/exposure_source_review`, `/root/flashlight_camera_review` | Read-only production source | Complete | Failure-path, collision-recovery, and input-order review |
| Final proximity and publication review | `/root/exposure_source_review`, `/root/flashlight_camera_review`, `/root/exposure_ui_tests` | Read-only production source and live GitHub state | Complete | Early smooth retraction, safety/cache contracts, and explicit feature-branch push target |

Workers retain disjoint file leases and release ownership through a distilled
handoff. The coordinator remains the only integrator and build/runtime operator.

## Planned Integration Order

1. Freeze the complete ray material, noise texture, and exposure contracts from
   source-backed audits.
2. Repair material-aware ray traversal and verify visibility and hit-distance
   semantics before changing sampling inputs.
3. Add the unified noise resources and data model, then route each producer to
   global or explicit per-effect settings.
4. Apply UI labels, defaults, collapsible sections, flashlight analytical-light
   controls, loading history, and separately attributed denoising timings.
5. Add sky auto exposure after the tone-mapping and luminance contract is fixed.
6. Reconcile tests, documentation, licensing, manifests, roadmap, and both
   execution-plan lifecycles once, then freeze and verify the composed result.

## Verification Plan

| Criterion | Evidence |
| --- | --- |
| Alpha-tested ray visibility | CPU/HLSL source contracts, opaque/cutout/transparent texture cases, closest accepted hit and miss tests, representative foliage runtime capture |
| Unified noise | Deterministic centered-coordinate and slice tests, texture statistics/format/dimension checks, shader bindings, reset and per-effect isolation tests, packaged-asset audit |
| Statistics and loading | Timer attribution source tests, monotonic 20 ms tick and average-history tests, exact label/UI tests, live loading presentation |
| Flashlight and collapsibility | Settings sanitization and command tests, analytical-light/shadow integration tests, drawer-order and independent-collapse UI tests |
| Auto exposure | Histogram or luminance reference tests, adaptation/clamp/reset tests, HDR/AgX composition tests, bright/dark scene runtime comparison |
| Integrated candidate | Standard and NRD Release builds, complete CTest matrices, shader/resource enumeration, document validators, diff audit, independent review, runtime smoke |

## Decisions

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-08-08 | Continue the dirty NRD candidate rather than remote `main` or the newer specular experiment. | The user's lineage description resolves exactly to `codex/main-lighting-denoising-controls`; remote `main` is its clean ancestor and the specular worktree contains excluded experimental work. |
| 2026-08-08 | Treat the dangling phrase `instead of hav` as incomplete prose with no independent requirement. | Every surrounding noise requirement is concrete; inventing an unspoken replacement behavior would expand scope. |
| 2026-08-08 | Keep automatic AO and GI strength as an evaluated recommendation, not an implementation. | The user explicitly requested implementation of auto exposure and advice on AO/GI feasibility. |
| 2026-08-08 | Generate first-party spectral noise assets rather than copy NVIDIA STBN code or textures. | NVIDIA's repository license does not establish a general commercial redistribution grant for the STBN work; the public paper and rendering guidance are sufficient to define the clean-room objective. |
| 2026-08-08 | Initially use a GPU median-luminance histogram with an 18% middle-gray target and PTRTX's **Brightness** label; the label was later clarified to **Exposure Compensation**. | This provides stable display adaptation without feeding exposure back into physical lighting or ray-effect histories, while the final label states that the control biases rather than replaces metering. |
| 2026-08-08 | Apply material-aware alpha candidates to sun, sky, and flashlight queries together. | All three previously forced opaque traversal and therefore shared the foliage-card defect and related hard-to-see transparency inconsistency. |
| 2026-08-08 | Keep optional auto exposure fail-open through a separately compiled buffer-free unity AgX path. | Exposure allocation or setup failure must not abandon an open renderer command list, and a texture-only binding contract keeps the fallback valid under NVRHI validation. |
| 2026-08-08 | Publish Screen Space Visibility stage timings as one per-slot snapshot and time multisample base-lighting preparation separately. | Independently resolving conditional queries could mix frames or retain a skipped reconstruction value; charging the preparation pass to Visibility or Direct Lighting would misattribute its cost. |
| 2026-08-09 | Trace four finite-emitter samples for every positive-radius flashlight shadow and retain the exact center ray at radius zero. | The previous implementation only shortened a single center ray, so Angular Size could not produce fractional penumbrae. Four samples provide bounded cost and quarter-step raw visibility while remaining compatible with SIGMA and TAA. |
| 2026-08-09 | Give the flashlight a swept spherical collision volume sized to the larger of the camera collision radius and analytical emitter radius. | This keeps the complete emitter outside production collision geometry, preserves a stable beam footprint near walls, and reuses the camera collision representation. |
| 2026-08-09 | Initially express Adjustment Period as an exposure-value half-life and migrate the shared AgX output toward Filament's linearization sequence; superseded after product review. | EV-domain adaptation was retained because it is symmetric, but the bundled tonemapper migration changed the disabled/default image and was therefore rejected as out of scope. |
| 2026-08-09 | Revert the global AgX migration and require the disabled/failure path to use the exact pre-Auto-Exposure texture-only permutation. | Product review showed the globally changed transfer and removed clamps made the default White World image grayer even with Auto Exposure off. A tonemapper migration needs its own separately accepted work item and cannot be bundled with optional exposure. |
| 2026-08-09 | Bound only automatic exposure correction with independent Maximum Brightening and Maximum Darkening magnitudes, then apply Exposure Compensation afterward. | This keeps intentional bias independent from safety limits and matches the separation used by NVIDIA RTXPT and Unreal exposure controls. The adjustable hard range remains RTXPT's `-16 EV` through `+16 EV`, while conservative `+5 EV` and `-2 EV` defaults follow NVIDIA RTX Remix and make the safeguards useful without setup. |
| 2026-08-09 | Compute a no-slide mount-extension limit from a camera-centered safe anchor before the ordinary continuous flashlight sweep. | The camera collision solver intentionally preserves tangential motion, which kept the flashlight right/down offset against a frontal wall. Scaling the complete mount by first contact retracts every component together without changing camera sliding. |
| 2026-08-09 | Replace contact-only flashlight correction with a cubic proximity envelope from `0.05` through at least `0.75` metres, scaled for the hitbox and mount length. | The default mount's four-centimetre forward component crossed the old transition in less than one frame at normal camera speed. Extended mount and hard-safe forward sweeps start the correction earlier while preserving the physical hard limit and final sweep. |
| 2026-08-09 | Publish only to `origin/codex/main-lighting-denoising-controls` after final verification. | Live `origin/main` still equals the feature base, but the local branch incorrectly tracks `origin/main`; an explicit feature ref avoids a direct canonical push and does not create or merge a PR. |

## Progress and Handoffs

| Date | Task | State | Evidence Or Next Action |
| --- | --- | --- | --- |
| 2026-08-08 | Preflight | Complete | Live remote, worktrees, open PRs, active plans, Coming Soon, clean original checkout, exact NRD and experimental dirty states, instructions, and starting hashes inspected |
| 2026-08-08 | UI and loading source map | Partial | Confirmed hard-coded `4.0` diffuse preset, `x/100` percent estimator with missing colon, fixed flashlight emitter radius, always-open sky visibility subsection, combined timing stages, and `Screenshot` footer label |
| 2026-08-08 | Integrated implementation | Complete | Added shared precomputed noise library/assets and three consumers, material-aware ray traversal, analytical flashlight angular size, median auto exposure, exact UI renames/defaults/disclosures, average loading ticks, and separately attributed denoising stages |
| 2026-08-08 | First NRD renderer build | Complete | Release app and all 305 first-party shader permutations compiled; runtime noise assets staged under `media/uvsr/noise` |
| 2026-08-08 | Independent ray review | Complete | Review verified material-aware traversal and geometry-index-map lifetime across TLAS-only invalidation and new-scene generation; the 305-task shader bundle passed on that frozen snapshot |
| 2026-08-08 | Post-integration repair | Complete | Review findings repaired atomic conditional Visibility timing, separate multisample preparation timing, buffer-free unity AgX packaging/layouts, bounded persistent load history, and asynchronous load exception/shutdown cleanup |
| 2026-08-09 | Independent loading-lifetime review | Complete | A UI-initialization failure race was repaired by destroying GUI and viewer ownership, joining the scene worker, and only then shutting down the device; re-review found no remaining P0 through P2 issue |
| 2026-08-09 | Final automated verification | Complete | Standard and isolated NRD Release builds passed after the lifetime repair; each full CTest matrix passed 40 of 40; 306 first-party shader permutations and all 12 noise assets packaged; 1,316 document headings and bold lead-ins passed Title Case validation |
| 2026-08-09 | User follow-up implementation | Complete | Added four-ray finite-emitter flashlight shadows, a swept emitter hitbox and teleport-safe collision cache, symmetric EV half-life Adjustment Period, corrected AgX display composition, removed both unwanted annotations, and changed Sun Irradiance to `8` |
| 2026-08-09 | Follow-up independent reviews | Complete | Frozen exposure and flashlight reviews found no remaining P0 through P2 issue after both camera-teleport paths were made to reset flashlight motion |
| 2026-08-09 | Follow-up automated verification | Complete | Standard and isolated NRD Release builds each passed all 40 tests; all 306 first-party shader permutations compiled; focused color, adaptation, collision, finite-emitter, UI, command, loading, and source contracts passed |
| 2026-08-09 | Exact-candidate runtime smoke | Complete | The final NRD executable launched as PID `34432`, exposed a targetable `UVSR Renderer D3D12 (51bad6a)` window, remained responsive, and ran at High priority; user visual acceptance of penumbra growth and wall standoff remains pending |
| 2026-08-09 | Second user follow-up review | Complete | Product review rejected the gray Auto Exposure-off appearance and incomplete flashlight centering, and requested directional exposure limits, an Auto Exposure technique submenu, and trackpad-safe `V` roll reset |
| 2026-08-09 | Second follow-up implementation | Complete | Restored the exact buffer-free AgX off path, added +5/-2 EV automatic safety defaults with independent 0..16 controls, moved Auto Exposure into its own disabled-first subsection, consumed pre-`V` pointer input, and uniformly retracted the flashlight mount through safe camera anchors |
| 2026-08-09 | Second follow-up focused verification | Complete | Exposure, UI/catalog, renderer, camera controls, collision, and flashlight focused Release tests pass; exposure re-review is clean after automatic-binding failure gained a texture-only fallback |
| 2026-08-09 | Second follow-up independent reviews | Complete | Frozen exposure and flashlight/camera reviews found no remaining P0 through P2 issue after binding-failure unity fallback, queued-pointer consumption, safe-anchor recovery, and radius-scaled collision arrival tolerance |
| 2026-08-09 | Second follow-up combined verification | Complete | Standard and NRD Release builds each compiled all 306 first-party shader tasks and passed all 40 tests; focused checks, line counts, diff hygiene, and 98 in-scope document headings also passed |
| 2026-08-09 | Replacement-candidate runtime smoke | Complete | NRD executable launched as PID `27488`, exposed a responsive `UVSR Renderer D3D12 (51bad6a)` window, and ran at High priority; product visual acceptance of the corrected off-path appearance and near-wall beam remains pending |
| 2026-08-09 | Final user review | Complete | The exposure, controls, shadows, and prior flashlight behavior were accepted except for a late snap of the mount to camera center; the user requested a smoother, earlier transition and authorized publication after repair |
| 2026-08-09 | Predictive flashlight retraction | Complete | Extended mount and forward sphere sweeps derive a cubic early proximity target while retaining the immediate hard bound, radius-growth clamp, cached idle path, anchor recovery, and final continuous sweep; focused standard and NRD tests pass |
| 2026-08-09 | Final independent review | Complete | Frozen flashlight and camera review found no P0 through P2 issue after radius-growth and idle-cache contracts were added to the focused source tests |
| 2026-08-09 | Final combined verification | Complete | Standard and NRD Release builds each compiled all 306 first-party shader tasks and passed all 40 tests; exact noise assets, loading lifetime, exposure color, collision, UI, command, packaging, line-count, and document contracts pass |
| 2026-08-09 | Final replacement runtime smoke | Complete | The exact NRD executable launched as PID `52564`, exposed a responsive `UVSR Renderer D3D12 (51bad6a)` window, and ran at High priority; visual acceptance of the smoother early wall transition remains pending before canonical integration |

## Risks and Stop Conditions

- Alpha testing may require new geometry metadata, bindless material and texture
  access, or BLAS flags shared by every ray effect. Do not implement a partial
  shadow-only workaround that leaves other rays inconsistent.
- NVIDIA source or asset licensing must permit the selected STBN and exposure
  treatment and must be preserved in notices and distributable packages.
- Precomputed `512x512x64` noise can be unnecessarily large. The final design
  must distinguish the requested 2D resolution from the temporal depth and
  avoid multiplying storage without a consumer benefit.
- Exposure must not silently double-apply manual exposure or chase transient UI,
  loading, or debug pixels.
- Stop affected writes for unexpected peer/user diffs, uncertain asset deletion,
  incompatible product outcomes, unavailable required licensing, or a runtime
  resource lease owned by another task.

## Completion

- Final integrated commit: the authorized
  `origin/codex/main-lighting-denoising-controls` feature-branch tip; no direct
  canonical push, pull request, or merge is authorized
- Exact NRD executable:
  `C:/Users/brock/OneDrive/Documents/uvsr/work/main-lighting-denoising-controls/build-nrd-final/bin/uvsr.exe`
- NRD executable SHA-256:
  `C57A500F0C93F6FC52206BD934D7469E23799E0D756A5974CF5F1EC38BA2CC1E`
- Exact standard executable:
  `C:/Users/brock/OneDrive/Documents/uvsr/work/main-lighting-denoising-controls/build/bin/uvsr.exe`
- Standard executable SHA-256:
  `7B3E118781A91AF225B4F31BF247E7D761C3995A32481AC7D098E2B9762476FF`
- Runtime smoke: the replacement NRD artifact is running responsively as PID
  `52564`; product visual acceptance remains pending.
- Publication target: `origin/codex/main-lighting-denoising-controls`
- Plan archive: pending product visual acceptance and canonical integration of
  the replacement candidate
- Predecessor reconciliation: complete
