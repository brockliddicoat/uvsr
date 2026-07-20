# UVSR

**Unified Visibility Stochastic Rendering**

UVSR is a DirectX 12 renderer built on NVIDIA's pinned Donut framework and its
NVRHI graphics abstraction layer. It ships with two ready-to-run Intel PBR
Sponza flat-roof scenes. The default **PBR Sponza Decorated** includes Intel's
separately distributed curtains and ivy; **PBR Sponza Plain** uses the same
architecture without either add-on.

## Renderer Baseline

- Deferred shading, UVSR PBR, screen-space visibility AO/GI, and the procedural
  sky start enabled.
- The normal **Aliasing** drawer contains **Enabled**, **Run 45-Degree Motion
  Test**, **Method**, **Quality**, **History Frames**, and **History Strength**.
  Method independently selects **Temporal**, **Conservative Morphological**,
  **Multisample**, or **Subpixel Morphological**. SMAA 1x, long-term temporal,
  and MSAA expose **Low**, **Medium**, **High**, and **Ultra**. CMAA2 exposes
  **Low**, **Medium**, and **High**.
  Spatial qualities are full-screen official SMAA 1x. Long-term temporal
  qualities use
  progressively stronger MiniEngine temporal bundles with presentation-only
  selective SMAA. CMAA2 uses Intel's official three-stage compute topology.
  Multisample displays **Low (2x)**, **Medium (4x)**, **High (8x)**, and
  **Ultra (16x)**. Every Multisample quality applies CMAA2 by default.
  In Deferred mode it preserves every G-buffer sample through material decode
  and PBR lighting, then averages final RGBA16F radiance. Full-screen SMAA or
  CMAA2 can run after that resolve; Temporal can likewise select CMAA2 as its
  presentation morphology without resetting history.
  Screen-space visibility remains available with Deferred MSAA: it selects one
  coherent closest reverse-Z surface per pixel for visibility and
  coverage-weights only that surface's signed lighting correction back into
  the per-sample MSAA result.
  The shared SMAA source preserves the official 0.15/0.10/0.10/0.05
  thresholds, actual 8/16/32/64-pixel axial reaches, 0/0/8/16-pixel diagonal
  reaches, and High/Ultra corner behavior.
  History Frames reports no history for spatial methods and exposes a 1–31
  prior-frame slider for long-term temporal
  methods. Its inherited values are 3/7/15/31. History Strength only reduces
  otherwise accepted temporal history;
  it cannot restore a sample rejected by motion, bounds, depth, disocclusion,
  or rectification.
  Production shows **Sharpness** only for the four long-term temporal
  qualities. Developer builds move it into default-open **Aliasing Algorithm
  Configuration** with resolved **(Preset)** controls; mutually exclusive
  controls show **(Mutex)**. Performance experiments remain collapsed.
  MiniEngine TAA owns the temporal history, validity, reset, bounds,
  motion/jitter, reverse-Z validation, and early-rejection infrastructure.
  Diagnostic SMAA uses one fixed spatial pixel/dense implementation without
  temporal phase history or SMAA-specific performance overrides.
  The motion-test button runs the exact Benchmark Position 1 warm,
  right-45-degree, hold, and return sequence used by the CLI benchmark. Both
  paths target 40 rendered frames per second, producing a fixed 15 degrees per
  second sweep so GPU speed cannot accelerate the camera. The button writes a
  self-validating 256-sample report, keeps
  the app open, and returns the camera to Piloted.
  Effective temporal image or history-layout changes reset state exactly once;
  presentation-only SMAA, Sharpness, and image-equivalent execution changes do
  not. Forward and legacy shading leave temporal AA unavailable because they
  do not produce the required motion contract. Visibility Temporal
  Reconstruction remains mutually exclusive until that history uses the same
  jitter convention. The complete method, quality, coordinate, SMAA, reset,
  performance, and benchmark contract is in
  [`docs/miniengine-taa-options.md`](docs/miniengine-taa-options.md).
- Screen-space visibility traces AO/GI at selectable full, half, or quarter
  linear resolution. Visibility-owned temporal accumulation is not exposed;
  renderer TAA owns temporal stability in the current build. The **Spatial
  Reconstruction** section exposes an explicit **Unreconstructed Full
  Resolution Input** choice, guide-aware upsampling for reduced-resolution data,
  the two legacy joint-bilateral reconstruction methods, and the retained Intel
  edge-guided methods. Full-resolution visibility can therefore composite
  unfiltered current output without a spatial dispatch or filter target.
