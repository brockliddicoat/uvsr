# Engine Core Cutdown and Restoration Report — August 2, 2026

## Report Identity

This report inventories the complete August 2 engine-core cleanup and orders the
removed or changed features by how plausible and consequential a future
restoration would be.

| Field | Value |
| --- | --- |
| Cleanup date | 2026-08-02 |
| Exact base | `f7c0c87d8cba6880428fbc34400eb2882fb5182e` |
| Historical executable SHA-256 | `536D2A6092927E483573144F858F927E8F3B580C2C2FB2ACE83B9FC7CF6B733C` |
| Integrated successor commit | `b4dc24128e4f38effdeaf5a2dbc33cae107e9134` |

The base commit is the exact recovery source for every recoverable pre-base path
removed in this batch. It does not contain the transient transparent shadow
overlay, for which no immutable source checkpoint is known. Recover SVSM or
diagnostic CSM by comparing their named paths in the base commit. Exact commits
and paths are the sole recovery evidence.

## How to Read the Restoration Order

The order below weighs visible capability, the user's stated likelihood of
future work, uniqueness of the removed behavior, and the cost of reconstructing
it after surrounding contracts evolve. A high position does not mean the
removal was mistaken. It means a future user is more likely to ask for that
capability back and should find its recovery boundary quickly.

| Priority | Retired or Changed Area | Why It Ranks Here | Preferred Recovery Source |
| ---: | --- | --- | --- |
| 1 | Sparse Virtual Shadow Maps | Largest removed renderer feature; explicitly moved aside for future repair | Base `f7c0c87` |
| 2 | Diagnostic Cascaded Shadow Maps | Valuable conventional comparison path; coupled but smaller than SVSM | Base `f7c0c87` |
| 3 | Recursive Multiple-Bounce Indirect Diffuse | Unique visible lighting capability with broad PBR and scheduling dependencies | Base `f7c0c87` |
| 4 | TAA Sample Resurrection | Unique older-frame recovery behavior, a debug view, 192 production permutations, and about 24 bytes per pixel | Base `f7c0c87` |
| 5 | Visibility-Owned Temporal Accumulation | Could alter AO/GI stability, but was unreachable and overlapped renderer TAA | Base `f7c0c87` |
| 6 | Visibility Depth Hierarchy | Potential tracing research value, but dormant in every factory recipe | Base `f7c0c87` |
| 7 | Debug Presentation and View Composition | Highly visible UX change and easy to notice, though the new model is more composable | Base `f7c0c87` plus current UI |
| 8 | Independent Anti-Aliasing and TAA Surface | Major user-facing behavior change requested by design; partial reversal is unsafe | Base `f7c0c87` plus current AA contracts |
| 9 | Forward PBR and Multi-Producer Lighting | A parallel renderer path with no current UI, but potentially useful for a named future material need | Base `f7c0c87` |
| 10 | AO-Only Profiles, Planner, Benchmarks, DRED, Build Profiles, and Taxonomies | Developer support rather than current image capability; restore only for an active experiment | Base `f7c0c87` or an older dated cutdown |

Noise naming and the new Standard White Noise mode appear near the end because
they are cheap, runtime-uniform choices whose algorithms can be changed without
restoring a renderer topology.

## Front-End Restoration Addendum — August 2, 2026

### Addendum Identity and Evidence Boundary

This addendum records the same-day restoration requested after the first
engine-cleanup interface compressed information by deleting too much of its
presentation contract. The renderer reductions were retained. Preset
ownership, reset behavior, labels, tooltips, animated disclosure, Buffers,
effect-specific Statistics, and richer Debug selection were restored on top.

| Field | Value |
| --- | --- |
| Restoration date | 2026-08-02 |
| Parent cleanup SHA-256 | `536D2A6092927E483573144F858F927E8F3B580C2C2FB2ACE83B9FC7CF6B733C` |
| Intake cleanup patch identity | Content hash `631ec35c500cc7bf4f78cc69b68a1a98cb32eebc`; not a Git object |
| Replacement SHA-256 | `58B58FBD643859D12CFB9A746BD5FA563048CB1F569B56449E31FBE6A5F82458` |
| Replacement size | 2,491,904 bytes |

The original sections below describe the verified cleanup snapshot identified
in Report Identity. In those sections, *current*, *final*, and *now* refer to
that snapshot. This addendum alone describes the later front-end restoration
candidate. Build, runtime, and confidence evidence does not transfer between
the two artifacts.

The recorded intake patch identity distinguishes the reviewed input but is not
a recovery object. In particular, the exact
transient transparent shadow overlay introduced by the original cleanup is not
present in base `f7c0c87` or the later source. A future restoration requires a
fresh implementation from the contract below. Git commits and named paths are
the sole source-recovery authority.

### Further-Cut Restoration Priority

The original Priority 1–10 table remains the overall ordering for the complete
engine cleanup. This smaller table ranks the three additional renderer choices
removed while restoring the interface:

| Priority | Further-Cut Feature | Why It Ranks Here |
| ---: | --- | --- |
| 1 | Transparent Screen-Space Shadow Edge Overlay | A unique visible, composable diagnostic with no current replacement and no exact recoverable source checkpoint |
| 2 | Standalone Packed Depth Reconstruction | A real reconstruction choice whose retained packed modes are similar but not identical |
| 3 | Ambient Occlusion Power | A visible response-shaping control, but Strength retains the dominant user-facing adjustment and the extra axis cost a shader task |

#### Reversion Priority 1: Transparent Shadow Edge Overlay

The further cut removed the independently composable translucent shadow-edge
filter. The complete removed bundle comprised:

- `edgeOverlay` state and opacity;
- the optional R8 edge-output texture and its clear/resize lifetime;
- trace unordered-access binding and edge-output shader branch;
- presentation constants and overlay mode;
- the alpha-blended presentation pipeline;
- Debug controls, commands, reset behavior, and tooltips; and
- source, layout, shader, and runtime contracts for overlay composition.

Thread Lanes and Wave Groups remain, but they are opaque replacement/isolation
views. They do not reproduce an edge stencil over the selected world or
lighting presentation.

Base `f7c0c87` contains only the older monolithic
`ScreenSpaceShadowDebugView::Edge` replacement view. It is useful reference
material, not the transparent overlay that existed between the two August 2
snapshots. Do not claim an exact restoration from the base. Reimplementation
must define the edge signal, alpha policy, render order, resource lifetime,
commands, and mixed-view validation together.

#### Reversion Priority 2: Standalone Packed Depth Reconstruction

The further cut removed the depth-only packed reconstruction choice and
compacted its enum value across the C++/HLSL boundary. The removal included:

- the CPU reconstruction enum and default/profile selection;
- the matching constant-buffer numeric contract;
- UI label, command-domain value, reset path, and tooltip;
- packed-edge selection and shader interpretation;
- reference fixtures and source-contract expectations; and
- documentation that previously counted five reconstruction modes.

The retained choices are one direct-or-guide-aware mode—shown as Full
Resolution or Guide-Aware Upsampling according to sampling resolution—plus
Packed Depth-Normal, Packed Slope-Aware, and Packed Leak-Controlled. Those
methods are not aliases for depth-only packing.

Base `f7c0c87` can explain the older algorithm but also contains the retired
planner, profiles, and ABI. A safe restoration must port only the depth-only
guide semantics into the compact four-mode route and update both sides of the
numeric contract in one change.

#### Reversion Priority 3: Ambient Occlusion Power

The further cut retained the one-word Strength control and removed the second
ambient-occlusion response axis. The complete bundle removed:

- setting/default/profile ownership and command binding;
- the CPU/HLSL constant;
- the compositor `pow` branch;
- the extra composite-pipeline identity;
- `ENABLE_AO_POWER` from `src/shaders.cfg`; and
- UI, command, source-contract, and shader-package tests.

This reduced the core shader catalog by exactly one task. Restoring only a
slider or only the macro would create a dead control or an unreachable
specialization. Restore the entire bundle only if Strength cannot express a
demonstrated image need.

### Restored Front-End Functionality

#### Visibility Ownership and Presentation

The profile selector again shows Low, Medium, High, or Ultra rather than a
generic preset label. Editing a profile-owned value preserves its origin and
appends `(Custom)`, such as `High (Custom)`. The circular reset beside the
profile restores the full High recipe; per-control arrows restore the
originating recipe value. Reset placement follows the control's actual animated
child-window owner so arrows remain aligned and interactive.

Ambient Occlusion, Indirect Diffuse, Sampling, and Reconstruction are animated
collapsible groups. Retained dropdowns reserve enough label space, display
their effective values, and carry concise hover explanations. Ambient
Occlusion exposes Enabled and one-word Strength only.

At the time of this cleanup, the noise names were Permutated White Noise,
Hashed White Noise, and Void Cluster Blue Noise. Current UVSR has since removed
Hashed White Noise and retains Permutated White Noise and Void Cluster Blue
Noise. The exact current reconstruction choices are:

1. Full Resolution or Guide-Aware Upsampling, selected dynamically for the same
   direct-or-guide-aware mode;
2. Packed Depth-Normal;
3. Packed Slope-Aware; and
4. Packed Leak-Controlled.

#### Buffers and Statistics

Settings again has nine drawers in this order: General, Visibility, Buffers,
Statistics, Aliasing, Debug, Sky, Lights, and Shadows.

Buffers is compact rather than deleted. It retains Performance, Maximum
Precision, Compact Occlusion, and Compact Indirect recipes plus direct 16-bit
or 32-bit precision selectors for Ambient Occlusion and Indirect Diffuse. Those
values participate in Visibility profile custom/reset ownership.

