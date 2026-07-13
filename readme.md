# UVSR

**Unified Visibility Stochastic Rendering**

UVSR is a DirectX 12 renderer built on NVIDIA's pinned Donut framework and its
NVRHI graphics abstraction layer. It launches the NVIDIA Bistro exterior scene
with its authored materials by default; the converted Bistro Interior Wine scene
is also available from the scene picker.

## Renderer baseline

- Deferred shading, UVSR PBR, screen-space visibility AO/GI, and the procedural
  sky start enabled.
- Native-Resolution Analytical Reconstructive Temporal Anti-Aliasing (NRA-RTAA)
  starts enabled on the deferred UVSR PBR path. Its **Aliasing** drawer separates
  Heavy, Medium, and clarity-biased Light temporal-response presets from an
  independent, statically compiled **Performance Profile** selector with
  Performance, Balanced, and Maximum Quality tiers. The factory default is
  Medium Temporal plus Balanced. Analytical validation and rejection controls,
  32 debug views, memory estimates, and GPU timings remain available, and all
  scene color and history resources stay at display resolution.
- Renderer settings always start from factory defaults; **Reset All Settings**
  restores those defaults in-session, and settings are not carried between
  launches.
- The HUD performance row reports resolution, frame time, FPS, current-clock
  memory bandwidth, and current-clock FP32 peak GFLOPS. The **All** row stays
  compact with total, Trace, Filter, and Composite GPU timings.
- **Enable PBR** switches between UVSR's shared forward/deferred
  metallic-roughness PBR path and Donut's legacy material-lighting path while
  retaining the same camera, scene, tone grade, sky, and lights.
- **White World Off** is the default. **White World On**, **White World Preserve
  Normals**, and **White World Preserve Emissives** override material color
  without modifying source assets. The last mode keeps authored emissive color
  alongside the scene's colored direct lights so GI sources remain easy to read.
- Camera controls are limited to **First Person** and **Third Person**.
- The first scene light is selected automatically in the **Lights** panel.
- The AgX display pipeline provides Base, Punchy, Golden, Mix, and Custom
  presets. Its controls are Exposure, Contrast, Saturation, Warmth, Tint, Slope,
  and Power.
- UVSR includes original AgX Base-space simulations of Kodak 2383 Print, Portra
  400, and Ektar 100 looks. They are not official Kodak LUTs. Additional licensed
  3D `.cube` LUTs can be placed in `assets/luts/kodak`; see the
  [LUT notes](assets/luts/kodak/README.md) for the supported format.

## Build and run

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
.\build\bin\uvsr.exe
```

Label an experimental run in the window and task title with:

```powershell
.\build\bin\uvsr.exe --experiment "testing program title on task title"
```

The title reports the active graphics API at runtime, for example
`UVSR Renderer, D3D12 (testing program title on task title)`.

The first configure may download Microsoft's Direct3D 12 Agility SDK if it is
not already cached.

Build and run the PBR, radial-visibility, and NRA-RTAA reference tests
separately:

```powershell
cmake --build build --config Release --target uvsr_pbr_tests uvsr_radial_visibility_tests uvsr_rtaa_reference_tests
ctest --test-dir build -C Release --output-on-failure
```

## Bistro material preparation

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

## Documentation and conventions

The [PBR foundation](docs/pbr-foundation.md) documents the material contract,
G-buffer packing, equations, debug views, validation controls, limitations, and
extension points.

The [screen-space visibility design](docs/screen-space-visibility.md) documents
the shared 32-sector AO/GI traversal, resources, coordinate/radiance contracts,
filtering, controls, limitations, and the upgrade path to persistent unified
visibility.

The [NRA-RTAA design](docs/nra-rtaa.md) documents pipeline placement, motion
and jitter conventions, validation and reconstruction behavior, resource
packing, memory costs, presets, timing interpretation, and current limitations.

UVSR runs uncapped with a single planar view. UVSR-owned interactive controls
provide concise hover tooltips; new controls should follow the same convention.
The renderer exposes **Reload Shaders**, **Restart Renderer**, **Reset All**, and
**Screenshot** controls in the main settings panel.

## Intentional omissions

The current baseline intentionally omits:

- DirectX 11 and Vulkan backends
- VSync, stereo, and bloom controls
- Imported scene cameras and translucent rendering
- Animation playback, ambient-intensity scaling, and material-event
  instrumentation
- Shadow rendering and shadow-map debugging
- Light-probe capture, filtering, image-based lighting, probe textures, and
  probe controls

## Repository naming

Use the lowercase engineering slug `uvsr` for repository URLs, terminal
commands, package names, and folder paths:

```text
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
```

The displayed project name remains **UVSR**.