- Screen-space visibility uses one exact compiled sample count shared by AO and
  every GI bounce. The factory default traces one stochastic slice with
  **20 Exact Samples** for every eligible pixel; the selectable counts are
  8, 12, 16, 20, 24, 48, and 64. Adaptive sparse sampling, its feedback
  resources, the free-form sample slider, and the separate later-bounce count
  selector have been removed.
- The **Estimator** control exposes **Uniform Projected Angle**, **Uniform Solid
  Angle**, and **Cosine-Weighted Solid Angle**. Uniform Solid Angle is the
  default. The cosine path is fully compiled and uses the complete joint-cosine
  CDF, projected slice mass, `pi` GI normalization, and no duplicate receiver-
  cosine factor.
- Renderer settings always start from factory defaults; **Reset Settings**
  restores those defaults in-session, and settings are not carried between
  launches.
- The rounded scrollbar-style loading track and its blue moving grab separate
  scene import, texture decoding, and render-thread texture finalization into
  weighted phases. Import progress comes from real parser, buffer, material,
  mesh, node, and animation milestones rather than one whole-model completion
  bit. It reports decoded and GPU-ready texture counts, eases monotonically
  between completed milestones, and reserves its final 5 percent for the scene
  setup that dismisses the loading screen. A launch counter begins at program
  entry and displays milliseconds to one decimal place on every loading screen.
- The settings overlay uses the installed Codex desktop app's Windows system
  UI face: 16 px Segoe UI Semibold with a 65-percent-wider word-space advance.
  Non-Windows builds fall back to bundled Geist 1.7.2 under the SIL Open Font
  License 1.1. The neutral-black panel stays at 0.60 opacity with a 4 px
  backdrop blur and one subdued transparent graphite treatment across every
  drawer body. Drawer headers use the authored transparent ImGui blue.
  Dropdown fields, dropdown-arrow and folder-button backgrounds, and slider
  tracks all reuse the panel's tinted-black RGB at 0.72 opacity, with
  opacity-only hover and active states. The three footer action buttons reuse
  the neutral Settings title-bar color. Dropdowns replace ImGui's sharp arrow
  with a compact Bézier-rounded triangle, and the larger drawer and tree
  disclosure triangles use the same rounded-corner construction in both
  orientations. The Settings title-bar disclosure hover uses the menu's 4 px
  frame rounding. Slider knobs use the same transparent blue appearance as the
  drawer headers. Two-state toggles animate between endpoints; their active
  track is 50-percent-opaque white and their solid compensated knob matches the
  rendered header blue. Mutually exclusive controls ease symmetrically between
  full opacity and the grayscale 0.38 disabled endpoint. Dropdown selections
  commit on the first UI frame after their popup closes, then use a synchronized
  0.30-second smoothstep so renderer reconfiguration cannot hide the transition.
  The outer edges retain an 8 px radius.
- The renderer/GPU summary and first performance line are pinned above an
  independently scrolling settings body, so they stay attached and visible at
  every drawer position. The panel shrinks to its open drawers and only scrolls
  when its content reaches the available screen height. Its width follows the
  widest status or control-and-label row rather than a proportional estimate.
  The performance line flows naturally from the left, and the status lines use
  an explicit 2 px gap. One queued snapshot captures and applies the top,
  visibility, and temporal-AA stats together 24 times per second. The first GPU clock
  sample is displayed directly; later hardware targets remain sampled every
  500 ms. Displayed gb/s follows each raw sample directly, while tflops alone
  moves toward each new target in 0.1 increments on each 24 Hz snapshot. The
  performance line reports resolution, current-clock memory bandwidth,
  utilization-adjusted current-clock FP32 tflops, frame time, and fps in that
  order, leaving fps at the outside edge. Millisecond and tflops values use one
  decimal place.
- Tree-row hover states, popup selections, and keyboard selection highlights
  use the same 4 px radius as other controls. The material editor continuously
  auto-fits its selected material, including immediately after a new surface is
  picked. The visibility panel uses the shorter **Distribution Exponent** label
  when determining its minimum width, then keeps a small readability allowance
  so its longest rows clear the scrollbar without clipping. Every text tooltip
  uses one compact fixed width and height at every nesting depth, with wrapped
  copy and a consistent inner safety margin.
