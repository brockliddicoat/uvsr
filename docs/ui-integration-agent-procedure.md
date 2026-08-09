# UVSR UI Design and Integration Reference

UI reference version: `2026-08-09.2`.

## Purpose

This is the canonical reference for UVSR-owned Settings, Statistics, loading,
command, pixel-zoom, and material-inspector UI. It defines the accepted visual
language, information hierarchy, control behavior, renderer boundary, and
verification evidence. Amend this file instead of creating a competing guide.

Record this exact version and the `AGENTS.md` policy version before a material UI
change and again at handoff. If either changed, reread the current documents and
reconcile the implementation before integration.

## Information Architecture

Settings uses one title, one status block, one scrolling body, and one footer.
The top-level drawers are:

1. General
2. Representation
3. Noise
4. Diffuse
5. Denoising
6. Buffers
7. Statistics
8. Aliasing
9. Debug
10. Sky
11. Lights
12. Shadows

The order is product behavior. Add a top-level drawer only when the feature has
a distinct user goal and enough retained controls to justify it. Effect-specific
diagnostics belong in Debug, grouped under the effect they explain.

## Visual and Copy System

- Use Title Case for visible drawer, section, and control headings.
- Use the displayed product name **UVSR** and lowercase executable slug `uvsr`.
- Prefer the shortest label that identifies the user's decision.
- Prefer spaces to hyphens for ordinary two word copy. Preserve fixed product
  names, code, command paths, and terms whose punctuation carries meaning.
- Describe effect, units, range, and important side effects in a concise hover
  tooltip for every new or changed UVSR-owned control.
- Keep dependent controls adjacent to their owner. Put uncommon implementation
  policy in a default-closed **Advanced** tree.
- Show unavailable state with a direct explanation; do not leave a control that
  accepts input but has no runtime effect.
- Do not use benchmark or developer language for normal product controls.

Amp is the authored animated skin. OG uses stock ImGui widgets and reaches UI
motion endpoints immediately. Both skins expose identical renderer state.

## Mandatory New-Element Intake Checklist

Before editing, record:

- the state owner and one authoritative default;
- control class, range, units, and accepted command values;
- reset behavior and whether a group reset is required;
- every direct and derived consumer;
- whether the value changes only constants, shader selection, resource
  topology, scene state, or process state;
- animation, scroll, popup, and focus ownership;
- scene-loading and unavailable-state behavior;
- command-catalog coverage or a reason the control is intentionally UI-only;
- focused tests and required live exercise; and
- any visible behavior that must remain unchanged.

If the control has no active consumer, do not add it. If an internal value is
required but is not a user decision, keep it out of Settings and document the
invariant near its owner.

## Defaults and Resets

Settings begin at factory defaults on every process start. Defaults live with
the owning settings type or preset function and are shared by UI reset,
commands, tests, and renderer creation. Do not duplicate numeric defaults in UI
code.

A reset icon restores the smallest coherent ownership group. A preset reset
must restore every value that defines that preset. Editing a preset-owned value
must preserve its origin while the reset icon communicates that the value has
changed. The Diffuse profile selector continues to show its originating Low,
Medium, High, or Ultra recipe and appends **(Custom)** when an owned value
differs. Its adjacent circular reset restores the complete High recipe, while
each owned control can return to its originating recipe value.

Diffuse defaults to 16 samples through the High recipe and Illumination
Intensity defaults to 1. Distribution and Occlusion Strength both expose their
complete maximum of 8. Global Noise defaults to Spatiotemporal Blue, 128x128,
with Animate Samples on. The default primary
sun uses irradiance 8 and a 0.2 degree full angular size. Ray Traced Sky
Visibility enables both Effect Diffuse and Effect Specular by default.
Flashlight Beam Size defaults to 16 degrees, Beam Roundness to 0.8, and Angular
Size to 2.86 degrees.

Auto Exposure defaults off. Maximum Brightening defaults to 5 EV and Maximum
Darkening defaults to 2 EV; both expose 0 through 16 EV. Exposure Compensation defaults to
0 EV and exposes -18 through +8 EV. Adjustment Period defaults to 0.20 seconds
and exposes 0.05 through 5.00 seconds.

