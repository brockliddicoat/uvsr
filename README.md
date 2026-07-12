# UVSR

**Unified Visibility Stochastic Rendering**

UVSR is a rendering project built on NVIDIA's Donut framework and its pinned NVRHI graphics abstraction layer.

The demo launches the full **NVIDIA Bistro exterior** scene by default. UVSR starts in **White World Preserve Detail**, so the Bistro geometry is shown as a neutral white-world reference while retaining normal and ambient-occlusion detail.

The scene picker also includes the converted, self-contained **NVIDIA Bistro Interior Wine** GLB.

The White World mode works with arbitrary scenes without modifying source assets. Its three settings are **White World Off**, **White World On**, and **White World Preserve Detail**.

UVSR also forces Bistro materials to render two-sided in both textured and white-world modes because the FBX source contains thin surfaces with mixed winding.

Its converted Bistro materials are normalized from blend to alpha-tested in textured mode so they write depth correctly while retaining texture cutouts. The source conversion mislabeled the packed roughness/metalness maps as specular-extension textures; repair freshly converted GLBs once with:

```powershell
tools\repair_bistro_orm.cmd assets/scenes/nvidia_bistro/BistroExterior.glb assets/scenes/nvidia_bistro/BistroInterior_Wine.glb
```

The repair edits only GLB metadata, preserves the original JSON chunk in a `.pre-uvsr-json` file, and deliberately ignores the maps' zero-filled red channel so SSAO remains the visibility source. Pass `--restore` to the same command to restore those original chunks.

The Bistro light is normalized from its exported real-world lux value to the renderer's unoccluded-light range. This scene calibration is applied to the light itself, outside the shared BSDF, and remains editable in the Lights panel.

UVSR uses an AgX display pipeline with Base, Punchy, Golden, Mix, and Custom grading presets. The Tonemapper section exposes camera white balance, EV exposure, contrast, saturation, slope, and power controls. Licensed 3D `.cube` LUTs placed in `assets/luts/kodak` are discovered at startup and applied in AgX Base space.

Forward and deferred rendering share UVSR's compact metallic-roughness PBR core. Its material contract, G-buffer packing, equations, validation controls, limitations, and extension points are documented in `docs/pbr-foundation.md`.

UVSR runs uncapped with a single planar view. Its UI intentionally omits VSync, stereo, and bloom controls, and provides a **Restart Renderer** button that shuts down graphics resources before relaunching the same command line.

Camera control is limited to First-Person and Third-Person modes. Imported scene cameras and translucent rendering are intentionally omitted from the current baseline.

Temporal anti-aliasing, animation playback, AA-mode selection, jitter selection, ambient-intensity scaling, and material-event instrumentation have been removed from the application.

Shadow rendering and shadow-map debugging are intentionally omitted from the current baseline.

Light-probe capture, filtering, image-based lighting, probe textures, and probe controls are intentionally omitted from the current baseline.

## Repository naming

Use the lowercase engineering slug `uvsr` for repository URLs, terminal commands, package names, and folder paths:

```text
git clone https://github.com/yourname/uvsr
cd uvsr
```

The displayed project name remains **UVSR**.
