# Advanced Settings and Developer Workflows

This guide is the detailed companion to the project README. It describes the
interactive renderer controls, factory behavior, benchmark entry points,
developer-only overrides, and specialist build modes without turning the
README into a control-by-control manual.

Renderer settings always start from factory defaults. **Reset** restores those
defaults in the current session, and settings are not persisted between
launches.

## General, Scenes, and Camera

The **General** drawer contains **Graphics Adapter**, **Camera Mode**, and
**Camera Location**. **World Materials** contains the White World presentations
and the **Indirect Diffuse Response** diagnostic. **World Scenes** owns the
scene picker.

At startup, UVSR selects the DirectX 12-capable adapter with the most dedicated
video memory. Selecting another compatible adapter restarts the renderer on that
device.

The bundled **PBR Sponza Decorated** and **PBR Sponza Plain** scenes open at
**Benchmark Position 1**, the
`intel-pbr-sponza-courtyard-simplified-v1` camera preset. The preset uses a
60-degree perspective view and a 1920x1080 reference frame. Moving or rotating
the camera changes **Camera Location** to **Piloted**; selecting **Piloted**
detaches the preset name without changing the view.

**Camera Mode** provides:

- **Freelook:** Mouse and arrow keys rotate; A/D strafe; W/S dolly; Space moves
  upward; either Shift key moves downward; X/C roll; V levels roll; and the
  wheel applies a small damped dolly. The camera uses collision and smooth
  acceleration and deceleration.
- **Locked:** Freezes the current view. Automated benchmark launches select
  this mode.

Right-click remains camera input. Middle-click performs material picking, and
**M** opens or closes the material editor. Selecting a material does not open
the editor automatically.

**White World Off** is the default. **White World On**, **White World Preserve
Normals**, and **White World Preserve Emissives** override material color
without changing source assets. **Indirect Diffuse Response** shows only the
material-applied screen-space diffuse GI contribution. It excludes direct
light, diffuse IBL, and AO-only darkening.

## Screen-Space Visibility and PBR

The default deferred path enables UVSR PBR, screen-space visibility AO/GI, and
the matched environment background. **Visibility > Enabled** controls
visibility and the UVSR PBR path together. AO and GI remain independent
consumers: setting one effect's strength to zero can release its resources while
the other continues.

### Visibility Profiles

The top-level **Profile** selector contains only product-facing Low, Medium,
High, and Ultra recipes. Editing resolution, estimator, AO, GI, or buffer
formats changes the label to an origin-preserving custom state such as
**Medium (Custom)**.

| Profile | Sampling Resolution | Samples | Spatial Path | Buffer Precision |
| --- | --- | ---: | --- | --- |
| Low | Quarter linear resolution | 8 | Compact joint-bilateral upsampling | Performance |
| Medium | Half linear resolution | 8 | Compact joint-bilateral upsampling | Performance |
| High | Full resolution | 20 | Unreconstructed full-resolution input | Performance |
| Ultra | Full resolution | 48 | Unreconstructed full-resolution input | Default |

Low uses Uniform Projected Angle. Medium and the factory-default High profile
use Uniform Solid Angle. Ultra enables two diffuse GI bounces. Every profile
uses the first-party Toroidal Blue rank field.

### Sampling and Estimation

**Sampling Resolution** selects full, half, or quarter linear resolution.
Full-resolution visibility can composite unfiltered current output without a
spatial reconstruction dispatch or filter target. Reduced-resolution modes use
guide-aware reconstruction.

**Estimator** selects:

- **Uniform Projected Angle**
- **Uniform Solid Angle**, the factory default
- **Cosine-Weighted Solid Angle**, implemented with the complete joint-cosine
  CDF, projected-slice mass, `pi` GI normalization, and no duplicate receiver
  cosine

**Noise Pattern** compares **Independent Hash** with first-party **Toroidal
Blue**. Both are selected by a frame-coherent runtime-uniform branch in the
shared Runtime shader. The previous packed and unpacked offline-noise assets,
uploads, bindings, and shader families are retired.