Denoising signal defaults are Method None, Quality Balanced, Resolution Half,
and History 16. The chosen method never changes a producer's Output Hit
Distance or Ratio Estimator setting. Those independent switches default off and
on respectively.

## Control Composition

Use the established UVSR helpers for drawer bodies, deferred combos, animated
tree and toggle regions, reset icons, tooltips, and footer actions. Every
dropdown uses ImGui's native integrated-arrow trigger presentation; deferred
and immediate dropdowns differ only in when their mutation is applied. Do not
paint a second background or custom arrow over the native trigger. Diffuse,
Denoising, Aliasing, Debug, Sky Visibility, and Shadows effect groups retain
independent animated disclosure even while enabled. Every retained
setting has a concise hover explanation, and dropdown width must leave its
label and reset lane visible. Maintain balanced ImGui ID, style, disabled, tree,
table, child, and popup lifetimes on every branch.

Debug and its World, Visibility, and Physically Based Lighting groups start
expanded, then preserve user owned disclosure state. Their
ordinary rendering choices, including the initial World material choice, are
labeled **Default**. Visibility Reconstruction starts collapsed for a full-
resolution trace and expanded for a reduced-resolution trace; its stored manual
state takes precedence after interaction.

Representation begins with **Allow Ray Traversal**, the single master permission
for every ray traced effect. Switching it off preserves every effect's stored
settings. Bounding Volume Hierarchy, Bottom-Level Acceleration
Structures, and Top-Level Acceleration Structure groups start expanded. Their
dropdowns use deferred mutations because build policy can invalidate shared
renderer resources. The read-only status names unsupported, inactive, BLAS
construction, TLAS construction, ready, or failed state.

Diffuse exposes independent **Output Hit Distance** controls inside Occlusion
and Illumination. They preserve profile origin and do not become active merely
because a denoising method is selected.

Noise owns Pattern, Resolution, and Animate Samples for every stochastic effect.
Pattern choices are **Spatial White**, **Spatial Blue**, and **Spatiotemporal
Blue**; Resolution choices are **64x64**, **128x128**, **256x256**, and
**512x512**. Diffuse visibility, Ray Traced Shadows, Ray Traced Sky Visibility,
and finite flashlight shadows inherit these values. The first three retain
their inherited values until their **Specify Noise** control is on.
The hidden override values remain stored but inert. Its exact tooltip begins
`Use custom noise sampling for this effect only` and states that no other
effect's sampling changes. AO and GI share one Diffuse override because they
share one dispatch.

Denoising contains AO, GI, Shadows, and Sky Visibility groups. AO offers None
or ReBLUR; GI and Sky Visibility offer None, ReBLUR, or ReLAX; Shadows offers
None or SIGMA. An active method exposes Quality, Resolution, and a default
closed Advanced group for History, Disocclusion, and Anti Lag. Missing producer
data and an unavailable optional NRD build receive direct status copy while the
raw signal remains active.

Sky's Ray Traced Sky Visibility group exposes **Ratio Estimator** and **Output
Hit Distance** independently. Ratio Estimator off is the one ray route accepted
by ReBLUR or ReLAX. **Effect Diffuse** and **Effect Specular** default on. The
group remains independently collapsible while enabled.

Sky's **Auto Exposure** is a separate animated subsection with the DefaultOpen
behavior used by Aliasing technique sections. It defaults disabled and submits
only **Enable** while off. Enabling it reveals **Maximum Brightening** and
**Maximum Darkening**, each spanning 0 through 16 EV with 5 EV and 2 EV defaults,
respectively;
**Exposure Compensation**, spanning -18 through +8 EV with a 0 EV default; and
**Adjustment Period**, spanning 0.05 through 5.00 seconds with a 0.20 second
default. The directional limits bound the automatic target first, then Exposure
Compensation biases the bounded result. These controls change display mapping
without changing physical lighting or effect history.

