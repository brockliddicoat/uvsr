# Advanced Settings and Developer Workflows

UVSR exposes one focused DirectX 12 renderer. Settings begin at factory defaults
on every launch and are not persisted.

## Settings Drawers

The Settings panel contains ten top-level drawers in this order:

1. **General** selects the interface skin, graphics adapter, Adaptive Sync,
   camera, and scene.
2. **Representation** configures the shared BVH, BLAS, and TLAS policies.
3. **Diffuse** controls Occlusion, Illumination, sampling, and
   reconstruction.
4. **Buffers** owns the two retained Visibility precision choices.
5. **Statistics** reports a compact frame summary and a detailed selected effect.
6. **Aliasing** independently enables the temporal, fast approximate,
   morphological, and multisample techniques.
7. **Debug** combines world appearance and effect-specific information views.
8. **Sky** configures the global environment and ambient fill.
9. **Lights** edits scene lights and the camera flashlight.
10. **Shadows** configures independent screen-space and ratio-estimator
    directional shadows.

Escape or `~` opens or closes Settings. A reset icon beside a control restores
that control or group to its current factory value. Q moves the camera up, E
moves it down, and the retired Space and Shift vertical bindings are inert.

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

## Representation

Representation owns UVSR's consumer-neutral world-space triangle hierarchy.
**Bounding Volume Hierarchy** selects Fast Trace, Balanced, or Fast Build for
acceleration-structure construction. **Bottom-Level Acceleration Structures**
selects Rebuild or Refit for changed dynamic geometry. **Top-Level Acceleration
Structure** selects Rebuild or Refit for changed instance transforms. A status
line reports unsupported, inactive, BLAS construction, TLAS construction,
ready, or failed state and the current structure counts.

Construction is lazy until a ray-query consumer is selected. Initial loading
builds one unique-mesh BLAS per presentation frame and then one coherent TLAS.
Changing the hierarchy preference or BLAS policy rebuilds both levels; changing
only the TLAS policy preserves BLAS allocations. Reset and invalidation release
consumer bindings before replacing acceleration structures.

## Diffuse

Diffuse is independent from PBR, lights, sky, shadows, and anti-aliasing.
Enabling or disabling it changes only the Screen-Space Visibility pass and its
resources.

The four quality recipes configure the supported route. The selector shows
**Low**, **Medium**, **High**, or **Ultra**. Editing an owned value preserves
that origin, appends **(Custom)**, and exposes the adjacent circular arrow to
restore the complete High recipe. Each owned control can also return to its
originating recipe value.

The main controls are:

- full, half, or quarter resolution;
- Occlusion enable and strength;
- one-bounce Illumination enable and intensity;
- **Bitmask Approximation**, **Bitmask Directional Visibility**, or **Bitmask
  Cosine Visibility** estimation;
- Permutated White Noise or Void Cluster Blue Noise;
- 1 through 64 samples, radius, thickness, and distribution;
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

Diffuse has no private temporal accumulation, depth hierarchy, recursive
diffuse bounces, resurrection history, benchmark planner, fused ambient-
occlusion-only profile, or separate contrast/power axis.

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
Reconstructive also keeps **Cost** visible beside Quality. Temporal Advanced
begins with an **Algorithm** group. Its first control is **Jitter Sequence**,
with Rotated Grid 4, Uniform Helix 4, Halton 8, Halton 16, Halton 32, and Sobol
32 choices. Filament Halton 16 is the factory default. The selector owns its
own reset and does not make Quality or Cost Custom. Changing it restarts
temporal history at phase zero. **Depth Validation** follows, with Stationary
Bypass and Four-Texel Footprint choices. Reconstruction, history, motion, and
rectification come next. The **Cost** group contains storage, weighting, motion
trust, clipping, blending, and sharpening policies. Inherited dropdowns preview
their effective choice and list every concrete choice once; the adjacent reset
icon reattaches a row to its recipe. Only Preset Sharpening retains an
**(Automatic)** choice. History Frames displays 1 through 32 and History
Strength displays 0 through 200 percent; neither exposes the internal
inheritance value. Recipe-owned Algorithm changes append **(Custom)** to
Quality, while Jitter Sequence remains independent and Cost changes append
**(Custom)** to Cost. Each marker clears after its group returns to the selected
recipe. Each top-level reset arrow restores its complete
factory preset-and-owned-settings group. Choosing a named preset reapplies the
complete group, and choosing a preset-equivalent Advanced value reattaches that
row. Disabling the technique does not erase stored choices.

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

## Debug Drawer

The Debug drawer and each animated effect group start expanded. Every group is
independently collapsible:

- **World** selects Default, White, White Detail, or White Lighting.
- **Visibility** selects Default, Ambient Visibility, Traced Indirect, or
  Applied Indirect.
- **Physically Based Lighting** selects Default or a concise information
  filter such as Surface Normals, Reflectance Response, or Specular Visibility.
- **Screen-Space Shadows** selects Default, Thread Lanes, or Wave Groups.

World appearance changes and information filters are separate state. A
physically based lighting filter keeps Visibility running so its history and
traced data remain valid, but ordinary Visibility lighting does not contaminate
the filtered presentation. An explicit Visibility view takes precedence when
both selectors are active. Shadow isolation remains a deliberate full-image
diagnostic; the unhelpful edge overlay was removed.

## Shadows

