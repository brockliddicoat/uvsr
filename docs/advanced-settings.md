# Advanced Settings and Developer Workflows

This guide is the detailed companion to the project README. It describes the
interactive renderer controls, factory behavior, benchmark entry points,
developer-only overrides, and specialist build modes without turning the
README into a control-by-control manual.

Renderer settings always start from factory defaults. **Reset** restores those
defaults in the current session, and settings are not persisted between
launches.

## General, Scenes, and Camera

The **General** drawer contains **Interface Skin**, **Graphics Adapter**,
**Camera Mode**, and **Camera Location**. **World Materials** contains the White
World presentations and the **Indirect Diffuse Response** diagnostic.
**World Scenes** owns the scene picker.

**Interface Skin** is a session-only presentation choice:

- **Amp** preserves UVSR's established animated translucent appearance and is
  the launch default. Its expanded Settings surface and collapsed status block
  use the same neutral `(0.018, 0.018, 0.018, 0.92)` surface as the slash
  command interface and Materials panel. Their shared blur preserves scene
  light and detail after removing chromatic spill, so the panels remain
  monotone over differently colored scene regions. The Settings title matches
  the resting blue drawer headers in color, 4 px corner radius, and one-pixel
  gradient outline. The command surface grows and fades in and out with the
  same presentation curve as the other floating windows and uses their shared
  backdrop blur and analytic outside-only shadow pipeline.
- The **OG** skin uses ImGui's stock dark style, default font, native widgets, and
  immediate interaction, with square scrollbars and two pinned performance
  rows. Pixel zoom is square, while Settings and the command surface retain the
  same outside-only shadow as pixel zoom, and its Settings title matches its
  stock resting drawer's color, square frame, and native outline policy. OG
  disables presentation motion for drawers, sections, toggles, popups,
  Settings, zoom, highlights, backdrop blur, and widgets so agents can
  configure experiments without waiting.

Skin selection does not alter renderer output or persist across launches.
Footer **Reset** and `/reset all` preserve the selected skin; use its inline
reset or `/reset ui.skin` to return to **Amp**.

At startup, UVSR selects the DirectX 12-capable adapter with the most dedicated
video memory. Selecting another compatible adapter restarts the renderer on that
device.

The bundled **PBR Sponza Decorated** and **PBR Sponza Plain** scenes open at
**Benchmark Position 1**, the
`intel-pbr-sponza-courtyard-simplified-v1` camera preset. The preset uses a
60-degree perspective view and a 1920x1080 reference frame. Moving or rotating
the camera changes **Camera Location** to **Piloted**; selecting **Piloted**
detaches the preset name without changing the view.

**Camera Mode** provides:

- **Freelook:** Mouse and arrow keys rotate; A/D strafe; W/S dolly; Space moves
  upward; either Shift key moves downward; X/C roll; V smoothly levels roll;
  and the wheel applies a small damped dolly. The V-key motion preserves
  position and view direction, follows a frame-rate-independent exponential
  curve, deliberately passes level by about 15 percent, and then readjusts to
  exact level. A new V press restarts from the current roll, while other camera
  input cancels it. The camera uses collision and smooth acceleration and
  deceleration.
- **Locked:** Freezes the current view. Automated benchmark launches select
  this mode.

Right-click remains camera input, and middle-click retains cursor-directed
material picking. With the material editor closed, **M** requests a fresh exact
material-ID pick at the framebuffer center. The editor opens only when that
readback resolves to an editable material in the current scene; a miss, a scene
change, or a controlled experiment leaves it closed instead of exposing a
previous selection. **M** closes an open editor or cancels its pending pick. It
never moves the camera. The center crosshair remains visible while the editor
is open.

**White World Off** is the default. **White World On**, **White World Preserve
Normals**, and **White World Preserve Emissives** override material color
without changing source assets. **Indirect Diffuse Response** shows only the
material-applied screen-space diffuse GI contribution. It excludes direct
light, diffuse IBL, and AO-only darkening.

## Screen-Space Visibility and PBR

