# UVSR

**Unified Visibility Stochastic Rendering**

<!-- uvsr-codebase-size:start -->
**First-Party Lines of Code:** 114,859 non-blank source lines.

**Third-Party Lines of Code:** 387,622 non-blank source lines.

**Total Lines of Code:** 502,481 non-blank source lines.

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
- **No Hidden Ambient Fill.** The legacy hemispherical ambient term is removed.
  With IBL disabled, shadowed regions contain only computed direct light and
  screen-space GI, so regions with neither can reach deep black rather than
  being cosmetically lifted.
- **Three Shadow-Rendering Research Paths.** Bend Studio screen-space shadows,
  UVSR sparse virtual shadow maps, and a conventional cascaded-shadow-map
  diagnostic each resolve an independent full-resolution visibility texture
  through a producer-neutral deferred-lighting interface. The SVSM path includes
  sparse residency, validated caching, localized invalidation, packet-page
  culling, page-safe filtering, and coarser-clipmap fallback.
- **Sample-Correct Anti-Aliasing Comparisons.** MiniEngine temporal
  reconstruction, CMAA2 morphology, and 2x through 16x deferred MSAA share one
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
- Press **M** to open or close the material editor; middle-click picks a scene
  material.
- Press **Z** or use **Zoom** to cycle through off, 2x, 3x, 4x, and 5x
  pixel inspection.
- Use **General > Camera Location > Benchmark Position 1** for the standardized
  1920x1080 Sponza view.

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

This includes the scene, camera, PBR, AA/UI, screen-space visibility, Bend,
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
- [Anti-Aliasing Options](docs/miniengine-taa-options.md) defines temporal,
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
