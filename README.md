# UVSR

**Unified Visibility Stochastic Rendering**

<!-- uvsr-codebase-size:start -->
**First-Party Lines of Code:** 136,621 non-blank source lines.

**Third-Party Lines of Code:** 388,222 non-blank source lines.

**Total Lines of Code:** 524,843 non-blank source lines.

Counts cover UVSR source, tests, tools, build scripts, retained pinned
dependency source, and final first-party dependency overrides. Documentation,
assets, licenses, binaries, and generated build output are excluded. Regenerate
with `tools/update_readme_line_counts.cmd --write`.
<!-- uvsr-codebase-size:end -->

UVSR is a focused DirectX 12 research renderer built on NVIDIA's pinned Donut
framework and NVRHI. It ships with two ready-to-run Intel PBR Sponza scenes, a
production-oriented deferred PBR path, and several independently testable
visibility, anti-aliasing, and shadow-rendering systems.

## Renderer Highlights

- **Unified Screen-Space Visibility.** AO and multi-bounce diffuse GI share one
  stochastic 32-sector traversal, exact sample budget, depth hierarchy, and
  directional visibility result. Reduced-resolution modes use guide-aware
  reconstruction, while full-resolution rendering can bypass spatial filtering
  and composite the current result directly.
- **GPU-Driven Indirect-Light Convergence.** Higher diffuse bounces transport
  only the newest light frontier. A wave-coalesced activity flag and indirect
  dispatch turn the remaining chain into zero work after scene-wide
  convergence, without a CPU readback. Resources for AO, GI, history, filtering,
  and later bounces exist only while their consumers need them.
- **Physically Grounded Deferred Lighting.** UVSR uses a packed G-buffer,
  material-aware direct lighting, shared contribution gates, Lambert-convolved
  SH9 diffuse IBL, roughness-prefiltered GGX specular IBL, and a split-sum
  environment BRDF. A fixed neutral AgX transform converts scene-linear HDR
  radiance for display.
- **Explicit Ambient Fill Gate.** The legacy hemispherical ambient term is
  removed. The Sky drawer's Ambient Fill setting explicitly gates diffuse and
  specular IBL while preserving the selected environment background.
- **Three Shadow-Rendering Research Paths.** UVSR Screen-Space Directional
  Shadows, sparse virtual shadow maps, and a conventional cascaded-shadow-map
  diagnostic each resolve an independent full-resolution visibility texture
  through a producer-neutral deferred-lighting interface. The SVSM path includes
  sparse residency, validated caching, localized invalidation, packet-page
  culling, page-safe filtering, and coarser-clipmap fallback.
- **Sample-Correct Anti-Aliasing Comparisons.** UVSR temporal reconstruction,
  CMAA2 morphology, and 2x through 16x deferred MSAA share one
  settings and benchmark surface. MSAA preserves every G-buffer sample through
  material decode and lighting before resolving final HDR radiance.
- **Built-In Measurement and Inspection.** GPU timings are separated by effect
  and visibility stage, logical and avoided memory payloads are reported
  independently, standardized motion and static benchmarks use a locked Sponza
  camera, and the pixel zoom view uses exact integer texel replication.
- **Source-Backed Optimization Decisions.** Retired shader families, rejected
  XeGTAO ports, packed-edge paths, scheduler variants, and math approximations
  remain documented with controlled evidence instead of surviving as dormant
  runtime code.

The bundled **PBR Sponza Decorated** scene includes Intel's separately
distributed curtains and ivy. **PBR Sponza Plain** keeps the same flat-roof
architecture without those add-ons. CMake stages both scenes; no separate model
download, conversion, or scene setup is required.

## Coming Soon

This section summarizes stable work that is active but not yet merged into
`main`. Experimental entries are not promises that the work will ship.