Statistics compresses its six general values into one dash-separated line in
this order: resolution, submitted triangles, memory bandwidth, computational
throughput, frame time, and frame rate. It then shows one selected effect
instead of every timing table simultaneously. The 13 choices are Complete
Renderer, Scene Setup, Geometry, Direct Lighting, Screen-Space Visibility,
Screen-Space Shadows, Temporal Reconstructive, Conservative Morphological,
Multisample, Material Picking, Environment Background, Tone Mapping, and
Output Blit.

Visibility, shadows, temporal reconstruction, and conservative morphology
retain their internal breakdowns. Multisample reports Geometry, Direct
Lighting, and any active Closest Surface resolve using the hardware-resolved
sample count. Renderer and effect timing structures now distinguish an
active pass from a completed graphics-processor query, so a newly enabled or
dormant effect reports unavailable instead of fabricating `0.000 ms`.
Temporal reconstruction reports Blend, Output, and deferred Presentation
Sharpen separately and counts the latter dispatch.

#### Aliasing

The drawer title is Aliasing. Temporal Reconstructive, Conservative
Morphological, and Multisample Reference are independent, animated,
collapsible techniques with a simple Enable control, and all default off.

Temporal Cost stays on the main Temporal Reconstructive surface. The
default-closed animated Advanced group owns Previous-Depth Validation, whose
choices are Stationary Bypass and Four-Texel Footprint, plus the retained
Wicked-derived image policies. Inherited dropdowns show their effective value
and owner rather than the internal sentinel. History Frames displays 1–32 and
History Strength displays 0–200 percent.

The command dispatcher now rejects the sentinel-adjacent values that the UI and
effective resolver cannot represent: History Frames accepts only `-1` or
1–32, and History Strength accepts only `-1` or 0–2.

#### Debug Composition

Every effect is an animated, independently collapsible Debug group. World
offers Scene, White, White Detail, and White Lighting. Visibility offers Final
Image, Ambient Visibility, Traced Indirect, and Applied Indirect. Physically
Based Lighting offers Final Image and concise material/environment information
filters, including Reflectance Response. Screen-Space Shadows offers Final
Image, Thread Lanes, and Wave Groups.

State ownership is independent, but not every view is visually alpha
composited. World changes the material presentation. Physically Based Lighting
and Visibility are information filters; a Physically Based Lighting filter
keeps Visibility executing while suppressing ordinary Visibility composition,
and an explicit Visibility view takes precedence. Visibility filters suppress
the environment background and write neutral no-surface pixels so unrelated
sky color does not leak into the filtered image. Thread/Wave isolation
overwrites the final framebuffer. No transparent Debug overlay remains.

An active Visibility debug choice is a real resource/runtime consumer. Ambient
Visibility therefore remains available when Ambient Occlusion is enabled even
if both environment-lighting lobes are off.

#### Command Interface

The command interface reserves one input row. The empty-input hint contains the
Enter, Tab, history, and close guidance and disappears when typing starts.
Results appear in a separate popup above the row rather than permanently
consuming Settings height.

The catalog contains 122 entries: 118 values and four actions. Current paths
include `anti-aliasing.taa.previous-depth` and
`debug.visibility.view`; current noise and reconstruction domains match the
labels above. Ambient-occlusion power, Packed Depth, shadow edge overlay, and
overlay opacity are absent.

### Addendum Accounting

The original cleanup and restoration are separate snapshots. The first column
below remains historical even after the replacement candidate is rebuilt.

| Measure | Verified Cleanup Snapshot | Restoration Candidate |
| --- | ---: | ---: |
| First-party nonblank source lines | 62,503 | 64,030 |
| `src/uvsr.cpp` physical lines | 15,882 | 17,029 |
| Core shader tasks | 269 | 268 |
| Screen-Space Directional Shadow tasks | 46 | 46 |
| First-party shader tasks | 315 | 314 |
| Integrated tasks including 76 Donut tasks | 391 | 390 |
| Command catalog entries | 125 | 122 |
| Runtime shader-package files | 39 | 39 |

The restoration intentionally spends 1,527 first-party nonblank source lines
over the verified cleanup snapshot on recovered information architecture,
runtime timing correctness, and regression tests. The complete batch still
moves from 145,256 to 64,030 first-party nonblank source lines: 81,226 fewer
lines, or a 55.92 percent reduction. `src/uvsr.cpp` moves from 33,577 to 17,029
physical lines: 16,548 fewer lines, or a 49.28 percent reduction. Build outputs
and documentation remain excluded from the line-count definition.

The final first-party production shader catalog moves from 823 to 314 tasks:
509 fewer tasks, or a 61.85 percent reduction. The former developer catalog
moves from 3,033 to the same 314-task catalog: 2,719 fewer tasks, or an 89.65
percent reduction. Including the fixed 76 Donut tasks, the integrated catalog
moves from 899 to 390 tasks: 509 fewer tasks, or a 56.62 percent reduction.

Against base `f7c0c87`, the recorded implementation diff touches 127 paths with
9,213 inserted and 100,971 deleted physical lines. Documentation outside that
implementation scope is not part of the physical-diff count.

### Addendum Changed-Path Ledger

#### Restored UI and Information Surfaces

| Area | Paths | Material Change |
| --- | --- | --- |
| Main Settings and commands | `src/uvsr.cpp`, `src/ui_settings_command_catalog.h` | Restored profile/custom/reset behavior, labels, tooltips, Buffers, one-effect Statistics, animated Aliasing/Debug/Visibility, command row, and current command domains |
| Visibility runtime | `src/screen_space_visibility.h`, `src/screen_space_visibility.cpp`, `src/screen_space_visibility_cb.h` | Added four-view debug ownership, debug-consumer gating, active/query timing validity, and compact reconstruction/noise contracts |
| Visibility shaders | `src/screen_space_visibility_cs.hlsl`, `src/screen_space_visibility_filter_cs.hlsl`, `src/screen_space_indirect_composite_cs.hlsl` | Added Permutated White Noise naming/implementation, current debug outputs, filter precedence, and neutral no-surface filtering |
| Deferred lighting | `src/pbr_deferred_lighting_cb.h`, `src/pbr_deferred_lighting_pass.h`, `src/pbr_deferred_lighting_pass.cpp`, `src/pbr_deferred_lighting_cs.hlsl`, `src/pbr_deferred_lighting_msaa_cs.hlsl` | Preserved production lighting while composing World, Visibility, and lighting information selectors |
| Effect timing | `src/cmaa2.h`, `src/cmaa2.cpp`, `src/temporal_aa.h`, `src/temporal_aa.cpp` | Restored conservative-morphology breakdown and query-valid temporal breakdown with deferred-sharpen accounting |
| Focused tests | `tests/ui_source_contract_tests.cpp`, `tests/ui_settings_command_catalog_tests.cpp`, `tests/pbr_lighting_source_contract_tests.cpp`, plus visibility/shadow/package tests | Locked restored information surfaces, command parity, composability, query validity, and further removals |
| Living documentation | `README.md`, `AGENTS.md`, `docs/advanced-settings.md`, `docs/screen-space-visibility.md`, `docs/pbr-foundation.md`, `docs/temporal-aa-options.md`, `docs/ui-integration-agent-procedure.md` | Reconciled the nine-drawer product contract and retained renderer |

#### Further Removals

| Feature | CPU and UI | Shader and Resource | Contract Cleanup |
| --- | --- | --- | --- |
| Transparent Edge Overlay | Shadow settings, Debug UI, commands, opacity | Edge output, extra binding, presentation constants, alpha pipeline | Shadow, catalog, UI, shader, and package tests |
| Packed Depth | Reconstruction enum, selector, commands, profile matching | Packed-guide numeric branch and ABI value | Visibility reference/source tests and docs |
| Ambient Occlusion Power | Setting, preset, command, composite identity | Constant, `pow` branch, `ENABLE_AO_POWER` task | Catalog, source, package, and UI tests |

This ledger is additive. It does not rewrite the original 124-file cleanup
ledger below, whose paths and reasons remain evidence for the parent artifact.

### Addendum Verification

| Check | Restoration Candidate Result |
| --- | --- |
| Release executable build | Passed |
| Core shader compilation | Passed, 268 of 268 tasks |
| Screen-Space Directional Shadow compilation | Passed, 46 of 46 tasks |
| Runtime shader package | Passed the exact 39-file contract |
| Full CTest | Passed, 30 of 30 tests |
| Permutated noise runtime | Passed; selected from the labeled Noise Pattern control and rendered successfully |
| Profile/custom/reset, labels, tooltips, and animation | Passed; observed `High`, `High (Custom)`, both reset levels, complete labels, hover explanations, and animated collapse |
| Buffers and one-effect Statistics | Passed; compact precision controls and Visibility timing breakdown observed |
| Aliasing and four-choice Visibility Debug | Passed; independent technique groups, Temporal Advanced, all four Visibility choices, and Ambient Visibility output observed |
| One-row command interface | Passed; empty guidance occupied one row and disappeared after typing `help` |
| Further-removal searches | Passed; no operational Ambient Occlusion Power, Packed Depth, Edge Overlay, sample-resurrection, retired shadow, or multiple-bounce route remains |
| Independent review | Passed; no unresolved priority-zero through priority-two source findings |
| Document validation | Passed title-case checker and final diff hygiene checks |

