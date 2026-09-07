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

developer builds keep logs, snapshot catalogs, and scene timing history in
`state` beside their executable, so separate build trees keep separate state.

without a startup snapshot, UVSR starts from canonical defaults. a requested
adapter is chosen before renderer state is created. changing the adapter in
settings performs one explicit restart. a failed adapter request must not apply
part of a snapshot or mutable settings transaction.

press **Escape** or **~** to open or close Settings. the panel contains General,
Pathing when applicable, Shadow, Light, Sky, Postprocess, Material,
Noise, Debug, and Developer controls. controls with unmet prerequisites
are hidden and keep their stored preference. a local reset restores its control or preset. the
footer **Reset** restores renderer and interface defaults without changing the
camera, scene, or adapter. **Capture** copies the current frame. **Restart**
restarts UVSR.

freelook uses **W/S**, **A/D**, and **Q/E** for forward, lateral, and vertical
movement. hold either **Shift** key for 2x keyboard movement speed. arrow keys
look, **X/C** roll, and **V** restores upright roll.
locked camera mode prevents navigation. camera movement invalidates histories
whose image samples no longer match.

general also selects the graphics adapter. adapter changes restart once.

Developer ends with a collapsed **Advanced** submenu containing
**Override Visual Maxes** and **Allow Ray Traversal**. the numeric override permits
entry beyond slider tracks within the setting's safe limits. traversal gates ray traced effects without clearing their
stored settings. **Display Sync Test**, also toggled with **F8**, draws moving bars
over the fully rendered scene. scene rendering and light animation continue.
**Vertical Sync** and **Frame Rate Limit** apply to both normal rendering and the
test. both switches default off. enable Frame Rate Limit to set **Maximum FPS**
from 1 to 960. disabling it retains the chosen number. a heavier scene can still
run slower.

the frame limit is a maximum, not a requested display refresh mode. Vertical Sync
presents complete frames at refresh boundaries. enabling it also enables the
frame limiter and sets Maximum FPS to the active panel refresh rate. its editable
maximum follows that rate. you can then choose a lower limit.
a lower limit, such as 60 FPS on a 240 Hz panel, remains effective.
turning Vertical Sync off also turns Frame Rate Limit off. its number remains
stored, and the limiter can be enabled independently afterward.
the UI reports the effective ceiling from the software limit, test target, and
display refresh when Vertical Sync is enabled. a limit that does not divide the
refresh rate can produce uneven displayed frame intervals.

**Pause Motion** freezes the bars, and **Speed** changes their movement rate.
measured FPS and the frame-interval graph work in both rendering modes.
**Test Frame Rate** defaults to Frame Rate Limit, which uses the same ceiling as
the scene. Below Refresh, Above Refresh, Fixed FPS, and Sweep FPS remain explicit
test modes, bounded by the shared limit and Vertical Sync. these test controls
are hidden outside the test. Sweep FPS varies its target over 12 seconds.
**Escape** hides the menu. stopping the test retains shared presentation settings.

sideways breaks in bars indicate tearing. straight bars that jump indicate
uneven frame delivery. for an unsynchronized reference, disable Vertical Sync
and the frame limit. Windows composition or driver settings can still prevent
visible tearing; clean bars are not an automatic test pass.

Cap is the only interface. it uses Segoe UI Semibold, bold headings, rounded
controls, translucent cool-gray drawers, and opaque navy surfaces. Windows supplies
the fonts; UVSR does not package them.

inactive number fields show exactly four digits. integers use leading zeros,
floats use decimals, and large or small values use scientific notation with the
exponent included in the four digits. editing shows and preserves full precision.
RGB and HSV fields use colored edge markers with channel-name tooltips. hue uses
a magenta gradient. color pickers retain RGB, HSV, and hexadecimal entry, omit
Current and Original previews, and use circular bar cursors.

## tonemapper

