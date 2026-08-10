# UVSR

**Unified Visibility Stochastic Rendering**

<!-- uvsr-codebase-size:start -->
**First-Party Lines of Code:** 88,228 non-blank source lines.

**Third-Party Lines of Code:** 387,603 non-blank source lines.

**Total Lines of Code:** 475,831 non-blank source lines.

Counts cover UVSR source, tests, tools, build scripts, retained pinned
dependency source, and final first-party dependency overrides. Documentation,
assets, licenses, binaries, and generated build output are excluded. Regenerate
with `tools/update_readme_line_counts.cmd --write`.
<!-- uvsr-codebase-size:end -->

UVSR is a focused DirectX 12 research renderer built on NVIDIA's pinned Donut
framework and NVRHI. It ships with five ready to run research scenes, a
production focused deferred PBR path, and several independently testable
visibility, anti aliasing, shadow, and denoising systems.

## Renderer Highlights

- **Unified Screen Space Visibility.** AO and one bounce indirect diffuse share
  a current frame stochastic traversal, exact runtime sample budget, and
  guide aware reconstruction. Both effects default to 16 samples and may emit
  physical hit distance for denoising. Distribution and occlusion controls
  extend through `8`, and diffuse illumination defaults to unit intensity.
- **Shared Precomputed Noise.** One Noise drawer provides Spatial White,
  Spatial Blue, and 64-layer Spatiotemporal Blue `R8_UNORM` textures at 64x64
  through 512x512. Effects inherit the centered global tile by default and may
  opt into isolated **Specify Noise** overrides. Finite flashlight shadows use
  the same global texture and animated sample clock.
- **Physically Grounded Deferred Lighting.** UVSR uses a packed G-buffer,
  material-aware direct lighting, shared contribution gates, Lambert-convolved
  SH9 diffuse IBL, roughness-prefiltered GGX specular IBL, and a split-sum
  environment BRDF. Optional, default-off median-luminance Auto Exposure has
  its own subsection with Exposure Compensation, Maximum Brightening, Maximum
  Darkening, and Adjustment Period controls. Disabled exposure selects the
  exact established texture-only AgX presentation path.
- **Explicit Ambient Fill Gate.** The legacy hemispherical ambient term is
  removed. The Sky drawer's Ambient Fill setting explicitly gates diffuse and
  specular IBL while preserving the selected environment background.
- **Ray Traced Visibility.** Sky visibility supports either a correlated ratio
  estimate or one scalar ray, and affects diffuse and specular IBL by default
  when enabled. The primary sun uses the same explicit ratio choice under
  **Ray Traced Shadows**. AO, GI, sky visibility, sun shadows, and flashlight
  shadows each expose an independent **Output Hit Distance** switch. Sun, sky,
  and flashlight ray queries honor alpha-tested cutouts while excluding blended
  and transmissive materials from binary visibility.
- **NVIDIA NRD Denoising.** The optional NRD 4.17.3 backend provides ReBLUR for
  AO, ReBLUR or ReLAX for GI and sky visibility, and separate SIGMA signal
  states for sun and flashlight shadows. Method, quality, and resolution remain
  independent for each effect. ReBLUR and ReLAX also expose history,
  disocclusion, and response. Sun SIGMA uses temporal stabilization, while
  flashlight SIGMA is spatial only. Raw rendering remains unchanged when NRD is
  not built or a required producer output is off.
- **Physical Flashlight.** The camera flashlight is one analytical spot
  light with an authored two lobe beam profile and full resolution ray traced
  visibility. Horizontal and vertical offsets span minus 40 through plus 40
  centimeters. Beam Size defaults to 16 degrees, Beam Roundness to `0.8`, and
  selectable full Angular Size to 2.86 degrees; its factory beam color is pure
  linear white. Positive size drives both its
  finite direct-light energy and a four-ray visible-emitter shadow estimate;
  zero is the exact point-light, hard-shadow branch. An emitter-aware collision
  sphere continuously sweeps the authored camera mount and repairs overlap when
  its radius changes, keeping the emitter outside nearby geometry. Collision
  may stop or slide the emitter, but scene depth never retracts the mount or
  retargets the beam. Lens sway remains direction-only. The rejected
  receiver-driven centering experiment is documented in
  [Flashlight Camera Centering v1](docs/postmortem/flashlight-camera-centering-v1.md).
- **Shared World Representation.** The Representation drawer owns the ray query
  BVH and one **Allow Ray Traversal** master switch. Turning it off stops sky,
  sun, and flashlight traversal without erasing their individual settings.
- **Focused Sun Lighting.** The primary directional sun defaults to irradiance
  `8` and angular size `0.2` degrees. Screen space directional shadows are
  quarantined with the CSM and SVSM experiments rather than packaged by main.
- **Composable Anti Aliasing.** TAA, Google Filament based Fast Approximate AA,
  CMAA2, and 2x through 16x deferred MSAA are independent, default off
  controls. When combined, MSAA resolves scene-linear lighting before TAA,
  tone mapping, display-linear Fast Approximate AA, and CMAA2. TAA exposes all
  five Filament camera-jitter sequences plus an experimental Sobol 32 sequence
  with stronger toroidal spacing than the matching Filament Halton prefixes.