The live smoke used the exact replacement executable above at 1920 by 1080 on
the NVIDIA GeForce RTX 4090 Laptop GPU, with Sponza Decorated at Benchmark
Position 1. It exercised the UI and representative rendering paths; it was not
a controlled performance benchmark. The candidate's factory settings were
restored afterward, the replacement process was closed, and the older cleanup
comparison process was left untouched.

The historical 269-task build, Standard White Noise smoke, Edge Overlay smoke,
30-test result, executable hash, and 94.85 percent confidence later in this
report belong only to the original cleanup artifact.

### Addendum Confidence

| Remaining Area | Weight | Confidence | Evidence |
| --- | ---: | ---: | --- |
| Core build, package, and launch | 20% | 98% | Complete Release build, exact shader-package contract, exact-artifact launch |
| Visibility and reconstruction | 20% | 96% | Reference/source tests, shader compilation, live profile/noise/debug exercise, independent review |
| Physically based lighting and scenes | 20% | 95% | Full tests, Sponza launch, composability contracts, independent review |
| Aliasing | 15% | 95% | Temporal and morphology tests, live independent-technique and Advanced exercise |
| Shadows and Debug | 15% | 95% | All 46 shadow tasks, focused tests, four-view Visibility runtime, collapsible effect groups |
| Interface and command catalog | 10% | 97% | 122-entry parity tests plus live labels, tooltips, reset, animation, Statistics, Buffers, and command-row checks |

The evidence-weighted average confidence in the remaining features is 96.0
percent. The original cleanup artifact's 94.85 percent value is not carried
forward. Residual uncertainty is concentrated in exhaustive visual quality
across every scene and camera, long-duration resource behavior, other graphics
adapters, and controlled performance comparisons; none of those were claimed
by this front-end restoration pass.

### Remaining Safe Cutdown Opportunities

No item below was implemented in the restoration pass. They are ordered by
expected safety and value, and each requires a fresh ownership audit before a
future deletion:

| Rank | Candidate | Confidence | Why It Is Plausibly Redundant | Required Guardrail |
| ---: | --- | ---: | --- | --- |
| 1 | Remove `UiSettingsNavigationExemptions` and `UiSettingsTelemetryExemptions` | High | The two arrays are consumed only by the catalog's own structural tests, not by runtime navigation or telemetry | Replace self-referential exemptions with direct assertions for the few intentional actions, then prove no runtime reference exists |
| 2 | Remove the `UiSettingsCommandBindings` mirror | High | `src/uvsr.cpp` rebuilds a second constexpr binding array used only to enumerate command-completion candidates | Iterate the authoritative catalog directly while preserving completion ordering, action filtering, and catalog-parity tests |
| 3 | Reconcile launcher experiment-label plumbing | Medium | `tools/launch_uvsr.ps1` still requires `-Experiment` and sets `UVSR_EXPERIMENT` after the renderer-side title path was retired, but `AGENTS.md` and `LaunchUVSR.cmd` still require and supply that identity | First decide whether labeled task windows remain a process contract; then update the guide, wrapper, parameter forwarding, and launch smoke tests together rather than deleting only the parameter |
| 4 | Collapse duplicate performance-line formatting | Medium | The compact six-field builder and the OG skin's intentional two-line Settings builder derive the same status data separately | Preserve exact OG two-row, Amp one-row, and Statistics one-row output and ordering while sharing only common tokenization |
| 5 | Remove the standalone Screen-Space Directional Shadow build option | Medium | The application always requires this component, so `UVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS=OFF` is useful only when the application is also disabled | Audit external consumers, decide whether application-off component and componentless configure/test matrices remain supported, and preserve licensing/package checks |
| 6 | Fold the standalone shadow library into the application target | Medium | The specialist library has one production application consumer in this repository | Preserve its focused unit-test seam, upstream hash validation, and shader-package ownership; do not combine this with algorithm changes |

The first two are the best next targets because they remove self-test or
completion-only duplication without changing a rendered pixel. Launcher work
must wait for an explicit experiment-identity decision. The last three are
structural consolidations and should wait until OG-skin presentation, internal
build matrices, and any out-of-tree component use are explicitly checked.

### Addendum Do-Not-Restore-Alone Matrix

| Feature | Coupled Restoration Bundle | Invalid Partial Restoration |
| --- | --- | --- |
| Transparent Edge Overlay | Edge definition, R8 resource, trace binding/output, alpha presentation, render ordering, opacity, UI/commands, tests | Reusing the old opaque Edge enum or adding only an opacity slider |
| Packed Depth | CPU enum, HLSL numeric ABI, packed-guide production/consumption, profile/UI/command values, fixtures | Renumbering only one side or copying the old planner profile |
| Ambient Occlusion Power | Setting/default/profile, constant, compositor branch and pipeline key, manifest task, UI/command/tests | Slider without shader consumption or macro without a reachable control |

## Original Cleanup Result

### Source and File Reduction

| Measure | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| First-party nonblank source lines | 145,256 | 62,503 | 82,753 (56.97%) |
| `src/uvsr.cpp` physical lines | 33,577 | 15,882 | 17,695 (52.70%) |
| First-party production shader tasks | 823 | 315 | 508 (61.73%) |
| First-party developer shader tasks | 3,033 | 315 | 2,718 (89.61%) |
| Integrated production tasks, including Donut | 899 | 391 | 508 (56.51%) |

The recorded cleanup implementation diff covers 124 files, with 8,317 inserted
and 101,702 deleted physical lines. Dated report documents are not included in
those Git numstat totals.

No performance improvement is claimed from the source or permutation reduction
alone. The durable results are smaller build/package surfaces and fewer
independent behavioral contracts.

### Shader Task Accounting

A task is one expansion of one non-comment shader-manifest line. It predicts
compile and package work, not runtime instructions or driver-internal PSOs.

| Area | Before Production | Before Developer | After | Production Removed | Developer Removed |
| --- | ---: | ---: | ---: | ---: | ---: |
| TAA | 485 | 2,695 | 197 | 288 | 2,498 |
| CMAA2 | 32 | 32 | 16 | 16 | 16 |
| Visibility | 67 | 67 | 27 | 40 | 40 |
| PBR and MSAA | 27 | 27 | 22 | 5 | 5 |
| Other core | 7 | 7 | 7 | 0 | 0 |
| Core total | 618 | 2,828 | 269 | 349 | 2,559 |
| Sparse Virtual Shadow Maps | 105 | 105 | 0 | 105 | 105 |
| Diagnostic Cascaded Shadow Maps | 54 | 54 | 0 | 54 | 54 |
| Screen-Space Directional Shadows | 46 | 46 | 46 | 0 | 0 |
| First-party total | 823 | 3,033 | 315 | 508 | 2,718 |

Donut's 76 tasks were unchanged. The final first-party total is 269 core tasks
plus 46 Screen-Space Directional Shadow tasks. The selected runtime package
contains 39 shader binaries.

### Approximate Physical Diff by Area

These groupings help locate the large cuts. They include related source, tests,
and support files but exclude the central `src/uvsr.cpp` integration from each
subsystem row, so they are diagnostic rather than a second LOC definition.

| Area | Added | Deleted | Net |
| --- | ---: | ---: | ---: |
| SVSM | 0 | 33,067 | -33,067 |
| Diagnostic CSM | 0 | 13,403 | -13,403 |
| Visibility | 880 | 12,513 | -11,633 |
| TAA | 674 | 5,847 | -5,173 |
| PBR and IBL | 327 | 1,714 | -1,387 |
| Command and UI contracts | 1,214 | 8,387 | -7,173 |
| Central `src/uvsr.cpp` | 4,305 | 22,000 | -17,695 |
| GPU diagnostics and normalization | 6 | 748 | -742 |

## Restoration Priority 1: Sparse Virtual Shadow Maps

### What Was Lost

The focused engine no longer has sparse cached directional shadow maps. The
removed subsystem included:

- a six-level clipmap system and dense-reference comparison;
- virtual-page allocation and a physical-page depth pool;
- cached static and dynamic depth, invalidation, recycling, and moving-light
  policy;
- page-safe filtering, resolution bias, receiver masks, and twelve diagnostic
  views;
- quality, performance, and custom presets with roughly sixty command controls;
  and
- camera-motion and sun-motion benchmark lanes.

Screen-Space Directional Shadows remain, but they are not a map-based
replacement for off-screen occluders or persistent world coverage.

### Removed Implementation Bundle

The production bundle comprised `src/sparse_virtual_shadow_map.cpp` and `.h`,
its settings and CPU/HLSL constant buffers, receiver-LOD helper, dense and
sparse depth shaders, allocation/mark/build/clear/recycle/schedule/stat kernels,
resolve and debug shaders, the dedicated 105-task manifest,
`src/svsm_motion_benchmark.h`, an 8,022-line reference test, CMake
component targets, shader staging, packaging, UI, commands, lifecycle,
telemetry, benchmarks, and output records.

The removed GPU state included the page table, physical depth, dense depth,
visibility and debug textures, physical-owner and render-page buffers, dirty
rectangles, invalidation pages, packet metadata/runtime, scheduled tile masks,
receiver masks, static depth hierarchy, candidate masks, counters, indirect
draw/dispatch arguments, bindings, and framebuffers.

### Why It Was Cut

The user explicitly requested that SVSM leave the focused engine and continue
as a separate experiment. Its large settings, benchmark, shader, and renderer
integration surface made every unrelated cleanup harder to reason about.

### Safe Restoration Bundle

Recover the named paths from exact commit
`f7c0c87d8cba6880428fbc34400eb2882fb5182e`. Restore the pass, settings,
CPU/HLSL ABI, shader manifest, CMake component,
resource lifecycle, UI, commands, and tests as one versioned subsystem. Do not
copy the old 36,000-line-era `uvsr.cpp` wholesale.

