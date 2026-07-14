# UVSR

**Unified Visibility Stochastic Rendering**

UVSR is a DirectX 12 renderer built on NVIDIA's pinned Donut framework and its
NVRHI graphics abstraction layer. It launches the NVIDIA Bistro exterior scene
with its authored materials by default; the converted Bistro Interior Wine scene
is also available from the scene picker.

## Renderer Baseline

- Deferred shading, UVSR PBR, screen-space visibility AO/GI, and the procedural
  sky start enabled.
- NRA-RTAA v1 is retired and removed. Its failure analysis remains in the
  [NRA-RTAA v1 postmortem](docs/nra-rtaa-v1-postmortem.md) so a successor starts
  from a proven reprojection base instead of restoring the failed subsystem.
- Screen-space visibility traces AO/GI at selectable full, half, or quarter
  linear resolution. Full resolution can composite raw output; reconstruction
  adds SSRT3-style temporal accumulation and either compact or Gaussian joint-
  bilateral filtering. Spatial filtering can be disabled independently;
  reduced modes retain only a minimal depth/normal-guided 2x2 upsampler because
  raw grid expansion produces coherent GI streaks. Full resolution incurs no
  spatial dispatch or filter target while filtering is disabled. Reconstruction,
  temporal reconstruction, and spatial filtering all start disabled.
- **Adaptive Sparse Sampling** is off by default. The default fixed-work
  specialization traces one stochastic slice and **20 Fixed Samples / Pixel**
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
- The renderer selector provides **Deferred**, **Forward**, and **Forward
  Tonemapperless** modes. The tonemapperless mode renders with the forward path
  and sends its scene-linear HDR result directly to the sRGB display target,
  bypassing AgX, exposure, grading, LUTs, and dithering; out-of-range values are
  clipped by the display target.
- Renderer settings always start from factory defaults; **Reset Settings**
  restores those defaults in-session, and settings are not carried between
  launches.
- The HUD performance row reports resolution, frame time, FPS, current-clock
  memory bandwidth, and current-clock FP32 peak GFLOPS. Visibility statistics
  start collapsed and report **All**, **Trace**, **Filter**, and **Other** GPU
  timings on one row. Two memory rows report exact logical **Outputs**,
  **Working**, **Mask Cache**, and **Avoided** payloads; **Shared** is explicitly
  an estimate of duplicate mask payload avoided by shared AO/GI traversal.
- UVSR's shared forward/deferred metallic-roughness PBR path is always enabled
  in the production UI. The legacy Donut comparison path remains implemented
  for possible future experiments, but its control is hidden.
- The top **General** drawer contains renderer and performance information, GPU
  selection, camera mode, White World, and rendering-path selection, with the
  scene picker at the bottom.
- **White World Off** is the default. **White World On**, **White World Preserve
  Normals**, and **White World Preserve Emissives** override material color
  without modifying source assets. The last mode keeps authored emissive color
  alongside the scene's colored direct lights so GI sources remain easy to read.
- Camera controls are limited to **First Person** and **Third Person**.
- The first scene light is selected automatically in the **Lights** panel.
- **Emissive Source Gain** in **Visibility > Indirect Diffuse GI** globally
  scales how much light emissive materials contribute to GI without changing
  the visible emissive surfaces themselves. Raising it expands the visibly
  illuminated area up to the screen-space sampling radius.
- **Show GI-Only Lighting** is the sole retained visibility diagnostic. It
  displays the material-applied screen-space diffuse GI contribution without
  direct light, sky fallback, fallback specular, or AO-only darkening.
- **Bounces** in **Indirect Diffuse GI** selects one through four finite diffuse
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

Coming Soon is UVSR's shared coordination ledger for every project or feature
that has not merged into `main`, plus every project or feature an agent is
currently working on. An entry is not shipped on `main`, and experimental
entries are not promises that the work will merge.

- **Bilateral-Grid Local Tone Mapping — Active Development**
  (`agent/bilateral-grid-local-tone-mapping`). Add a first-party D3D12 GPU
  bilateral-grid analysis pass over the final scene-linear display source after
  visibility composition and before AgX, then apply one scalar local-EV
  correction inside the existing AgX display pass. This owns
  `src/local_tone_mapping*`, AgX bindings,
  display eligibility, reference tests, and its UI. It may extend the higher-
  bounce contribution cutoff conservatively without changing visibility
  estimator math or adding motion-reprojected local-exposure history.

### How Work Gets Listed

This README-first workflow is how every project or feature gets onto Coming
Soon:

1. Before writing implementation code, the owning agent reads this entire
   section into working memory and checks open pull requests, unmerged branches,
   and visible agent worktrees for overlapping or missing work. An agent
   resuming an in-flight project performs this reconciliation before its next
   code edit.
2. The agent imports every missing unmerged or current-agent project it finds.
   If its own project is absent, adding or updating that entry is its first
   repository change. Each entry includes status, branch when one exists,
   intended scope, affected subsystems, and integration dependencies.
