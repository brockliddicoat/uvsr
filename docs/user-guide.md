# UVSR user guide

UVSR presents one DirectX 12 renderer through ImGui. settings change visible
renderer behavior directly. there is no text command console. use the drawers,
their reset buttons, and the startup snapshot path described in
[settings](settings.md).

## startup and interface

use `uvsr-launcher.exe` for normal installation, update, repair, launch, and
uninstall. the installed renderer is `uvsr-engine.exe`. a direct developer
launch accepts `-width <pixels>`, `-height <pixels>`, `-fullscreen`,
`-adapter <index>`, an optional scene name, and one
`--settings-snapshot <32-character-code>`.

without a startup snapshot, UVSR starts from canonical defaults. a requested
adapter is chosen before renderer state is created. changing the adapter in
settings performs one explicit restart. a failed adapter request must not apply
part of a snapshot or mutable settings transaction.

press **Escape** or **~** to open or close Settings. the panel contains General,
noise, Visibility, Denoising, Aliasing, Debug, Sky, Lights, Shadows, Material,
Interface, and Developer controls. disabled rows explain the missing prerequisite and keep
their stored preference. a local reset restores its control or preset. the
footer **Reset** restores renderer and interface defaults without changing the
camera, scene, or adapter. **Capture** copies the current frame. **Restart**
restarts UVSR.

freelook uses **W/S**, **A/D**, and **Q/E** for forward, lateral, and vertical
movement. arrow keys look, **X/C** roll, and **V** restores upright roll.
locked camera mode prevents navigation. camera movement invalidates histories
whose image samples no longer match.

general also selects the graphics adapter and Off, vendor agnostic, or NVIDIA
exclusive adaptive sync. adapter changes restart once. adaptive sync changes
presentation timing, not renderer features.

interface selects Amp or Ogg, Codex, Noto Sans, or ProggyClean font roles,
bounded numeric entry beyond compact visual tracks, and the available accent,
text, and background colors. Amp supplies authored widgets. Ogg uses stock
ImGui widgets. panels, controls, popups, and pixel zoom respond immediately.
panels use flat palette colors with their existing opacity controls. appearance
changes do not alter scene lighting or sampling.

## display sync test

open **Developer** and choose **Start Test**, or press **F8**. the scene pauses
while vertical bars move horizontally through the engine's normal presentation
path. **F8** stops the test and restores scene presentation. **Escape** still
hides or shows the menu. test controls last for the session and are excluded from
settings snapshots. scene changes stop the test.

horizontal tears create sideways breaks in the vertical bars. whole straight bars
jumping forward indicate uneven frame delivery. **Pause Motion**, **Speed**,
**Target FPS**, and the twelve-second **Sweep FPS** make these easier to compare.
the graph measures frame intervals; its FPS value is not a live display Hz counter.

for a tearing comparison, disable VRR in the driver/display, select **Above
Refresh**, and compare **VSync** off and on. for a VRR comparison, leave **VSync**
off, select **Below Refresh**, and toggle VRR in the driver/display. ensure the FPS
target or sweep is inside the panel's actual VRR range. the presets use the
display's configured refresh rate and cannot discover its lower VRR boundary.

maximize the window and stop competing GPU workloads. if the unsynchronized
reference has no visible tearing, the comparison is inconclusive. use the driver
VRR indicator or a display's live refresh counter to confirm activation. the
engine reports eligibility, not verified FreeSync/G-SYNC activity. screenshots
of complete rendered frames cannot prove what happened during physical scanout.

references: NVIDIA's [G-SYNC Pendulum demo](https://www.nvidia.com/en-us/geforce/community/demos/)
demonstrates the visual comparison. Microsoft's
[D3D12 fullscreen sample](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Fullscreen)
and [VRR documentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)
describe presentation. this implementation uses original procedural bars and the
existing engine swap chain, with no copied demo assets or new dependency.

## rendering and antialiasing

general selects **Ray Marching** or **Path Tracing**. ray Marching rasterizes a
deferred material buffer, evaluates physically based direct and environment
lighting, and composes the enabled visibility effects. it is the mode for
AO/GI, selective ray traced shadows, and FXAA.