The default deferred path enables UVSR PBR, screen-space visibility AO/GI, and
the matched environment background. **Visibility > Enabled** controls
visibility and the UVSR PBR path together. AO and GI remain independent
consumers: setting one effect's strength to zero can release its resources while
the other continues.

### Visibility Profiles

The top-level **Profile** selector contains only product-facing Low, Medium,
High, and Ultra recipes. Editing resolution, estimator, AO, GI, or buffer
formats changes the label to an origin-preserving custom state such as
**Medium (Custom)**.

| Profile | Sampling Resolution | Samples | Spatial Path | Buffer Precision |
| --- | --- | ---: | --- | --- |
| Low | Quarter linear resolution | 8 | Compact joint-bilateral upsampling | Performance |
| Medium | Half linear resolution | 8 | Compact joint-bilateral upsampling | Performance |
| High | Full resolution | 20 | Unreconstructed full-resolution input | Performance |
| Ultra | Full resolution | 48 | Unreconstructed full-resolution input | Default |

Low uses Uniform Projected Angle. Medium and the factory-default High profile
use Uniform Solid Angle. Ultra enables two diffuse GI bounces. Every profile
uses the first-party Toroidal Blue rank field.

### Sampling and Estimation

**Sampling Resolution** selects full, half, or quarter linear resolution.
Full-resolution visibility can composite unfiltered current output without a
spatial reconstruction dispatch or filter target. Reduced-resolution modes use
guide-aware reconstruction.

**Estimator** selects:

- **Uniform Projected Angle**
- **Uniform Solid Angle**, the factory default
- **Cosine-Weighted Solid Angle**, implemented with the complete joint-cosine
  CDF, projected-slice mass, `pi` GI normalization, and no duplicate receiver
  cosine

**Noise Pattern** compares **Independent Hash** with first-party **Toroidal
Blue**. Both are selected by a frame-coherent runtime-uniform branch in the
shared Runtime shader. The previous packed and unpacked offline-noise assets,
uploads, bindings, and shader families are retired.

One exact sample budget is shared by AO and every GI bounce. **Samples** exposes
the complete 1 through 64 range directly. There is no sampling-mode dropdown or
separate Fixed and Generic shader family. **Distribution** stays with
Estimator, Noise Pattern, Samples, Radius, and Thickness in **Shared Visibility
Sampling**.

Adaptive sparse sampling, feedback resources, and a separate later-bounce
sample count are intentionally absent.

### Spatial Reconstruction and Precision

**Spatial Reconstruction** provides unreconstructed full-resolution input,
guide-aware reduced-resolution upsampling, retained Intel edge-guided methods,
and legacy joint-bilateral comparison methods. AO-only fused final-application
paths are explicitly labeled **(Mutex GI)** when GI cannot share them.

The separate **Buffers** drawer owns precision and resource-format choices.
Changing a profile-owned buffer format clears the product preset label rather
than silently keeping a recipe that no longer matches the renderer.

### Indirect Lighting Controls

**Limit Bounces** starts enabled. While enabled, **Bounces** selects one through
eight finite diffuse bounces; one bounce keeps the compact shader path.

Disabling **Limit Bounces** enables GPU-driven contribution termination. Each
later bounce transports only the newest light frontier, and the continuation
threshold becomes four times stricter after every bounce. A wave-coalesced GPU
flag and indirect dispatch turn every pass after convergence into zero work. A
16-bounce guard contains malformed or non-contracting data; it is not the
normal stop condition.

**Bounce Contribution Cutoff** defaults to `0.001`. It skips source shading
whose conservative exposed upper bound cannot make a meaningful contribution.
Zero retains only exact-zero exits in explicitly limited mode.

Authored emissive radiance remains visible in forward, deferred, MSAA, and
G-buffer rendering, but it is not classified, boosted, or transported as a
screen-space GI source. First-bounce diffuse transport comes from shadowed
direct diffuse and directly reflected diffuse environment lighting.

**AO Power** defaults to its identity value of `1.0`. The default compositor
uses a specialization with the power operation compiled out; changing the
value selects the powered specialization.