Prefer one selected shadow producer handing one
`DirectionalLightVisibility` texture to current PBR. The old three-slot PBR
consumer does not need to return unless simultaneous producer composition is a
named requirement. Restore the benchmark harness only after the renderer path
is correct and inspectable.

### Required Reacceptance

- Full 105-task shader compilation and exact package contract.
- Reference tests for allocation, invalidation, caching, hierarchy, resolve, and
  moving-light behavior.
- Long camera and sun-motion runtime checks with cache diagnostics disabled for
  accepted performance measurements.
- Matched whole-frame quality and performance against Screen-Space Directional
  Shadows or a neutral no-shadow control.
- Product acceptance for leakage, popping, resolution transitions, off-screen
  occluders, and moving geometry.

## Restoration Priority 2: Diagnostic Cascaded Shadow Maps

### What Was Lost

The engine no longer exposes a conventional directional CSM comparison path.
The removed implementation supported single-map, low-cost, UE5-reference,
cached-single, optimized-cached-single, and optimized-cached-CSM profiles;
one through four cascades; per-cascade resolution; split exponent; transition
and fade; depth, slope, and receiver bias; 16-bit depth; UE5-style filtering;
cached draw lists; whole-map and cascade reuse; dirty rectangles; scrolling;
receiver scissor; and cascade/cache diagnostics.

### Removed Implementation Bundle

The bundle comprised `src/diagnostic_cascaded_shadow_map.cpp` and `.h`, settings,
constant buffers, clear/depth/scroll/resolve shaders, a 54-task manifest,
`src/diagnostic_csm_benchmark.h`, a 3,582-line reference test,
CMake component and staging rules, package entries, UI and command controls, and
benchmark CLI flags.

Its primary GPU resources were array depth, scroll scratch, visibility/debug
output, resolve and scroll constants, a sampler, bindings, and timer state.

### Safe Restoration Bundle

Recover the named CSM paths from exact base
`f7c0c87d8cba6880428fbc34400eb2882fb5182e`. Port CSM independently from SVSM
unless a shared comparison UI is explicitly required. Integrate through the
current singular directional-visibility contract.

Do not restore only the old settings header: its defaults selected resource and
shader behavior. Restore settings, shaders, ABI, renderer scheduling, UI,
commands, packaging, tests, and the intended profile set together. Benchmark
support can remain external until the path is correct.

### Required Reacceptance

- Full 54-task shader and package verification.
- Cascade split, cache reuse, scrolling, bias, filtering, and invalidation
  reference tests.
- Runtime inspection across camera and sun motion, thin geometry, alpha-tested
  casters, cascade boundaries, and distant receivers.
- A defined relationship to SVSM and Screen-Space Directional Shadows: one
  producer, explicit arbitration, or an intentionally tested composition.

## Restoration Priority 3: Recursive Multiple-Bounce Indirect Diffuse

### What Was Lost

Visibility now produces current-frame, one-bounce indirect diffuse only. The
cleanup removed finite bounce counts, contribution-terminated mode, minimum
contribution, cutoff/growth rules, later-bounce timings, bounce-frontier
progression, and recursive reinjection of previous indirect radiance. Authored
emission remains visible material radiance but does not recursively seed the
screen-space diffuse transport.

### Removed Implementation Bundle

- `src/screen_space_visibility_bounce_control_cs.hlsl`.
- Bounce-reinjection, cumulative-initialization, metadata, and continuation
  branches in `screen_space_visibility_cs.hlsl`.
- Frontier ping-pong and cumulative-indirect textures.
- A wave-coalesced continuation flag and indirect-dispatch argument buffers.
- Per-bounce metadata, activity tracking, later-bounce timer queries, and
  statistics.
- The PBR `WRITE_BOUNCE_METADATA` axis, its third deferred PBR pipeline, and
  source-radiance alpha metadata.
- HLSL metadata bits for surface eligibility and the `limitBounces`,
  `bounceCount`, and contribution-cutoff settings, UI, commands, and tests.
- Planner and profile identities that described later-bounce execution.

The removal contributed to Visibility's 67-to-27 task reduction and PBR/MSAA's
27-to-22 reduction.

### Safe Restoration Bundle

Restore the PBR metadata producer, third deferred pipeline, shared CPU/HLSL
layout, frontier/cumulative textures, continuation and indirect-argument
buffers, trace reinjection, dispatch-control shader, settings, UI, commands,
timing, manifests, and reference tests together.

Do not restore a bounce-count control without restoring metadata and indirect
execution. Re-establish an energy/exposure contract, determine whether emission
is a source, and prove convergence and complete-frame cost. Renderer TAA must be
tested because recursive screen-space history-like behavior can amplify lag or
noise even without a private temporal pass.

## Restoration Priority 4: TAA Sample Resurrection

### What Was Lost

TAA can no longer consult one or two older frames after immediate history is
rejected, and the Sample Resurrection diagnostic view is gone. The feature was
off in normal presets but remained selectable through the advanced and command
surfaces.

### Removed Cost and Implementation

- Exactly 192 production shader tasks attributable to the resurrection modes.
- Two persistent RGBA16F color surfaces and two R32 depth surfaces, about 24
  bytes per output pixel before allocator overhead.
- Two saved planar views, a validity mask, snapshot copies, exact older-frame
  age bookkeeping, and camera-cut/reset handling.
- Persistent SRVs, expanded blend constants, reprojection and depth-validation
  HLSL, and related binding slots.
- Resurrection enums, preset/override resolution, labels, UI, commands,
  reference fixtures, and build-time availability macros.

The wider TAA cleanup reduced production tasks from 485 to 197 and developer
tasks from 2,695 to 197. Resurrection alone accounts for 192 of the production
reduction; the remainder comes from consolidating execution and diagnostic
axes.

### Safe Restoration Bundle

Restore settings, persistent textures, captured views, invalidation and copy
lifecycle, blend constants, bindings, HLSL rejection/resurrection logic,
manifest entries, diagnostics, and tests as one feature. Do not restore only
the macro or only the history textures.

A narrow successor is preferable to reviving the old TAA experiment matrix.
Reacceptance needs exact memory accounting, camera-cut correctness, motion and
disocclusion image comparisons, output-resolution tests, and evidence that the
older samples improve the current TAA rather than merely increasing persistence.

## Restoration Priority 5: Visibility-Owned Temporal Accumulation

### What Was Lost

AO and indirect diffuse no longer run a private temporal accumulation stage.
Renderer TAA is the sole long-term temporal reconstruction system. This path was
unreachable from current factory/UI behavior and duplicated the temporal owner.

### Removed Implementation Bundle

- `src/screen_space_visibility_temporal_cs.hlsl` and its AO, GI, and combined
  task variants.
- AO and indirect-diffuse ping-pong history.
- Previous depth and normal history.
- Motion-vector input and the Visibility `RequiresMotionVectors` dependency.
- Temporal response settings, history-valid state, configuration keys,
  estimator identity, parity, clears, resize/recreation, bindings, timer stage,
  and memory counters.

### Safe Restoration Bundle

Restore motion input, all four history families, constants, shader, bindings,
resource lifecycle, UI/defaults, history resets, tests, and statistics together.
Define how it composes with renderer TAA before implementation. Silently stacking
two accumulation systems risks ghosting, excess lag, and confusing reset
ownership.

Do not restore only `temporalEnabled`; it would advertise a setting without its
memory, synchronization, and history contract.

## Restoration Priority 6: Visibility Depth Hierarchy

### What Was Lost

No factory recipe used the depth hierarchy, so the cleanup removed no active
factory image. The lost capability is a potential future long-range or
hierarchical screen-space tracing experiment.

### Removed Implementation Bundle

`src/screen_space_depth_hierarchy_cs.hlsl`, the depth-pyramid texture and
precision option, generation pass, bindings, timer stage, memory/statistics,
planner flags, shader/package entry, and tests were removed.

### Safe Restoration Bundle

Restore it only with a trace shader that demonstrably consumes the hierarchy and
with a named UI/default contract. Generation, texture lifetime, bindings,
traversal, timing, packaging, and tests must land together. A standalone pyramid
shader that no current path reads is not a functional restoration.

## Restoration Priority 7: Debug Presentation and View Composition

### User-Visible Change

Debug controls moved from feature drawers and the old World Materials section
into one effect-grouped Debug drawer. The old model encoded world appearance,
Visibility isolation, and some two-color diagnostics as mutually exclusive
full-screen modes. The new model separates:

- **World Appearance**, which changes material presentation;
- **Visibility**, which can isolate indirect diffuse;
- **PBR Lighting**, which filters the information shown; and
- **Screen-Space Shadows**, which has an isolation view and an independent
  translucent edge overlay.

World appearance, an information filter, Visibility isolation, and the edge
overlay can now coexist. This composability is the main contract to preserve if
individual names or colors change later.

### View Name Mapping