Lights treats the flashlight as one analytical spot light. Horizontal and
Vertical Offset cover minus 40 through plus 40 centimeters. Output Hit Distance
belongs beside Cast Shadows. Beam Size and Beam Roundness retain 16 degrees and
0.8 defaults. Angular Size spans 0 through 20 degrees and controls the finite
spherical emitter used by direct-light energy and shadow rays. Zero keeps the
exact point-light and hard center-ray branches. Positive size uses four
noise-shifted finite-emitter rays. The offset emitter has its own collision
sphere. Predictive probes along the mount and forward from the hard-safe emitter
begin a cubic retraction fade at least 0.75 metres before a nearby wall. The
result uniformly retracts the complete mount offset toward the camera rather
than sliding its lateral offset along the wall. The hard collision limit remains
immediate, the offset restores smoothly after clearance returns, a final safety
sweep protects continuous camera movement, and aim is recomputed from the safe
light position.

Shadows exposes one **Ray Traced Shadows** group for the primary sun. It owns
**Enabled**, **Ratio Estimator**, and **Output Hit Distance**, and preserves all
stored values while inactive. Ratio Estimator off is the one ray route accepted
by SIGMA. The group directly explains a missing directional light, DXR 1.1,
MSAA, Representation readiness, zero angular size, producer data, and optional
backend conditions. **Specify Noise** reveals its private Pattern, Resolution,
and Animate Samples settings. Screen space directional shadow controls and debug
state are absent from main.

Temporal Reconstructive, Fast Approximate, Conservative Morphological, and
Multisample Adaptive each show a Low, Medium, High, or Ultra **Quality** row
while enabled, followed by an animated **Advanced** tree that starts collapsed.
Disabling a technique preserves its stored values. Temporal **Cost** also
remains visible. Temporal **Jitter Sequence** is the first control under
Advanced's **Algorithm** section; it owns a dedicated reset and does not
participate in either Custom recipe marker. **Depth Validation** follows it.
Fast Approximate's three edge controls, CMAA2 Edge Threshold and Detector, and
Multisample Adaptive Samples live inside their respective Advanced trees.

An inherited dropdown previews its resolved concrete value but lists that value
only once. Its reset icon is the route back to recipe ownership. Do not append
owner text in parentheses; **(Automatic)** and the modified-profile status
**(Custom)** are the only exceptions. Temporal Advanced begins with Jitter
Sequence and Depth Validation under **Algorithm** and places its cost policies
under **Cost**. A recipe-owned Algorithm change appends **(Custom)** to the
selected Quality preview; Jitter Sequence remains independent. A Cost change
appends it to the selected Cost preview. The marker clears
when every setting in its ownership group returns to the selected recipe. Each
top-level reset arrow restores its complete factory preset and owned group. A
named preset selection reapplies the complete selected group, including when it
is already the Custom preview's base name. A concrete Advanced choice matching
its inherited value restores recipe ownership unless the control exposes a
distinct **(Automatic)** choice.

Do not create hidden duplicate controls as animation placeholders. Draw one
authoritative control and animate its containing region. Disabled styling must
not allow mouse or keyboard input to leak through the Settings window to the
camera.

## Dropdown and Layout Safety

Dropdown popup motion uses the canonical full-alpha geometric roll. Input before
roll-down completes is discarded, never replayed. Popup size and row layout stay
fixed during the transition.

A dropdown that changes dependent layout uses
`DeferredUiStructuralPresentation<T>` or an equivalent tested phase machine.
Synchronize every dependent consumer, finish the originating popup transition,
wait for composition and scroll layout to become idle, then commit through the
shared deferred-action barrier. If an owner becomes hidden, clipped, or
unsubmitted, explicitly finish or cancel its scoped transition.

Independent controls that do not change layout or renderer topology can mutate
their owned settings directly during UI composition.

## Statistics Presentation

