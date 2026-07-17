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
- Screen-space visibility traces AO/GI at selectable full, half, or quarter
  linear resolution. **Temporal Reconstruction** independently enables
  SSRT3-style history accumulation, while **Spatial Filtering** independently
  enables compact or Gaussian joint-bilateral filtering. Both start disabled.
  Full resolution can therefore composite unfiltered current or temporally
  accumulated output without a spatial dispatch or filter target. Half and
  quarter resolution always retain a minimal depth/normal-guided 2x2 upsampler
  when spatial filtering is disabled because raw grid expansion produces
  coherent GI streaks. The **Reconstruction and Upsampling** drawer starts
  collapsed.
- **Adaptive Sparse Sampling** is off by default. The default fixed-work
  specialization traces one stochastic slice and **20 Fixed Samples Per Pixel**
  for every eligible pixel, with the adaptive neighborhood, reprojection,
  feedback, and stochastic budget code compiled out. Its feedback textures and
  motion dependency are also absent.
- When adaptive sampling is explicitly enabled, every eligible pixel receives
  one stochastic slice and a configurable minimum depth-tap budget.
  Depth/normal edges, disocclusions, unstable history, low confidence, and
  reprojected neighboring GI contribution stochastically raise the radial
  budget toward **Maximum Samples / Pixel**. Neighbor-driven work cannot
  recursively become a new propagation seed.
- The **Estimator** control exposes **Uniform Projected Angle**, **Uniform Solid
  Angle**, and **Cosine-Weighted Solid Angle**. Uniform Solid Angle is the
  default. The cosine path is fully compiled and uses the complete joint-cosine
  CDF, projected slice mass, `pi` GI normalization, and no duplicate receiver-
  cosine factor.
- The **Sample Scheduler** compares **Independent Hash Noise**, a first-party
  **Toroidal Blue Noise**, and an offline optimized
  **Filter-Adapted Spatiotemporal Noise**. Every scheduler toroidally
  rotates the complete nested radial prefix, so fixed-work and adaptive modes
  do not reuse global radius shells as the budget changes.
- **AO Performance Verification** retains **Reference** as a hard lock to the
  canonical generic shaders and exposes CPU-selected fixed 8/12/16/20 trace
  permutations, packed current FAST delivery, exact fused AO resolve/apply,
  16x8 and 8x16 trace groups, 32/64/128-pixel projected-radius clamps, packed-
  edge reconstruction/fusion experiments, the Activision 4x4-by-6 scheduler,
  duplicate/full-mask off controls, diagnostic floors, repaired Activision PS4
  GTAO approximation profiles, and a separate Intel XeGTAO 1.30 High source
  port. Reference allocates, binds, and dispatches no candidate-only work. The
  advanced controls use familiar dropdowns and compact one-click buttons rather
  than exposing an unrestricted permutation product.
- The pinned XeGTAO source port provides LUT/mixed-precision, inline-Hilbert/
  mixed-precision, and LUT/FP32 profiles. It follows Intel's High preset with
  five depth mips, three slices by three steps per side, Hilbert/R2 noise, packed
  edges, and one sharp denoise pass. It remains an AO-only algorithmic comparison,
  not a bit-identical engine import or an exact replacement for UVSR's finite-
  thickness bitmask producer.
- The Activision PS4 profiles remain explicitly labeled **Approximation** because
  Activision did not publish its shipping source or every constant. They now use
  eight total linear-distribution taps, a half-resolution closest-valid-depth
  guide with packed 2-bit source identity, the published 4x4-by-6 schedule, a
  derivative-aware 4x4 spatial pass, motion/depth-validated temporal
  accumulation, conservative far falloff, and the required full-resolution
  upsample. Temporal reprojection accepts the valid bilinear clamp footprint
  `[-0.5, size - 0.5)` but rejects nonfinite motion, true viewport exits, and
  odd-size padding. Scalar-filter and packed-gather versions remain available
  for direct comparison. This workload follows the console implementation
  disclosed in the 2016 slide deck. The expanded 2019
  `ATVI-TR-19-01` report is a separate analytical source: it documents the GTAO
  formulation, effective spatiotemporal sampling, and a 0.5 ms PS4 GTAO-plus-GI
  result, but it does not publish replacement shipping shader code or all
  constants needed for an exact port.
- Both the Activision PS4 approximation and XeGTAO source port require a
  perspective camera and a viewport at origin `(0, 0)` whose size exactly
  matches the depth texture. Orthographic, offset, cropped, or mismatched views
  report a clear profile error and run Reference instead of partially applying
  a source profile.