| Earlier Label | Current Label or Control | Change Type |
| --- | --- | --- |
| White World Off | Scene Materials | Rename |
| White World On | White Materials | Rename |
| White World Preserve Normals | White + Surface Detail | Rename |
| White World Preserve Emissives | White + Original Lighting | Rename |
| Indirect Diffuse Response | Indirect Diffuse Only | Moved to an independent Visibility checkbox |
| Off | Final Lighting | PBR filter rename |
| Shading Normal | Surface Normals | PBR filter rename |
| Geometric Normal | Geometry Normals | PBR filter rename |
| Normal Difference | Normal Difference | Retained |
| Diffuse Environment | Diffuse Environment | Retained |
| Cardinal Environment Test | Environment Direction | PBR filter rename |
| Prefiltered Specular | Reflected Environment | PBR filter rename |
| Environment BRDF | BRDF Response | PBR filter rename |
| Final Specular IBL | Specular Environment | PBR filter rename |
| Combined IBL | All Environment Light | PBR filter rename |
| Specular Occlusion | Specular Visibility | PBR filter rename |
| Environment Mip | Environment Level | PBR filter rename |
| Screen-Space Shadow Off | Final Image | Isolation-view rename |
| Screen-Space Shadow Thread | Thread Lanes | Isolation-view rename |
| Screen-Space Shadow Wave | Wave Groups | Isolation-view rename |
| Screen-Space Shadow Edge | Edge Overlay | Converted from replacement view to transparent overlay |

### Removed and Modified Implementation

The cleanup deleted `src/world_material_view.h` and its standalone test because
the old five-way selector could not represent composable state. PBR view labels
and command paths moved to the Debug section. TAA-specific debug views left with
the retired TAA diagnostic outputs, and SVSM/CSM views left with their
subsystems.

Screen-Space Directional Shadows changed rather than shrinking to a rename. It
now owns separate isolation and edge-overlay state, two optional R8 debug
outputs, separate trace bindings, a 16-byte presentation constant buffer, and
replacement versus alpha-blended presentation pipelines. The edge overlay uses
adjustable opacity and can appear over the final image or another selected
presentation.

### Safe Reversion Boundary

Labels, ordering, overlay color, and opacity defaults can be revised without
restoring the old state model. Restoring `WorldMaterialView` or the old
`ScreenSpaceShadowDebugView` enum would require removing or adapting current
composable state, commands, render ordering, resources, and tests; the models
are not drop-in compatible.

Before changing this area, exercise every world appearance with every PBR
filter, Indirect Diffuse Only, shadow isolation, and Edge Overlay. A two-color
diagnostic must not erase the underlying presentation unless it is explicitly
classified as an isolation view.

## Restoration Priority 8: Independent Anti-Aliasing and TAA Surface

### Earlier Contract

Anti-aliasing used one enabled-by-default method selector. Its default selected
a Temporal plus Subpixel Morphological bundle, and Temporal, CMAA2, and MSAA
were mutually exclusive implementation identities. Some topology decisions were
encoded in the selected preset rather than explicit technique ownership.

### Current Contract

TAA, CMAA2, and MSAA are separate, default-off settings. Any combination is
supported in this fixed order:

1. deferred MSAA lighting and resolve;
2. scene-linear TAA;
3. tone mapping; and
4. display-linear CMAA2.

When MSAA and TAA are combined, the MSAA closest-surface resolve supplies
single-sample color, depth, and motion to TAA. That resource path was added
during smoke repair after D3D12 rejected a multisampled UAV topology.

Stationary Bypass moved into the main TAA section. Retained Wicked-derived
motion, reconstruction, history filter, rectification, storage, weighting,
motion trust, blend-domain, frame-horizon, strength, and sharpening choices now
live in a default-closed Advanced section.

### Removed TAA Experiment Axes

The general developer matrix no longer exposes:

- fullscreen-pixel blending;
- 8×8-two-pixel versus 16×8-one-pixel compute kernels;
- legacy, split, and split-and-packed LDS layouts;
- explicit shared-work reuse and early-history rejection;
- two-, three-, and four-band cache blocking;
- separate/fused pass selection as a direct override;
- selective morphology exports and their private textures;
- dedicated developer debug storage;
- Final History Weight and Sample Resurrection resolve views;
- developer-debug permutations and `UVSR_AA_DEVELOPER_OVERRIDES`; or
- the embedded AA motion benchmark and result export surface.

The current TAA manifest keeps the four image-algorithm axes—motion source,
current reconstruction, history filter, and rectification—plus two compact
static performance decisions: optimized compute and fused output. Quality and
Temporal Cost resolve those decisions instead of exposing the earlier cross
product.

### High-Dynamic-Range CMAA2 Removal

CMAA2 is fixed to display-linear post-tonemap input. Removing the HDR-range
axis reduced CMAA2 from 32 to 16 tasks. Restoring it is meaningful only if
CMAA2 intentionally moves to scene-linear HDR or a defined HDR output mode. The
macro, input format/range, pass order, UI, package entries, and image tests must
agree; recompiling an HDR macro while still feeding display-linear color adds no
product behavior.

### Safe Reversion Boundary

Do not partially restore the old anti-aliasing settings struct or exclusive
method enum. A topology change must update UI, commands, render-target
allocation, MSAA resolve, TAA history keys and resets, output ordering,
manifests, and tests together.

If an old TAA experiment becomes useful, restore one measured axis at a time.
A debug view may return independently only with its output texture, shader
binding, resolve path, UI, and package task. A pixel path needs its graphics
pipeline and framebuffer contract; an LDS/kernel experiment needs the matching
compute layout and benchmark evidence.

## Restoration Priority 9: Forward PBR and Multi-Producer Lighting

### What Was Lost

UVSR is now one opaque-focused deferred PBR renderer. The cleanup removed the
dormant `Forward` and `ForwardTonemapperless` modes, the hidden PBR-versus-legacy
comparison flag, `PbrForwardShadingPass`, `src/pbr_forward_ps.hlsl`, forward
framebuffer/target scheduling, transmissive-material and White World forward
tasks, and unused Donut generic forward/deferred/G-buffer runtime package
entries.

The former **Enable PBR** comparison control was compiled but hidden. Despite
that, Visibility enable, preset, and reset callbacks assigned the same PBR flag,
so toggling Visibility could alter PBR and clear White World. The cleanup made
deferred PBR an invariant and removed that accidental coupling. Visibility now
changes only its pass and resources.

### Directional Visibility and Lighting Simplification

- Three `DirectionalLightVisibilitySet` producer slots became one
  `DirectionalLightVisibility` texture.
- PBR's UVSR extension shrank from two registers to one.
- Directional-visibility SRVs `t20`, `t21`, and `t22` became singular `t20`.
- The third deferred PBR pipeline used only for recursive-bounce metadata was
  removed.
- Dormant indirect-specular and legacy shadow-channel inputs were removed.
- Broad CPU/HLSL lighting-source and rejection bitmasks were replaced by exact
  positive-finite-signal and surface-facing helpers.
- Dead IBL cached-accessor state and unused direct texture getters were removed.

Local-light shadow evaluation remains on the retained PBR path. Screen-Space
Directional Shadows produce the singular primary directional-light visibility
input; a neutral texture means fully visible.

### Safe Restoration Bundle

Restore forward rendering only for a named capability such as a transparent or
transmissive material path that cannot use the deferred contract, or for an
explicit comparison product mode. Restore selection, targets/framebuffers,
draw/material bindings, shader tasks, AA and tone-map ordering, debug
interpretation, package entries, and tests together. The old hidden Boolean is
not a usable feature by itself.

When SVSM or CSM returns, prefer arbitration among producers before the singular
PBR input. Restore three PBR slots only if simultaneous producer composition is
a deliberate requirement.

## Restoration Priority 10: Developer Planning and Support Surfaces

### Visibility AO-Only Profiles and Planner

The cleanup removed named Reference, Runtime, Exact Fused Resolve/Apply,
packed-edge, slope, leakage, and fused-packed planner profiles. It also removed
the application-category path that could silently disable GI to satisfy an
AO-only fused kernel.

Packed reconstruction itself remains. Current Visibility still supports
Standard, Packed Depth, Packed Depth + Normal, Slope-Adjusted, and Controlled
Leakage reconstruction. Packed metadata can be emitted by the trace and consumed
by the consolidated filter.

Deleted implementation included
`src/screen_space_visibility_composed_edges_cs.hlsl`, the separate packed-edge
filter wrapper, the 458-line fused-apply shader, planner/profile/application
taxonomies, half-roundtrip/AO-only requirements, validation branches, and
benchmark cases.

Do not restore the profile layer merely to regain packed reconstruction. Restore
a fused AO-only route only after it beats the current default AO-plus-GI route
at equal quality in a complete frame. It would need an explicit AO-only product
mode, resource/application topology, shader and package tasks, direct selection
or a smaller planner, UI, and tests.

### Benchmark and Evidence Infrastructure

The renderer no longer embeds:

- Visibility warmup, measurement, cancellation, export, and auto-close;
- the AA motion benchmark;
- SVSM camera and sun-motion benchmarks;
- diagnostic CSM benchmark and output recording;
- benchmark camera lock, fixed 1920×1080 staging, progress overlays, timer tags,
  query draining, key interception, or `UVSR_PERF` capture variants;
- Visibility planner, benchmark-statistics, capture, schema, and DXIL measurement
  files; or
- machine-specific GPU timing normalization and grade taxonomies.

Removed normalization data included the `brock-rtx4090-laptop-v1` calibration,
42.5 TFLOPS and 582.464 GB/s reference values, telemetry freshness and
generation tracking, A/B/C/directional grade classification, normalized
milliseconds, and work-index estimates. Raw matched-run frame time remains the
authoritative performance evidence.

Restore only the harness for a subsystem under active measurement. Prefer
external scripts and artifacts over placing experiment orchestration back in
`uvsr.cpp`. A benchmark must not become a runtime dependency of the feature it
measures.

### Device-Removal Breadcrumb Diagnostics