Keep the six general values on one slash-separated summary line. The labeled
Effect selector shows one retained renderer effect at a time and keeps its reset
beside the label. Complete Renderer uses a striped two-column table for the full
retained stage list; an ordinary stage includes the complete frame for context.
Visibility, Directional Shadows, Temporal Reconstructive, Fast Approximate,
Conservative Morphological, and Multisample Adaptive use the same readable table language for their
retained breakdowns. Completed query availability gates every timing. Dormant,
unsupported, or newly enabled work displays `--` or a direct status instead of
a fabricated zero. Directional Shadows uses **Shadow Ray Dispatch**, **Shadow
Denoise**, **Sky Visibility Ray Dispatch**, and **Sky Visibility Denoise**.
Visibility keeps **Ambient Occlusion Denoise** and **Diffuse Illumination
Denoise** separate from its trace. Multisample base lighting used only to feed
Visibility appears as **Visibility Lighting Preparation**, separate from both
**Direct Lighting** and **Screen Space Visibility**. Complete Renderer also
exposes **Auto Exposure**. Never repopulate this panel with retired planners, benchmarks,
shadow techniques, or shader taxonomies.

## Scrolling and Input

Settings has one scrolling body. Keep title/status/footer positions stable while
drawers open, close, or animate. Same-frame scroll-anchor correction must block
interaction until submitted geometry and hit rectangles agree. The command bar
owns its permanently reserved one-row bottom lane; Settings must not overlap or
resize that lane. Guidance belongs in the empty input hint and disappears when
typing begins. Separate guidance with slashes. After submission, the same empty
input shows a blue `Success` or saturated-crimson `Error` message until editing
resumes. Never add a floating result window above the command row. Up and Down continue
to recall command history. A long or multiline result may expose a trailing
details button; only an explicit click may open its bounded, scrollable,
selectable read only popup. The catalog mirrors all visible lighting,
representation, producer, and denoising choices. A `list` result uses `/`
between each row's supported verbs and value domain.

Escape or the grave-accent/tilde key toggles Settings unless an active edit or
popup owns it. The physical grave-accent key works with or without Shift so a
US-layout `~` chord remains valid. `/` toggles the command interface when text
input does not already own the key. M, F, V, and Z shortcuts must respect active
text/popup ownership. V continues leveling camera roll under a stationary held
trackpad touch; a new camera-look delta or another real camera input cancels
that leveling. Q moves the camera up and E moves it down; Space and Shift must
not retain vertical-motion behavior, and Shift must not restore Donut's sprint
path.

## Renderer Boundary

Renderer-affecting changes cross after ImGui composition, at a point where no
control or popup still references the previous structure. Scene-loading resource
ownership rejects renderer mutations while leaving interface-only commands
usable.

Controls must remain decoupled unless their actual resource contract requires a
dependency. In particular:

- Diffuse changes only Screen Space Visibility owned state and resources.
- Denoising reads explicit raw signals and physical hit distance outputs. It
  never enables a producer, enables hit output, disables Ratio Estimator, or
  changes a sampling recipe. Each signal falls back to raw output when its
  selected optional backend route is unavailable.
- Adaptive Sync changes only process presentation state. VSync remains
  disabled. Off suppresses the windowed DXGI Present allow-tearing flag; Vendor
  Agnostic and Nvidia Exclusive request the same tearing-compatible path, with
  the latter offered only on NVIDIA adapters. Windows, the driver, and the
  display determine actual variable-refresh operation, which UVSR cannot enable
  or confirm directly.
- Representation owns shared BLAS/TLAS lifetime and build policy. **Allow Ray
  Traversal** gates every ray query consumer without clearing its settings. A consumer
  may read only a coherent ready TLAS and must release its bindings before
  hierarchy invalidation or reset. TLAS-only invalidation retains the
  BLAS-coupled material geometry map; a new BLAS generation recreates and
  uploads it.
- Opaque and alpha-tested triangles are eligible for binary ray visibility.
  Every ray-query consumer uses the shared material candidate helper, which
  evaluates base-color alpha and cutoff before committing alpha-tested hits.
  Blended and transmissive domains remain outside this hierarchy.
- Global Noise changes only inheriting consumers. A custom override changes only
  its owning effect. Either change resets the affected sample clock and required
  downstream image history without resetting another effect's phase.
