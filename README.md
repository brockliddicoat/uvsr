![UVSR Engine banner](assets/branding/uvsr-banner.png)

# UVSR

**Unified Visibility Stochastic Rendering Engine**

[![License: Polyform Noncommercial](https://img.shields.io/badge/license-polyform_noncommercial-8250DF?style=flat-square)](LICENSE.md)
[![Engineering Docs](https://img.shields.io/badge/docs-engineering-238636?style=flat-square&logo=readthedocs&logoColor=white)](#engineering-documentation)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![HLSL](https://img.shields.io/badge/HLSL-shaders-C62828?style=flat-square&logo=microsoft&logoColor=white)

UVSR is a focused C++17 and HLSL renderer for real-time visibility, lighting,
and anti-aliasing research. The current product uses DirectX 12, ImGui, and
pinned NVIDIA Donut and NVRHI dependencies.

This revision builds the renderer as `uvsr.exe`. Its public launcher installs
UVSR by building renderer source locally; it does not install a prebuilt
renderer package.

<!-- uvsr-codebase-size:start -->
**First-Party Lines of Code:** 131,798 non-blank source lines.

**Third-Party Lines of Code:** 386,156 non-blank source lines.

**Total Lines of Code:** 517,954 non-blank source lines.

Counts cover UVSR source, tests, tools, build scripts, retained pinned
dependency source, and final first-party dependency overrides. Documentation,
assets, licenses, binaries, and generated build output are excluded. Regenerate
with `tools/update_readme_line_counts.cmd --write`.
<!-- uvsr-codebase-size:end -->

## Renderer Highlights

- A deferred PBR path combines a packed G-buffer, material-aware lighting,
  diffuse and specular image-based lighting, automatic exposure, and AgX
  display mapping.
- Screen-space visibility provides ambient occlusion and one-bounce diffuse
  illumination. The current tree also contains Realtime Path Tracer, Reservoir
  Path Tracer, and Reservoir Indirect Lighting modes.
- Shared inline ray queries support sun shadows, ray-traced sky visibility, and
  the flashlight's finite-emitter shadows.
- Deferred 2x, 4x, 8x, and 16x MSAA can be combined with temporal AA and the
  current spatial anti-aliasing options.
- White noise, blue noise, and 64-layer spatiotemporal blue-noise assets support
  per-effect sampling. Six HDR environments provide sky lighting.
- Five staged scenes are available: Sponza Decorated, Sponza Plain, Bistro
  Interior, San Miguel, and Classroom Interior.
- ImGui exposes renderer settings, commands, timing, buffer inspection, and
  lighting and visibility diagnostics.

## Requirements

The supported public path is 64-bit Windows 11. A manual build needs:

- a hardware DirectX 12 adapter supporting Shader Model 6.5 and a current
  driver; ray-query effects additionally require DXR 1.1 support;
- CMake 3.24 or newer;
- Visual Studio 2022 with the Desktop development with C++ workload and a
  Windows SDK; and
- Git with submodule support.

UVSR activates its packaged Direct3D Agility runtime. The first configure may
download pinned Microsoft graphics dependencies.

## Build and Test

From PowerShell:

```powershell
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\bin\uvsr.exe
```

CMake stages the executable, compiled shaders, scenes, environments, fonts, and
notices under `build\bin`. See
[Advanced Settings](docs/advanced-settings.md) for build variants, controls,
commands, and the complete validation workflow.

## Launcher

UVSR Launcher is a self-contained Windows 11 x64 frontend for install, update,
launch, repair, and uninstall. It downloads verified prerequisites and source,
then builds an exact public `main` revision on the user's computer. Visual
Studio Build Tools can require several gigabytes and administrator approval.

<!-- uvsr-launcher-download:start -->
> [Download Unsigned UVSR Launcher v1.1.14 (unsigned; signed-feed updates)](https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v1.1.14/UVSR-Launcher-Windows-11-x64.exe)
>
> [SHA-256 checksum](https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v1.1.14/UVSR-Launcher-Windows-11-x64.exe.sha256)
>
> Not Authenticode-signed. Windows may warn. Launcher updates are authorized by UVSR’s pinned signed feed and exact immutable release identity.
<!-- uvsr-launcher-download:end -->

The bootstrap remains unsigned, so an authenticated update feed does not remove
Windows' unknown-publisher warning. Use the exact current download above rather
than an older bootstrap. The [launcher guide](launcher/README.md) documents its
source-build flow, trust boundary, recovery behavior, and development checks.

## Engineering Documentation

- [Screen-Space Visibility](docs/screen-space-visibility.md) and
  [Ratio Estimation](docs/ratio-estimation.md) define visibility and shadow
  algorithms.
- [PBR Foundation](docs/pbr-foundation.md) and
  [Path-Tracing Transport](docs/path-tracing-transport.md) define lighting and
  transport contracts.
- [Temporal Aliasing Options](docs/temporal-aa-options.md) and
  [Noise Sampling](docs/noise.md) cover anti-aliasing, histories, and stochastic
  inputs.
- [Scene Catalog](assets/scenes/README.md) and
  [Environment Catalog](assets/environments/README.md) record packaged assets,
  provenance, and licenses.
- [Legal Guide](legal/README.md) records third-party notices, license scope,
  and contributor terms.

## Licensing

UVSR first-party code is source-available under the
[Polyform Noncommercial License](LICENSE.md). Commercial use or sublicensing
requires a separate written agreement. Third-party code and assets remain under
their own terms; consult the [Legal Guide](legal/README.md) before
redistribution.