the Aliasing drawer retains **FXAA** with Enable, Low through Ultra quality,
edge sharpness, relative edge threshold, and minimum edge threshold. it filters
display-linear edges after tone mapping, before output transfer and dithering.
it can remain off. single-sample rasterization requires no coverage resolve.
Ray Marching **Accumulate Samples** owns its progressive image mean independently.

TAA, its sharpening pass, and MSAA were removed by explicit product decision.
their [TAA](postmortem/taa.md) and [MSAA](postmortem/msaa.md) postmortems retain
the rationale and recovery boundaries. no replacement reconstruction is enabled.

debug separates presentation from rendering policy. world views inspect scene
or simplified materials. visibility views show final, ambient visibility,
traced indirect, or applied indirect output. PBR filters inspect normals,
environment terms, reflectance, specular visibility, environment level, and sky
visibility. selecting a view does not change the estimator or history contract.

## AO, GI, and denoising

visibility runs one current frame screen space trace for ambient occlusion and
one bounce diffuse GI. **Enabled** gates the pass. low, Medium, High, and Ultra
recipes choose a coherent resolution, estimator, sample count, and precision.
custom exposes all retained choices without selecting another algorithm.

the shared controls are:

- **Estimator**: Projected Angle, Solid Angle, or Cosine Weighted.
- **Resolution**: Full, Half, or Quarter. reduced results use automatic depth
  and normal guided upsampling.
- **Samples**: any integer from 1 through 64.
- **Radius**, **Thickness**, and **Distribution**: trace reach, depth acceptance,
  and sample placement.
- **Specify Noise and Denoising**: inherit global settings or choose private
  noise for the shared AO/GI trace and independent AO/GI filters.

AO has independent Enable, Strength from 0 through 8, Output Hit Distance, and
16 or 32 bit precision. GI has independent Enable, Intensity from 0 through 16,
output Hit Distance, and 16 or 32 bit precision. AO modulates ambient response.
GI adds current frame diffuse transport. their raw outputs and denoiser
histories remain separate.

the global Denoising drawer applies **Raw** or **SVGF** to AO,
GI, sun shadows, flashlight shadows, sky visibility, and path tracing. each
signal retains its own history. **Specify Noise and Denoising** enables private
noise and filter settings. AO and GI share the noise gate and retain separate
private filters. sun shadows have **Specify Denoising**, since their sampling
has no noise texture selection.

**Raw** preserves the producer output. **SVGF** filters current illumination
using matching surface guides and previous observations. its controls are
Iterations, Feedback Tap, Luminance Sigma, Normal Power, Radiance Alpha, and
Moments Alpha. feedback tap one retains the second wavelet pass for the next
frame. filtering uses the producer's sample resolution.

see [denoising](denoising.md) for defaults and the input contract. older filtered
methods migrate to default SVGF. Raw and independent override gates remain.

## noise and accumulation

global Noise selects **Spatial White**, **Spatial Blue**, or
**Spatiotemporal Blue** at 64x64, 128x128, 256x256, or 512x512. **Animate
samples** advances the selected sequence. effect specific overrides inherit the
global choice while off and keep their private preference.

in Ray Marching, **Accumulate Samples** enables one cumulative scene linear mean
for accepted samples. path Tracing always owns its separate fixed cumulative
mean. camera, scene, resolution, material, geometry, lighting, environment,
rendering solution, noise, and other image defining changes reset the applicable
history. the [noise asset catalog](../assets/noise/README.md) records all 12
retained files, dimensions, layers, hashes, and provenance.

## sky, exposure, and lights

sky selects Day, Bright Overcast, Soft Day, Night, Starry Night, or Cloudy. the
[environment catalog](../assets/environments/README.md) records the exact HDR
sources and starting exposure for each. **Exposure** applies one common scale to
the environment. **Ambient Fill** gates its diffuse and specular IBL lobes.
diffuse and specular IBL then have independent enables and strengths. **Show
environment Background** controls visibility of the same source without
changing lighting.

**Automatic Exposure** meters the rendered luminance and changes exposure
before the same AgX display transform. its controls are Enable, Exposure
compensation, Maximum Brightening, Maximum Darkening, and Adjustment Period.
manual environment exposure still defines the source scale. automatic exposure
does not enter AO/GI or path tracing history as display mapped color.