- Press **M** to open or close the material editor. Selecting a scene material
  does not open the editor automatically.
- The three footer actions use explicitly centered labels to compensate for the
  system font's visual baseline.
- The single **Noise Pattern** dropdown compares **Independent Hash Noise**,
  first-party **Toroidal Blue Noise**, **Offline Spacetime Noise**, and
  **Offline Packed Spacetime Noise**. The packed choice delivers the same
  offline-computed values through one RGBA8 lookup instead of a separate
  second control. Noise Pattern appears immediately below Estimator.
- The **Profile** dropdown directly beneath **Sampling Resolution** begins with
  exactly four product presets. **Low** uses Uniform Projected Angle, quarter
  resolution, 8 exact samples, and compact joint-bilateral upsampling;
  **Medium** uses Uniform Solid Angle, half resolution, the same 8 samples,
  and the same upsampler. Factory-default **High** uses full resolution and
  20 samples; **Ultra** uses full resolution, 48 samples, and two GI bounces.
  High and Ultra use unreconstructed full-resolution input. Every preset uses
  Offline Packed Spacetime Noise. Low and Medium select Performance Precision
  buffers; High and Ultra select Default Precision buffers. Failed experiments,
  diagnostic floors, and implementation-profile presets are not packaged or
  selectable, while the retained controls remain independently editable.
- While a benchmark is queued, warming up, or collecting data, an independent
  top-right overlay remains visible even when the settings UI is hidden. It
  animates from `Benchmarking.` through `Benchmarking...` and reports collected
  measured frames over the requested total, for example
  `Benchmarking... (67/420)`. Warm-up frames intentionally do not increase the
  collected count.
- Controlled Intel measurements rejected the XeGTAO profiles as a faster UVSR
  replacement, the packed-edge 4x4 paths, and the per-function math
  approximations. Their UI, benchmark entries, host paths, shaders, and
  test fixtures have been removed. The optimization ledger retains the measured
  evidence and rejection reasons.
- The PS4 4x4-by-6 scheduler, scalar and packed-gather spatial paths, prepared
  depth surface, coupled temporal pass, profiles, shader permutations, test
  fixtures, and the separate analytic-horizon attribution control have been
  removed. Their source and timing evidence remains in the ledger.
- The collapsed **Statistics** drawer begins with an effect selector for the
  complete renderer, geometry/G-buffer, direct lighting, screen-space
  visibility, material picking, procedural sky, tone mapping, or output blit.
  Screen-space visibility expands into its outer effect envelope, named-stage
  total, signed unattributed timer difference, depth preparation, first trace,
  one combined later-bounces row, spatial denoise,
  fused spatial denoise/upsample, required upsample, fused
  resolve/application, and composition. No stage is labeled **Other** and
  unrelated concepts are not combined. Benchmark controls and the last result
  table are in this same drawer. Two memory
  rows report exact logical **Outputs**,
  **Working**, **Mask Cache**, and **Avoided** payloads; **Shared** is explicitly
  an estimate of duplicate mask payload avoided by shared AO/GI traversal.
- Visibility controls use compact, scrollable sections modeled on the
  established AA panel: full-width dropdowns and dedicated Noise, Spatial
  Reconstruction, and Resolve areas. **Buffers** is its own sibling drawer
  directly below **Visibility**, and **Statistics** follows it. The unified
  **Profile** dropdown provides **Low**, **Medium**, **High**, and **Ultra** as
  the only presets. Only the genuinely AO-only fused final-application choices
  remain labeled **(Mutex GI)**.
  Benchmark and scene locations use folder buttons instead of displaying long
  filesystem paths in the main panel. Ordinary resolution, estimator, AO, or
  GI edits clear the quality preset and switch the selector to custom settings,
  so a preset label cannot silently survive a renderer fallback. Buffer-format
  edits also clear the quality preset because the four recipes own
  their starting buffer formats. Every compatible custom setting remains
  active; the internal generic fallback used to compose those settings is not
  exposed as a selectable profile.