Visibility-owned temporal accumulation is not exposed. Renderer TAA owns
temporal stability in the current build.

## Anti-Aliasing

The **Aliasing** drawer exposes **Enabled**, **Method**, **Quality**, and
**Temporal Cost**. Temporal Reconstructive opens its default-open **Developer
Options** surface for retained controls and the resolved policy overrides.

### Methods and Quality

- **Temporal Reconstructive** uses UVSR's temporal history, motion and jitter
  conventions, reverse-Z validation, disocclusion rejection, and rectification.
- **Conservative Morphological** uses CMAA2 as a spatial presentation method.
- **Multisample Reference** provides Low 2x, Medium 4x, High 8x, and Ultra 16x
  deferred MSAA.

Deferred MSAA preserves every G-buffer sample through material decode and PBR
lighting, then averages final RGBA16F radiance. Screen-space visibility selects
one coherent closest reverse-Z surface per pixel and coverage-weights only that
surface's signed correction back into the per-sample result.

Temporal and Multisample presets leave CMAA2 off by default, so neither hides an
extra full-screen morphology pass. CMAA2 can be selected after MSAA resolve or
as temporal presentation morphology.

Temporal quality bundles progress as follows:

| Quality | Reconstruction | Inherited History Frames | Additional Behavior |
| --- | --- | ---: | --- |
| Low | 1x Bilinear | 3 | Pair Tristimulus rectification |
| Medium | 1x Bilinear | 6 | Pair Tristimulus rectification |
| High | 1x Bicubic | 9 | Higher-quality reconstruction |
| Ultra | 5x Bicubic | 12 | Dejitter enabled |

**9x Bicubic** is the complete nine-bilinear-tap Catmull-Rom reconstruction.
**History Frames** accepts 1 through 32 prior frames for long-term temporal
methods, while **History Strength** accepts 0% through 200% of already-valid
history.

### Temporal Cost and Developer Options

**Temporal Cost** selects a visible default policy rather than hiding its
controls:

| Cost | History Layout | Intended Tradeoff |
| --- | --- | --- |
| **Full Quality** | Robust RGBA16F color and R32 depth | Maximum robustness and the full feature set |
| **Reduced** | Robust RGBA16F color and R32 depth | Default lower-compute profile with Stationary Bypass |
| **Minimum** | Compact history when supported | Explicit lowest-cost image-quality tradeoff |

UVSR starts in **Medium** Temporal Reconstructive with **Reduced** cost.
Reduced retains robust history while using **Stationary Bypass** for
Previous-Depth Validation.

Developer Options retains **Dejitter**, **Sharpness**, **History Frames**,
**History Strength**, **Morphology**, **Motion Source**, **Reconstruction**,
and **Rectification**, plus the policy rows **History Storage**,
**Previous-Depth Validation**, **History Weight**, **Motion Trust**,
**Rectification Clip**, **Blend Domain**, and **Sharpness Policy**. Resetting a
policy row restores its selected Temporal Cost default. **Sample Resurrection**
is available only at Full Quality; changing to Reduced or Minimum retains its
stored Full Quality value without applying it.

Effective image-policy or history-layout transitions reset temporal history
once. Presentation-only CMAA2 and image-equivalent sharpening changes do not.
Forward and legacy shading leave Temporal Reconstructive unavailable because
they do not provide its motion contract.

The slash interface exposes the same policies through
`anti-aliasing.temporal-cost`, `anti-aliasing.history.storage`,
`anti-aliasing.previous-depth-validation`, `anti-aliasing.history.weight`,
`anti-aliasing.motion-trust`, `anti-aliasing.rectification-clip`,
`anti-aliasing.blend-domain`, `anti-aliasing.sharpen.policy`, and
`anti-aliasing.sample-resurrection`.

The complete coordinate, reset, quality, and motion-test contract is in
[Temporal Anti-Aliasing Options](temporal-aa-options.md).

## Shadow Visibility Producers