ray traced sky visibility can affect diffuse IBL, specular IBL, or both. it has
enable, Output Hit Distance, 1, 2, 4, 8, 16, 32, or 64 samples, an optional
noise override, Maximum Distance, and Ray Bias. sky denoising is selected in
denoising. sky visibility owns occlusion for each IBL component it affects.
screen AO remains active for other IBL components and its diagnostic view.
the renderer skips AO work when no active consumer needs it.

if an HDR asset is missing or invalid, environment contribution is
zero. UVSR does not reuse the previous environment or invent a procedural one.

lights selects an editable directional, point, spot, or camera flashlight.
available controls follow the selected type: direction, color, irradiance,
angular size, radius, intensity, and spot angles. shadows controls directional
ray traced visibility with Enable, Maximum Distance, and Ray Bias. it evaluates the visible receiver at each pixel.

press **F** to toggle the flashlight when no text field is active. its retained
controls include Enable, Cast Shadows, Output Hit Distance, Brightness, Beam
size, Angular Size, Beam Roundness, Edge Softness, Range, and camera offsets.
**Realistic Flashlight** adds Hotspot Size and Strength, Stationary When Idle,
sway, and Aim Correction. these settings still describe one analytical camera
light. turning shadows off removes its visibility trace but not its direct
lighting.

## path tracing

choose **Path Tracing** under Lighting Solution to use the conventional path
tracer. the user choice is the solution itself. the transport recipe is fixed:
four maximum bounces, one fresh sample per pixel per frame, uniform analytical
light selection, a fixed diffuse and GGX proposal mixture, and Russian roulette
from continuation three.

the tracer supports opaque and alpha tested triangles, scene emission,
directional, point, spot, and flashlight lighting, environment misses, Lambert
diffuse, and GGX reflection. it advances one cumulative scene linear mean and
accepted sample count. camera, scene, resize, material, light, flashlight,
environment, exposure, noise, or solution changes restart that history.

path Tracing bypasses the raster G-buffer, screen space AO/GI, selective shadow
passes. their saved settings remain available when Ray Marching
is selected again. the fixed recipe exposes no transport tuning or denoising
controls.

## scenes, materials, and assets

general selects **Bistro Interior** or **San Miguel**. each package includes a
loadable descriptor, components, attribution, and provenance. the
[scene catalog](../assets/scenes/README.md) links both exact records. scene
loading keeps the UI responsive and reports pending, ready, or failed state.

press **M** to open or refresh Material for the center surface. the drawer shows
the selected material and exposes its available domain, double sided state,
base texture and color, metal or specular texture, specular color or metalness,
roughness, opacity and alpha cutoff, normal texture and scale, occlusion texture
and strength, emissive texture, color, and intensity, transmission controls, and
alpha mask texture. a control is disabled when the material has no matching
texture or domain. material changes reset affected lighting histories.

retained assets are package content, not downloads generated at first run.
their canonical records are the [scene catalog](../assets/scenes/README.md),
[environment catalog](../assets/environments/README.md),
[noise catalog](../assets/noise/README.md), and
[legal guide](../legal/README.md).

## pixel zoom and timing

press **Z** or use the footer zoom button to cycle Off, 2x, 3x, 4x, and 5x.
pixel zoom magnifies the selected frame region for inspection without changing
renderer resolution or sampling policy.

performance reports GPU stages, effect totals, logical texture payloads, and
active resources only after a complete timing snapshot is available. a stage
that did not run reports zero or unavailable, never an older measurement.
material picking, visibility, each denoiser, antialiasing, exposure, tone
mapping, and output remain distinguishable. treat the numbers as measurements
of the displayed executable and settings, not as a general benchmark.

## troubleshooting

- if UVSR rejects an adapter, choose hardware that supports DirectX 12, Shader
  model 6.5, and the required resource binding tier. ray query features require
  DXR 1.1.
- if a control is unavailable, read its inline reason. common prerequisites are
  ray Marching, an active effect, hit distance, a directional light, or DXR.
- if a snapshot fails before apply, confirm its 32 lowercase hexadecimal
  characters and local catalog entry. adapter mismatch must cause zero mutable
  state change. see [settings](settings.md).
- if lighting disappears after an asset failure, verify package contents and
  the selected scene or HDR record. UVSR deliberately avoids stale asset and
  hidden ambient fallbacks.
- for a visual or package defect, record source revision, settings identity,
  executable SHA-256, adapter, scene, camera, resolution, and a capture. the
  required evidence is defined in [validation](validation.md).