- The default deferred UVSR PBR path starts enabled. **Visibility > Enabled**
  turns visibility and PBR off or on together. The legacy Donut comparison path
  remains implemented for possible future experiments, but its separate control
  is hidden.
- The **General** drawer contains **Graphics Adapter**, **Camera Mode**, and
  **Camera Location** for the standardized Sponza scenes. **World Materials**
  contains the White World presentations and the **Indirect Diffuse Response**
  view. **World Scenes** labels the scene picker at the bottom.
  Named multi-model descriptors appear as one clean entry while their component
  GLBs stay available to explicit command-line loads without cluttering the
  picker.
- **White World Off** is the default. **White World On**, **White World Preserve
  Normals**, and **White World Preserve Emissives** override material color
  without modifying source assets. The last mode keeps authored emissive color
  alongside the scene's colored direct lights so GI sources remain easy to read.
- **Camera Mode** offers **Freelook** and **Locked**. Freelook is
  collision-enabled: mouse and arrow keys rotate the view, A/D strafe left and
  right, the wheel applies a small damped dolly, and W/S dolly at up to 16% of
  the initial framing distance per second with smooth acceleration and finite
  deceleration. Holding Shift doubles A/D strafe, W/S dolly, and wheel zoom.
  Moving inward gently
  lowers dolly sensitivity on a linear scale that bottoms out at 40% of the
  starting speed; the floor affects speed rather than position, so the eye
  remains free to continue forward without converging on a fixed pivot.
  Q/E translation stays disabled. Locked freezes the current view.
  Camera keys and mouse buttons are reconciled with physical input after UI or
  window focus transitions, preventing a consumed release event from latching
  motion.
- **PBR Sponza Decorated** and **PBR Sponza Plain** open in **Freelook** at
  **Benchmark Position 1**, the
  `intel-pbr-sponza-courtyard-simplified-v1` preset. The **Camera Location**
  dropdown contains that named location and an always-selectable **Piloted**
  entry,
  leaving room for more named locations later. Choosing Benchmark Position 1
  recalls the complete pose without changing Camera Mode. After translation or
  rotation moves the view away from the recalled pose, the dropdown reports
  Piloted. Choosing Piloted also detaches the location name without moving or
  reorienting the camera. The preset uses a 60-degree perspective view and a
  1920x1080 reference frame.
- The first scene light is selected automatically in the **Lights** panel.
- Authored emissive materials always remain GI sources at the calibrated 4.0
  source gain. This is an implementation constant rather than a user-facing
  checkbox or strength control.
- **Indirect Diffuse Response** in **World Materials** is the sole retained
  visibility diagnostic. It displays the material-applied screen-space diffuse
  GI contribution without direct light, sky fallback, fallback specular, or
  AO-only darkening. Selecting it turns White World off; selecting any White
  World presentation exits the diagnostic. The entry is available only while
  deferred PBR visibility and effective diffuse GI are active; disabling a
  prerequisite returns the dropdown to **White World Off**.
- **Limit Bounces** is on by default. While on, **Bounces** selects one through
  eight finite diffuse bounces; one keeps the original compact shader path.
  Turning the limit off enables GPU-driven contribution termination. Each later
  bounce transports only the newest light frontier, and the continuation bar
  becomes four times stricter after every bounce. A wave-coalesced GPU flag and
  indirect dispatch turn every pass after convergence into zero work without a
  CPU readback. A 16-bounce fault guard contains malformed or non-contracting
  data; it is not the normal termination condition.
- **AO Power** defaults to its identity value of 1.0. The default compositor
  is a separate shader specialization with the power operation compiled out;
  moving the slider away from 1.0 selects the powered specialization.
- **Bounce Contribution Cutoff** skips higher-bounce source shading whose
  conservative exposed upper bound is too small to matter. The default is
  `0.001`; zero keeps exact-zero exits only in explicitly limited mode. With
  **Limit Bounces** off, it becomes the nonzero starting cutoff for the
  exponentially rising continuation bar.
- Later bounces reject receivers with proven-zero diffuse throughput before
  view-position reconstruction, normal fetches, or slice setup.
- AO, GI, the GI source-radiance target, temporal history, filtered outputs,
  depth hierarchy, and extra-bounce targets exist only while their consumers
  require them. AO strength zero or GI intensity zero removes that consumer
  while the other effect can continue independently. The default directional
  mask remains register-local and consumes zero persistent mask-cache bytes.