One exact sample budget is shared by AO and every GI bounce. **Samples** exposes
the complete 1 through 64 range directly. There is no sampling-mode dropdown or
separate Fixed and Generic shader family. **Distribution** stays with
Estimator, Noise Pattern, Samples, Radius, and Thickness in **Shared Visibility
Sampling**.

Adaptive sparse sampling, feedback resources, and a separate later-bounce
sample count are intentionally absent.

### Spatial Reconstruction and Precision

**Spatial Reconstruction** provides unreconstructed full-resolution input,
guide-aware reduced-resolution upsampling, retained Intel edge-guided methods,
and legacy joint-bilateral comparison methods. AO-only fused final-application
paths are explicitly labeled **(Mutex GI)** when GI cannot share them.

The separate **Buffers** drawer owns precision and resource-format choices.
Changing a profile-owned buffer format clears the product preset label rather
than silently keeping a recipe that no longer matches the renderer.

### Indirect Lighting Controls

**Limit Bounces** starts enabled. While enabled, **Bounces** selects one through
eight finite diffuse bounces; one bounce keeps the compact shader path.

Disabling **Limit Bounces** enables GPU-driven contribution termination. Each
later bounce transports only the newest light frontier, and the continuation
threshold becomes four times stricter after every bounce. A wave-coalesced GPU
flag and indirect dispatch turn every pass after convergence into zero work. A
16-bounce guard contains malformed or non-contracting data; it is not the
normal stop condition.

**Bounce Contribution Cutoff** defaults to `0.001`. It skips source shading
whose conservative exposed upper bound cannot make a meaningful contribution.
Zero retains only exact-zero exits in explicitly limited mode.

Authored emissive radiance remains visible in forward, deferred, MSAA, and
G-buffer rendering, but it is not classified, boosted, or transported as a
screen-space GI source. First-bounce diffuse transport comes from shadowed
direct diffuse and directly reflected diffuse environment lighting.

**AO Power** defaults to its identity value of `1.0`. The default compositor
uses a specialization with the power operation compiled out; changing the
value selects the powered specialization.

Visibility-owned temporal accumulation is not exposed. Renderer TAA owns
temporal stability in the current build.

## Anti-Aliasing

The **Aliasing** drawer exposes **Enabled**, **Method**, **Quality**, **History
Frames**, **History Strength**, **Dejitter**, and **Sharpness**.

### Methods and Quality

- **Temporal Reconstructive** uses MiniEngine temporal history, motion and
  jitter conventions, reverse-Z validation, disocclusion rejection, and
  rectification.
- **Conservative Morphological** uses CMAA2 as a spatial presentation method.
- **Multisample Reference** provides Low 2x, Medium 4x, High 8x, and Ultra 16x
  deferred MSAA.

Deferred MSAA preserves every G-buffer sample through material decode and PBR
lighting, then averages final RGBA16F radiance. Screen-space visibility selects
one coherent closest reverse-Z surface per pixel and coverage-weights only that
surface's signed correction back into the per-sample result.

Temporal and Multisample presets leave CMAA2 off by default, so neither hides an
extra full-screen morphology pass. CMAA2 can be selected after MSAA resolve or
as temporal presentation morphology.

Temporal quality bundles progress as follows:

| Quality | Reconstruction | Inherited History Frames | Additional Behavior |
| --- | --- | ---: | --- |
| Low | 1x Bilinear | 3 | Pair Tristimulus rectification |
| Medium | 1x Bilinear | 6 | Pair Tristimulus rectification |
| High | 1x Bicubic | 9 | Higher-quality reconstruction |
| Ultra | 5x Bicubic | 12 | Dejitter enabled |

**9x Bicubic** is also available as the complete nine-bilinear-tap Catmull-Rom
reconstruction. **History Frames** accepts 1 through 32 prior frames for
long-term temporal methods and reports no history for spatial methods.
**History Strength** accepts 0% through 200%; values above 100% reinforce only
history that has already passed every validity gate.

**Aliasing Algorithm Configuration** exposes Subpixel Morphology, Motion Source,
Reconstruction, and Rectification. Rectification retains Pair Tristimulus and
Variance YCoCg. Sharpness is disabled by default for every quality. Stable
Interior and both per-pixel rectification modes are retired.