`-debug` still enables the NVRHI validation runtime, but the opt-in DRED
breadcrumbs and page-fault report path was removed. Device removal no longer
writes `outputs/dred-latest.txt`.

The deleted bundle was `src/gpu_crash_diagnostics.cpp` and `.h`, pre-device
DRED enablement, native queue/list names, PIX breadcrumb markers, device-removal
report generation, `--dred` parsing, and the CMake library/dependency. DRED can
return independently. Build it for the D3D12 application, parse its opt-in flag
before device creation, and restore markers and shutdown reporting. Do not tie
it to the SVSM build option again.

### Build Profiles and Manifests

The normal engine now has one authoritative `src/shaders.cfg` plus the retained
Screen-Space Directional Shadow manifest. The cleanup removed:

- `UVSR_AA_DEVELOPER_OVERRIDES`;
- `UVSR_DEFAULT_SETTINGS_EXPERIMENT_SHADERS`;
- `UVSR_TAA_SAMPLE_RESURRECTION_AVAILABLE`;
- SVSM and diagnostic CSM component build options;
- `src/shaders_production.cfg`;
- `src/shaders_experiment_defaults.cfg`;
- SVSM and diagnostic CSM manifests, shader bundles, staging directories,
  component libraries, tests, and application dependencies; and
- runtime package entries for every retired shader family.

A future experiment profile must be explicit, isolated, and fail closed when a
runtime selection is not packaged. It should not recreate production,
developer, and factory manifests whose operational routes differ only by hidden
availability.

### Startup, Title, and Process Behavior

Custom benchmark/developer startup switches were removed, including
`--visibility-benchmark`, `--aa-*`, `--svsm-*`, `--diagnostic-csm-*`, `--dred`,
`--experiment`, and graphics-backend aliases. Retained startup options are
`-width`, `-height`, `-fullscreen`, `-debug`, `-adapter`, and a positional
scene. The application is fixed to DirectX 12.

The window title is now the API plus embedded commit. Launch-time and experiment
label components, `src/experiment_title.h`, and its test were removed. The
existing launcher still sets `UVSR_EXPERIMENT`, but the renderer no longer
consumes that environment value.

Process priority changed from benchmark-controlled Normal/High selection to an
unconditional request for High at startup. Restoring benchmark priority control
must not silently alter ordinary launch behavior or unrelated process priority.

## Major Modifications That Were Not Straight Removals

### Visibility Noise Options

The cleanup added **Standard White Noise**, implemented with a deterministic
PCG-style output permutation. It is intentionally distinct from the retained
custom hash sequence and acts as a conventional white-spectrum baseline.

Two existing options were renamed without changing their algorithms:

| Earlier Name | Current Name | Algorithm Change |
| --- | --- | --- |
| Independent Hash | Hashed White Noise | None |
| Toroidal Blue Noise | Void Cluster Blue Noise | None to the retained scheduler semantics |

Noise selection remains a uniform runtime branch and adds no shader tasks. The
prepared Void Cluster texture was compacted from eight independent rank layers
to the five semantic random dimensions still consumed after planner and bounce
removal. Deterministic fixtures now cover exact sequences, unit-interval bias,
cross-sequence distinction, nearest-neighbor correlation, per-layer rank
coverage, and inter-layer decorrelation.

A name-only reversion affects UI, command values, documentation, and exact-label
tests; it does not require a shader algorithm restoration. Removing Standard
White Noise additionally removes its HLSL function, scheduler enum value,
commands, and deterministic fixtures, but still does not alter manifest counts.

### Visibility Pipeline Consolidation

The old stage graph—depth hierarchy, first trace, later bounces, temporal,
spatial denoise, fused upsample/apply, separate apply, and benchmark envelope—was
replaced by:

1. **First Trace**;
2. **Reconstruction**;
3. **Composition**; and
4. the complete effect envelope.

Precision settings collapsed from raw, cumulative, temporal, final, and
hierarchy variants to user-facing AO and indirect-diffuse precision. Runtime
still owns raw and final AO/GI textures; only redundant aliases and dormant
history/frontier resources left.

Reference and Runtime planner identities became direct quality recipes plus a
Custom state. Dormant math-choice taxonomies for alternate `acos`, trigonometry,
radial power, normalization, thread groups, and filter experiments were replaced
by their chosen implementations. Exact 1–64 sample counts, three estimators,
full/half/quarter resolution, five reconstruction modes, two spatial filters,
and 16/32-bit buffers remain configurable.

### Visibility and PBR Decoupling

Before the cleanup, Visibility enable, preset, and reset paths assigned the
hidden PBR comparison flag and could also clear White World. After the cleanup:

- deferred PBR is the supported renderer invariant;
- Visibility enable affects only AO/GI execution and their resources;
- no Visibility action changes sky, lights, shadows, AA, or world appearance;
  and
- Indirect Diffuse Only is a Debug presentation choice rather than a renderer
  mode switch.

This decoupling should not be reverted unless PBR-off becomes an explicit,
supported renderer mode. Restoring the old assignments would recreate the
unwanted cross-effect behavior without restoring a useful alternate renderer.

### Command and Settings Surface

The settings command catalog fell from 245 controls to 125. Old shared AA paths
were replaced by `anti-aliasing.taa.*`, `anti-aliasing.cmaa2.*`, and
`anti-aliasing.msaa.*`. New composable debug paths use `debug.world.*`,
`debug.visibility.*`, `debug.pbr.*`, and `debug.shadows.*`.

Removed commands covered SVSM, CSM, benchmarks, planner/profile identity,
recursive bounce controls, Visibility temporal/hierarchy controls, TAA
resurrection and performance experiments, retired statistics actions, and the
legacy World Materials selector. The command catalog now mirrors controls that
exist in the current UI plus four retained actions.

Restoring a UI control requires restoring its command, reset/default ownership,
renderer consumer, unavailable-state behavior, and focused tests. A command-only
backdoor is not a supported feature.

### Statistics and GPU Monitoring

Statistics now reports only active renderer/effect stages. Visibility planner,
avoided-profile, benchmark-run, export, later-bounce, private temporal, and
hierarchy lines were removed. CMAA2's private multi-frame timer-query surface
was also removed while the effect remains available.

`gpu_performance_monitor.h` now exposes only the small live metrics contract
used by the current stat line. The machine-specific normalization, calibration,
freshness, grading, and benchmark-evidence structures were removed. This does
not claim that GPU telemetry is unnecessary; it keeps experiment policy outside
the production renderer.

### Documentation and Agent Procedure

Living documentation was rewritten around the retained UI and renderer. The
large UI integration procedure was condensed from incident-specific drawer and
benchmark choreography into the reusable control-ownership, default/reset,
composition, dropdown-safety, animation, command, and validation rules current
features need.

`AGENTS.md` stopped treating forward PBR and SVSM diagnostics as permanent
exceptions. Its performance guidance now refers to renderer settings and
composable debug isolation/overlays rather than SVSM-specific cases. The
DirectX 12, source-ownership, verification, and product-acceptance rules remain.

## Complete Do-Not-Restore-Alone Matrix

| Feature | Required Coupled Restoration | Dangerous Partial Restoration |
| --- | --- | --- |
| SVSM | Pass, settings, CPU/HLSL ABI, shaders, manifest, CMake, scheduling, PBR handoff, UI, commands, tests | Copying only the pass or settings header |
| Diagnostic CSM | Pass, profiles, constants, shaders, resources, scheduling, PBR handoff, UI, commands, tests | Reintroducing cascade controls without map/resolve ownership |
| Multiple-bounce GI | PBR metadata source, frontier/cumulative textures, continuation and indirect dispatch, trace branches, manifest, controls | Restoring bounce count or trace macros alone |
| TAA Sample Resurrection | Persistent color/depth, saved views, copy/reset lifecycle, constants, bindings, HLSL, tasks, diagnostics, tests | Allocating old textures without exact age/reprojection logic |
| Visibility temporal | Motion vectors, AO/GI/depth/normal history, resets and key, shader, explicit TAA coexistence | Restoring `temporalEnabled` alone |
| Depth hierarchy | Generation, texture, consumer traversal, setting/default, timing, package, tests | Shipping an unconsumed pyramid pass |
| Fused AO-only route | Explicit AO-only product mode, resource/application topology, shader/package, selection, UI, tests | Restoring planner names while the current AO+GI route still runs |
| Forward PBR | Product mode, targets/framebuffer, draw/material path, shaders, AA/debug/output ordering | Restoring hidden `EnablePbr` or forward shader only |
| HDR CMAA2 | HDR input-domain decision, pass order, shader macro, tasks, formats, UI, tests | Compiling HDR tasks while feeding LDR color |
| Old shadow debug enum | Replacement-view resources and old state model, or an explicit mapping to current state | Overlaying the enum on independent isolation/overlay state |
| Three shadow-producer slots | A requirement for simultaneous composition and a tested arbitration rule | Adding empty SRVs and CPU taxonomy without producers |
| Benchmark harness | Existing measured subsystem, isolated orchestration, exact identity and evidence output | Making the renderer depend on a dormant benchmark |
| DRED | Pre-device opt-in, diagnostic library, native naming/markers, device-removal output | Re-coupling diagnostics to the SVSM build option |
| Experiment shader profile | Exact manifest, runtime topology lock, fail-closed controls, package test | Omitting shaders while leaving their controls enabled |

## Original Cleanup Changed-Path Ledger

This ledger covers the 124 tracked files in the implementation diff. New
execution-plan and dated-archive documents are listed separately afterward.
“Modified” means retained behavior or contracts were rewritten; “deleted” means
the path has no current working-tree implementation but remains recoverable from
the exact base.