- **Composable Debugging.** World appearance is independent from the
  Visibility and physically based lighting information filters. Shadow
  thread/wave isolation remains a deliberate full-image diagnostic.
- **Compact Runtime Surface.** The first party build compiles 306 shader
  permutations, for 382 integrated permutations after Donut's 76. Twelve
  Settings drawers and 184 command entries retain the active product controls
  without benchmark planners or dormant profiles.
- **Source Backed Optimization Decisions.** Retired shader families, rejected
  XeGTAO ports, Packed Depth and packed/fused ambient-occlusion-only paths,
  scheduler variants, and math approximations remain documented with controlled
  evidence instead of surviving as dormant runtime code.

The bundled **Sponza Decorated** scene includes Intel's separately distributed
curtains and ivy. **Sponza Plain** keeps the same flat-roof architecture without
those add-ons. **Bistro Interior** packages the Wine variant of Amazon
Lumberyard Bistro, and **San Miguel** packages the full high-detail San Miguel
2.1 model. **Classroom Interior** packages Christophe Seux's official Blender
Classroom demo under CC0 1.0 with its legacy Cycles
materials translated for UVSR's PBR path while retaining the source-authored
image textures. The deferred PBR path keeps mirrored double-sided instances
view-facing, so Classroom's duplicated doors and papers remain lit with
screen-space Visibility enabled. Its spawn-side wire bin and 13 crumpled-paper
objects are omitted, and eight source diagnostic UV-checker sheets use the
scene's blank-paper appearance. The large scenes use standard glTF external
buffers; Bistro and San Miguel are cut losslessly at buffer-view boundaries,
while Classroom needs only one buffer beneath the repository's 90 MB cutting
threshold. Every tracked file remains below GitHub's hard 100,000,000-byte
limit. CMake stages the explicit five-scene bundle; no separate model download,
conversion, or scene setup is required.

## Coming Soon

This section summarizes stable work that is active but not yet merged into
`main`. Experimental entries are not promises that the work will ship.

- **Main Lighting, Denoising, and Controls — In Development**
  (`codex/main-lighting-denoising-controls`). Integrates the ray traversal
  master switch, physical flashlight shadows, hit distance producers, ratio
  choices, alpha-tested visibility, NVIDIA NRD, shared precomputed noise,
  automatic display exposure, updated lighting defaults, and the loading
  progress estimate while removing production screen space directional shadows.