Effective temporal image or history-layout changes reset history once.
Presentation-only CMAA2, Sharpness, and image-equivalent execution changes do
not. Forward and legacy shading leave temporal AA unavailable because they do
not produce the required motion contract. Visibility Temporal Reconstruction
remains mutually exclusive until both histories share one jitter convention.

The complete coordinate, reset, quality, and motion-test contract is in
[Anti-Aliasing Options](miniengine-taa-options.md).

## Shadow Visibility Producers

Bend screen-space shadows, Sparse Virtual Shadow Maps, and Diagnostic Cascaded
Shadow Maps are independent, initially disabled directional-light visibility
producers. Each resolves a full-resolution linear `R8_UNORM` texture. Deferred
lighting multiplies complete visibility factors only for the exact
pointer-identical light.

The primary directional sun, `sun_1`, is selected automatically. When
**Lights** is opened, all three producer drawers start expanded with their
**Enabled** toggles off. **Lights** itself remains closed at launch.

### Bend Screen-Space Shadows

Bend shadows require deferred UVSR PBR and a primary directional light. UVSR's
adapter consumes the existing single-sample reverse-Z depth buffer while
keeping Bend Studio's released CPU and GPU headers byte-for-byte unchanged.

| Profile | Length | Shared Defaults |
| --- | ---: | --- |
| Performance | 60 pixels | 4 hard samples, 8 fade-out samples |
| Balanced | 240 pixels | 4 hard samples, 8 fade-out samples |
| Quality | 960 pixels | 4 hard samples, 8 fade-out samples |

All three restore `0.005` surface thickness, `0.02` bilinear threshold,
contrast `4`, and optional modes and diagnostics off without changing
**Enabled**. **Length** selects compiled `SAMPLE_COUNT` variants of 60, 120,
240, 480, or 960 pixels.

Custom settings include **Surface Thickness**, **Bilinear Threshold**, **Shadow
Contrast**, compiled **Hard Shadow Samples** and **Fade-Out Samples**, **Ignore
Edge Pixels**, **Precision Offset**, **Bilinear Offset Mode**, and **Early
Out**. **Debug View** presents Bend's Edge, Thread, or Wave output as grayscale
visibility.

The vendored integration boundary is recorded in
[Bend Screen-Space Shadows](../third_party/bend_sss/README.md).

### Sparse Virtual Shadow Maps

The SVSM path exposes product profiles plus independent controls for sparse
marking, resolution policy, filter kernel, cache behavior, invalidation,
submission, culling, raster, and page-safe translation reuse.

| Profile | Filter | Taps | Resolution Policy |
| --- | --- | ---: | --- |
| Performance | Adaptive page-safe nearest-Poisson | 8 | Global `+1`, moving light `+2`, receiver clamp |
| Balanced | Page-safe bilinear PCF | 4 | No global bias, moving light `+1`, receiver clamp |
| Quality | Page-safe bilinear PCF | 8 | No bias and no receiver clamp |

All three use the validated cache, static zero-work path, packet culling,
batching, sorting, and empty-work skips. Unsupported, missing, invalid, dirty,
over-budget, or out-of-range samples fall back to a valid coarser clipmap and
then to white.

The normal surface retains **Profile**, **First Clipmap Extent**, **Maximum
Light Depth**, filter and taps, **Adaptive Filtering**, global resolution bias,
and receiver-distance clamping. **Developer Options** contains:

- **Resources and Cache Policy**
- **Movement and Invalidation**
- **Culling and Raster**
- **Unabstracted**

**Diagnostics** owns benchmarks, detailed stage timing, debug views, and
counters. **Dense Reference** backs all six 8192-square atomic-depth clipmaps
and can require about 1.5 GiB; use it only for validation.

### Diagnostic Cascaded Shadow Maps

Diagnostic CSM is a conventional UE5-style comparison path, not UVSR's
preferred shadow renderer. It supports one through four cascades, opaque and
alpha-tested casters, D16 or D32 depth, manual Gather4 filtering, conservative
caster culling, and whole-map, per-cascade, dirty-rectangle, or integer-texel
scrolling cache updates.

Profiles include **Single-Map Reference**, **Low-Cost CSM**, **UE5 CSM
Reference**, **Cached Single Shadow**, **Optimized Cached Single Shadow**,
**Optimized Cached CSM**, and **Custom**.

