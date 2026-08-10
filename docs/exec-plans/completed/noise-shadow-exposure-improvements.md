# Noise, Shadow, and Exposure Improvements

## Status

- State: complete, technically verified, and product accepted for canonical integration
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
- [x] The rejected receiver-driven flashlight centering experiment is removed
      end to end and documented with enough implementation history, failure
      analysis, and future instrumentation guidance to support a new approach.
- [x] Only emitter-aware overlap repair and continuous sphere collision remain
      in flashlight-to-camera movement; receiver rays, proximity retraction,
      temporal controls, movement diagnostics, and compatibility aliases are
      absent. The pure-white factory beam remains.

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
| Receiver-feedback follow-up review | `/root/exposure_source_review`, `/root/flashlight_camera_review`, `/root/exposure_ui_tests` | Read-only flashlight geometry, source, and focused tests | Complete | Exact receiver feedback, bounded visibility state, collision recovery, and regression coverage |
| Depth-discontinuity stability review | `/root/exposure_source_review`, `/root/flashlight_camera_review`, `/root/exposure_ui_tests` | Read-only temporal response, receiver visibility, sway isolation, white default, and focused tests | Complete | Frozen final reviews found no P0 through P2 issue after optical misses gained a distinct temporal retraction state |
| Pillar-transition stabilization | `/root` | Flashlight settings, receiver controller, commands, UI, shared documentation, integration, build, and runtime | In Progress | Sustained-input gating, stale-state repair, response controls, cached diagnostics, builds, and automated checks are complete; replacement runtime and product acceptance remain |
| Pillar-transition tests and review | `/root/exposure_ui_tests`, `/root/exposure_source_review`, `/root/flashlight_camera_review` | Focused tests followed by frozen read-only source review | Complete | Target-specific timing/state contracts and two independent frozen reviews pass with no P0 through P2 finding |

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
| 2026-08-09 | Supersede the rejected cubic proximity envelope with camera-center receiver feedback and the exact `d / 6` physical mount law. | The fixed lookahead still began too late and combined an abrupt target drop with fixed six-metre aim. Measuring the actual camera-ray receiver continuously starts correction at the authored convergence distance, centers the beam on that receiver, and retracts the complete mount in the same proportion. |
| 2026-08-09 | Apply a 200 ms inward and 80 ms outward half-life to receiver-driven mount changes, then derive transient aim from the final extension. | A raw depth discontinuity across a pillar could move most of the off-axis mount in one frame. The asymmetric response limits a one-frame 60 Hz near pulse to 5.61 percent while persistent geometry still converges continuously; using the same final extension for aim prevents a parallax snap. Hard collision and recovery remain immediate. |
| 2026-08-09 | Keep sway direction-only and change the factory flashlight color to pure linear white. | Receiver sampling, collision, and physical mount position must remain deterministic for a camera pose regardless of the cosmetic lens sway phase. A neutral white factory color avoids an unwanted warm tint while preserving the existing Color control. |
| 2026-08-09 | Require 100 ms of target-specific continuous soft inward evidence by default, expose 0 through 500 ms Time to Action and 0.25x through 4.00x Adjustment Speed, and keep hard collision outside that controller. | The 200 ms filter still moved on the first pillar sample, so repeated short hits accumulated as visible sticking. A separate confirmation phase rejects transient geometry completely, and materially different depths cannot inherit an already-confirmed target or pending dwell. The speed multiplier lets users tune sustained inward and outward response without weakening physical safety. |
| 2026-08-09 | Prevalidate every changed receiver and clear exact receiver aim on release before applying the temporal response. | Reusing Occluded Retraction across different columns pulled inward on clear surfaces, while open-space aim correction could remain attached to a departed pillar. These are state-lifetime defects rather than response-speed choices. |
| 2026-08-09 | Expose a default-closed, read-only Camera Movement Diagnostics group backed only by cached controller state. | Receiver, delay, target, visibility, recovery, collision, and aim state are useful for human tuning, but diagnostics must not add rays or alter renderer behavior. |
| 2026-08-09 | Reject and remove every receiver-driven or proximity-driven flashlight camera-centering approach. | Product review still observed harsh face-to-face lurching and sticking across rows of columns after proximity probes, `d / 6` receiver scaling, connected visibility, asymmetric filtering, target-specific delay, and diagnostics. The stable retained boundary is physical emitter collision only; the complete failure record is in `docs/postmortem/flashlight-camera-centering-v1.md`. |
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
| 2026-08-09 | Receiver-feedback product review | Complete | Product review rejected the fixed-lookahead artifact because its mount still lurched around one metre and its beam centered too late and too weakly |
| 2026-08-09 | Receiver-feedback implementation | Complete | Added a specialized camera-center point ray, proportional `d / 6` complete-mount retraction, exact receiver aiming, immediate inward coupling, tolerance-stepped outward validation, blocked-idle caching, damped restoration, and phased collision recovery |
| 2026-08-09 | Receiver-feedback combined verification | Complete | Standard and NRD Release builds compiled all 306 shader tasks and passed all 40 tests; focused receiver, collision, recovery, cache, UI, asset, line-count, and package contracts pass |
| 2026-08-09 | Receiver-feedback independent review | Complete | Three frozen read-only reviews found no remaining P0 through P2 issue after blocked-idle, stale recovery-state, second-leg progress, and radius-change waypoint repairs |
| 2026-08-09 | Depth-discontinuity stability implementation | Complete | Added 200 ms inward and 80 ms outward mount response, final-extension-coupled aim, temporal optical retraction around corners, explicit sway isolation, and a pure-white factory beam |
| 2026-08-09 | Depth-discontinuity stability independent review | Complete | Frozen reviews found and repaired initialization bypass, immediate optical-zero collapse, nonzero corner stalling, and equal-extension cache termination; final source and focused-test reviews report no remaining P0 through P2 issue |
| 2026-08-09 | Depth-discontinuity stability combined verification | Complete | Standard and NRD Release applications each compiled all 306 shader tasks and passed all 40 tests; focused contracts and frozen independent reviews pass, and the replacement runtime is responsive |
| 2026-08-09 | Pillar-transition product review | Complete | Product review rejected the remaining harsh, sticky response while panning across rows of columns and requested configurable adjustment speed, configurable time to action, and useful movement diagnostics |
| 2026-08-09 | Pillar-transition implementation | Complete | Added a target-specific continuous-evidence gate with a 100 ms default, a 0.25x through 4.00x speed multiplier, receiver-state prevalidation, released-aim reset, and cached raw/accepted/pending diagnostics |
| 2026-08-09 | Pillar-transition focused verification | Complete | Direct flashlight, renderer, UI, and command tests cover accepted/pending targets, one-frame 4 m to 1 m discontinuities, alternating 1 m/2 m targets, exact threshold overflow, 30/60/120 Hz partitioning, speed endpoints, hard safety, and diagnostics |
| 2026-08-09 | Pillar-transition independent review | Complete | Two frozen read-only reviews found no P0 through P2 defect after materially different depths gained independent evidence and diagnostics gained reachable hard-limit and exact aim states |
| 2026-08-09 | Pillar-transition combined verification | Complete | Standard and NRD Release builds each compiled all 306 shader tasks and passed all 40 tests; README line counts, document Title Case, package contracts, asset provenance, and diff hygiene pass |
| 2026-08-09 | Final flashlight centering product review | Complete | Product review rejected the configurable pillar-transition candidate because the beam still appeared to teleport between nearer column faces and remained sticky despite slower response and target-specific delay |
| 2026-08-09 | Flashlight camera-centering rollback | Complete | Removed receiver/proximity movement, temporal and recovery state, commands, controls, and diagnostics; retained camera-side overlap repair plus continuous emitter sweep, pure-white defaults, and finite-emitter shadows. Standard and NRD Release each passed all 40 tests, the 306-task shader bundle passed, and the repaired initialization path passed a frozen independent review. |

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