Screen-Space Directional Shadows, Sparse Virtual Shadow Maps, and Diagnostic
Cascaded Shadow Maps are independent, initially disabled directional-light
visibility producers. Each resolves a full-resolution linear `R8_UNORM`
texture. Deferred lighting multiplies complete visibility factors only for the
exact pointer-identical light.

The primary directional sun, `sun_1`, is selected automatically. When
**Shadows** is opened, all three producer sections start expanded with their
**Enabled** toggles off. **Shadows** is a top-level sibling immediately after
**Lights**, and both drawers remain closed at launch. The selected flashlight's
**Cast Shadows** control remains in **Lights** because it governs only the
flashlight's local planar shadow map.

### Screen-Space Directional Shadows

Screen-Space Directional Shadows require deferred UVSR PBR and a primary
directional light. The first-party path consumes the existing single-sample
reverse-Z depth buffer and produces full-resolution directional visibility.

| Profile | Length | Shared Defaults |
| --- | ---: | --- |
| Default | 60 pixels | 4 hard samples, 8 fade-out samples |
| Long | 240 pixels | 4 hard samples, 8 fade-out samples |
| Maximum Validation | 960 pixels | 4 hard samples, 8 fade-out samples |

All three restore `0.005` surface thickness, `0.02` bilinear threshold,
contrast `4`, and optional modes and diagnostics off without changing
**Enabled**. **Length** selects compiled `SAMPLE_COUNT` variants of 60, 120,
240, 480, or 960 pixels.

Custom settings include **Surface Thickness**, **Bilinear Threshold**, **Shadow
Contrast**, compiled **Hard Shadow Samples** and **Fade-Out Samples**, **Ignore
Edge Pixels**, **Precision Offset**, **Bilinear Offset Mode**, and **Early
Out**. **Debug View** presents Occlusion, Trace Progress, or Ray Bounds as
grayscale visibility.

### Sparse Virtual Shadow Maps

The SVSM path exposes product profiles plus independent controls for sparse
marking, resolution policy, filter kernel, cache behavior, invalidation,
submission, culling, raster, and page-safe translation reuse.

| Profile | Filter | Taps | Resolution Policy |
| --- | --- | ---: | --- |
| Performance | Adaptive page-safe nearest-Poisson | 8 | Global `+1`, moving light `+2`, receiver clamp |
| Balanced | Page-safe bilinear PCF | 4 | No global bias, moving light `+1`, receiver clamp |
| Quality | Page-safe bilinear PCF | 8 | No bias and no receiver clamp |

All three use the validated cache, static zero-work path, packet culling,
batching, sorting, and empty-work skips. Unsupported, missing, invalid, dirty,
over-budget, or out-of-range samples fall back to a valid coarser clipmap and
then to white.

The normal surface retains **Profile**, **First Clipmap Extent**, **Maximum
Light Depth**, filter and taps, **Adaptive Filtering**, global resolution bias,
and receiver-distance clamping. **Developer Options** contains:

- **Resources and Cache Policy**
- **Movement and Invalidation**
- **Culling and Raster**
- **Unabstracted**

**Diagnostics** owns benchmarks, detailed stage timing, debug views, and
counters. **Dense Reference** backs all six 8192-square atomic-depth clipmaps
and can require about 1.5 GiB; use it only for validation.

### Diagnostic Cascaded Shadow Maps

Diagnostic CSM is a conventional UE5-style comparison path, not UVSR's
preferred shadow renderer. It supports one through four cascades, opaque and
alpha-tested casters, D16 or D32 depth, manual Gather4 filtering, conservative
caster culling, and whole-map, per-cascade, dirty-rectangle, or integer-texel
scrolling cache updates.

Profiles include **Single-Map Reference**, **Low-Cost CSM**, **UE5 CSM
Reference**, **Cached Single Shadow**, **Optimized Cached Single Shadow**,
**Optimized Cached CSM**, and **Custom**.

The normal surface retains **Enabled**, **Profile**, **Cascades**, **Resolution
Per Cascade**, **Maximum Shadow Distance**, **Maximum Light Depth**, **Filter**,
**Filter Taps**, and **Filter Radius**. **Developer Options** contains:

- **Projection and Bias**
- **Cache Update Policy**
- **Culling and Raster**
- **Unabstracted**

**Diagnostics** owns paired setup, culling, update, raster, and sampling timing,
SVSM match checks, debug views, and live statistics.

## Lights and Camera Flashlight

The **Lights** drawer includes editable scene lights and the camera-mounted
`flashlight_1`; its internal lens hotspot is deliberately not selectable. Press
**F** only when the command bar is closed and ImGui is not capturing text to
toggle the same **Enabled (F)** setting shown in the drawer. UVSR submits the
flashlight and hotspot before ordinary scene lights, with exact-zero output
while disabled.

When `flashlight_1` is selected, controls remain in this order: **Enabled (F)**,
**Cast Shadows**, **Realistic Flashlight**, its animated **Hotspot Size**,
**Hotspot Strength**, **Sway**, and **Aim Correction** region, then
**Brightness**, **Beam Size**, **Beam Roundness**, **Edge Softness**, **Range**,
**Color**, and **Camera Offset**. **Cast Shadows** controls the flashlight's
local planar shadow map and is independent of the directional techniques in
**Shadows**. The 0 to 40 cm Camera Offset moves the emitter sideways while its
beam converges on the camera aim at 6 m, exposing useful shadow parallax.
The default camera-light profile is enabled with **Cast Shadows** and
**Realistic Flashlight**: 600 cd, a 25 degree beam, 0.70 roundness, 0.60 edge
softness, 30 m range, 20 cm camera offset, and `(1.00, 0.80, 0.65)` color. Its
hotspot defaults are 0.40 size, 0.70 strength, 0.20 degree sway, and 0.05 s aim
correction.

The slash interface uses `light.selected` to choose `flashlight_1`. Its
dedicated `light.selected.flashlight.*` commands use the same bounds and reject
generic selected-light properties while the flashlight is selected.

## Sky and Image-Based Lighting

Forward, deferred, and screen-space composition share one persistent global
environment. One scene-linear radiance field produces the Lambert-convolved SH9
`E / pi` diffuse cube, roughness-prefiltered GGX specular cube, and optional
visible background. A source-independent split-sum BRDF LUT completes the
specular receiver.

The factory environment is Poly Haven's CC0 **Day - Kloppenheim 03** at its
calibrated `-2.75 EV`, with diffuse and specular IBL enabled at `1.00`
strength. Common exposure preserves the relationship among diffuse IBL,
specular IBL, and background. Independent strengths apply after exposure;
diffuse strength also scales the environment contribution entering SSGI.

**Ambient Fill** is the master gate for diffuse and specular IBL. Turning it
off preserves the sky background and the retained per-lobe settings, but removes
their beauty-image contribution and resets IBL history. The slash equivalent is
`sky.ambient-fill.enabled`.

The source menu contains six imported HDR radiance fields: three day or
overcast skies, Quadrangle Cloudy, and two night sources. Selecting a source
applies its calibrated default EV. A missing or invalid asset deactivates
environment lighting and background instead of retaining stale data or using an
ambient fallback.

See the [Environment Catalog](../assets/environments/README.md) for source
provenance, hashes, licenses, and calibrated exposures.

UVSR uses a fixed neutral AgX display transform. The retired Tonemapper drawer,
grading presets, LUT loader, and film looks are preserved with their paired
revival contract in the [Tonemapper Drawer and LUTs v1
Postmortem](postmortem/tonemapper-drawer-and-luts-v1.md).

## Settings Interface and Inspection

Settings launch hidden. The first Escape press opens **General** while
Visibility, Buffers, Statistics, Aliasing, Sky, Lights, and Shadows remain
collapsed.
The renderer summary and performance metrics remain pinned above the
independently scrolling settings body. Amp keeps the six metrics on one row.
OG splits them after bandwidth so compute, frame time, and FPS remain visible
on a second row at the narrower stock-font width.

The performance line reports resolution, submitted triangles, current-clock
memory bandwidth, utilization-adjusted current-clock FP32 throughput, frame
time, and FPS. One synchronized snapshot updates renderer, visibility, and
temporal-AA statistics 24 times per second.

