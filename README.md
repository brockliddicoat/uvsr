# UVSR

**Unified Visibility Stochastic Rendering**

UVSR is a rendering project built on NVIDIA's Donut framework and its pinned NVRHI graphics abstraction layer.

The demo launches the full **NVIDIA Bistro exterior** scene by default. UVSR starts in **White World Preserve Detail**, so the Bistro geometry is shown as a neutral white-world reference while retaining normal and ambient-occlusion detail.

The scene picker also includes the converted, self-contained **NVIDIA Bistro Interior Wine** GLB.

The White World mode works with arbitrary scenes without modifying source assets. Its three settings are **White World Off**, **White World On**, and **White World Preserve Detail**.

UVSR also forces Bistro materials to render two-sided in both textured and white-world modes because the FBX source contains thin surfaces with mixed winding.

Its converted Bistro materials are normalized from blend to alpha-tested in textured mode so they write depth correctly while retaining texture cutouts.

The Bistro light is normalized from its exported real-world lux value to Donut's sample-lighting range. Direct illumination is reduced because this early renderer baseline intentionally omits shadows.

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