### Root and Build Files

| State | Path | Change |
| --- | --- | --- |
| Modified | `AGENTS.md` | Removed forward-PBR and SVSM-specific policy exceptions; generalized retained debug/performance guidance. |
| Modified | `CMakeLists.txt` | Removed shadow components, experiment/developer profiles, retired tests and sources, manifest forks, package entries, and obsolete dependencies. |
| Modified | `README.md` | Reframed the product around deferred PBR, focused Visibility, independent AA, one shadow technique, composable Debug, and compact shader counts. |

### Living Documentation

| State | Path | Change |
| --- | --- | --- |
| Modified | `docs/advanced-settings.md` | Replaced historical settings and benchmark surfaces with the eight current drawers, controls, commands, build, and runtime boundary. |
| Modified | `docs/pbr-foundation.md` | Defined deferred-only PBR, singular directional visibility, current Visibility integration, and composable Debug. |
| Modified | `docs/postmortem/README.md` | Added discoverability for the cutdown archive. |
| Modified | `docs/postmortem/shader-path-retirements.md` | Converted the ambiguous continuous path into a compatibility index for dated reports. |
| Modified | `docs/screen-space-visibility.md` | Replaced planner, temporal, hierarchy, and bounce documentation with the current three-stage route and restoration boundary. |
| Modified | `docs/temporal-aa-options.md` | Documented independent TAA/CMAA2/MSAA, current history ownership, and removed resurrection/HDR paths. |
| Modified | `docs/ui-integration-agent-procedure.md` | Condensed incident-specific and retired-feature choreography into current reusable UI rules. |
| Modified | `docs/visibility-estimator-validation.md` | Removed retired planner/benchmark dependencies while retaining estimator fixtures and evidence limits. |

### Deleted Documentation

The exact base commit is the recovery authority for deleted documentation.
Deleted execution-plan archives are not an active or preferred recovery layer.

| State | Path | Disposition |
| --- | --- | --- |
| Deleted | `docs/ao-optimization-ledger.md` | Large AO optimization and experiment ledger superseded by the focused route and dated postmortem. |
| Deleted | `docs/visibility-dxil-evidence.md` | DXIL evidence tied to retired Visibility profiles. |
| Deleted | `docs/visibility-estimator-benchmark.schema.json` | Embedded Visibility benchmark export schema. |

### Modified Core Renderer and UI Sources

| State | Paths | Change Family |
| --- | --- | --- |
| Modified | `src/uvsr.cpp` | Central integration rewrite: renderer topology, UI, commands, resources, AA ordering, debug composition, shadows, statistics, startup, benchmarks, and dead modes. |
| Modified | `src/ui_settings_command_catalog.h` | Reduced the command surface from 245 to 125 and aligned it with current UI ownership. |
| Modified | `src/command_line_options.h` | Removed graphics-backend taxonomy and unused unsigned parser; retained the integer parser used by startup. |
| Modified | `src/sponza_camera_preset.cpp`, `src/sponza_camera_preset.h` | Removed benchmark-only camera identity and retained normal camera presets. |
| Modified | `src/gpu_performance_monitor.cpp`, `src/gpu_performance_monitor.h` | Retained live metrics and removed calibration/normalization/evidence taxonomy. |

### Modified Visibility Sources

| State | Paths | Change Family |
| --- | --- | --- |
| Modified | `src/screen_space_visibility.cpp`, `src/screen_space_visibility.h` | Consolidated resources, pipelines, stages, settings, timings, lifecycle, and direct configuration. |
| Modified | `src/screen_space_visibility_cb.h` | Removed bounce, temporal, hierarchy, and planner constants; retained current trace/reconstruction data. |
| Modified | `src/screen_space_visibility_cs.hlsl` | Added Standard White Noise; removed bounce metadata/reinjection and dormant hierarchy branches; consolidated active trace. |
| Modified | `src/screen_space_visibility_filter_cs.hlsl` | Consolidated standard and packed guide-aware reconstruction for AO, GI, or both. |
| Modified | `src/visibility_blue_noise.cpp`, `src/visibility_blue_noise.h` | Renamed the retained scheduler and compacted eight prepared layers to five live semantic dimensions. |
| Modified | `src/visibility_estimator_shared.h` | Removed unused taxonomy while preserving current estimator constants. |
| Modified | `src/shaders.cfg` | Became the one core manifest with 269 tasks and no retired shader families. |

### Deleted Visibility Sources and Tools

| State | Path | Removed Surface |
| --- | --- | --- |
| Deleted | `src/screen_space_depth_hierarchy_cs.hlsl` | Dormant depth-pyramid generation. |
| Deleted | `src/screen_space_visibility_bounce_control_cs.hlsl` | Recursive-bounce continuation and indirect-dispatch control. |
| Deleted | `src/screen_space_visibility_composed_edges_cs.hlsl` | Separate composed-edge trace wrapper. |
| Deleted | `src/screen_space_visibility_filter_packed_edge_cs.hlsl` | Separate packed-edge filter wrapper. |
| Deleted | `src/screen_space_visibility_fused_apply_cs.hlsl` | AO-only fused resolve/apply implementation. |
| Deleted | `src/screen_space_visibility_temporal_cs.hlsl` | Visibility-owned AO/GI temporal accumulation. |
| Deleted | `src/visibility_benchmark_statistics.cpp`, `src/visibility_benchmark_statistics.h` | Embedded sample collection and summary statistics. |
| Deleted | `src/visibility_perf_capture.h` | Visibility capture metadata and result formatting. |
| Deleted | `src/visibility_performance_plan.cpp`, `src/visibility_performance_plan.h` | Reference/runtime/profile planner and execution taxonomy. |
| Deleted | `tools/measure_visibility_dxil.ps1` | Retired DXIL measurement workflow. |

### Modified PBR, Lighting, and Environment Sources

| State | Paths | Change Family |
| --- | --- | --- |
| Modified | `src/directional_light_visibility.h` | Replaced three producer slots with one upstream directional-visibility input. |
| Modified | `src/pbr.hlsli` | Removed legacy contribution/rejection taxonomy and unused visibility inputs; retained current material and lighting gates. |
| Modified | `src/pbr_deferred_lighting_cb.h` | Shrunk the UVSR extension to the singular visibility contract. |
| Modified | `src/pbr_deferred_lighting_cs.hlsl`, `src/pbr_deferred_lighting_msaa_cs.hlsl` | Removed multi-producer and bounce-metadata branches; kept single-sample and per-sample deferred lighting. |
| Modified | `src/pbr_deferred_lighting_pass.cpp`, `src/pbr_deferred_lighting_pass.h` | Removed the third metadata pipeline and redundant bindings; retained deferred PBR and MSAA. |
| Modified | `src/image_based_lighting_environment.cpp`, `src/image_based_lighting_environment.h` | Removed dead cache/accessor state while retaining the active environment, SH, prefilter, and BRDF resources. |

### Deleted PBR and Runtime Taxonomy Sources

| State | Path | Removed Surface |
| --- | --- | --- |
| Deleted | `src/pbr_forward_ps.hlsl` | UVSR forward PBR comparison shader and its transmissive/White World tasks. |
| Deleted | `src/lighting_contribution.hlsli`, `src/lighting_contribution_shared.h` | Broad CPU/HLSL source and rejection bitmask taxonomy with no current control. |
| Deleted | `src/experiment_title.h` | Launch-time experiment-label and title composition helper. |
| Deleted | `src/gpu_crash_diagnostics.cpp`, `src/gpu_crash_diagnostics.h` | Opt-in DRED setup, markers, and report generation. |
| Deleted | `src/world_material_view.h` | Mutually exclusive World Materials and indirect-diffuse debug abstraction. |

### Modified Screen-Space Directional Shadow Sources

| State | Paths | Change Family |
| --- | --- | --- |
| Modified | `src/screen_space_directional_shadows.cpp`, `src/screen_space_directional_shadows.h` | Retained the shadow technique and split isolation from translucent overlay presentation. |
| Modified | `src/screen_space_directional_shadows_debug_ps.hlsl` | Added replacement/overlay presentation behavior and opacity handling. |
| Modified | `src/screen_space_directional_shadows_settings.h` | Replaced the monolithic debug enum with isolation and edge-overlay state. |

### Deleted Diagnostic Cascaded Shadow Map Sources

| State | Paths | Removed Surface |
| --- | --- | --- |
| Deleted | `src/diagnostic_cascaded_shadow_map.cpp`, `src/diagnostic_cascaded_shadow_map.h` | Complete diagnostic CSM renderer and public pass contract. |
| Deleted | `src/diagnostic_cascaded_shadow_map_settings.h` | Profiles, cascades, cache, filtering, bias, scrolling, and diagnostics. |
| Deleted | `src/diagnostic_cascaded_shadow_map_cb.h` | CPU/HLSL constants. |
| Deleted | `src/diagnostic_cascaded_shadow_map_clear_vs.hlsl`, `src/diagnostic_cascaded_shadow_map_depth_vs.hlsl`, `src/diagnostic_cascaded_shadow_map_scroll_ps.hlsl`, `src/diagnostic_cascaded_shadow_map_resolve_cs.hlsl` | Clear, depth, scroll, and resolve shader stages. |
| Deleted | `src/diagnostic_cascaded_shadow_map_shaders.cfg` | Dedicated 54-task manifest. |
| Deleted | `src/diagnostic_csm_benchmark.h` | Embedded benchmark evidence and acceptance model. |

