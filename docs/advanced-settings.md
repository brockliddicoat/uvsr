# Advanced Settings and Developer Workflows

UVSR exposes one focused DirectX 12 renderer. Settings begin at factory defaults
on every launch and are not persisted.

## Keyboard and Interface Controls

- **Escape** or **~** opens and closes Settings.
- **/** opens the command interface. Enter applies, Tab completes, Up and Down
  recall history, and Escape cancels the active edit.
- **M** opens or closes the Material drawer for the surface at screen center.
- **F** toggles the camera flashlight while the command interface is closed and
  text input is inactive.
- **Z** or **Zoom** cycles Off, 2x, 3x, 4x, and 5x pixel inspection.
- In **Freelook**, **W/S** dollies forward and backward, **A/D** strafes, and
  **Q/E** moves vertically. Space and Shift are not movement modifiers.
- The **arrow keys** look left, right, up, and down; **X/C** rolls; and **V**
  restores upright roll.
- Hold the **left mouse button** and drag to look. The mouse wheel dollies
  forward and backward. Stationary trackpad contact does not cancel motion; a
  real camera input does.
- **Capture** copies the current frame to the clipboard. The other footer
  actions reset settings, control pixel zoom, and restart the renderer.

## Settings Drawers

Performance is an independently collapsible panel immediately above and fully
detached from Settings. The panels use the same separation as adjacent
top-level drawers, retain independent fully rounded silhouettes, and keep smooth
authored opening and closing motion. It reports a compact per-window frame
summary and selected renderer timings.
The compact summary remains visible below the title when Performance is fully
collapsed; only its selector and table close. The summary is vertically centered
with equal top and bottom breathing room, and only its retained body fades toward
an opaque surface during collapse so scene detail cannot overpower the line.

The menu uses one scaled 1x/2x/4x spacing ladder. At 100 percent display scale,
1x is 4 pixels for adjacent drawers, the Performance-to-Settings gap, and footer
button gaps, as well as the Settings-to-command-interface gap; 2x is 8 pixels
for the Settings title-to-General inset and body padding; and 4x is 16 pixels
for outer panel and command-interface margins.

The Settings panel contains thirteen scrolling top level drawers in this order:

1. **General** selects the graphics adapter, Adaptive Sync, camera, and scene.
2. **Representation** controls whether ray traversal is allowed and configures
   the shared BVH, BLAS, and TLAS policies.
3. **Noise** defines the shared precomputed noise pattern, resolution, and
   animation policy.
4. **Diffuse** controls Occlusion, Illumination, sampling, and
   reconstruction.
5. **Denoising** selects optional NVIDIA NRD processing for AO, GI, shadows,
   and sky visibility.
6. **Buffers** owns the two retained Visibility precision choices.
7. **Aliasing** independently enables the temporal, fast approximate,
   morphological, and multisample techniques.
8. **Debug** combines world appearance and effect specific information views.
9. **Sky** configures the global environment, display exposure, and ambient
    fill.
10. **Lights** edits scene lights and the camera flashlight.
11. **Shadows** configures ray traced sun shadows.
12. **Material** selects and edits the surface under the center crosshair.
13. **Interface** selects the skin and edits its RGBA accent, font, and
    background roles.

Escape or `~` opens or closes Settings. A reset icon beside a control restores
that control or group to its current factory value. Q moves the camera up, E
moves it down, and the retired Space and Shift vertical bindings are inert.
V restores an upright camera roll. A stationary held trackpad touch does not
cancel that leveling motion; new camera-look movement or another real camera
input does.

**Camera Location > Position 1** is the exact
spawn view for every loaded scene. Press M to open or refresh the Material
drawer's center selection; close it with the drawer header. Settings use the full
available height until the slash command interface opens, then shrink smoothly
above its bottom-centered two-axis entrance. Settings retain the same ordinary
inset below the title as along the body sides and bottom. A late topmost-child
opaque frame fills the complete rounded margin, including all four
outer-to-inner corner wedges, while a shallow shadow remains above whichever
ordinary drawer reaches the viewport edge. Performance uses the same filled
frame and inner outline so its retained table lines terminate cleanly. The
scrolling child and scrollbar still begin at General. Amp uses one shared corner
radius for the panel frame, drawer bodies, controls, and scrollbar; the
scrollbar keeps a one-pixel inset against the outline. At 100 percent scale its
12-pixel channel produces a 10-pixel visible grab, the minimum standard geometry
that preserves the shared 4-pixel radius.
The fully visible command interface uses the same opaque body surface as the
collapsed Performance summary; only its whole-window appearance transform
changes opacity during entry and exit.
The authored stack targets 23.44 font heights, exactly 20 percent narrower than
the previous layout, but never contracts below its longest intentional control
plus padding and the scrollbar.
The four footer actions are **Reset**, **Capture**, **Zoom**, and
**Restart**; Capture copies the current frame to the clipboard. Reset restores
renderer settings, Adaptive Sync, Interface skin and colors, animation and
visual-maximum preferences, and the default collapsed Complete Renderer
Performance view. It
preserves the camera, scene, active graphics adapter, and command history.

## Interface

The first Interface controls are **Disable Animations** and **Override Visual
Maxes**. Disable Animations moves every authored drawer, panel, slider,
command-interface, and zoom transition directly to its current endpoint.
Override Visual Maxes defaults off. Enabling it lets exact numeric entry exceed
a shortened visual track up to that setting's safe logical limit; pointer and
navigation travel remain on the compact track. Disabling it again preserves an
already entered supported value. **Interface Skin** selects Amp or Ogg. Amp
owns the animated UVSR presentation; Ogg uses stock ImGui presentation and
immediate endpoints. Performance and Settings use the same authored root-panel
motion as the drawers in Amp.

**Primary Accent** edits Amp's drawer and panel headers, footer buttons,
selection details, checkmarks, and raised slider knobs. Ogg disables this row
because stock ImGui does not have one honest primary-accent owner. **Secondary
Accent** defaults to historical translucent Amp blue `#4296FA4F` and drives
errors, negative state, and authored toggle-off knobs. **Tertiary Accent**
defaults to the historically compensated source blue `#1E3757FF`; it was tuned
to retain the accepted blue appearance over the light enabled track and drives
success, positive state, Material status, and authored toggle-on knobs. Every
editor includes alpha.

**Font Color** for all authored copy and **Primary Background Color** for the
menu body and resting closed controls follow the three accent rows directly;
there is no separate Advanced Accents submenu. Hover, active, and strengthened
body opacity are derived from the resting background. Ultra-bright Primary
Accent surfaces automatically use a transparent dark depth gradient. Slider
tracks follow Primary Background Color so the Primary Accent knob remains
distinct.
These interface choices, including Override Visual Maxes, are session-only and
are restored by footer Reset or `/reset all`.

Authored Primary Accent slider knobs retain a subtle raised depth surface. When
a parent setting gates a slider, the control moves to the renderer's effective
value without erasing the stored preference and eases into or out of grayscale
over 280 milliseconds; enabling the parent moves it back to that preference.
A fixed right-hand value bubble inside the existing control width is twice the
authored toggle width and opens exact numeric input with one click. Its inactive
copy has exactly four digit glyphs, excluding a minus sign or decimal point, so
examples include `0.200`, `16.00`, `123.5`, and `1235`. This presentation never
rounds the stored setting; active input uses undecorated native precision. It
remains 2 pixels from the track bubble so both controls retain four inner
fillets. Authored color components also omit `R:`, `G:`, `B:`, and `A:`
prefixes. Ogg keeps stock ImGui slider and color-editor presentation.

Authored combo options roll into view over 180 milliseconds and roll away after
selection. A renderer-changing choice remains queued until that roll completes,
the existing 250-millisecond settle expires, and one complete idle UI frame has
been presented. Ogg retains immediate stock popup behavior.

Every first-party Interface, Material, flashlight, and scene-light color editor
uses one shared policy and the same authored popup. It has a hue wheel with a
finely tessellated rounded saturation/value triangle and four equal vertical
lanes aligned exactly to the fourth component column: hue, alpha, Current, and
Original. RGB controls keep the alpha lane in the same position but render it
as disabled neutral gray without reading or writing alpha. The RGB, HSV, and
hex rows omit their redundant preview squares and fill the available width;
visible control labels are not repeated as popup headings. One intermediate
hollow-circle size is shared by wheel, selector, endpoint, hue-bar, and
alpha-bar markers, while larger invisible snap zones keep exact hue, white, and
black pointer reachable.

The popup base and two interior depth layers are translucent. Only a two-pixel
outer rim and the pointer frame are opaque; the selector, bars, checker, and
comparison colors remain opaque at steady state so scene color cannot mix into
the selected color. Pickers begin at the Settings content-right edge and sit
flush with the current Settings bottom edge. Their body zooms and fades
reversibly on open and close, while the rounded source pointer stays attached to
the edited swatch. A Settings scroll requests the same animated close;
disabling animations snaps to the endpoint. Generic and Ogg popups keep stock
behavior. Repeated wheel input beyond the top or bottom of Settings stays locked
to that endpoint rather than adding a second scroll-anchor correction.

## General

**Graphics Adapter** selects the DirectX 12 device and restarts UVSR when it
changes. **Adaptive Sync** follows it directly and offers **Off**, **Vendor
Agnostic**, and **Nvidia Exclusive**. Off suppresses the windowed DXGI Present
allow-tearing flag. Both enabled choices request the same Windows
tearing-compatible presentation path while VSync remains disabled; Nvidia
Exclusive is offered only on NVIDIA adapters. Windows, the driver, and the
display decide whether variable refresh actually engages, and UVSR cannot
confirm that state. Systems without DXGI tearing-present support expose Off
only. The reset restores Nvidia Exclusive on a supported NVIDIA adapter, Vendor
Agnostic on any other supported adapter, and Off when the path is unsupported.

During scene loading, the second status line shows `x/average` and updates every
20 milliseconds.
The `x` counter advances once every 20 milliseconds instead of depending on a
loader stage, and the denominator is the completed-load average for that scene
or the all-scene average when no scene-specific sample exists. `--` is shown
until a completed load establishes a baseline, so a value beyond the
denominator directly indicates a slower-than-usual load. The phase label is
followed by the missing colon. Successful per-scene and all-scene aggregates
are persisted in the current Windows user's local application data; failed
imports never enter the average. A failed asynchronous import leaves loading
state cleanly and exposes **Retry Scene Load** instead of leaving the counter
running forever.

## Representation

Representation owns UVSR's consumer neutral world space triangle hierarchy.
**Allow Ray Traversal** is the master permission for every ray traced effect.
Turning it off stops sky visibility, sun shadows, flashlight shadows, and any
other traversal consumer without clearing their individual settings. Turning it
back on lets the selected effects resume from their stored configuration.

**Bounding Volume Hierarchy** selects Fast Trace, Balanced, or Fast Build for
acceleration structure construction. **Bottom-Level Acceleration Structures**
selects Rebuild or Refit for changed dynamic geometry. **Top-Level Acceleration
Structure** selects Rebuild or Refit for changed instance transforms. A status
line reports unsupported, inactive, BLAS construction, TLAS construction,
ready, or failed state and the current structure counts. Inactive is the
subdued status itself rather than a second explanatory line.

Construction is lazy until a ray-query consumer is selected. Initial loading
builds one unique-mesh BLAS per presentation frame and then one coherent TLAS.
Changing the hierarchy preference or BLAS policy rebuilds both levels; changing
only the TLAS policy preserves BLAS allocations. Reset and invalidation release
consumer bindings before replacing acceleration structures.

Opaque and alpha-tested triangle geometry participates in the shared
representation. Ray queries evaluate the authored base-color alpha and cutoff
before committing an alpha-tested candidate, so image-backed foliage and fences
cast their cutout silhouette rather than the rectangular mesh card. Blended and
transmissive material domains are excluded from these binary visibility
queries.

## Noise

The Noise drawer defaults to **Spatiotemporal Blue**, **128x128**, and animated
sampling. **Spatial White**, **Spatial Blue**, and **Spatiotemporal Blue** are
precomputed `R8_UNORM` textures available at **64x64**, **128x128**, **256x256**,
and **512x512**. Tiles are anchored to the center of each effect's local
dispatch, so clipped work at one screen edge follows the same mapping as clipped
work at another edge.

Diffuse visibility, Ray Traced Shadows, Ray Traced Sky Visibility, and finite
flashlight shadows inherit the drawer by default. The first three expose
**Specify Noise**; enabling it reveals that effect's private Pattern,
Resolution, and Animate Samples controls without changing another effect. AO
and diffuse illumination share one visibility dispatch and therefore share one
override. The flashlight always uses the global configuration and owns its own
sample phase. See [Noise](noise.md) for assets, memory, phase, and provenance
details.

## Diffuse

Diffuse is independent from PBR, lights, sky, shadows, and anti-aliasing.
Enabling or disabling it changes only the Screen Space Visibility pass and its
resources.

The four quality recipes configure the supported route. The selector shows
**Low**, **Medium**, **High**, or **Ultra**. Editing an owned value preserves
that origin, appends **(Custom)**, and exposes the adjacent circular arrow to
restore the complete High recipe. Each owned control can also return to its
originating recipe value.

The main controls are:

- full, half, or quarter resolution;
- Occlusion enable, strength from 0 through 8, and optional **Output Hit
  Distance**;
- one-bounce Illumination enable and intensity, with a factory intensity of 1;
- **Bitmask Approximation**, **Bitmask Directional Visibility**, or **Bitmask
  Cosine Visibility** estimation;
- an optional **Specify Noise** override for the shared AO/GI dispatch;
- 1 through 64 samples, radius, thickness, and distribution from 0.25 through
  8;
- one direct-or-guide-aware reconstruction mode, labeled **Full Resolution** at
  full sampling resolution and **Guide-Aware Upsampling** at reduced resolution,
  plus **Packed Depth-Normal**, **Packed Slope-Aware**, or **Packed
  Leak-Controlled**; and
- optional spatial reconstruction.

Occlusion, Illumination, Sampling, and Reconstruction are animated
collapsible groups. Reconstruction starts collapsed when tracing at full
resolution and expanded when a reduced-resolution trace needs reconstruction;
after the first interaction, the user's disclosure choice is preserved. Every
retained setting has a concise hover explanation, and dropdown widths preserve
both the value and its visible label.

The factory High recipe uses 16 samples. Illumination has its own **Output Hit
Distance** control. Both hit outputs are off by default and allocate an R16
physical distance texture only while requested. They do not trace extra
samples, but they add a shader output, storage, and memory traffic. Selecting a
denoiser does not silently enable either producer control.

Diffuse has no private temporal accumulation, depth hierarchy, recursive
diffuse bounces, resurrection history, benchmark planner, fused ambient
occlusion only profile, or separate contrast/power axis.

## Denoising

The Denoising drawer remains visible in every build, but processing is available
only when UVSR is built with the optional NVIDIA NRD backend. A build without
NRD places its short availability note after the signal groups so it cannot
push the controls away from the drawer header. Each signal starts with
**Method: None**, while its stored controls start at **Balanced**, **Half**, and
16 history frames. Method selection is independent for each signal:

- AO supports None or ReBLUR.
- GI supports None, ReBLUR, or ReLAX.
- Shadows supports None or SIGMA.
- Sky Visibility supports None, ReBLUR, or ReLAX.

Every active method exposes Performance, Balanced, Quality, and Ultra quality,
plus Quarter, Half, and Full processing resolution. ReBLUR and ReLAX also expose
history, disocclusion, and response controls. Shadows expose the SIGMA settings
that NRD actually consumes: quality controls sun stabilization, disocclusion
controls sun history rejection, and resolution controls both sources.
Flashlight SIGMA is spatial only because reliable local light reprojection is
not available; it keeps an independent signal state without exposing inactive
history or response controls.

NRD needs a noisy signal and physical hit distance data. AO, GI, sky
visibility, sun shadows, and flashlight shadows therefore retain explicit
**Output Hit Distance** switches with a default of off. Sky and sun also retain
independent **Ratio Estimator** switches with a default of on. Sky ReBLUR or
ReLAX and sun SIGMA consume the raw one ray producer route, so sky or sun
denoising also requires its Ratio Estimator to be off. Changing a denoising
choice never changes those producer switches. Missing data, an unsupported
combination, or an unavailable backend leaves the raw signal in use and reports
the reason.

Performance keeps each denoising dispatch separate from its producer. The
Complete Renderer table and relevant effect table therefore report **Ambient
Occlusion Denoise**, **Diffuse Illumination Denoise**, **Shadow Denoise**, and
**Sky Visibility Denoise** independently from their raw trace or ray-dispatch
cost.

AO and GI keep their established aggregate estimators. AO's guide is the
expected first bounce distance over the same equal measure sector mask, with
visible sectors censored at the configured radius. GI's guide is the first
bounce distance weighted by the NRD luminance of the exact RGB contributions
that form raw GI. These matched first moments let NRD filter the original
signals instead of substituting a different occlusion or illumination model.

The normal build leaves NRD out. Enabling it requires both
`UVSR_WITH_NRD=ON` and explicit acknowledgement with
`UVSR_ACCEPT_NRD_LICENSE=ON` after reviewing NVIDIA's license. The packaged
executable copies that license beside the optional backend.

## Buffers

Buffers is a compact precision surface for the two Visibility outputs that
remain in production. **Performance** selects 16-bit floating point for both;
**Maximum Precision** selects 32-bit for both; **Compact Occlusion** keeps the
Occlusion output at 16-bit and the Illumination output at 32-bit; **Compact
Indirect** uses the opposite combination. The two precision selectors are
labeled **Occlusion** and **Illumination**, remain directly editable, and
participate in Visibility profile custom/reset tracking.

## Aliasing

TAA, Fast Approximate AA, CMAA2, and MSAA are separate checkboxes and all
default off. They can be combined. The execution order is deferred MSAA
resolve, TAA, tone mapping, display-linear Fast Approximate AA, then
display-linear CMAA2.

The four animated technique sections are **Temporal Reconstructive**, **Fast
Approximate**, **Conservative Morphological**, and **Multisample Adaptive**.
Each has an **Enable** control, a visible **Quality** selector with Low, Medium,
High, and Ultra choices, and its own default-closed **Advanced** tree. Temporal
Advanced opens directly on its current algorithm controls. Its first control is
**Jitter Sequence**,
with Rotated Grid 4, Uniform Helix 4, Halton 8, Halton 16, Halton 32, and Sobol
32 choices. Filament Halton 16 is the factory default. The selector owns its
own reset and does not make Quality or Cost Custom. Changing it restarts
temporal history at phase zero. **Depth Validation** follows, with Stationary
Bypass and Four-Texel Footprint choices. Reconstruction, history, motion, and
rectification come next. A default-closed **Cost** submenu is last. Its **Mode**
selects Full Quality, Reduced, or Minimum, followed by storage, weighting,
motion trust, clipping, blending, and sharpening policies. Inherited dropdowns preview
their effective choice and list every concrete choice once; the adjacent reset
icon reattaches a row to its recipe. Only Preset Sharpening retains an
**(Automatic)** choice. History Frames displays 1 through 32 and History
Strength displays 0 through 200 percent; neither exposes the internal
inheritance value. Recipe-owned Algorithm changes append **(Custom)** to
Quality, while Jitter Sequence remains independent and Cost changes append
**(Custom)** to the Cost Mode. Each marker clears after its group returns to the
selected recipe. The Quality reset restores its complete factory
preset-and-owned-settings group; the Cost Mode reset does the same for Cost.
Choosing a named preset reapplies the complete group, and choosing a
preset-equivalent Advanced value reattaches that row. Disabling the technique
does not erase stored choices.

Fast Approximate Quality owns Edge Sharpness, Relative Edge Threshold, and
Minimum Edge Threshold. Low uses 2, 0.25, and 0.06; Medium uses 4, 0.1875, and
0.055; High uses 8, 0.125, and 0.05; Ultra uses Filament's 8, 0.08, and 0.04.
Advanced exposes those three controls over their source-backed ranges.

CMAA2 Quality owns **Edge Threshold** and **Detector**. Low, Medium, High, and
Ultra use thresholds 0.15, 0.10, 0.07, and 0.05 respectively; Low through High
use Luma detection, while Ultra uses Full Color. Advanced exposes the continuous
0.05-through-0.15 threshold and Luma/Full Color detector. CMAA2 remains
LDR/display-linear only; the retired HDR variant is not compiled.

Multisample Adaptive Quality maps Low, Medium, High, and Ultra to 2x, 4x, 8x,
and 16x respectively. Advanced retains direct sample-count selection and falls
back with a visible diagnostic when the active adapter cannot provide the
requested topology.

See [Temporal Aliasing Options](temporal-aa-options.md) for the retained
history and coordinate contracts.

## Sky

**Auto Exposure** is its own animated subsection. It starts expanded like the
Aliasing technique sections, defaults off, and shows only **Enable** while off.
When enabled, a GPU luminance histogram meters the median scene luminance,
targets 18% middle gray, and adapts display exposure over time without changing
scene lighting, ray effects, or their histories. **Maximum Brightening** and
**Maximum Darkening** each span `0` through `16 EV`. Maximum Brightening
defaults to `5 EV`, while Maximum Darkening defaults to `2 EV`.
They bound how far the automatic target may raise or lower exposure from unity.
**Exposure Compensation** is applied after that automatic bound and biases the
result from `-18 EV` through `+8 EV`. **Adjustment Period** spans `0.05` through
`5.00` seconds and sets the half-life of the remaining exposure-value
difference; its default is `0.20` seconds. Adaptation interpolates in
exposure-value space so equal bright and dark changes respond symmetrically.

The automatic exposure pass runs after TAA and before the neutral AgX transform.
When Auto Exposure is off, UVSR selects the exact established texture-only,
buffer-free AgX presentation. No automatic exposure buffer or automatic shader
permutation is used, so the disabled feature cannot change the rendered color.
The enabled route differs by applying its bounded exposure multiplier to
scene-linear input before the same established AgX transform; it does not
replace that transform's clamps or output handling.

Ray Traced Sky Visibility is off by default and traces the current frame at full
resolution. Its independently collapsible section remains closable while the
effect is enabled, matching the other effect sections. **Effect Diffuse** and
**Effect Specular** both default on, so enabling sky
visibility applies the same geometric visibility to the diffuse and specular
environment response. Either application can still be disabled independently.
Diffuse application also affects diffuse IBL before it becomes GI source
radiance.

**Ratio Estimator** defaults on and uses the selected 1, 2, 4, 8, 16, 32, or 64
samples. Turning it off selects one raw stochastic visibility ray for a cleaner
ReBLUR or ReLAX input. **Output Hit Distance** is independent, defaults off, and
emits the nearest committed physical distance or the documented miss value.
Both controls preserve the other sky settings.

**Specify Noise** reveals a private pattern, resolution, and animation policy;
otherwise this effect inherits the Noise drawer. Animated sampling advances its
private phase only after a successful dispatch.
**Max Distance** defaults to Max, which preserves the scene diagonal reference
reach. The `32m`, `16m`, `8m`, `4m`, and `2m` choices intentionally ignore
farther occluders and are bounded visibility rather than exact sky visibility.
**Ray Bias** uses the same geometric normal origin clearance policy as ray
traced sun shadows. Disabled, unsupported, unavailable, and enabled with neither
IBL consumer states supply white visibility and preserve the old image.

## Debug Drawer

The Debug drawer and each animated effect group start expanded. Every group is
independently collapsible:

- **World** selects Default, White, White Detail, or White Lighting.
- **Visibility** selects Default, Ambient Visibility, Traced Indirect, or
  Applied Indirect.
- **Physically Based Lighting** selects Default or a concise information
  filter such as Surface Normals, Reflectance Response, or Specular Visibility.

World appearance changes and information filters are separate state. A
physically based lighting filter keeps Visibility running so its history and
traced data remain valid, but ordinary Visibility lighting does not contaminate
the filtered presentation. An explicit Visibility view takes precedence when
both selectors are active. The removed screen space directional shadow
diagnostics do not remain as hidden main branch state.

All three Debug selectors defer their renderer mutation until the native popup
has closed, the 250-millisecond settle interval has elapsed, and one complete
idle Settings frame has been presented. Selecting a White world mode therefore
does not rewrite materials or recreate render passes inside the popup's click
frame.

## Lights

The flashlight is one analytical physical spot light in scene submission and
deferred PBR. Its two lobe beam profile shapes that exact light; it does not add
a duplicate hotspot light or a private raster shadow map. **Cast Shadows**
defaults on and uses finite ray traced visibility from the surface toward the
emitter. It respects Representation's **Allow Ray Traversal** master switch.

The flashlight defaults to a 16 degree beam, 0.8 roundness, and a 2.86 degree
full **Angular Size** at the one-metre reference distance. The selectable
`0`-through-`20` degree angular size controls the analytical spherical emitter
used by both direct-light energy and finite flashlight shadow rays, so its
apparent size changes naturally with receiver distance. Positive size bounds
near-field irradiance and converges to the same luminous-intensity inverse
square result in the far field. `0` selects the exact point-light and hard
center-ray branches. A positive size traces four animated, noise-shifted rays
over the emitter's visible spherical cap and averages their visibility, which
creates a distance-dependent fractional penumbra before optional SIGMA or TAA.
Its factory color is pure linear white; the Color control remains available for
intentional tinting.
The flashlight also uses a dedicated collision sphere whose radius covers both
the camera collision radius and the analytical emitter. The authored camera
mount is swept continuously through the static collision hierarchy, and a
stationary overlap is repaired when the emitter radius changes. This prevents
the light volume from crossing a wall without using scene depth to retract the
mount or retarget the beam. Lens sway is applied afterward to direction only
and cannot change the collision solution or resolved light position.

The rejected receiver-driven centering work, including its depth probes,
temporal controls, diagnostics, observed pillar sticking, and future
investigation notes, is preserved in
[Flashlight Camera Centering v1](postmortem/flashlight-camera-centering-v1.md).
Independent
**Horizontal Offset** and **Vertical Offset** controls cover `-40 cm` through
`40 cm`. **Output Hit Distance** defaults off and supplies the physical blocker
distance needed by SIGMA. Flashlight shadow data is matched to the exact
flashlight instance before deferred lighting applies it.

## Shadows

**Ray Traced Shadows** controls visibility for the primary directional sun and
defaults off. The sun itself initializes with irradiance `8` and a `0.2`
degree full angular size. A zero angular size or **Hard Shadows** selects one
center ray.

**Ratio Estimator** defaults on. Its soft route uses matched RGB stochastic
numerator and denominator sums in one inline ray query dispatch. Turning Ratio
Estimator off uses one raw scalar stochastic ray, replicated for deferred PBR,
which is the input expected by SIGMA. **Output Hit Distance** is independent,
defaults off, and writes the nearest physical blocker distance. SIGMA needs both
the raw route and hit output; choosing SIGMA in Denoising does not change either
producer switch.

**Samples Per Pixel** covers `1` through `64` while Ratio Estimator is active.
**Specify Noise** reveals this effect's private pattern, resolution, and Animate
Samples controls; otherwise it inherits the Noise drawer. **Max Distance** defaults to
the scene diagonal Max reference; finite `32m` through `2m` modes intentionally
ignore farther blockers. **Ray Bias** moves the origin once along the view
facing raster triangle normal, defaults to `0.002` world units, and can detach
nearby contact shadows when raised. Ray traced sun shadows require DirectX
Raytracing 1.1, a ready Representation hierarchy, and single sample deferred
rendering.

All three material-aware ray-query effects use the same alpha-test candidate
contract. Alpha-tested sun-shadow occluders commit only where base-color alpha
passes the material cutoff; blended and transmissive materials do not become
binary shadow blockers.

See [Ratio Estimation](ratio-estimation.md) for the
mathematical contract and the raw denoising route.

Screen space directional shadows are absent from main. Their implementation is
quarantined with the CSM and SVSM experiments on the local
`codex/svsm-csm-preserved` branch and is not compiled or packaged by the normal
renderer.

## Performance

The detached Performance panel shows resolution, submitted triangles, frame
time, and frame rate in one slash-separated line. Renderer identity remains in
General. Amp draws the same opaque rounded inset frame around Performance and
Settings after their content, so every corner wedge is filled and Performance
table lines terminate at the inner outline. Its collapsed summary is centered
inside balanced vertical padding while the retained body alone fades toward
opaque as collapse progresses. The selector uses the same inset and width as
ordinary long General controls and shows one selected view at a time in a
labeled, striped two-column table. It contains **Complete Renderer**, **Scene
Setup**, **Geometry**, **Direct Lighting**, **Screen Space
Visibility**, **Directional Shadows**, **Temporal Reconstructive**,
**Fast Approximate**, **Conservative Morphological**, **Multisample Adaptive**,
**Material Picking**,
**Environment Background**, **Tone Mapping**, and **Output
Blit**. Complete
Renderer restores the available stage breakdown, including Closest Surface
Resolve when active. Timing rows with no current measurement are omitted
instead of displaying `--`; meaningful resource, count, format, and status rows
retain their unavailable-state text. The multisample-only base-lighting pass
that prepares Screen
Space Visibility is reported separately as **Visibility Lighting Preparation**
instead of being folded into either Direct Lighting or Visibility. Selecting an ordinary stage keeps
the complete frame beside it for context. Directional Shadows includes
**Shadow Ray Dispatch**, **Shadow Denoise**, **Sky Visibility Ray Dispatch**,
and **Sky Visibility Denoise** as independent rows.
Visibility, shadows, temporal
reconstruction, and conservative morphology retain their measured stages,
resource or history metrics, and active-work counts. Multisample reports its
requested and hardware-resolved sample counts plus Geometry, Direct Lighting,
Visibility Lighting Preparation, and any active Closest Surface resolve. A
timing appears only after its graphics-processor query completes; dormant or
newly enabled timing work stays hidden instead of reporting a fabricated zero.

There are no built-in benchmark runners, export schemas, thermal planners, or
factory-experiment modes. Performance work should use an isolated build and an
external, task-specific measurement plan.

## Command Interface

Press `/` to open the command bar. Enter applies a command, Tab completes the
current token, Up and Down browse history, and Escape cancels the active edit.
The input reserves one row. Its empty guidance separates those instructions
with slashes and disappears as soon as typing begins. After Enter, that same
input shows `Success` in the configured positive accent or `Error` in the
configured negative accent until the next command is typed; no floating result
bar can cover Settings. When the complete result is
longer than the input, a trailing details button deliberately opens a bounded,
scrollable, selectable read only view. The catalog mirrors the current UI
settings. Type a section prefix such as
`representation.`, `noise.`, `visibility.`, `denoising.`, `anti-aliasing.taa.`, `anti-aliasing.fxaa.`,
`anti-aliasing.cmaa2.`,
`anti-aliasing.msaa.`, `debug.`, or `shadows.` and use completion to inspect the
exact paths and accepted values. A `list` result uses `/` between each row's
supported verbs and value domain.

Renderer mutations are rejected while a scene load owns renderer resources.
Interface-only commands remain available. Accepted renderer changes use the
same post-ImGui mutation boundary as visible controls.

## Build and Test

The normal build uses one authoritative first party shader manifest,
`src/shaders.cfg`. Configure, build, and test from PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release --target uvsr
ctest --test-dir build -C Release --output-on-failure
```

Screen space directional shadows have no main branch target or shader manifest.
An NRD build is intentionally explicit:

```powershell
cmake -S . -B build-nrd -DUVSR_WITH_NRD=ON -DUVSR_ACCEPT_NRD_LICENSE=ON
cmake --build build-nrd --config Release --target uvsr
```

The second option records that the builder reviewed and accepted NVIDIA's NRD
license. A normal build keeps the backend unavailable while retaining the same
settings and raw signal fallback contract.

## Runtime Validation

Validate a candidate with the exact executable from its isolated build tree.
At minimum, load a bundled scene and exercise Diffuse, all four noise
resolutions and three patterns, inheritance and each Specify Noise override,
the independent AA toggles, Debug composition, sky, lights, the flashlight,
Ray Traced Shadows on a single sample target, and the Representation
rebuild/refit choices. Confirm that **Allow Ray Traversal** stops sky, sun, and
flashlight queries while preserving every individual setting. Exercise hard and
soft sun shadows, both Ratio Estimator states, both Output Hit Distance states,
1 and 64 sample endpoints, zero and positive directional light angular sizes,
zero and default Ray Bias, cutout foliage in all three ray-query effects, and
the separate Shadow Ray Dispatch and denoising timings. Confirm the unavailable
explanation under MSAA. Confirm that disabled Auto Exposure shows only Enable
and exactly matches the established manual AgX presentation. Enable it, compare
its median-luminance adaptation across bright, dark, neutral, and saturated
views, exercise both Maximum Brightening and Maximum Darkening endpoints,
Exposure Compensation endpoints, and Adjustment Period endpoints, and verify
equal-magnitude brightening and darkening changes settle symmetrically. For the
flashlight, compare Angular Size zero, default, and maximum around a thin
occluder. Approach a wall head-on and along a corner, then change Angular Size
at contact; verify the emitter sphere never crosses the collision surface and
that moving away restores the authored offset. Pan across rows of pillars and
confirm scene-depth changes do not retract the mount or retarget the beam.
Change Sway and confirm it changes only beam direction, never the physical
mount position. Reset Color and confirm the beam returns to pure white.
restores smoothly without crossing the surface. Hold a stationary trackpad
touch while pressing V and confirm roll leveling completes; then confirm actual
camera-look movement cancels it.

On an NRD build, exercise every supported method, quality, resolution, and
history endpoint. Verify that a missing hit output or an active sky/sun Ratio
Estimator reports raw fallback without changing the producer setting. Confirm
separate sun and flashlight SIGMA histories through motion and disocclusion.
A launch alone is not verification; pair
it with the Release build, complete CTest result, shader-package checks, and a
record of the observed scene and settings.