- Renderer settings always start from factory defaults; **Reset Settings**
  restores those defaults in-session, and settings are not carried between
  launches.
- The renderer/GPU summary and first performance line stay visible above the
  **General** drawer. That line reports resolution, frame time, FPS,
  current-clock memory bandwidth, and current-clock FP32 peak GFLOPS.
  Visibility statistics start collapsed and distinguish the outer effect
  envelope, named-stage total, signed unattributed timer difference, depth
  preparation, first trace, later GI trace and each bounce, spatial denoise,
  temporal reconstruction, fused spatial denoise/upsample, required upsample,
  fused resolve/application, and composition. No stage is labeled **Other** and
  unrelated concepts are not combined. Two memory
  rows report exact logical **Outputs**,
  **Working**, **Mask Cache**, and **Avoided** payloads; **Shared** is explicitly
  an estimate of duplicate mask payload avoided by shared AO/GI traversal.
- AO controls use compact, scrollable sections modeled on the established AA
  panel: full-width dropdowns, one-click Reference/Exact Fast/Fast Edges
  buttons, and dedicated Method, Noise, Denoiser, Resolve, and Benchmark areas.
  Benchmark and scene locations use folder buttons instead of displaying long
  filesystem paths in the main panel. Ordinary quality, resolution, estimator,
  AO, or GI edits clear any named source/verification profile and switch the
  selector to **Generic Fallback** with custom settings, so a source label
  cannot silently survive a renderer fallback.
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
  dropdown contains that named location and an always-selectable **Free** entry,
  leaving room for more named locations later. Choosing Benchmark Position 1
  recalls the complete pose without changing Camera Mode. After translation or
  rotation moves the view away from the recalled pose, the dropdown reports
  Free. Choosing Free also detaches the location name without moving or
  reorienting the camera. The preset uses a 60-degree perspective view and a
  1920x1080 reference frame.
- The first scene light is selected automatically in the **Lights** panel.
- **Emissive Source Gain** in **Visibility > Indirect Diffuse** globally
  scales how much light emissive materials contribute to GI without changing
  the visible emissive surfaces themselves. Raising it expands the visibly
  illuminated area up to the screen-space sampling radius.
- **Indirect Diffuse Response** in **World Materials** is the sole retained
  visibility diagnostic. It displays the material-applied screen-space diffuse
  GI contribution without direct light, sky fallback, fallback specular, or
  AO-only darkening. Selecting it turns White World off; selecting any White
  World presentation exits the diagnostic. The entry is available only while
  deferred PBR visibility and effective diffuse GI are active; disabling a
  prerequisite returns the dropdown to **White World Off**.
- **Bounces** in **Indirect Diffuse** selects one through four finite diffuse
  bounces. One is the default and keeps the original compact shader path. Later
  bounces transport only the newest light frontier and accumulate it separately;
  their GI-only sample budgets halve toward 8 taps without raising a lower
  first-bounce limit, so stochastic work grows
  sublinearly while bounce one stays at full quality.
- **Bounce Contribution Cutoff** skips higher-bounce source shading whose
  conservative exposed upper bound is too small to matter. The default is
  `0.001`; zero keeps exact-zero exits only. The gate saves source material and
  lighting work, but each active bounce still dispatches and performs its
  visibility traversal.
- Later bounces reject receivers with proven-zero diffuse throughput before
  view-position reconstruction, normal fetches, or slice setup.
- AO, GI, the GI source-radiance target, adaptive feedback, temporal history,
  filtered outputs, depth hierarchy, and extra-bounce targets exist only while
  their consumers require them. AO strength zero or GI intensity zero removes
  that consumer while the other effect can continue independently. The default
  directional mask remains register-local and consumes zero persistent mask-
  cache bytes.
- Proven scene-wide source inactivity terminates the complete higher-bounce
  dispatch chain. The shared CPU/HLSL activity mask is
  extensible to future clustering, probe, cache, visibility, and residency data;
  unknown sources always remain active.
- A shared, future-extensible contribution-gate contract also gives forward and
  deferred direct lighting exact early outs for zero, out-of-influence,
  back-facing, or fully occluded lights before unnecessary shadow/BSDF work.
- The AgX display pipeline provides Base, Punchy, Golden, Mix, and Custom
  presets. Its controls are Exposure, Contrast, Saturation, Warmth, Tint, Slope,
  and Power.
- UVSR includes original AgX Base-space simulations of Kodak 2383 Print, Portra
  400, and Ektar 100 looks. They are not official Kodak LUTs. Additional licensed
  3D `.cube` LUTs can be placed in `assets/luts/kodak`; see the
  [LUT notes](assets/luts/kodak/README.md) for the supported format.