- Published feature checkpoint: `f892c17e33c007db69ca10f055bd7e59301b37d0`
  on `origin/codex/main-lighting-denoising-controls`; this completed record
  accompanies the fast-forward integration into `origin/main`
- Exact NRD executable:
  `C:/Users/brock/OneDrive/Documents/uvsr/work/main-lighting-denoising-controls/build-nrd-final/bin/uvsr.exe`
- NRD executable SHA-256:
  `A10FBB14CDCF1965AD3FBCDAEB40146EA998A3A474B4D717BF51F1BA1571A1E6`
- Exact standard executable:
  `C:/Users/brock/OneDrive/Documents/uvsr/work/main-lighting-denoising-controls/build/bin/uvsr.exe`
- Standard executable SHA-256:
  `936037A99EC041A0C2453243B411C7AA50DEBF9B61D9B2C84E2725AF777AF4C1`
- Runtime smoke: the collision-only rollback NRD artifact is responsive as PID `43184`
  with a High-priority `UVSR Renderer D3D12 (51bad6a)` window; the user accepted
  the replacement's wall safety, pure-white default, and removal of rejected
  camera-centering behavior.
- Publication target: `origin/main` by fast-forward
- Plan archive: complete in
  `docs/exec-plans/completed/noise-shadow-exposure-improvements.md`
- Predecessor reconciliation: complete
