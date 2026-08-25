![UVSR Engine Banner](assets/branding/uvsr-banner.png)

# UVSR

**Unified Visibility Stochastic Rendering Engine**

[![License: Polyform Noncommercial](https://img.shields.io/badge/license-polyform_noncommercial-8250DF?style=flat-square)](LICENSE.md)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![HLSL](https://img.shields.io/badge/HLSL-shaders-C62828?style=flat-square&logo=microsoft&logoColor=white)

UVSR is a focused C++17/HLSL renderer for real-time visibility, lighting, and
anti-aliasing research. It is DirectX 12 only and uses ImGui. Production and
developer builds share the same renderer feature code.

## Required Renderer Contract

- Preserve deferred physically based lighting with image-based lighting, automatic
  exposure, and AgX display mapping.
- Preserve screen-space ambient occlusion and one-bounce diffuse illumination, with all
  retained user-facing quality and filtering combinations.
- Preserve direct ray-traced sun, sky-visibility, and flashlight shadows.
- Require correct ray-traced shadow visibility at 2x, 4x, 8x, and 16x MSAA.
- Require one conventional path tracer with one fixed transport recipe.
- Preserve white, blue, and spatiotemporal blue-noise sampling plus all six HDR
  environments.
- Preserve Bistro Interior and San Miguel with adjacent provenance and licenses.
- Keep ImGui settings, commands, timing, buffer inspection, and diagnostics.

## Distribution

The only shipped executable names are `uvsr-launcher.exe` and
`uvsr-engine.exe`. Filenames never contain a version.

The public installation path is:

```text
uvsr-launcher.exe -> signed feed -> signed/hash-bound renderer package -> uvsr-engine.exe
```

The launcher verifies feed authorization, sequence, artifact size, and SHA-256,
then installs transactionally. End users do not download renderer source,
build tools, interpreters, SDKs, or build trees. A release link belongs here only
after the exact signed launcher and renderer package pass production
verification. See the [launcher guide](launcher/README.md) for the maintained
trust and recovery contract.

The engine version is derived deterministically from the canonical settings
schema hash. The complete hash is shared by the executable, diagnostics,
launcher, and package metadata; there is no independent marketing version.

## Developer Build

Requirements are 64-bit Windows 11, Visual Studio 2022 with C++ and a Windows
SDK, CMake 3.24 or newer, and a DirectX 12 adapter with Shader Model 6.5. Ray
queries require DXR 1.1.

```powershell
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
$buildRoot = Join-Path $env:LOCALAPPDATA 'UVSR\builds\<worktree-id>'
cmake -S . -B $buildRoot -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build $buildRoot --config Release --target uvsr-engine --parallel
ctest --test-dir $buildRoot -C Release --output-on-failure
& "$buildRoot\bin\uvsr-engine.exe"
```

Replace `<worktree-id>` with one stable identifier unique to this worktree.
Use `BUILD_TESTING=OFF` for a production package. Keep build trees, caches,
downloads, binaries, staged packages, and generated output outside Git.

## Documentation

- [Advanced Settings](docs/advanced-settings.md) describes retained controls,
  commands, and validation.
- [UI Integration](docs/ui-integration-agent-procedure.md) and
  [Agent Collaboration](docs/agent-collaboration.md) are scoped procedures.
- [Scene Catalog](assets/scenes/README.md),
  [Environment Catalog](assets/environments/README.md), and
  [Noise Sampling](docs/noise.md) cover retained assets.
- [Engine Cutdown Archive](docs/postmortem/engine-cutdowns/README.md) preserves
  decisions, evidence, recovery boundaries, and restoration criteria.
- [Legal Guide](legal/README.md) records licenses, notices, and provenance.

## Licensing

UVSR first-party material is source-available under the
[Polyform Noncommercial License](LICENSE.md). Commercial use or sublicensing
requires a separate written agreement. Third-party code and assets retain their
own terms; review the [Legal Guide](legal/README.md) before redistribution.