- Proven scene-wide source inactivity terminates the complete higher-bounce
  dispatch chain. The shared CPU/HLSL activity mask is
  extensible to future clustering, probe, cache, visibility, and residency data;
  unknown sources always remain active.
- A shared, future-extensible contribution-gate contract also gives forward and
  deferred direct lighting exact early outs for zero, out-of-influence,
  back-facing, or fully occluded lights before unnecessary shadow/BSDF work.
- UVSR uses one fixed neutral AgX display transform to convert scene-linear HDR
  radiance for display. The optional Tonemapper drawer, grading presets, LUT
  loader, and bundled film looks are strategically sunset while scene lighting
  is still developing. This is a sequencing decision, not a failed feature.
  The exact implementation and its paired revival contract with bilateral-grid
  local tone mapping are preserved in the
  [postmortem](docs/postmortem/tonemapper-drawer-and-luts-v1.md).

## Coming Soon

Coming Soon is UVSR's user-facing roadmap and integration summary for stable,
active work that has not merged into `main`. It is not a mutex or a live task
ledger. An entry is not shipped on `main`, and experimental entries are not
promises that the work will merge.

- **Merged Anti-Aliasing and UI Candidate — Experiment**
  (`codex/aa-ui-merged-experiment`, local and not published). Integrate the
  complete MiniEngine TAA, diagnostic spatial SMAA, Intel CMAA2, and diagnostic
  deferred MSAA feature set with canonical UI mainline `a55e215`. Production
  uses deterministic spatial SMAA without SMAA-only execution experiments.