- **Screen-Space Visibility Shared Shader Helpers — In Review**
  (`devin/1784102514-screen-space-shared-helpers`, PR #10). Consolidates shared
  depth, pixel-coordinate, and safe-normal helpers without changing equations,
  bindings, UI, or scenes.
- **Visibility Degenerate-Path Test Coverage — In Review**
  (`devin/1784102780-visibility-test-coverage`, PR #11). Adds reference coverage
  for degenerate clipping, radial-mask edge cases, and blue-noise rank fields
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

Launch through the experiment wrapper so the window title records the source
commit, launch time, and a one-word experiment label:

```powershell
.\tools\launch_uvsr.ps1 -Experiment main
```

The label must be one lowercase ASCII word. `LaunchUVSR.cmd` provides the same
launcher with a fixed main-build label for double-click launches. Use
`-BuildDirectory <path>` to launch an isolated build tree.

At startup, UVSR selects the DirectX 12 adapter with the most dedicated video
memory. A different compatible adapter can be selected from **General >
Graphics Adapter**, which restarts the renderer on that device.

### Useful Controls

- Press **Escape** to open or close Settings.
- Press **/** to open or close the command interface. Enter applies, Tab
  completes, Up/Down recalls history, and Escape cancels the active edit without
  closing the bar.
- Press **M** to inspect the editable material at the exact screen center or
  close the material editor.
- Press **F** to toggle the selected camera flashlight when the command bar is
  closed and text input is not active.
- Press **Z** or use **Zoom** to cycle through off, 2x, 3x, 4x, and 5x
  pixel inspection.
- Press **V** to level camera roll with an exponential overshoot-and-settle
  motion while preserving camera position and view direction.
- Use **General > Camera Location > Benchmark Position 1** for the standardized
  1920x1080 Sponza view.

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
guide](docs/advanced-settings.md) covers renderer controls, defaults, benchmark
flags, developer overrides, and specialist build modes.

### Build and Test Everything

Build every registered Release target, then run the complete deterministic test
suite:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

This includes the scene, camera, PBR, AA/UI, screen-space visibility,
Screen-Space Directional Shadows,
diagnostic CSM, SVSM, environment, command-line, benchmark, and runtime shader
bundle contracts.

## Engineering Documentation

- [Advanced Settings and Developer Workflows](docs/advanced-settings.md) covers
  the full interactive control surface, factory behavior, command-line
  benchmarks, developer overrides, and specialist build modes.
- [PBR Foundation](docs/pbr-foundation.md) defines material inputs, G-buffer
  packing, lighting equations, IBL, contribution gates, validation, and
  extension points.
- [Screen-Space Visibility](docs/screen-space-visibility.md) documents the
  shared AO/GI traversal, estimators, reconstruction, memory contracts,
  performance profiles, and runtime evidence.
- [Temporal Anti-Aliasing Options](docs/temporal-aa-options.md) defines temporal,
  morphological, and multisample quality bundles, history behavior, coordinate
  conventions, and motion benchmarks.
- [AO Optimization Ledger](docs/ao-optimization-ledger.md) records implemented
  and rejected optimization candidates, controlled measurements, quality
  boundaries, and source provenance.
- [Shader Path Retirement Postmortem](docs/postmortem/shader-path-retirements.md)
  records removed shader families, the multiplication mechanisms behind their
  cost, supporting evidence, and restoration boundaries.
- [Visibility Estimator Validation](docs/visibility-estimator-validation.md)
  records the shared C++/HLSL measure contracts and deterministic fixtures.
- [GPU Clock Normalization](docs/performance/gpu-clock-normalization.md)
  describes an advisory same-GPU trend estimate while preserving raw clean-run
  GPU time as the official score.
- [Environment Catalog](assets/environments/README.md) lists imported HDR
  radiance sources, hashes, licenses, and calibrated default exposures.
- [Experiment Postmortems](docs/postmortem/) preserve retired work, negative
  results, reusable evidence, and explicit restart conditions.

## Project Boundaries

- DirectX 12 is the production backend; DirectX 11 and Vulkan are disabled.
- Deferred UVSR PBR is the primary path. Forward and legacy shading remain
  comparison paths and do not provide the complete temporal motion contract.
- The renderer is uncapped, single-view, and opaque-focused. VSync, stereo,
  bloom, translucent rendering, and animation playback are intentionally out of
  scope.
- IBL uses one infinite global environment. Local probe capture,
  parallax-corrected probe volumes, and probe blending are not implemented.
- The conventional CSM path is a diagnostic comparison, not the preferred
  production shadow renderer.

The executable, repository slug, package names, and paths use lowercase
`uvsr`; the displayed product name is uppercase **UVSR**.