- **Screen Space Visibility Shared Shader Helpers — In Review**
  (`devin/1784102514-screen-space-shared-helpers`, PR #10). Consolidates shared
  depth, pixel coordinate, and safe normal helpers without changing equations,
  bindings, UI, or scenes.
- **Visibility Degenerate Path Test Coverage — In Review**
  (`devin/1784102780-visibility-test-coverage`, PR #11). Adds reference coverage
  for degenerate clipping, radial mask edge cases, and blue noise rank fields
  without changing runtime rendering.

## Build and Run

### Requirements

- Windows with a DirectX 12-capable GPU and current driver
- CMake 3.24 or newer
- Visual Studio with a C++17-capable MSVC toolchain
- Git with submodule support

Clone, configure, and build a Release executable from PowerShell:

```powershell
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
cmake -S . -B build
cmake --build build --config Release --target uvsr
```

If the repository was cloned without submodules, initialize them before
configuring:

```powershell
git submodule update --init --recursive
```

The first configure may download Microsoft's Direct3D 12 Agility SDK when it is
not already cached.

The default build keeps raw rendering available and leaves NRD processing off.
After reviewing NVIDIA's RTX SDK license, configure an NRD enabled build with
explicit acknowledgement:

```powershell
cmake -S . -B build-nrd -DUVSR_WITH_NRD=ON -DUVSR_ACCEPT_NRD_LICENSE=ON
cmake --build build-nrd --config Release --target uvsr
```

Launch the exact executable produced by that build:

```powershell
.\build\bin\uvsr.exe
```

The window title records the graphics API and source commit. Use the executable
inside an isolated build tree when validating an experimental branch.

At startup, UVSR selects the DirectX 12 adapter with the most dedicated video
memory. A different compatible adapter can be selected from **General >
Graphics Adapter**, which restarts the renderer on that device.

**General > Adaptive Sync** selects **Off**, **Vendor Agnostic**, or **Nvidia
Exclusive**. The enabled choices request UVSR's windowed DXGI
tearing compatible presentation path while VSync remains disabled; the
Nvidia Exclusive choice is available only on NVIDIA adapters. Windows, the
driver, and the display decide whether adaptive refresh actually engages.

### Useful Controls

- Press **Escape** or **~** to open or close Settings.
- Press **/** to open or close the command interface. Enter applies, Tab
  completes, Up/Down recalls history, and Escape cancels the active edit without
  closing the bar. Slash-separated tips appear inside the single-row input only
  while no command result is pending. Enter replaces them with a blue success
  or saturated-crimson error status; long output remains available through an
  explicit details button. The status disappears as soon as typing begins.
- Press **M** to inspect the editable material at the exact screen center or
  close the material editor.
- Press **F** to toggle the selected camera flashlight when the command bar is
  closed and text input is not active.
- Press **Z** or use **Zoom** to cycle through off, 2x, 3x, 4x, and 5x
  pixel inspection.
- Use **Capture** in the Settings footer to copy the current frame to the
  clipboard.
- Press **V** to level camera roll with an exponential overshoot-and-settle
  motion while preserving camera position and view direction. A stationary
  held trackpad gesture does not cancel the reset; real camera motion does.
- Use **Q** to move the camera up and **E** to move it down. Space and Shift are
  not vertical-motion bindings.
- Use **General > Camera Location > Position 1** for the standardized Sponza
  view.

**General > Interface Skin** selects **Amp** or **OG**. Amp is UVSR's authored animated
presentation and gives expanded Settings and its collapsed status block the
same neutral dark surface color and transparency as the command bar and
Materials panel. Their blurred backdrop retains scene light and detail but
removes scene color spill. The command bar uses the same zoom-and-fade language
as the other floating windows. OG uses stock ImGui widgets, square scrollbars
and zoom corners, a two-row performance summary, zoom-matched Settings and
command shadows, and no UI motion so automated experiments can configure UVSR
without waiting for presentation animations.
The Settings title uses the same resting blue, corner radius, and outline path
as its drawer headers. The slash interface always fills the complete
margin-to-margin width at the bottom.
The Materials panel uses the same stacked blue title and neutral blurred body,
with a translucent light drawer plate behind its editable controls. It keeps
the pixel-zoom panel's full resting width and follows only its animated lower
edge. Amp zooms and fades the panel itself; OG reaches each endpoint
immediately. Its title has no X button; **M** toggles the complete panel, and
the title triangle closes it through the same skin-specific presentation.
Settings stop one consistent margin above that permanently reserved command
lane, so opening, collapsing, or hiding either surface never makes them overlap
or changes the command width.

Settings always begin at factory defaults and are not persisted between
launches. The [advanced settings and developer workflows
guide](docs/advanced-settings.md) covers renderer controls, defaults, commands,
and the retained component build.

### Build and Test Everything

Build every registered Release target, then run the complete deterministic test
suite:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

This includes the scene, camera, PBR, AA/UI, screen space visibility, ray
traced shadows, denoising, world representation, environment, command line,
and runtime shader bundle contracts.

## Engineering Documentation

- [Advanced Settings and Developer Workflows](docs/advanced-settings.md) covers
  the interactive control surface, factory behavior, commands, and the retained
  specialist build.
- [PBR Foundation](docs/pbr-foundation.md) defines material inputs, G-buffer
  packing, lighting equations, IBL, contribution gates, validation, and
  extension points.
- [Screen Space Visibility](docs/screen-space-visibility.md) documents the
  shared AO/GI traversal, estimators, reconstruction, memory contracts,
  supported quality profiles, and validation boundary.
- [Heitz Ratio Estimator Shadows](docs/heitz-ratio-estimator-shadows.md)
  documents the single dispatch matched RGB estimator, current frame sampling,
  hard shadow path, ray origin safety, independent composition, shared
  BLAS/TLAS representation, and limits.
- [Noise](docs/noise.md) defines global inheritance, effect overrides,
  precomputed assets, centered sampling, temporal progression, and provenance.
- [Temporal Aliasing Options](docs/temporal-aa-options.md) defines temporal,
  fast approximate, morphological, and multisample composition, history
  behavior, and coordinate conventions.
- [Engine Cutdown Archive](docs/postmortem/engine-cutdowns/README.md) keeps the
  dated shader and renderer cutdown reports, their measurements, complete
  removal inventories, and restoration boundaries.
- [Visibility Estimator Validation](docs/visibility-estimator-validation.md)
  records the shared C++/HLSL measure contracts and deterministic fixtures.
- [GPU Clock Normalization](docs/performance/gpu-clock-normalization.md)
  describes an advisory same-GPU trend estimate while preserving raw clean-run
  GPU time as the official score.
- [Environment Catalog](assets/environments/README.md) lists imported HDR
  radiance sources, hashes, licenses, and calibrated default exposures.
- [Experiment Postmortems](docs/postmortem/) preserve retired work, negative
  results, reusable evidence, and explicit restart conditions.

## Licensing

- **Community Use.** UVSR's first-party code is source-available under the
  [Polyform Noncommercial License 1.0.0](LICENSE). Noncommercial use and
  modification are welcome; when sharing, preserve the license and Required
  Notice.
- **Commercial Use.** Commercial use or sublicensing requires a separate
  written agreement. [Contact the UVSR project](mailto:brockliddicoat@gmail.com).
- **Legal Details.** Third-party material remains under its own terms. See the
  [Legal Guide](legal/README.md) for the full scope, documentation registry,
  commercial-readiness notes, and contributor agreement.