## Coming Soon

Coming Soon is UVSR's user-facing roadmap and integration summary for stable,
active work that has not merged into `main`. It is not a mutex or a live task
ledger. An entry is not shipped on `main`, and experimental entries are not
promises that the work will merge.

- **Bilateral-Grid Local Tone Mapping — Active Development**
  (`agent/bilateral-grid-local-tone-mapping`). Add a first-party D3D12 GPU
  bilateral-grid analysis pass over the final scene-linear display source after
  visibility composition and before AgX, then apply one scalar local-EV
  correction inside the existing AgX display pass. This owns
  `src/local_tone_mapping*`, AgX bindings,
  display eligibility, reference tests, and its UI. It may extend the higher-
  bounce contribution cutoff conservatively without changing visibility
  estimator math or adding motion-reprojected local-exposure history.

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
  This work owns visibility performance diagnostics, curated fixed-sample and
  resource permutations, optional noise/depth/packed-edge reconstruction
  experiments, AO-only fused resolve/application, advanced verification UI,
  focused reference tests, and the optimization ledger. The branch now
  contains the curated profile and benchmark/export implementation. The final
  all-profile smoke completed 58/58 entries and 116/116 frames with zero
  incomplete frames and matching JSON/CSV/BMP sets. A controlled
  local RTX 4090 Laptop run used 120 warm-up and 600 measured frames per profile,
  completed every frame, and produced clean final captures. Exact fused resolve/
  apply saved 6.25% median and 17.51% p95; Fixed 8 plus fusion saved 7.87% and
  19.07%. Fixed 8 alone regressed 0.93% median and is not a standalone production
  candidate. Core Ultra 9 185H/Xe-LPG timing remains a user-run validation step,
  so NVIDIA results are not generalized to that target.
  The current candidate also keeps a repaired Activision PS4 GTAO approximation
  separate from a pinned Intel XeGTAO 1.30 High source port, exposes scalar/
  gather, LUT/inline-Hilbert, and mixed/FP32 comparisons, and adds the bounded
  **New AO Candidates** benchmark sequence. The controlled nine-entry run and an
  independent two-entry Xe precision repeat both completed 600/600 frames per
  profile with zero incomplete frames. On this GPU, prefer packed over scalar
  for the PS4 comparison and LUT/FP32 over Xe mixed precision; retain both
  algorithm families for comparison and quality testing rather than treating
  them as faster UVSR bitmask profiles.
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
Unknown or unavailable profiles and invalid frame counts report to standard
error and return a nonzero process exit code; they do not open modal dialogs.
Run a complete fixed-sample, noise, reconstruction, math, new-candidate, or
precision matrix through the
same headless path by replacing `--visibility-profile ... --visibility-benchmark`
with `--benchmark-sequence fixed-sample`, `noise`, `reconstruction`, `math`,
`new-candidates`, or `precision`.
Each entry is an isolated run with its own history reset and artifacts. The
`--benchmark-warmup`, `--benchmark-frames`, `--benchmark-output`, and
`--benchmark-auto-close` options apply to the whole sequence. **New AO
Candidates** runs Reference, Fixed 8, exact fusion, Fixed 8 plus fusion, both
Activision PS4 approximations, and all three XeGTAO High profiles. **XeGTAO
Precision Matrix** compares the LUT mixed-precision and LUT FP32 source-port
profiles without substituting the unrelated conservative FP32 filter experiment.
Use `--benchmark-sequence all` for a smoke pass over every implemented or
partial benchmark-control performance profile; unavailable profiles are skipped.
The UI also provides **Benchmark Current Profile**, **Cancel Benchmark**, and
**Export Benchmark Results**. Reference-versus-current, fixed-count, noise,
reconstruction, math, new-candidate, and precision actions run their entries
sequentially with per-entry history reset/export and exact starting-setting
restoration on completion, cancellation, or failure.
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
non-additive engineering forecasts. Its XeGTAO evidence is pinned to Intel
commit `a5b1686c7ea37788eeb3576b5be47f7c03db532c`; published Intel timings are
reported only as upstream provenance and never as UVSR measurements or promises.

The [visibility DXIL evidence](docs/visibility-dxil-evidence.md) provides a
reproducible static generated-shader comparison for the core Reference,
candidate, diagnostic, reconstruction, and fusion permutations. It does not
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
The bottom action row exposes equally sized **Reload Shaders**, **Reset Settings**,
**Restart**, and **Screenshot** buttons.

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