Settings layout changes preserve the visible reading anchor with one direct,
current-frame correction to `Scroll.y`, clamped against the live scroll range.
The already-submitted Settings content receives the same visual translation in
that frame, so the state correction cannot leave a one-frame snap behind.
A still-finite ImGui navigation or programmatic scroll target owns its frame and
remains untouched; UVSR does not replace it or carry the same anchor correction
into a later frame.

### Materials

The Materials panel is an independent floating surface and remains composited
when Settings are hidden. It keeps pixel zoom's full resting width and right
edge even while the zoom surface grows, shrinks, or pulses between levels. With
zoom off, it rests one consistent margin from the top edge; with zoom visible,
its top follows the zoom surface's animated lower edge at one consistent
margin. Amp moves the panel down before zoom opens, holds it below zoom until
zoom has completely closed, and applies the same 86-to-100-percent zoom and
fade curve when the panel itself opens or closes. OG resolves every endpoint
immediately.

The title and body use the same two stacked rounded blocks as Settings. Amp
reuses the Settings and command-interface neutral blurred surface, blue
drawer-header title, and outside-only shadow. A translucent light drawer plate
is stacked over the dark body behind the editable controls below **Material
Domain**, matching a Settings drawer body. Texture filenames use a full-opacity
match of the exact authored drawer-header blue instead of green, and **Material
Domain** occupies the same control column width as the material sliders. The
title has no X button. Its triangle closes the complete panel through Amp's
retained zoom-out and fade or OG's immediate endpoint; **M** remains the
full-window toggle.

The panel always represents the fresh current-scene material selected by
**M**. It never opens on a miss or reuses a stale cursor selection, never moves
the camera, and keeps the exact framebuffer-center crosshair visible while it
is open. Controlled experiments suppress new material-inspector picks.

### Slash Command Interface

Press **/** to open or close a focused command bar near the bottom of the renderer. It
always spans the complete margin-to-margin work width, regardless of whether
Settings are expanded, collapsed, or hidden. UVSR permanently reserves its
fixed three-line bottom lane and caps Settings one consistent margin above it,
so same-frame command output cannot resize either surface into the other. Long
results scroll inside the command surface. Amp grows and fades the bar through
the shared 180 ms presentation curve while preserving its full horizontal
extent, keeping keyboard focus live, and using the same neutral,
chroma-suppressed backdrop blur and analytic outside-only shadow pipeline as
Settings and Materials. Its closing surface is noninteractive and releases
shortcut ownership immediately. OG opens
and closes at exact immediate endpoints, retains the outside-only shadow, and
does not blur its backdrop. The bar remains available while a scene is loading.
At a physically impossible resize with no usable Settings region, UVSR
withholds the bar until enough space returns instead of drawing either window
over the other.
Enter parses and queues the command, Tab completes the current verb, path,
action, or value, and Up/Down recalls up to 32 earlier commands. Escape cancels
the active ImGui edit without closing the bar; press **/** again to close it.
While it is open, keyboard input is captured before application shortcuts,
including Alt+Enter, so commands containing V, Z, M, or F cannot move the
camera, toggle another feature, or change fullscreen state.

The grammar is:

```text
/help
/list [path-prefix]
/get <path>
/set <path> <value>
/toggle <boolean-path>
/reset <path|all>
/run <action>
```

Aliases make common operations shorter: `/skin [value]`,
`/ui [show|hide|toggle]`, `/scene [value]`, `/camera [freelook|locked]`,
`/camera-location [piloted|position-1]`, and `/reload-shaders`. Values containing
spaces can be quoted. For example:

```text
/skin og
/list
/get ui.skin
/set sky.diffuse-ibl-strength 1.25
/toggle visibility.enabled
/run screenshot
```

`/list` discovers the complete live catalog. Every user-adjustable Settings
value has one stable lowercase path with `get` and validated `set`; Boolean
values also support `toggle`. A path supports `reset` whenever its visible
control or product contract defines a reset. Every Settings command or button
needed for agent setup is discoverable as a `/run` action. Completion discovers
enum domains, dynamic scene, light, and material choices, and actions without
requiring the corresponding drawer to open.

