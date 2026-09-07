![UVSR engine banner](assets/branding/uvsr-banner.png)

# UVSR

**Unified Visibility Stochastic Rendering Engine**

[![license: Polyform Noncommercial](https://img.shields.io/badge/license-polyform_noncommercial-8250DF?style=flat-square)](LICENSE.md)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![HLSL](https://img.shields.io/badge/HLSL-shaders-C62828?style=flat-square&logo=microsoft&logoColor=white)

UVSR is a focused C++17 and HLSL renderer for real time visibility, lighting,
and antialiasing research. it is DirectX 12 only and uses ImGui. developer and
production builds use the same renderer features.

## product

UVSR provides deferred physically based lighting, one conventional path tracer,
ray traced directional, sky, and flashlight
visibility and FXAA with single-sample rasterization. it
ships Bistro Interior, San Miguel, six HDR environments, and the retained white,
blue, and spatiotemporal blue noise set. material editing, pixel zoom, timing,
buffer inspection, settings snapshots, and diagnostics remain available through
the ImGui interface.

the [user guide](docs/user-guide.md) explains controls and visible outcomes.

## install and update

the only shipped executable names are `uvsr-launcher.exe` and
`uvsr-engine.exe`. the launcher installs and updates a signed and hash bound
renderer package transactionally:

```text
uvsr-launcher.exe -> signed feed -> verified renderer package -> uvsr-engine.exe
```

public packages contain runtime files, retained assets, settings, notices, and
licenses. they do not contain source, tests, interpreters, Git, CMake, compilers,
SDKs, debug layers, symbols, or benchmark tools. see the
[launcher guide](launcher/README.md) for the trust and recovery contract. no
download is linked here until an exact published artifact has passed the release
gate.

## build

use 64 bit Windows 11, Visual Studio 2022 with C++, a Windows SDK, CMake 3.24 or
newer, and a DirectX 12 adapter with Shader Model 6.5. ray queries require DXR
1.1. clone submodules and keep one external build tree per worktree.

```powershell
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
$buildRoot = Join-Path $env:LOCALAPPDATA 'UVSR\builds\<worktree-id>'
cmake -S . -B $buildRoot -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build $buildRoot --config Release --target uvsr-engine --parallel
ctest --test-dir $buildRoot -C Release --output-on-failure
& "$buildRoot\bin\uvsr-engine.exe"
```

use a stable `<worktree-id>`. use `BUILD_TESTING=OFF` only for a production
package. keep builds, caches, downloads, binaries, and staging outside Git.

## documentation

- [documentation map](docs/README.md)
- [settings and snapshots](docs/settings.md)
- [validation](docs/validation.md)
- [scene catalog](assets/scenes/README.md)
- [environment catalog](assets/environments/README.md)
- [noise assets](assets/noise/README.md)
- [launcher and package contract](launcher/README.md)
- [legal and provenance guide](legal/README.md)
- [contribution guide](CONTRIBUTING.md)

## license

first party material is available under the
[Polyform Noncommercial License](LICENSE.md). commercial use or sublicensing
requires a separate written agreement. third party code and assets retain their
own terms. review the [legal guide](legal/README.md) before redistribution.