Screen-Space Directional Shadows and Ratio-Estimator Ray-Traced Shadows each
have an independent **Enabled** control and both default off. Both can be off,
either can run alone, or both can run together. Both-on takes the componentwise
minimum visibility, preserving the strongest occlusion without multiplying two
estimates of the same blocker. Every active producer requires a primary
directional light.

Screen Space exposes Default, Long, Maximum Validation, and Custom profiles
plus its retained trace and filtering controls. Ratio Estimator uses matched
RGB stochastic numerator and denominator sums and inline ray queries in one
compute dispatch, with no private spatial or temporal denoiser. **Hard Shadows**
takes an early one-center-ray path and rejects surfaces that cannot receive the
selected light before issuing a query. **Animate Samples** sits directly above
the logarithmic **Samples Per Pixel** slider, which covers `1` through `64`.
**Noise Pattern** selects Permutated White Noise or Void Cluster Blue Noise.
Animated samples change the current-frame emitter set independently of TAA;
final-color TAA is the only temporal accumulator. **Ray Bias** moves the origin
along the view-facing raster triangle normal, defaults to `0.002` world units,
and is applied once rather than as `TMin`; larger values can miss nearby blockers
or detach contact shadows without changing ray count or reach. Ratio Estimator
additionally requires
DirectX Raytracing 1.1 and
single-sample deferred rendering. The Representation drawer reports its staged
BVH/BLAS/TLAS readiness. A zero-extent directional emitter takes the hard path;
a primary sun defaults to a `0.53` degree full diameter for physical penumbrae.

See [Heitz Ratio-Estimator Shadows](heitz-ratio-estimator-shadows.md) for the
mathematical contract, framework adaptations, and current ray-query limits.

Sparse virtual shadow maps and diagnostic cascaded shadow maps are not part of
the engine or production shader package. Their pre-removal source is preserved
on the local `codex/svsm-csm-preserved` branch.

## Statistics

Statistics condenses the six general values into one slash-separated line and
shows one selected effect at a time in a labeled, striped two-column table. The
selector contains **Complete
Renderer**, **Scene Setup**, **Geometry**, **Direct Lighting**, **Screen-Space
Visibility**, **Directional Shadows**, **Temporal Reconstructive**,
**Fast Approximate**, **Conservative Morphological**, **Multisample Adaptive**,
**Material Picking**,
**Environment Background**, **Tone Mapping**, and **Output Blit**. Complete
Renderer restores the full stage breakdown, including an available Closest
Surface Resolve; selecting an ordinary stage keeps
the complete frame beside it for context. Directional Shadows includes the
screen-space breakdown plus the complete Ratio-Estimator Ray Dispatch timing.
Visibility, shadows, temporal
reconstruction, and conservative morphology retain their measured stages,
resource or history metrics, and active-work counts. Multisample reports its
requested and hardware-resolved sample counts plus Geometry, Direct Lighting,
and any active Closest Surface resolve. A timing appears only after its
graphics-processor query completes; dormant or newly enabled work reports an
explicit unavailable state instead of a fabricated zero.

There are no built-in benchmark runners, export schemas, thermal planners, or
factory-experiment modes. Performance work should use an isolated build and an
external, task-specific measurement plan.

## Command Interface

Press `/` to open the command bar. Enter applies a command, Tab completes the
current token, Up and Down browse history, and Escape cancels the active edit.
The input reserves one row. Its empty guidance separates those instructions
with slashes and disappears as soon as typing begins. After Enter, that same
input shows a blue `Success` or saturated-crimson `Error` result until the next
command is typed; no floating result bar can cover Settings. When the complete result is
longer than the input, a trailing details button deliberately opens a bounded,
scrollable, selectable read-only view. The catalog mirrors the current
UI-backed settings with 141 entries: 137 values and four actions. Type a section
prefix such as
`representation.`, `visibility.`, `anti-aliasing.taa.`, `anti-aliasing.fxaa.`,
`anti-aliasing.cmaa2.`,
`anti-aliasing.msaa.`, `debug.`, or `shadows.` and use completion to inspect the
exact paths and accepted values. A `list` result uses `/` between each row's
supported verbs and value domain.

Renderer mutations are rejected while a scene load owns renderer resources.
Interface-only commands remain available. Accepted renderer changes use the
same post-ImGui mutation boundary as visible controls.

## Build and Test

The normal build uses one authoritative core manifest, `src/shaders.cfg`, plus
the retained Screen-Space Directional Shadows manifest. Configure, build, and
test from PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release --target uvsr
ctest --test-dir build -C Release --output-on-failure
```

The shader build contains 259 core tasks and 46 Screen-Space Directional Shadow
tasks, for 305 first-party and 381 integrated tasks after Donut's 76.
`uvsr_screen_space_directional_shadows` remains the only specialist renderer
component target. Production/developer/factory manifest forks and
shadow-technique component builds were removed.

## Runtime Validation

Validate a candidate with the exact executable from its isolated build tree.
At minimum, load a bundled scene and exercise Diffuse, each noise pattern,
the independent AA toggles, Debug composition, sky, lights, the flashlight,
all four directional-shadow enable combinations, Ratio Estimator on a
single-sample target, and the Representation rebuild/refit choices. Exercise
hard and soft shadows, 1- and 64-sample endpoints, zero and positive
directional-light angular sizes, zero and default Ray Bias, temporal motion and
disocclusion, and the Ratio-Estimator Ray Dispatch timing. Confirm the Ratio
Estimator's unavailable
explanation under MSAA. A launch alone is not verification; pair
it with the Release build, complete CTest result, shader-package checks, and a
record of the observed scene and settings.