Commands use the same defaults, numeric ranges, validation, resource and history
side effects, deferred mutation barrier, and availability locks as their visible
controls. Renderer, scene, and camera mutations are rejected while loading or
during a controlled experiment; screenshots are also rejected because their
readback and clipboard work can contaminate a measurement. UI visibility and
skin selection remain available. Accepted commands execute after the complete
ImGui frame. Submission first fast-forwards and commits any older deferred UI
selection at that safe barrier, then applies the newer command, so a stale popup
choice cannot later overwrite it. The immediate OG skin removes presentation
delays without allowing unsafe mutation from inside an active widget or popup.

The footer provides **Reset**, **Screenshot**, **Zoom**, and **Restart**.
Pressing **Z** or **Zoom** cycles off, 2x, 3x, 4x, and 5x. Pixel zoom copies the
untouched scene before UI composition and uses integer source-texel loads, so
the inspected texel becomes an exact 2x2, 3x3, 4x4, or 5x5 destination group.
Amp uses the authored rounded zoom silhouette; OG uses square corners. Both
share the same analytic outside-only shadow. Benchmark runs suspend the
feature, and disabled zoom submits no capture or composite work.

The complete UI hierarchy, animation, input-transaction, copy, and verification
contract is in the [UI Design and Integration
Reference](ui-integration-agent-procedure.md).

## Statistics and Benchmarks

The **Statistics** drawer can isolate the Complete Renderer, Geometry, Direct
Lighting, Screen-Space Visibility, Anti-Aliasing, Screen-Space Directional Shadows,
Sparse Virtual Shadow Maps, Diagnostic Cascaded Shadow Maps, Material Picking,
Environment Background, Tone Mapping, or Output Blit.

Visibility expands into its outer effect envelope, named-stage total, signed
unattributed difference, depth preparation, first trace, later bounces,
reconstruction, upsampling, resolve or application, and composition. Memory
rows report exact **Outputs**, **Working**, **Mask Cache**, and **Avoided**
payloads; **Shared** estimates duplicate mask payload avoided by shared AO/GI
traversal.

**Run Current** measures the effective visibility configuration being rendered.
**Run Current With Motion** executes the current AA configuration through the
fixed Benchmark Position 1 warm, right-45-degree sweep, hold, and return path.
The turn advances exactly `0.375` degrees per rendered frame. The path is
uncapped, so GPU speed changes wall-clock duration without changing its 256
measured camera samples.

### Benchmark Launch

Use the standard launcher with `--benchmark-camera` to select and lock Benchmark
Position 1, enforce a non-resizable 1920x1080 backbuffer, and block fullscreen
transitions:

```powershell
.\tools\launch_uvsr.ps1 -Experiment benchmark --benchmark-camera
```

Run a visibility profile without UI and close after the timing summary:

```powershell
.\tools\launch_uvsr.ps1 -Experiment benchmark --benchmark-camera `
  --visibility-profile runtime-ao-8t --visibility-benchmark `
  --benchmark-warmup 120 --benchmark-frames 240 --benchmark-auto-close
```

Profile matching ignores punctuation and case. `--benchmark-warmup` accepts 0
through 100000 frames; `--benchmark-frames` accepts 1 through 100000. Invalid or
unavailable settings return a nonzero process exit code without opening a modal
dialog.

Add `--visibility-contribution-terminated-bounces` to a GI-capable profile to
disable **Limit Bounces** before an automated run. The result is no longer that
profile's declared bounce contract, so the renderer deliberately clears the
one-click verification label.

Production builds also accept `--aa-enabled`, `--aa-method`, `--aa-quality`
(`--aa-preset` alias), `--aa-sharpness`, `--aa-rectification`, and
`--aa-benchmark-output`. Rectification accepts `preset`, `pair-rgb`, or
`variance-ycocg`.