- Auto Exposure meters the rendered HDR image and changes only the exposure
  multiplier consumed by AgX. It never edits environment exposure, light
  intensity, AO strength, GI intensity, or their histories. Maximum Brightening
  and Maximum Darkening clamp the automatic EV target independently; Exposure
  Compensation is applied afterward. Adjustment Period is an EV-space
  half-life. When Auto Exposure is off, the renderer selects the exact
  established texture-only, buffer-free AgX path; it binds no automatic
  exposure buffer and cannot change color. The enabled route changes only the
  scene-linear exposure multiplier before the same AgX clamps and output
  handling.
- TAA, Fast Approximate AA, CMAA2, and MSAA are independent states with
  deterministic pass order.
- World appearance, Visibility views, and Physically Based Lighting filters are
  separate Debug states. A Physically Based Lighting
  filter preserves Visibility execution but suppresses its ordinary composite;
  an explicit Visibility view wins. The retired Edge Overlay must not return as
  hidden shadow state.
- Ray Traced Shadows requires a primary directional light, DXR 1.1, a ready
  Representation hierarchy, and single sample deferred rendering. Ratio
  Estimator and Output Hit Distance remain independent. Screen space
  directional shadows are quarantined with the CSM and SVSM experiment rather
  than compiled into main.
- The flashlight remains one analytical spot light. Its ray traced visibility is
  matched to that exact light, and its SIGMA history is independent from the
  sun's history. A positive emitter radius uses four shared-noise rays, while a
  dedicated sphere resolves penetration in the same static collision hierarchy
  used by the camera. Near contact, the complete mount vector retracts uniformly
  toward the camera, restores smoothly with available clearance, and receives a
  final continuous-motion sweep before the beam aim is recomputed.

Changing renderer topology must invalidate only the affected passes/history.
Do not force a scene reload when a narrower pass or render-target refresh is
sufficient.

## Loading and Error Presentation

Loading keeps presenting frames while staged scene and renderer work proceeds.
The UI must never expose a partially prepared renderer state as interactive.
Errors name the unavailable feature and the missing condition in user language.
Do not expose internal planner, factory-profile, or permutation terminology.
The second loading line uses `phase: x/average`. The numerator advances every
20 milliseconds independently of loader progress. The denominator
is the completed-load average for the selected scene, then the all-scene average,
or `--` when no history exists. This is elapsed-time context, not fabricated
completion percentage. Only successful loads update the versioned per-user
history. A failed asynchronous import must leave busy state, retain a safe
splash presentation, and expose **Retry Scene Load** for the current selection.
Importer exceptions follow that same retryable path. Closing the viewer waits
for its worker before renderer-owned scene resources are destroyed, and a failed
attempt discards deferred texture finalization before retrying.

## Pixel Zoom and Material Inspector

Pixel zoom uses exact integer texel replication. The material inspector follows
the zoom panel's placement without owning its renderer texture. Their Amp
transitions and OG endpoints must preserve stable hit testing, focus, and the
reserved command lane.

The M shortcut and title affordance close the same material-inspector state.
Material picking must distinguish center inspection from camera-focus picking.

## Required Checks

After any Settings, animation, dropdown, scrolling, loading, pixel-zoom, or
material-inspector change, run:

```powershell
cmake --build build --config Release --target uvsr_ui_source_contract_tests uvsr_ui_animation_tests uvsr_imgui_dropdown_roll_tests
ctest --test-dir build -C Release -R "uvsr_ui_source_contract|uvsr_ui_animation_reference|uvsr_imgui_dropdown_roll_lifecycle" --output-on-failure
```

Also run `uvsr_renderer_source_contract` and the affected feature tests when a
choice changes renderer resources or pass order. Run the complete Release build
and CTest suite before handoff.

## Live Exercise

Use the exact candidate executable and a bundled scene. Exercise:

- opening, closing, scrolling, and resetting Settings in Amp and OG;
- toggling Settings with Escape, grave accent, and shifted tilde while
  preserving text-input ownership;
- leveling camera roll with V while a stationary trackpad touch remains held,
  then confirming a real camera-look delta cancels the leveling motion;
- Q/E vertical camera motion with Space/Shift confirmed inert;
- all three Adaptive Sync choices, reset behavior, and capability/vendor
  unavailable states;
- the Diffuse, Occlusion, Illumination, and three estimator labels in both
  skins;
- the Diffuse sample default, Distribution and Occlusion endpoints, and both
  Output Hit Distance switches;