### Deleted Sparse Virtual Shadow Map Sources

| State | Paths | Removed Surface |
| --- | --- | --- |
| Deleted | `src/sparse_virtual_shadow_map.cpp`, `src/sparse_virtual_shadow_map.h` | Complete dense/sparse SVSM renderer and public pass contract. |
| Deleted | `src/sparse_virtual_shadow_map_settings.h` | Clipmaps, page/cache policy, presets, filtering, bias, diagnostics, and performance settings. |
| Deleted | `src/sparse_virtual_shadow_map_cb.h`, `src/sparse_virtual_shadow_map_sparse_cb.h` | Dense and sparse CPU/HLSL constants. |
| Deleted | `src/sparse_virtual_shadow_map_depth_ps.hlsl`, `src/sparse_virtual_shadow_map_sparse_depth.hlsl` | Dense and sparse depth rendering. |
| Deleted | `src/sparse_virtual_shadow_map_sparse_cs.hlsl` | Allocation, marking, hierarchy, clearing, recycling, scheduling, and statistics kernels. |
| Deleted | `src/sparse_virtual_shadow_map_resolve_cs.hlsl`, `src/sparse_virtual_shadow_map_sparse_resolve_cs.hlsl` | Dense and sparse visibility resolve families. |
| Deleted | `src/sparse_virtual_shadow_map_receiver_lod.hlsli` | Receiver-level selection helper. |
| Deleted | `src/sparse_virtual_shadow_map_debug_ps.hlsl` | SVSM full-screen diagnostics. |
| Deleted | `src/sparse_virtual_shadow_map_shaders.cfg` | Dedicated 105-task manifest. |
| Deleted | `src/svsm_motion_benchmark.h` | Camera and sun-motion benchmark orchestration and evidence. |

### Modified Anti-Aliasing Sources

| State | Paths | Change Family |
| --- | --- | --- |
| Modified | `src/temporal_aa_options.h`, `src/temporal_aa_options_shared.h` | Replaced exclusive method/preset and experiment taxonomies with independent TAA/CMAA2/MSAA settings and retained image policies. |
| Modified | `src/temporal_aa.cpp`, `src/temporal_aa.h` | Removed persistent resurrection, pixel-path, selective-export, debug, and general experiment resources; retained compact TAA execution. |
| Modified | `src/temporal_aa_blend_cs.hlsl` | Removed resurrection and developer cross-product branches; retained image algorithms and compact performance modes. |
| Modified | `src/temporal_aa_resolve_cs.hlsl` | Removed TAA debug-view variants; retained final resolve. |
| Modified | `src/temporal_aa_core.cpp`, `src/temporal_aa_core.h` | Removed unused method-level helpers and retained active history/core behavior. |
| Modified | `src/temporal_aa_reference.h` | Removed resurrection and dormant experiment oracles; retained current reference math and history contracts. |
| Modified | `src/cmaa2.cpp`, `src/cmaa2.h`, `src/cmaa2.hlsl` | Fixed CMAA2 to display-linear LDR, removed HDR task selection and private stage timers, retained four qualities. |

### Deleted Anti-Aliasing and Manifest Sources

| State | Path | Removed Surface |
| --- | --- | --- |
| Deleted | `src/temporal_aa_debug.hlsli` | TAA Final History Weight and Sample Resurrection visualization helpers. |
| Deleted | `src/shaders_production.cfg` | Redundant production manifest fork. |
| Deleted | `src/shaders_experiment_defaults.cfg` | Factory-settings experiment manifest and abbreviated topology. |

### Modified Tests

| State | Paths | Updated Contract |
| --- | --- | --- |
| Modified | `tests/command_line_options_tests.cpp` | Retained bounded signed-integer parsing used by startup. |
| Modified | `tests/pbr_lighting_source_contract_tests.cpp`, `tests/pbr_reference_tests.cpp` | Singular directional visibility, simplified contribution gates, deferred-only behavior, and current PBR math. |
| Modified | `tests/production_shader_bundle_tests.cpp` | One core manifest, 269/46 task counts, 39-file runtime package, and retired-family absence. |
| Modified | `tests/renderer_source_contract_tests.cpp` | Current resources, pass ordering, feature independence, and absence of retired renderer, backend, and benchmark paths. |
| Modified | `tests/screen_space_directional_shadows_tests.cpp` | Independent isolation/overlay settings and presentation ABI. |
| Modified | `tests/sponza_camera_preset_tests.cpp` | Removed benchmark-camera identity while retaining normal camera fixtures. |
| Modified | `tests/temporal_aa_tests.cpp` | Independent AA, history/reset keys, retained policies, compact tasks, and resurrection/HDR absence. |
| Modified | `tests/ui_settings_command_catalog_tests.cpp`, `tests/ui_source_contract_tests.cpp` | Current drawers, 125-command ownership, concise labels, Debug composition, and removed controls. |
| Modified | `tests/visibility_estimator_reference_tests.cpp` | Current estimator API and removed planner taxonomy. |
| Modified | `tests/visibility_sampling_reference_tests.cpp` | Standard/Hashed/Void Cluster sequences, exact trace controls, five blue-noise layers, and one-bounce behavior. |

### Deleted Tests

| State | Path | Removed Contract |
| --- | --- | --- |
| Deleted | `tests/sparse_virtual_shadow_map_tests.cpp` | 8,022-line SVSM reference and lifecycle suite. |
| Deleted | `tests/diagnostic_cascaded_shadow_map_tests.cpp` | 3,582-line CSM reference and cache suite. |
| Deleted | `tests/visibility_performance_plan_tests.cpp` | Visibility profile/planner validation matrix. |
| Deleted | `tests/visibility_benchmark_statistics_tests.cpp` | Embedded benchmark sample/statistics validation. |
| Deleted | `tests/world_material_view_tests.cpp` | Old mutually exclusive debug-state resolver. |
| Deleted | `tests/experiment_title_tests.cpp` | Experiment-label and launch-time title formatting. |
| Deleted | `tests/experiment_shader_bundle_tests.cpp` | Factory experiment manifest and topology-lock contract. |

### New Cleanup Documents

These files were added after the tracked implementation-diff snapshot and are
not removed runtime surface:

| State | Path | Purpose |
| --- | --- | --- |
| Added | `docs/postmortem/engine-cutdowns/README.md` | Dated cutdown index and count-comparability warning. |
| Added | `docs/postmortem/engine-cutdowns/2026-07-30-shader-permutation-cutdown.md` | Restored historical record for the first shader cutdown. |
| Added | `docs/postmortem/engine-cutdowns/2026-08-02-engine-core-cleanup.md` | This complete restoration-focused report. |

## Immutable Recovery Boundary

Recover pre-cut implementation paths from
`f7c0c87d8cba6880428fbc34400eb2882fb5182e` and the integrated successor from
`b4dc24128e4f38effdeaf5a2dbc33cae107e9134`. The content hash
`631ec35c500cc7bf4f78cc69b68a1a98cb32eebc` identifies an intake patch but is
not recoverable source. Deleted plan archives are not recovery evidence.

## Original Cleanup Verification

| Check | Result |
| --- | --- |
| Release build | Passed on the final source state |
| Core shader compilation | 269 of 269 passed |
| Screen-Space Directional Shadow compilation | 46 of 46 passed |
| Runtime shader package | Exact 39-file contract passed |
| Full CTest | 30 of 30 passed |
| Noise runtime smoke | All three final labels opened and selected; Standard White Noise rendered with Visibility enabled |
| AA runtime matrix | TAA, CMAA2, MSAA, TAA+MSAA, all three, and MSAA restart transitions passed |
| Debug composition | World appearance and PBR filtering coexisted; the edge overlay rendered over Final Lighting |
| Shadow pruning | Only Screen-Space Directional Shadows remained and enabled successfully |
| Retired-symbol search | No operational production references remained; expected negative test assertions only |
| Independent review | No remaining P0–P2 findings |

The source build and runtime smoke predate this documentation record. No
renderer source changed while creating it, so the executable was not rebuilt.

## Original Cleanup Confidence

| Remaining Area | Weight | Confidence | Evidence |
| --- | ---: | ---: | --- |
| Core build, package, and launch | 20% | 98% | Full build, exact package contract, exact-artifact launch |
| Visibility | 20% | 95% | Reference tests, shader build, noise selection and enable smoke |
| PBR, lighting, scenes, and lights | 20% | 93% | Source contracts, reference tests, bundled scene runtime |
| Anti-aliasing | 15% | 94% | Unit contracts and mixed TAA/CMAA2/MSAA D3D12 matrix |
| Shadows and debug presentation | 15% | 93% | Shadow tests plus final shadow and overlay smoke |
| UI and command surface | 10% | 96% | Command, layout, source, animation, and runtime checks |

The evidence-weighted average confidence in the remaining features is 94.85%.
Residual uncertainty is concentrated in visual quality across every camera and
scene, long-duration runtime soak, multiple hardware adapters, and controlled
performance benchmarking. The later integrated verification is recorded in the
[Front-End Fidelity Restoration](2026-08-03-frontend-fidelity-restoration.md).

## Restoration Rule of Thumb

Use the smallest current product need to define a successor, but restore every
owner of that behavior. Historical availability is evidence that code existed,
not evidence that it still fits current resources, UI, or output ordering.

For every August 2 cut, compare named paths against exact base `f7c0c87`. For
paths first removed on July 30, use the
[dated shader cutdown](2026-07-30-shader-permutation-cutdown.md) and its named
commits. Use only exact commits and named paths as recovery points.