The normal surface retains **Enabled**, **Profile**, **Cascades**, **Resolution
Per Cascade**, **Maximum Shadow Distance**, **Maximum Light Depth**, **Filter**,
**Filter Taps**, and **Filter Radius**. **Developer Options** contains:

- **Projection and Bias**
- **Cache Update Policy**
- **Culling and Raster**
- **Unabstracted**

**Diagnostics** owns paired setup, culling, update, raster, and sampling timing,
SVSM match checks, debug views, and live statistics.

## Sky and Image-Based Lighting

Forward, deferred, and screen-space composition share one persistent global
environment. One scene-linear radiance field produces the Lambert-convolved SH9
`E / pi` diffuse cube, roughness-prefiltered GGX specular cube, and optional
visible background. A source-independent split-sum BRDF LUT completes the
specular receiver.

The factory environment is Poly Haven's CC0 **Day - Kloppenheim 03** at its
calibrated `-2.75 EV`, with diffuse and specular IBL enabled at `1.00`
strength. Common exposure preserves the relationship among diffuse IBL,
specular IBL, and background. Independent strengths apply after exposure;
diffuse strength also scales the environment contribution entering SSGI.

The source menu contains six imported HDR radiance fields: three day or
overcast skies, Quadrangle Cloudy, and two night sources. Selecting a source
applies its calibrated default EV. A missing or invalid asset deactivates
environment lighting and background instead of retaining stale data or using an
ambient fallback.

See the [Environment Catalog](../assets/environments/README.md) for source
provenance, hashes, licenses, and calibrated exposures.

UVSR uses a fixed neutral AgX display transform. The retired Tonemapper drawer,
grading presets, LUT loader, and film looks are preserved with their paired
revival contract in the [Tonemapper Drawer and LUTs v1
Postmortem](postmortem/tonemapper-drawer-and-luts-v1.md).

## Settings Interface and Inspection

Settings launch hidden. The first Escape press opens **General** while
Visibility, Buffers, Statistics, Aliasing, Sky, and Lights remain collapsed.
The renderer summary and first performance line remain pinned above the
independently scrolling settings body.

The performance line reports resolution, submitted triangles, current-clock
memory bandwidth, utilization-adjusted current-clock FP32 throughput, frame
time, and FPS. One synchronized snapshot updates renderer, visibility, and
temporal-AA statistics 24 times per second.

The footer provides **Reset**, **Screenshot**, **Zoom**, and **Restart**.
Pressing **Z** or **Zoom** cycles off, 2x, 3x, 4x, and 5x. Pixel zoom copies the
untouched scene before UI composition and uses integer source-texel loads, so
the inspected texel becomes an exact 2x2, 3x3, 4x4, or 5x5 destination group.
Benchmark runs suspend the feature, and disabled zoom submits no capture or
composite work.

The complete UI hierarchy, animation, input-transaction, copy, and verification
contract is in the [UI Design and Integration
Reference](ui-integration-agent-procedure.md).

## Statistics and Benchmarks

The **Statistics** drawer can isolate the Complete Renderer, Geometry, Direct
Lighting, Screen-Space Visibility, Anti-Aliasing, Material Picking, Environment
Background, Tone Mapping, or Output Blit.

Visibility expands into its outer effect envelope, named-stage total, signed
unattributed difference, depth preparation, first trace, later bounces,
reconstruction, upsampling, resolve or application, and composition. Memory
rows report exact **Outputs**, **Working**, **Mask Cache**, and **Avoided**
payloads; **Shared** estimates duplicate mask payload avoided by shared AO/GI
traversal.

**Run Current** measures the effective visibility configuration being rendered.
**Run Current With Motion** executes the current AA configuration through the
fixed Benchmark Position 1 warm, right-45-degree sweep, hold, and return path.
The turn advances exactly `0.375` degrees per rendered frame. The path is
uncapped, so GPU speed changes wall-clock duration without changing its 256
measured camera samples.

### Benchmark Launch

Use the standard launcher with `--benchmark-camera` to select and lock Benchmark
Position 1, enforce a non-resizable 1920x1080 backbuffer, and block fullscreen
transitions:

```powershell
.\tools\launch_uvsr.ps1 -Experiment benchmark --benchmark-camera
```

Run a visibility profile without UI and close after the timing summary:

```powershell
.\tools\launch_uvsr.ps1 -Experiment benchmark --benchmark-camera `
  --visibility-profile runtime-ao-8t --visibility-benchmark `
  --benchmark-warmup 120 --benchmark-frames 240 --benchmark-auto-close
```

Profile matching ignores punctuation and case. `--benchmark-warmup` accepts 0
through 100000 frames; `--benchmark-frames` accepts 1 through 100000. Invalid or
unavailable settings return a nonzero process exit code without opening a modal
dialog.

Add `--visibility-contribution-terminated-bounces` to a GI-capable profile to
disable **Limit Bounces** before an automated run. The result is no longer that
profile's declared bounce contract, so the renderer deliberately clears the
one-click verification label.

Production builds also accept `--aa-enabled`, `--aa-method`, `--aa-quality`
(`--aa-preset` alias), `--aa-sharpness`, `--aa-rectification`, and
`--aa-benchmark-output`. Rectification accepts `preset`, `pair-rgb`, or
`variance-ycocg`.

For controlled performance methodology and the optional same-GPU clock trend,
read [GPU Clock Normalization](performance/gpu-clock-normalization.md). Raw
clean-run GPU time remains the official score.

## Developer Anti-Aliasing Overrides

Experimental AA execution-path overrides require a developer build:

```powershell
cmake -S . -B build-aa-dev -DUVSR_AA_DEVELOPER_OVERRIDES=ON
cmake --build build-aa-dev --config Release --target uvsr
```

A production build rejects `--aa-execution`, `--aa-kernel`, `--aa-lds`,
`--aa-reuse`, `--aa-early`, `--aa-fusion`, and `--aa-cache` when the requested
static PSO is absent.

## Factory-Settings Experiment Build

Repeated code experiments that use only the shader topology selected by a fresh
factory-default launch can use a separate opt-in build:

```powershell
cmake -S . -B build-experiment `
  -DUVSR_DEFAULT_SETTINGS_EXPERIMENT_SHADERS=ON
cmake --build build-experiment --config Release --target uvsr
.\tools\launch_uvsr.ps1 -Experiment testing `
  -BuildDirectory build-experiment
```

This configuration compiles 51 UVSR permutations and stages 37 runtime shader
blobs, compared with 516 first-party compile tasks and 76 blobs in the complete
production build. It locks renderer-topology drawers to factory settings and
omits Bend, SVSM, diagnostic CSM, CMAA2, non-default visibility, and developer
shader families. A fresh tree still compiles Donut's pinned 76-task framework
catalog once.

Use this mode to shorten repeated UVSR edits, not for release, full-settings,
diagnostic, or benchmark-matrix verification.

## Component-Only Builds

Bend, SVSM, and diagnostic CSM are separate static-library targets. A
component-only configuration can omit the application, other producers, and
their reference tests.

Build only SVSM and its deterministic reference test:

```powershell
cmake -S . -B build-svsm -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS=OFF -DUVSR_BUILD_DIAGNOSTIC_CASCADED_SHADOW_MAPS=OFF
cmake --build build-svsm --config Release --target uvsr_sparse_virtual_shadow_maps uvsr_sparse_virtual_shadow_map_tests
```

Build only Bend screen-space shadows and its deterministic reference test:

```powershell
cmake -S . -B build-bend -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS=OFF -DUVSR_BUILD_DIAGNOSTIC_CASCADED_SHADOW_MAPS=OFF
cmake --build build-bend --config Release --target uvsr_bend_screen_space_shadows uvsr_bend_screen_space_shadow_tests
```

Build only diagnostic CSM and its deterministic reference test:

```powershell
cmake -S . -B build-csm -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS=OFF -DUVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS=OFF
cmake --build build-csm --config Release --target uvsr_diagnostic_cascaded_shadow_maps uvsr_diagnostic_cascaded_shadow_map_tests
```

The full application integrates all three producers. Build every registered
Release target and run the complete suite with:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
