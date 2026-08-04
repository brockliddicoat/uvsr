# Advanced Settings and Developer Workflows

UVSR exposes one focused DirectX 12 renderer. Settings begin at factory defaults
on every launch and are not persisted.

## Settings Drawers

The Settings panel contains nine top-level drawers in this order:

1. **General** selects the interface skin, graphics adapter, camera, and scene.
2. **Visibility** controls ambient occlusion, indirect diffuse, sampling, and
   reconstruction.
3. **Buffers** owns the two retained Visibility precision choices.
4. **Statistics** reports a compact frame summary and a detailed selected effect.
5. **Aliasing** independently enables the temporal, fast approximate,
   morphological, and multisample techniques.
6. **Debug** combines world appearance and effect-specific information views.
7. **Sky** configures the global environment and ambient fill.
8. **Lights** edits scene lights and the camera flashlight.
9. **Shadows** configures Screen-Space Directional Shadows.

Escape opens or closes Settings. A reset icon beside a control restores that
control or group to its current factory value.

## Visibility

Visibility is independent from PBR, lights, sky, shadows, and anti-aliasing.
Enabling or disabling it changes only the visibility pass and its resources.

The four quality recipes configure the supported route. The selector shows
**Low**, **Medium**, **High**, or **Ultra**. Editing an owned value preserves
that origin, appends **(Custom)**, and exposes the adjacent circular arrow to
restore the complete High recipe. Each owned control can also return to its
originating recipe value.

The main controls are:

- full, half, or quarter resolution;
- ambient occlusion enable and strength;
- one-bounce indirect diffuse enable and intensity;
- Projected Angle, Solid Angle, or Cosine Weighted estimation;
- Permutated White Noise, Hashed White Noise, or Void Cluster Blue Noise;
- 1 through 64 samples, radius, thickness, and distribution;
- one direct-or-guide-aware reconstruction mode, labeled **Full Resolution** at
  full sampling resolution and **Guide-Aware Upsampling** at reduced resolution,
  plus **Packed Depth-Normal**, **Packed Slope-Aware**, or **Packed
  Leak-Controlled**; and
- optional spatial reconstruction.

Ambient Occlusion, Indirect Diffuse, Sampling, and Reconstruction are animated
collapsible groups. Reconstruction starts collapsed when tracing at full
resolution and expanded when a reduced-resolution trace needs reconstruction;
after the first interaction, the user's disclosure choice is preserved. Every
retained setting has a concise hover explanation, and dropdown widths preserve
both the value and its visible label.

Visibility has no private temporal accumulation, depth hierarchy, recursive
diffuse bounces, resurrection history, benchmark planner, fused ambient-
occlusion-only profile, or separate contrast/power axis.

## Buffers

Buffers is a compact precision surface for the two Visibility outputs that
remain in production. **Performance** selects 16-bit floating point for both;
**Maximum Precision** selects 32-bit for both; **Compact Occlusion** keeps
ambient occlusion at 16-bit and indirect diffuse at 32-bit; **Compact
Indirect** uses the opposite combination. The two labeled precision selectors
remain directly editable and participate in Visibility profile custom/reset
tracking.

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

Screen-Space Directional Shadows are the sole retained directional-shadow
technique. They default off and are available only when the scene has a primary
directional light. The drawer exposes Default, Long, Maximum Validation, and
Custom profiles plus the trace and filtering controls required by the retained
pass.

Sparse virtual shadow maps and diagnostic cascaded shadow maps are not part of
the engine or production shader package. Their pre-removal source is preserved
on the local `codex/svsm-csm-preserved` branch.

## Statistics

Statistics condenses the six general values into one dash-separated line and
shows one selected effect at a time in a labeled, striped two-column table. The
selector contains **Complete
Renderer**, **Scene Setup**, **Geometry**, **Direct Lighting**, **Screen-Space
Visibility**, **Screen-Space Shadows**, **Temporal Reconstructive**,
**Fast Approximate**, **Conservative Morphological**, **Multisample Adaptive**,
**Material Picking**,
**Environment Background**, **Tone Mapping**, and **Output Blit**. Complete
Renderer restores the full stage breakdown, including an available Closest
Surface Resolve; selecting an ordinary stage keeps
the complete frame beside it for context. Visibility, shadows, temporal
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
UI-backed settings with 131 entries: 127 values and four actions. Type a section
prefix such as
`visibility.`, `anti-aliasing.taa.`, `anti-aliasing.fxaa.`,
`anti-aliasing.cmaa2.`,
`anti-aliasing.msaa.`, `debug.`, or `shadows.` and use completion to inspect the
exact paths and accepted values.

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

The shader build contains 258 core tasks and 46 Screen-Space Directional Shadow
tasks, for 304 first-party and 380 integrated tasks after Donut's 76.
`uvsr_screen_space_directional_shadows` remains the only specialist renderer
component target. Production/developer/factory manifest forks and
shadow-technique component builds were removed.

## Runtime Validation

Validate a candidate with the exact executable from its isolated build tree.
At minimum, load a bundled scene and exercise Visibility, each noise pattern,
the independent AA toggles, Debug composition, sky, lights, the flashlight,
and Screen-Space Directional Shadows. A launch alone is not verification; pair
it with the Release build, complete CTest result, shader-package checks, and a
record of the observed scene and settings.