- all global Noise patterns and resolutions, Animate Samples, inheritance, each
  Specify Noise override, centered clipping, and override isolation;
- all four Denoising groups, supported method lists, stored controls, missing
  producer data, and optional backend unavailable state;
- every changed control at both endpoints and its unavailable state;
- Representation rebuild/refit transitions, staged status, and **Allow Ray
  Traversal** with sky, sun, and flashlight effects selected;
- both sun Ratio Estimator states, both Output Hit Distance states, and the
  MSAA unavailable state;
- flashlight ray traced shadows, horizontal and vertical offset endpoints,
  beam and Angular Size defaults/endpoints, increasing penumbra width, early
  near-wall onset, smooth uniform mount retraction, centered contact beam,
  smooth offset restoration, final-sweep collision safety, and independent
  SIGMA history;
- alpha-tested foliage silhouettes under sun, sky, and flashlight ray queries;
- Auto Exposure off with only Enable visible and exact color parity with the
  established manual AgX path; then bright, dark, neutral, and saturated views
  while enabled, both Maximum Brightening and Maximum Darkening endpoints, both
  Exposure Compensation endpoints, both Adjustment Period endpoints, and
  symmetric bright/dark settling;
- affected dropdowns while their dependent layout is open and clipped;
- scene loading with the colon, 20 ms numerator, and historical average
  denominator, plus command completion;
- Debug composition rather than each debug state only in isolation;
- window resize, pixel zoom, material inspection, and shortcut focus; and
- the renderer result and resource transition caused by each changed control.

Record the executable, scene, settings, skin, and observed outcome. A launch is
not proof that an interaction is correct.

## Handoff Evidence

The UI handoff includes:

- intake decisions and exact UI reference version;
- visible labels, defaults, reset ownership, and command paths changed;
- renderer/resource consumers changed;
- focused and full test results;
- live exercise scene/settings and screenshots when visual acceptance matters;
- known limitations or unexercised combinations; and
- confirmation that this reference was updated when normative behavior changed.

## Reference Revision History

- `2026-08-09.3`: Replaced the flashlight's contact-only mount correction with
  an early cubic proximity fade using mount-direction and hard-safe forward
  probes while retaining immediate physical collision limits.
- `2026-08-09.2`: Added independent automatic brightening and darkening limits,
  moved Auto Exposure into its own default-open disabled-first subsection,
  restored the exact buffer-free manual AgX presentation while Auto Exposure is
  off, kept V roll leveling active under stationary trackpad contact, and made
  near-wall flashlight collision uniformly retract and smoothly restore the
  complete mount offset. The earlier global AgX clamp removal and post-outset
  `pow(2.2)` change were reverted as too broad and are not current behavior.
- `2026-08-09.1`: Made flashlight Angular Size trace four visible-emitter rays,
  added an emitter-aware collision sphere and post-collision aim, changed AgX
  clamp/output handling globally while adding EV-space exposure adaptation,
  added Adjustment Period, removed loading and flashlight shortcut annotations,
  and changed the default Sun Irradiance to 8. Revision `2026-08-09.2` reverts
  the global AgX presentation change while retaining the scoped adaptation.
- `2026-08-08.1`: Added the Noise drawer and per-effect Specify Noise
  inheritance, centered precomputed Spatial White/Spatial Blue/Spatiotemporal
  Blue assets and resolutions, alpha-tested ray-query visibility, independently
  collapsible effect sections, analytical flashlight Angular Size, Sky Auto
  Exposure and Brightness, separated ray/denoise statistics, Capture footer
  label, unit diffuse illumination, and historical 20 ms loading ticks.
- `2026-08-06.1`: Added the Denoising drawer and explicit producer data
  contract; added Allow Ray Traversal, raw sky and sun routes, ray traced
  flashlight shadows and offsets, updated lighting and visibility defaults,
  removed screen space directional shadows from main, and required loading
  preparation progress to end in `x/100`.
- `2026-08-05.6`: Replaced hyphen field separators with slash separators in
  Amp and OG performance summaries and command-interface `list` rows.