- **Screen-Space Visibility Shared Shader Helpers — In Review**
  (`devin/1784102514-screen-space-shared-helpers`, PR #10). Consolidate shared
  depth, pixel-coordinate, and safe-normal helpers used by the visibility
  sampling, depth-hierarchy, temporal, and filter shaders. This is a mechanical
  extraction with no equations, bindings, UI, or scene changes.

- **Visibility Degenerate-Path Test Coverage — In Review**
  (`devin/1784102780-visibility-test-coverage`, PR #11). Add reference coverage
  for degenerate visibility clipping, radial-mask edge cases, and blue-noise
  rank-field paths. This owns only visibility test sources and has no runtime
  rendering, UI, or asset overlap.

- **AO Performance Optimization — Ready for Manual Validation**
  (`codex/ao-performance-optimization`). Measure and optimize the AO-only
  visibility-bitmask path from depth preparation through application while
  retaining the canonical generic implementation as a zero-cost-off reference.
  This work owns historical visibility performance evidence, curated fixed-sample and
  resource permutations, optional noise/depth/packed-edge reconstruction
  experiments, AO-only fused resolve/application, advanced verification UI,
  focused reference tests, and the optimization ledger. The branch now
  contains the curated profile and benchmark/export implementation. Controlled
  Intel Arc integrated-GPU matrices used 120 warm-up and 600 measured frames per
  entry at 1920x1080 and completed every requested frame with zero incomplete
  frames. Exact fused resolve/apply saved 17.7-18.8% median across repeats;
  Fixed 8 plus fusion saved 20.6-22.4%; Fixed 8 alone saved 1.9-4.7%. The
  strongest paired format results were final GI `RGBA16_FLOAT` at 8.39% faster
  than `RGBA32_FLOAT` and the `R16_FLOAT` depth hierarchy at 3.88% faster than
  `R32_FLOAT`. Manual image and motion validation remains required.
  The current candidate removes the Activision PS4 scheduler, scalar/gather
  approximations, coupled temporal path, analytic-horizon control, and their
  resources. Every
  XeGTAO variant was slower than canonical Reference on the controlled Intel
  driver, so the XeGTAO runtime family was removed instead of being retained as
  a misleading faster-UVSR option. Historical PS4 scalar/packed timing remains
  in the ledger, but neither profile is present in the build.
  **Ready for Manual Validation** means the curated candidate can be built,
  launched, and tested; it does not mean every isolatable experiment was
  implemented or rejected. The explicit implementation/evidence follow-ups are
  Auto Fixed (D-005), trace LDS (D-020), reconstruction LDS (D-024/R-011),
  four-output coarsening (D-026/R-013), staged AO-only ILP (L-015/O-022), and
  exhaustive generated-code coverage (F-030). Feasible curated source
  experiments also remain: screen-space-size horizon-thickness EMA (A-011),
  depth-derived receiver normals (A-014), velocity-agreement adaptive clamp
  width (Q-027), XeGTAO Low (X-002), XeGTAO Medium (X-003), native R8 AO
  storage/decode (X-018), standalone depth-derived-normal generation (X-020),
  and in-main depth-derived normals (X-021). These are deferred/unimplemented,
  not impossible; the [optimization ledger](docs/ao-optimization-ledger.md#ledger-status)
  records each prerequisite and evidence boundary.
  It deliberately starts from `5f43205`; PR #10's shared-helper extraction and
  PR #11's test additions remain later integration dependencies, and the
  separate bilateral-grid work retains ownership of local tone mapping and AgX
  integration after visibility composition.

### Roadmap Ownership

The task coordinator or final integrator owns this section:

1. Small fixes, read-only investigations, and short-lived private experiments
   do not require an entry.
2. Before complex, concurrent, shared-hotspot, integration, or publication work,
   the coordinator reviews this entire section together with relevant pull
   requests, branches, worktrees, and active execution plans. The coordinator
   records overlap and dependency decisions in the task plan; individual
   workers do not each edit this README.
3. Add or update an entry once its scope and branch are stable. Include status,
   branch when one exists, intended scope, affected subsystems, and integration
   dependencies. A private experiment is listed only when it becomes stable
   roadmap information.
4. Reconcile the entry again during integration. Publishing a roadmap update
   still requires explicit authorization and never grants permission to push,
   open a pull request, or merge implementation work.
5. When work merges, remove its entry and move durable user-facing behavior into
   the renderer baseline or relevant design documentation. Mark or remove
   abandoned work explicitly rather than leaving a stale promise.

## Build and Run

Requirements:

- Windows with a DirectX 12-capable GPU and driver
- CMake 3.24 or newer
- A C++17-capable Visual Studio toolchain
- Git submodules initialized

At startup, UVSR selects the D3D12-capable adapter with the most dedicated
video memory. The **Graphics Adapter** selector lists every compatible GPU and
restarts the renderer immediately on the selected adapter.

Intel PBR Sponza is completely included in the repository and staged by CMake;
there is no separate model download, conversion, or scene setup step. The
default **PBR Sponza Decorated** scene composes the flat-roof architecture,
curtains, and roof-trimmed ivy. **PBR Sponza Plain** loads only the same two
architecture components. Every component remains below GitHub's 100 MB
per-file limit. Attribution and the exact runtime edits are recorded in
[`assets/scenes/intel_sponza/README.md`](assets/scenes/intel_sponza/README.md).

Configure and build a Release executable from PowerShell:

```powershell
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --config Release --target uvsr
.\tools\launch_uvsr.ps1 -Experiment naming
```

The launcher requires one lowercase ASCII word matching `\A[a-z]+\z`; uppercase
letters, digits, spaces, hyphens, underscores, and punctuation are rejected:

```powershell
.\tools\launch_uvsr.ps1 -Experiment naming
```

For a repeatable Sponza benchmark launch, add `--benchmark-camera`:

```powershell
.\tools\launch_uvsr.ps1 -Experiment benchmark --benchmark-camera
```

This flag selects and locks **Benchmark Position 1**
(`intel-pbr-sponza-courtyard-simplified-v1`), enforces a non-resizable
1920x1080 backbuffer, blocks fullscreen transitions, and selects **Locked** so
input cannot move or reframe the benchmark view. The disabled **Camera
Location** dropdown remains on Benchmark Position 1 throughout the benchmark
launch. The benchmark pose is shared by **PBR Sponza Decorated** and **PBR
Sponza Plain**; benchmark records identify it by that preset ID while the
separate `scene` field identifies which scene was measured.

### Anti-Aliasing Benchmark Overrides

Production builds accept `--aa-enabled`, `--aa-method`, `--aa-quality`
(`--aa-preset` alias), `--aa-sharpness`, and `--aa-benchmark-output`.
Algorithm and execution overrides require a developer build:

The same motion path is available interactively from **Aliasing > Run
45-Degree Motion Test** on either standardized PBR Sponza scene. It uses the
current AA settings, writes `aa-motion-test-latest.json` beside the executable,
and returns Camera Location to **Piloted** when it finishes.

```powershell
cmake -S . -B build-aa-dev -DUVSR_AA_DEVELOPER_OVERRIDES=ON
cmake --build build-aa-dev --config Release --target uvsr
```

A production build rejects `--aa-execution`, `--aa-kernel`, `--aa-lds`,
`--aa-reuse`, `--aa-early`, `--aa-fusion`, `--aa-cache`, and `--aa-smaa`
instead of accepting an override whose static PSO is absent.

### Visibility Benchmark Workflow

Run one visibility profile without UI, export its frame-correlated JSON/CSV and
last measured frame, and close after completion with:

```powershell
.\tools\launch_uvsr.ps1 benchmark --benchmark-camera `
  --visibility-profile exact-fast-ao-8t --visibility-benchmark `
  --benchmark-warmup 120 --benchmark-frames 240 `
  --benchmark-output .\benchmark-results --benchmark-auto-close
```

Profile matching ignores punctuation and case, so either the displayed
one-click name or a hyphenated form is accepted. `--benchmark-warmup` accepts
0 through 100000 frames and `--benchmark-frames` accepts 1 through 100000.
Add `--visibility-contribution-terminated-bounces` to a GI-capable profile to
turn **Limit Bounces** off before an automated run. This deliberately clears
the one-click verification label because the effective 16-entry,
GPU-terminated workload is no longer that preset's fixed-bounce contract.
Unknown or unavailable profiles and invalid frame counts report to standard
error and return a nonzero process exit code; they do not open modal dialogs.
The **Statistics** drawer provides **Run Current**, **Cancel**, and
**Export Last Run**. Run Current measures the effective configuration being
rendered, even when it no longer matches the selected preset label. It
automatically locks Benchmark Position 1, resizes to 1920x1080, waits for the
matching rendered workload, and restores the previous interactive window size
afterward. The former comparison, test-matrix runners, and
`--benchmark-sequence` command-line option have been removed. A live
`Benchmarking... (completed/total)` overlay continues animating while the
settings UI is hidden.
Readiness is based on the workload and permutation reported by the renderer,
not on a possibly stale preset label. A run can remain unavailable only while
the current settings have not reached the GPU, while no AO/GI effect is active,
outside deferred rendering, during another run, or outside PBR Sponza Decorated
and PBR Sponza Plain. The Sponza restriction remains because those are the only
scenes with the standardized camera used for comparable results.
The schema-v2 JSON includes a human-readable and hashed snapshot of the full
profile-relevant AO/GI, sampling, reconstruction, format, dispatch, and resource
contract that was active for the run.
On Windows, artifact filenames dynamically shorten only the redundant display-
name token so the complete path stays at or below a conservative 240-character
budget. The full profile name remains in JSON, while hashes, timestamp, extension,
and collision suffix retain their reserved space. Extension-length accounting
uses native path-size arithmetic without a narrowing conversion. Re-export is
all-or-nothing: if the recorded final frame cannot be copied, the newly created
JSON, CSV, and BMP are removed instead of leaving a misleading partial set.

After building, Windows users can also double-click `LaunchUVSR.cmd`. It
delegates to the same required experiment launcher with a fixed main-build
label; optional renderer arguments can be appended from a terminal.
`tools/launch_uvsr.ps1` also accepts `-BuildDirectory <path>` when an isolated
production or experiment build needs to be launched without replacing the
main build.

Windowed launches are centered in the primary monitor's usable work area. If
the requested client size plus decorations would cross a work-area edge, UVSR
preserves its aspect ratio while fitting it inside the available bounds before
centering, so taskbars cannot cover its right or bottom edges.

The title reports the active graphics API followed by the description, the
seven-character source commit embedded at build time, and the local launch time
in 24-hour `HHmm` form. Each field is separated by a dash, for example
`UVSR Renderer D3D12 (naming-b216081-2117)`. CMake watches the worktree's Git
HEAD and branch ref so the embedded commit refreshes on the next build after a
commit or checkout. Direct and IDE-driven launches can instead supply the one-
word description through `--experiment naming` or the `UVSR_EXPERIMENT`
environment variable; omitted descriptions default to `main`.

The first configure may download Microsoft's Direct3D 12 Agility SDK if it is
not already cached.

Build and run the scene-catalog, experiment-title, camera-collision,
camera-controls, Sponza-camera-preset, PBR, World-Material-view,
radial-visibility, estimator, visibility-projection, visibility-sampling,
visibility-performance-plan, and visibility-benchmark-statistics tests
separately:

```powershell
cmake --build build --config Release --target uvsr_scene_catalog_tests uvsr_experiment_title_tests uvsr_camera_collision_tests uvsr_camera_controls_tests uvsr_sponza_camera_tests uvsr_pbr_tests uvsr_world_material_view_tests uvsr_radial_visibility_tests uvsr_visibility_estimator_tests uvsr_visibility_projection_tests uvsr_visibility_sampling_tests uvsr_visibility_performance_plan_tests uvsr_visibility_benchmark_statistics_tests
ctest --test-dir build -C Release --output-on-failure
```

## Documentation and Conventions

The [PBR foundation](docs/pbr-foundation.md) documents the material contract,
G-buffer packing, equations, validation, limitations, and extension points.

The [screen-space visibility design](docs/screen-space-visibility.md) documents
the shared 32-sector AO/GI traversal, resources, coordinate/radiance contracts,
controls, limitations, and the upgrade path to persistent unified visibility.

The [AO optimization ledger](docs/ao-optimization-ledger.md) inventories every
supplied, Activision, XeGTAO, and further-research candidate; records its
classification, evidence, quality boundary, zero-cost-off disposition, and
measurement method; and ranks all implemented runtime families with explicitly
non-additive engineering forecasts. Its
[Remaining Feature Scorecard](docs/ao-optimization-ledger.md#remaining-feature-scorecard)
provides four 0-100 rankings for universal performance, situational
performance, UI nonredundancy, and their unweighted average. XeGTAO is retained
there as rejected
historical evidence, pinned to Intel
commit `a5b1686c7ea37788eeb3576b5be47f7c03db532c`; published Intel timings are
reported only as upstream provenance and never as UVSR measurements or promises.

The [visibility DXIL evidence](docs/visibility-dxil-evidence.md) provides a
reproducible historical static generated-shader comparison for the core
Reference, candidate, diagnostic, reconstruction, and fusion permutations.
Diagnostic entries describe the investigation and are no longer packaged. It does not
substitute static IR counts for target-GPU timings or physical Intel register,
spill, SIMD-width, and occupancy data.

The [visibility estimator validation](docs/visibility-estimator-validation.md)
records the shared C++/HLSL measure contracts, deterministic reference fixtures,
and the boundary between automated evidence and required runtime evaluation.

The [retired Visibility Sample Rotation v1 notes](docs/visibility-sample-rotation-v1.md)
define the supported layout, exact four-phase sequence, history convention,
resource contract, and technical evidence. Its
[postmortem](docs/postmortem/visibility-sample-rotation-v1.md) records the negative
visual result, lessons, and explicit triggers for any future reconsideration.

The [experiment postmortem archive](docs/postmortem/) preserves retired work,
supporting evidence, and restart guidance, including the
[native-resolution analytical/reconstructive temporal anti-aliasing v1 postmortem](docs/postmortem/native-resolution-analytical-reconstructive-temporal-anti-aliasing-v1.md).

All Markdown headings, standalone bold headings, and initial bold list-item
headings use conventional English Title Case. Run
`tools/check_document_title_case.cmd` to validate the entire tracked
documentation set plus nonignored new Markdown files; the same check runs for
documentation changes on GitHub.

UVSR runs uncapped with a single planar view. UVSR-owned interactive controls
provide short, plain-English hover tooltips; new controls should follow the same
convention.
The bottom action row exposes equally sized **Reset**, **Screenshot**, and
**Restart** buttons.

## Intentional Omissions

The current baseline intentionally omits:

- DirectX 11 and Vulkan backends
- VSync, stereo, and bloom controls
- Imported scene cameras and translucent rendering
- Animation playback, ambient-intensity scaling, and material-event
  instrumentation
- Shadow rendering and shadow-map debugging
- Light-probe capture, filtering, image-based lighting, probe textures, and
  probe controls

## Repository Naming

Use the lowercase engineering slug `uvsr` for repository URLs, terminal
commands, package names, and folder paths:

```text
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
```

The displayed project name remains **UVSR**.