For controlled performance methodology and the optional same-GPU clock trend,
read [GPU Clock Normalization](performance/gpu-clock-normalization.md). Raw
clean-run GPU time remains the official score.

## Developer Anti-Aliasing Overrides

Experimental AA execution-path overrides require a developer build:

```powershell
cmake -S . -B build-aa-dev -DUVSR_AA_DEVELOPER_OVERRIDES=ON
cmake --build build-aa-dev --config Release --target uvsr
```

A production build rejects `--aa-execution`, `--aa-kernel`, `--aa-lds`,
`--aa-reuse`, `--aa-early`, `--aa-fusion`, and `--aa-cache` when the requested
static PSO is absent.

## Factory-Settings Experiment Build

Repeated code experiments that use only the shader topology selected by a fresh
factory-default launch can reuse a separate opt-in build. Give every concurrent
task its own build directory and replace `<task>` with that task's stable slug:

```powershell
cmake -S . -B build-experiment-<task> `
  -DUVSR_DEFAULT_SETTINGS_EXPERIMENT_SHADERS=ON
cmake --build build-experiment-<task> --config Release --target uvsr
.\tools\launch_uvsr.ps1 -Experiment testing `
  -BuildDirectory build-experiment-<task>
```

Re-run the configure command after HEAD changes so the source revision embedded
in the executable title is current. The `-Experiment` value must be one
lowercase ASCII letters-only token; `testing` is valid, while uppercase, digits,
spaces, hyphens, underscores, and punctuation are not.

As soon as the renderer opens, use the slash interface instead of waiting for
Settings animation:

```text
/skin og
/list
```

Keep using slash paths and actions for setup whenever that is faster than the
menu. OG is session-only, affects presentation rather than renderer output, and
does not change Amp as the normal launch default.

This configuration stages 40 runtime shader blobs, compared with 77 blobs in
the complete production build. It locks renderer-topology drawers to factory
settings and omits Screen-Space Directional Shadows, SVSM, diagnostic CSM,
CMAA2, non-default visibility, and developer shader families. A fresh tree
still compiles Donut's pinned framework catalog once.

The reduced bundle is valid only while the required runtime configuration is
exactly the factory startup shader topology. Discovery and reads remain
available, but commands that would require an excluded shader, permutation,
pass, resource topology, runtime-selectable topology, or factory-locked setting
fail before mutation. They must never report success for state that the next
factory-topology lock would silently overwrite. Switch immediately to the
production catalog, or the developer catalog for developer AA axes, when one of
those limits applies.

Use this mode to shorten repeated UVSR edits, not for release, full-settings,
diagnostic, or benchmark-matrix verification. Final technical verification
uses a full production configuration, every registered Release target, and the
complete suite:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Component-Only Builds

Screen-Space Directional Shadows, SVSM, and diagnostic CSM are separate
static-library targets. A component-only configuration can omit the
application, other producers, and their reference tests.

Build only SVSM and its deterministic reference test:

```powershell
cmake -S . -B build-svsm -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS=OFF -DUVSR_BUILD_DIAGNOSTIC_CASCADED_SHADOW_MAPS=OFF
cmake --build build-svsm --config Release --target uvsr_sparse_virtual_shadow_maps uvsr_sparse_virtual_shadow_map_tests
```

Build only Screen-Space Directional Shadows and its deterministic reference test:

```powershell
cmake -S . -B build-screen-space-directional -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS=OFF -DUVSR_BUILD_DIAGNOSTIC_CASCADED_SHADOW_MAPS=OFF
cmake --build build-screen-space-directional --config Release --target uvsr_screen_space_directional_shadows uvsr_screen_space_directional_shadow_tests
```

Build only diagnostic CSM and its deterministic reference test:

```powershell
cmake -S . -B build-csm -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_SCREEN_SPACE_DIRECTIONAL_SHADOWS=OFF -DUVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS=OFF
cmake --build build-csm --config Release --target uvsr_diagnostic_cascaded_shadow_maps uvsr_diagnostic_cascaded_shadow_map_tests
```

The full application integrates all three producers. Build every registered
Release target and run the complete suite with:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