**Postprocess > Tonemapper > Enabled** defaults off. off retains the original neutral AgX
display transform and bypasses custom grading and the film LUT. their values
stay stored. automatic exposure, FXAA, display transfer, and dithering remain
independent. enable it to use the AgX Base,
Punchy, Golden, and Mix grades. changes to
Image Exposure, Contrast, Saturation, Warmth, Tint, Slope, or Power display **Custom**.
the preset resolves to those seven values; it does not keep a second copy of
the grade. **Film LUT** independently selects None, 2383 Print, Portra 400, or
Ektar 100. these bundled looks are artistic simulations, not official Kodak profiles.

Base with neutral controls and Film LUT set to None preserves the neutral AgX
transform. **Auto Environment Exposure** remains in Sky; **Image Exposure** applies a separate
global EV adjustment after it, including direct lights and emissive materials.
it changes the displayed image without changing scene lighting.
white balance, exposure, AgX contrast, grade,
and the optional LUT precede the output gamut transform. FXAA, output transfer,
and dithering retain their existing order. there is no local exposure pass.

tone values and LUT selection persist in snapshots and reset to neutral defaults.
older supported snapshots acquire those defaults. LUT loading validates the
complete table before replacing the active texture. None uses shader variants
with no LUT texture or sampler binding.

## rendering and antialiasing

general selects **Ray Tracing** or **Path Tracing**. ray tracing rasterizes a
deferred material buffer, evaluates physically based direct and environment
lighting with selective ray traced shadows and sky visibility.

Postprocess > FXAA contains the **Enabled** toggle, Low through Ultra
quality, and **FXAA Tuning** for edge sharpness, relative edge threshold,
and minimum edge threshold. it filters
display-linear edges after tone mapping, before output transfer and dithering.
it can remain off. single-sample rasterization requires no coverage resolve.
Ray Tracing **Accumulate Samples** owns its progressive image mean independently.

TAA, its sharpening pass, and MSAA were removed by explicit product decision.
their [TAA](postmortem/taa.md) and [MSAA](postmortem/msaa.md) postmortems retain
the rationale and recovery boundaries. no replacement reconstruction is enabled.

debug separates presentation from rendering policy. world views inspect scene
or simplified materials. PBR filters inspect normals, environment terms,
reflectance, specular visibility, environment level, and sky visibility.

screen-space AO and diffuse GI were removed. material occlusion textures,
environment lighting, ray traced sky visibility, and path tracing remain.
the [diffuse postmortem](postmortem/screen-space-diffuse.md) preserves the
method history, experiment results, and requirements for a future implementation.

## noise and accumulation

global Noise selects **Spatial White**, **Spatial Blue**, or
**Spatiotemporal Blue** at 64x64, 128x128, 256x256, or 512x512. **Animate
samples** advances the selected sequence. effect specific overrides inherit the
global choice while off and keep their private preference.

in Ray Tracing, **Accumulate Samples** enables one cumulative scene linear mean
for accepted samples. path Tracing always owns its separate fixed cumulative
mean. camera, scene, resolution, material, geometry, lighting, environment,
rendering solution, noise, and other image defining changes reset the applicable
history. the [noise asset catalog](../assets/noise/README.md) records all 12
retained files, dimensions, layers, hashes, and provenance.

## sky, exposure, and lights

sky selects Day, Bright Overcast, Soft Day, Night, Starry Night, or Cloudy. the
[environment catalog](../assets/environments/README.md) records the exact HDR
sources and starting exposure for each. **Environment Exposure** scales only
environment lighting and its matching background. direct lights and emissive
materials retain their intensity. **Ambient Fill** gates its diffuse and specular IBL lobes.
diffuse and specular IBL then have independent enables and strengths. **Show
environment Background** controls visibility of the same source without
changing lighting.

**Auto Environment Exposure** meters the rendered luminance and changes exposure
before the same AgX display transform. its controls are Enable, Compensation,
Maximum Brightening, Maximum Darkening, and Adjustment Period.
manual environment exposure still defines the source scale. automatic exposure
does not enter lighting or path tracing history as display mapped color.