3. The agent cites the exact Coming Soon entry in its implementation plan and
   records any overlap or coordination needed with another entry. Implementation
   does not begin until both the entry and plan reference exist.
4. Scope and status changes update the same entry. When publication is
   authorized, commit and push the coordination update before creating or
   pushing implementation commits so simultaneous agents can see it. For pre-
   existing implementation, reconcile and publish the ledger before the next
   code edit or implementation push. This rule does not grant permission to
   push by itself.
5. A merge removes the entry and moves durable user-facing behavior into the
   renderer baseline or design documentation. Abandoned work is removed
   explicitly rather than left as a stale promise.

## Build and Run

Requirements:

- Windows with a DirectX 12-capable GPU and driver
- CMake 3.24 or newer
- A C++17-capable Visual Studio toolchain
- Git submodules initialized

At startup, UVSR selects the D3D12-capable adapter with the most dedicated
video memory. The **Graphics Adapter** selector lists every compatible GPU and
restarts the renderer immediately on the selected adapter.

The Bistro source assets are licensed local content and are intentionally
gitignored. Before building, place these files in
`assets/scenes/nvidia_bistro/`:

```text
BistroExterior.glb
BistroInterior_Wine.glb
LICENSE.txt
README.txt
```

Configure and build a Release executable from PowerShell:

```powershell
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --config Release --target uvsr
.\tools\launch_uvsr.ps1 -Experiment "testing program title on task title"
```

The launcher requires a description and puts it in the window and task title:

```powershell
.\tools\launch_uvsr.ps1 -Experiment "testing program title on task title"
```

After building, Windows users can also double-click `LaunchUVSR.cmd`. It
delegates to the same required experiment launcher with a fixed main-build
label; optional renderer arguments can be appended from a terminal.

The title reports the active graphics API at runtime, for example
`UVSR Renderer D3D12 (testing program title on task title, 4:32 AM)`. The time
is captured when the experiment process launches and displayed in local time.
Direct and IDE-driven launches can instead supply the description through
`--experiment "description"` or the `UVSR_EXPERIMENT` environment variable.

The first configure may download Microsoft's Direct3D 12 Agility SDK if it is
not already cached.

Build and run the PBR, radial-visibility, estimator, visibility-projection, and
visibility-sampling reference tests separately:

```powershell
cmake --build build --config Release --target uvsr_pbr_tests uvsr_radial_visibility_tests uvsr_visibility_estimator_tests uvsr_visibility_projection_tests uvsr_visibility_sampling_tests
ctest --test-dir build -C Release --output-on-failure
```

## Bistro Material Preparation

UVSR renders Bistro materials two-sided because the FBX source contains thin
surfaces with mixed winding. The repair identifies the small set of base-color
images with real binary/fractional alpha, converts those materials to
depth-writing alpha test, and restores opaque domain for the exporter-mislabeled
remainder while preserving genuine Water/Ice/Wine blend materials. White World
samples only real coverage alpha and overrides RGB in a
shader permutation, so foliage cutouts survive without leaking albedo.

Some source conversions label packed roughness/metalness maps as specular
extension textures. Repair freshly converted GLBs once with:

```powershell
tools\repair_bistro_orm.cmd assets/scenes/nvidia_bistro/BistroExterior.glb assets/scenes/nvidia_bistro/BistroInterior_Wine.glb
```

The repair changes only GLB metadata, saves the original JSON chunk in a
`.pre-uvsr-json` file, and ignores the maps' zero-filled red channel so authored
material occlusion is not fabricated. Screen-space ambient visibility remains a
separate renderer input. Pass `--restore` to restore the original chunks.

The Bistro light is normalized from its exported real-world lux value to the
renderer's unoccluded-light range and uses a neutral illuminant so White World
does not inherit the source scene's strong amber cast. This scene-specific
calibration is applied to the light, outside the shared BSDF, and remains
editable in **Lights**.

## Documentation and Conventions

The [PBR foundation](docs/pbr-foundation.md) documents the material contract,
G-buffer packing, equations, validation, limitations, and extension points.

The [screen-space visibility design](docs/screen-space-visibility.md) documents
the shared 32-sector AO/GI traversal, resources, coordinate/radiance contracts,
controls, limitations, and the upgrade path to persistent unified visibility.

The [visibility estimator validation](docs/visibility-estimator-validation.md)
records the shared C++/HLSL measure contracts, deterministic reference fixtures,
and the boundary between automated evidence and required runtime evaluation.

The [NRA-RTAA v1 postmortem](docs/nra-rtaa-v1-postmortem.md) preserves why the
retired anti-aliasing experiment failed and the required order for any successor.

UVSR runs uncapped with a single planar view. UVSR-owned interactive controls
provide concise hover tooltips; new controls should follow the same convention.
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