- `2026-08-05.5`: Renamed the Visibility drawer and its Ambient Occlusion,
  Indirect Diffuse, and estimator choices to Diffuse, Occlusion, Illumination,
  and the three Bitmask names; added Adaptive Sync presentation policy; moved
  vertical camera input to Q/E; and added grave-accent/tilde Settings access.
- `2026-08-05.4`: Removed fractional shadow rates and both private ratio
  histories, made final-color TAA the only temporal accumulator, reduced both
  Visibility and ray-traced noise choices to the two legacy spatial patterns,
  moved Animate Samples directly above Samples Per Pixel,
  and replaced the rejected `TMin` policy with a low-default raster
  triangle-normal origin bias. This supersedes the shadow sampling and bias
  behavior recorded in revisions `2026-08-05.2` and `2026-08-05.3`.
- `2026-08-05.3`: Defined Ray Bias exclusively as the ray-query `TMin`, made
  fractional duty and sample animation independent of global TAA, exposed three
  emitter-noise patterns plus Animate Samples, and culled non-receiving surfaces
  before the hard-shadow query.
- `2026-08-05.2`: Replaced ineffective Origin Safety with the known-working
  world-unit Ray Bias default; added the hard-shadow fast path, logarithmic
  `1/16`-through-`64` sample slider, `0.53` degree sun default, blue-noise
  phases, and motion-reprojected ratio history before division.
- `2026-08-05.1`: Replaced the exclusive directional-shadow selector with two
  independent enable controls; added conservative both-on composition,
  Ratio-Estimator Ray Dispatch timing, depth-aware Origin Safety, and explicit
  finite-emitter guidance; and removed the ray-traced spatial denoiser.
- `2026-08-04.1`: Added the Representation drawer and its BVH, BLAS, and TLAS
  policy/status contract; replaced the implicit screen-space shadow toggle with
  one directional-shadow Technique selector; and added the Ratio-Estimator
  Ray Traced Shadows group, command coverage, unavailable states, and
  consumer-before-representation reset ordering.
- `2026-08-03.8`: Moved Jitter Sequence into Temporal Advanced Algorithm,
  shortened its Halton/Sobol and Depth Validation labels, and unified every
  dropdown on the native integrated-arrow presentation without changing
  deferred mutation timing.
- `2026-08-03.7`: Added the visible Temporal Jitter Sequence selector with all
  five Filament choices, experimental Sobol 32, an independent reset,
  and history-reset ownership outside Advanced.
- `2026-08-03.6`: Added visible four-tier Quality rows to every Aliasing
  technique, renamed Temporal Cost to Cost and Multisample Reference to
  Multisample Adaptive, and exposed CMAA2 Edge Threshold and Detector.
- `2026-08-03.5`: Added Fast Approximate AA, kept all four Aliasing techniques
  independent, and gave each a default-collapsed Advanced disclosure.
- `2026-08-03.4`: Added group-owned Custom markers to Quality after Algorithm
  changes and to Cost after Cost changes.
- `2026-08-03.3`: Restored the Visibility profile Custom notice and renamed the
  initial World material choice from Scene to Default.
- `2026-08-03.2`: Removed duplicate inherited dropdown choices and parenthetical
  owner copy, retained only Automatic provenance, moved Stationary Bypass to the
  top of Algorithm, and renamed Behavior to Cost.
- `2026-08-03.1`: Restored expanded Debug defaults, resolution-aware
  Reconstruction disclosure, detailed retained Statistics tables, and colored
  in-input command results with slash-separated guidance.
- `2026-08-02.2`: Restored the ninth Buffers drawer, profile-origin Custom and
  reset behavior, visible labels and tooltips, animated effect disclosures,
  one-effect Statistics, four Visibility debug choices, and the one-row command
  contract while retaining the renderer cutdown.
- `2026-08-02.1`: Replaced retired Buffers, Aliasing Method, benchmark, factory,
  SVSM, CSM, and sample-resurrection guidance with the eight current drawers,
  independent AA controls, Debug composition, and the focused renderer boundary.
- `2026-07-31.5`: Last pre-cleanup reference; recoverable from Git history for
  historical integrations only.