ray traced sky visibility can affect diffuse IBL, specular IBL, or both. it has
enable, 1, 2, 4, 8, 16, 32, or 64 samples, an optional
noise override, Maximum Distance, and Ray Bias. if an HDR asset is missing or invalid, environment contribution is
zero. UVSR does not reuse the previous environment or invent a procedural one.

lights selects an editable directional, point, spot, or camera flashlight.
available controls follow the selected type: direction, color, irradiance,
angular size, radius, intensity, and spot angles. shadows controls directional
ray traced visibility with Enable, Maximum Distance, and Ray Bias. **Samples Per
Pixel** selects 1, 2, 4, 8, 16, 32, or 64 shadow rays for the directional light
and flashlight. **Hard Shadows** forces one ray and zero emitter angles and
radii for all lights in both rendering modes. stored sample counts and emitter
sizes return when it is disabled. the path tracer retains its own path sample
count. zero-size emitters always need only one visibility ray.

press **F** to toggle the flashlight when no text field is active. its retained
controls include Enable, Cast Shadows, Brightness, Beam
size, Angular Size, Beam Roundness, Edge Softness, Range, and camera offsets.
**Realistic Flashlight** adds Hotspot Size and Strength, Stationary When Idle,
sway, and Aim Correction. these settings still describe one analytical camera
light. turning shadows off removes its visibility trace but not its direct
lighting.

## path tracing

choose **Path Tracing** under Lighting Solution. **Pathing** contains Max Bounces,
Min Bounces, Firefly Filter, and Firefly Threshold. each control has a reset.

Max Bounces allows 1 to 30 scattering events, default 30. the camera surface is
depth zero. following Capsaicin, Min Bounces delays Russian roulette until the
current surface depth is greater than its value, default 2. it cannot exceed
Max Bounces. lowering the maximum also lowers the minimum when needed. paths
can still end on a miss, invalid transport, or the maximum limit. roulette keeps
UVSR's existing bounded survival probability.

Firefly Filter defaults on with a threshold of 50 scene-linear radiance units.
it follows RTXPT's probability-scaled cap on each emission or sampled direct
lighting event before path throughput and accumulation. the RGB average keeps
the contribution's color ratio. lower thresholds suppress more bright outliers
and can darken legitimate lighting. this is a biased estimator. the threshold
is independent of exposure, and off bypasses the filter exactly.

one fresh path per pixel uses the retained diffuse/GGX mixture and analytical
light selection. opaque and alpha-tested triangles, scene emission, directional,
point, spot, and flashlight lighting, and environment misses remain supported.
the tracer advances a cumulative scene-linear mean and accepted sample count.
camera, scene, resize, material, light, environment, noise, solution, or Pathing
changes restart history. Path Tracing bypasses raster geometry and selective
shadow passes; their saved settings remain available in Ray Tracing.

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
material picking, visibility, antialiasing, exposure, tone
mapping, and output remain distinguishable. treat the numbers as measurements
of the displayed executable and settings, not as a general benchmark.

## troubleshooting

- if UVSR rejects an adapter, choose hardware that supports DirectX 12, Shader
  model 6.5, and the required resource binding tier. ray query features require
  DXR 1.1.
- if a control is unavailable, read its inline reason. common prerequisites are
  ray tracing, an active effect, hit distance, a directional light, or DXR.
- if a snapshot fails before apply, confirm its 32 lowercase hexadecimal
  characters and local catalog entry. adapter mismatch must cause zero mutable
  state change. see [settings](settings.md).
- if lighting disappears after an asset failure, verify package contents and
  the selected scene or HDR record. UVSR deliberately avoids stale asset and
  hidden ambient fallbacks.
- for a visual or package defect, record source revision, settings identity,
  executable SHA-256, adapter, scene, camera, resolution, and a capture. the
  required evidence is defined in [validation](validation.md).
