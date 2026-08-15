# UVSR UI Design and Integration Reference

UI reference version: `2026-08-14.4`.

## Purpose

This is the canonical reference for UVSR-owned Settings, Performance, loading,
command, pixel-zoom, and Material drawer UI. It defines the accepted visual
language, information hierarchy, control behavior, renderer boundary, and
verification evidence. Amend this file instead of creating a competing guide.

Record this exact version and the `AGENTS.md` policy version before a material UI
change and again at handoff. If either changed, reread the current documents and
reconcile the implementation before integration.

## Information Architecture

Performance is an independently collapsible top-level panel immediately above
and fully detached from Settings. Separate it from Settings by the same vertical
space used between adjacent top-level drawers. Both panels keep independent
complete rounded silhouettes. Together they form one
managed vertical stack that yields space to the command interface. Settings
uses one title, one fixed snapshot row, one scrolling body, and one footer. The
root owns the fixed 32-character code above the borderless scrolling child.
Keep the code visible below the title when Settings collapses, with the same
tight horizontal inset, fixed top-padded baseline, and permanently opaque
one-line content surface as the retained Performance line. Submit one invisible
layout/click item and exactly one visible hash glyph run. General and the
scrollbar share the scrolling viewport's top edge beneath that fixed row. Do
not synthesize fixed top or bottom shadows over that viewport.
Reserve and render the scrollbar channel through every submitted opening and
closing frame; remove it only with the scrolling child at the fully collapsed
endpoint. Gate child submission on both `Begin()` visibility and logical
non-collapsed state: Dear ImGui may temporarily return visible for an auto-fit
window that is already logically collapsed. Do not continuously submit a zero
height through `SetNextWindowSize`; exact-X constraints own panel width while
`AlwaysAutoResize` owns height. The compact Settings and Performance rows must
call the same renderer for text placement, opaque retained fill, inset fill,
outer outline, and inner outline so their settled bodies differ only in text
and click behavior. The text baseline and retained fill must not depend on
collapse amount.
Its top-level drawers are:

1. General
2. Pathing
3. Representation
4. Noise
5. Diffuse
6. Denoising
7. Buffers
8. Aliasing
9. Debug
10. Sky
11. Lights
12. Shadows
13. Material
14. Interface

The order is product behavior. Add a top-level drawer only when the feature has
a distinct user goal and enough retained controls to justify it. Effect-specific
rendering views and output diagnostics belong in Debug, grouped under the effect
they explain.

The fixed settings code is exactly 32 lowercase hexadecimal characters. The
first four characters are its registered schema version (currently `0003`);
the remaining 112 bits are a deterministic fingerprint of every sorted
represented non-action command value,
including stored inactive and runtime-dynamic selections. Root Settings
collapse and Material drawer visibility are presentation state and are the two
gettable values omitted. Clicking the code copies it and archives its full
canonical snapshot in
`%LOCALAPPDATA%\UVSR\settings-snapshots-v<version>.txt`. The companion
`tools/decode_settings_snapshot.py` also searches writable package-local UVSR
catalogs when Windows redirects a packaged launch. It must validate version,
fingerprint, missing catalog entries, and collisions across every matching
catalog before outputting settings. A complete menu state cannot be silently
reduced to a reversible 112-bit subset.
Version allocation follows
[`settings-snapshot-schema-versions.md`](settings-snapshot-schema-versions.md):
the collision-resistant full schema fingerprint owns the version, branch rows
are provisional, and integration composes the live schema before assigning the
next number after the append-only registry maximum.

General begins with **Lighting Solution**. Pathing is submitted every frame but
animates into view only for Path Tracing. Diffuse, Buffers, Aliasing, and
Shadows are likewise always submitted and animate out for Path Tracing. Shared
drawers keep their header and use stable independent body regions for
solution-specific controls. Never erase inactive settings or force a disclosure
closed merely because its lighting solution is not selected.
An actual transition from Ray Marching to Path Tracing opens Pathing once. Later
manual disclosure remains user-owned until another away-and-back transition;
do not use a persistent default-open flag.

Lighting Solution and solver changes use the ordinary deferred combo lifecycle.
Renderer topology may change only after popup roll-up, the settle interval, and
one complete idle composition frame. `DeferredUiStructuralPresentation` stays
retired; whole-drawer and shared-body visibility use stable-ID animated toggle
regions without creating a second mutation queue.

## Visual and Copy System

- Use Title Case for visible drawer, section, and control headings.
- Use the displayed product name **UVSR** and lowercase executable slug `uvsr`.
- Prefer the shortest label that identifies the user's decision.
- Prefer spaces to hyphens for ordinary two word copy. Preserve fixed product
  names, code, command paths, and terms whose punctuation carries meaning.
- Describe effect, units, range, and important side effects in a concise hover
  tooltip for every new or changed UVSR-owned control. Keep every rendered
  tooltip at or below 120 Unicode code points.
- Keep dependent controls adjacent to their owner. Put uncommon implementation
  policy in a default-closed **Advanced** tree.
- Show unavailable state with a direct explanation; do not leave a control that
  accepts input but has no runtime effect.
- Do not use benchmark or developer language for normal product controls.

### Spacing Scale

Derive every managed menu margin from one 4-pixel base at 100 percent display
scale. Scale that base with the UI, then use only these exact multiples:

| Token | Ratio | 100 Percent Size | Managed Uses |
| --- | ---: | ---: | --- |
| Tight | 1x | 4 px | Adjacent drawers, Performance-to-Settings, Settings-to-command-interface, footer buttons, and compact internal padding |
| Regular | 2x | 8 px | Settings title-to-General, side and bottom body insets, and ordinary compound-control spacing |
| Section | 4x | 16 px | Viewport and command-interface outer margins |

Do not derive a menu margin from font size, rounding, grab size, or control
height. Header-to-body joins remain attached surfaces; frame padding, slider
height, scrollbar width, radii, and one-pixel depth strokes are geometry rather
than additional spacing tokens.

Amp is the authored animated blue-accent skin. Ogg uses stock ImGui widgets and
reaches UI motion endpoints immediately. Both skins expose identical renderer
state. A user-edited ultra-bright Amp Primary Accent automatically selects the
dark transparent depth polarity.

Interface is the final drawer. Its first two toggles disable or enable every
authored UI animation and opt exact numeric entry into values beyond compact
visual tracks but within each setting's safe logical bounds; both preferences
are session-only, and Ogg remains immediate regardless. It also owns the skin
selector plus session-only RGBA editors. Amp Primary Accent drives drawer and
panel headers, footer buttons, checkmarks, selection, and raised slider knobs;
it is unavailable under Ogg. Shared Secondary Accent defaults to
`#4296FA4F` and drives error, negative, and toggle-off presentation. Shared
Tertiary Accent defaults to the historically light-track-compensated
`#1E3757FF` and drives success, positive, Material status, and toggle-on
presentation. Font Color and Background Color follow the three accent
rows directly; do not place them in a separate Advanced Accents submenu. Font
Color owns all authored copy, while Background Color owns the menu body,
resting closed controls, and slider tracks. Role-state colors derive from these
resting RGBA values, and ultra-bright Primary Accent surfaces select dark
transparent depth automatically. Slider knobs remain raised. Footer Reset and
`/reset all` restore renderer defaults, the dynamic Adaptive Sync default, Amp, enabled
animations, disabled visual-maximum override, every Interface color, and the
collapsed Complete Renderer view.
They preserve camera, scene, active adapter, and shell navigation state.

Authored sliders render the Primary Accent knob as a restrained raised gradient
with automatic bright-surface depth polarity. A semantically gated slider blocks
input immediately but eases its grayscale presentation over 280 milliseconds;
its stored preference is not erased. If the renderer's effective value changes
because of that gate, the knob animates toward the effective value while direct
mouse or navigation edits track immediately. A fixed right-hand value bubble
inside the existing slider width uses exactly twice the authored toggle width
and accepts exact input on one click. Its closed copy contains four numeric
digit glyphs with no units, while active input retains native precision. Exact
input is clamped to the compact visual range by default. Override Visual Maxes
allows values beyond that track up to the established safe logical range
without extending pointer or navigation travel, and turning it off never
silently reduces an accepted value. A 2-pixel gap separates its four inner
fillets from the rounded track bubble.
Authored ColorEdit component controls omit channel letter prefixes. Ogg retains
stock slider rendering, centered value text, and stock ColorEdit labels.
All first-party color controls route through one UVSR-owned RGB/RGBA wrapper.
The scoped Amp picker uses a hue wheel around a rounded saturation/value
triangle and four equal vertical lanes aligned to the fourth
component column: hue, alpha, Current, and Original. RGB retains a
noninteractive checkerboard alpha lane without accessing a fourth component;
the aligned fourth RGB/HSV numeric slot remains visually empty and no input
widget is submitted there. One continuous carved perimeter encloses both empty
slots and their inter-row spacing, with no internal seam or per-row frames.
Popup RGB, HSV, and hex rows force `NoSmallPreview`,
fill the available width, and never
repeat a visible control label as a heading. Exact hue, white, and black remain
pointer reachable through forgiving snap zones. One midpoint hollow-circle
radius feeds selector endpoints, the active selector, wheel cursor, hue bar,
and alpha bar without active-state growth. Begin the bright popup interior at
Regular padding, then inset all controls by one additional Tight token so four
bright pixels remain on every side at 100 percent scale without changing the
fourth-column lane geometry. Give both hue-wheel edges a visible one-pixel
white transparency gradient. The popup base and two depth layers are
translucent. Fill the complete outer-to-inner frame band and pointer frame with
the same panel-inset role and inherited transparency used by the Settings and
Performance frames. Selector, bar, checker, and comparison colors remain opaque
at steady state. Emit the rounded source pointer after the popup-body transform so its tip
stays on the canonical Settings content edge at the source row's vertical center
throughout reversible zoom and fade. Unscoped and Ogg pickers stay stock.

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
Size to 2.86 degrees. Its factory Color is pure linear white.

Auto Exposure defaults off. Maximum Brightening defaults to 5 EV and Maximum
Darkening defaults to 2 EV; both expose 0 through 16 EV. Exposure Compensation defaults to
0 EV and exposes -18 through +8 EV. Adjustment Period defaults to 0.20 seconds
and exposes 0.05 through 5.00 seconds.

Denoising signal defaults are Method Raw, Quality Balanced, Resolution Half,
History 16, and spatial Radius 4. The chosen method never changes a producer's
Output Hit Distance or Ratio Estimator setting. Those independent switches
default off and on respectively.

## Control Composition

Use the established UVSR helpers for drawer bodies, deferred combos, animated
tree and toggle regions, reset icons, tooltips, and footer actions. Every
dropdown uses ImGui's native integrated-arrow trigger presentation; deferred
and immediate dropdowns differ only in when their mutation is applied. Do not
size authored hover tooltips from their text or nesting depth. The shared
authored tooltip path fixes every outer window to at most 20 font heights by
7 font heights, bounded by 42 percent of viewport width and 25 percent of
viewport height, with a 5-pixel inner inset and shared wrapping. It caps every
rendered tooltip at 120 Unicode code points, using a three-dot suffix when
needed. Do not bypass that path for a UVSR-owned control. Do not paint a second
background or custom
arrow over the native trigger. Diffuse,
Denoising, Aliasing, Debug, Sky Visibility, and Shadows effect groups retain
independent animated disclosure even while enabled. Every retained
setting has a concise hover explanation, and dropdown width must leave its
label and reset lane visible. Maintain balanced ImGui ID, style, disabled, tree,
table, child, and popup lifetimes on every branch.

Authored opened combo popups use compact, uniform option rows. Their selected,
hovered, and navigation highlights use the same frame radius as closed controls.
One popup-scoped surface moves smoothly between rows. Authored popups roll down
or up over 180 milliseconds and retain a selected popup through its roll-up;
Ogg retains stock immediate popup spacing and rendering. This popup-only
presentation must not change the closed combo trigger or own renderer mutation.

Debug and its World, Visibility, and Physically Based Lighting groups start
expanded, then preserve user owned disclosure state. Their
ordinary rendering choices, including the initial World material choice, are
labeled **Default**. Diffuse has no Reconstruction group: full-resolution
signals bypass upsampling, while reduced-resolution signals use the one
automatic guide-aware upsample.

Representation begins with **Allow Ray Traversal**, the single master permission
for every ray traced effect. Switching it off preserves every effect's stored
settings. Bounding Volume Hierarchy, Bottom Level Acceleration
Structures, and Top Level Acceleration Structure groups start expanded. Their
dropdowns use deferred mutations because build policy can invalidate shared
renderer resources. The read-only status names unsupported, inactive, BLAS
construction, TLAS construction, ready, or failed state.

Diffuse exposes independent **Output Hit Distance** controls inside Occlusion
and Illumination. They preserve profile origin and do not become active merely
because a denoising method is selected.

Material Domain, Interface Skin, and every Interface color use the ordinary
side-labeled bounded lane. Material sliders inherit that same bounded width;
never restore a full-drawer slider. The Material hover contract explains the
meaning of glossiness, metalness, roughness, opacity, alpha cutoff, normal
scale, occlusion, emissive, and transmission. A gated alpha value keeps its
picker lane visible with a checkerboard base instead of replacing the lane with
solid gray.

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

**Accumulate Samples** is the final Noise section. It is an independently
collapsible tree containing a top-level **Enable** toggle and all enabled-only
options. Do not render gray explanatory status text beneath the section.

Denoising contains AO, GI, Shadows, and Sky Visibility groups. Every group
offers Raw, Joint Bilateral, and Gaussian Bilateral. AO additionally offers
ReBLUR; GI and Sky Visibility add ReBLUR and ReLAX; Shadows adds SIGMA. A
first-party bilateral method exposes only Radius and works without NRD, motion,
history, or hit distance. An active third-party method exposes Quality,
Resolution, and the applicable default-closed Advanced controls. The only gray
copy in the complete drawer is the final exact line `Third Party denoisers are
configurable, but not installed in this build.`

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
sphere, sized to the larger of the camera collision radius and analytical
emitter radius. The authored mount is repaired on initialization or radius
change and swept continuously as the camera moves. Collision may stop or slide
the emitter, but scene depth must not scale the camera offset, select a receiver,
or retarget the beam. Sway is applied afterward to direction only and cannot
alter collision or physical position. The factory Color is pure linear white.
The removed receiver-driven camera-centering controls and diagnostics are
recorded in
[Flashlight Camera Centering v1](postmortem/flashlight-camera-centering-v1.md).

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

The closed combo frame, preview, arrow, and reset ownership use UVSR's authored
control presentation. Authored skins retain native popup position and selection
semantics while a 180-millisecond geometric clip rolls options into or out of
view. Input remains blocked behind the moving clip and is never replayed. One
rounded popup-scoped surface glides between selected or hovered rows. Ogg keeps
stock row geometry, rendering, and immediate dismissal. Popup presentation
never owns renderer mutation.

A dropdown that changes dependent layout or renderer resources queues its value,
records its exact originating combo, waits for roll-up to finish, waits at least
250 milliseconds for composition and scroll layout to become idle, presents one
complete idle frame, then commits through the shared deferred-action barrier. A
clipped, collapsed, or hidden owner finishes only its exact popup transition so
the action cannot strand. Cancel queued work when its owner becomes invalid
during a scene or process transition. World Materials, Visibility View, and the
Physically Based Lighting Information Filter in Debug all use this path; in
particular, selecting a White world mode must not rebuild materials or render
passes in the native popup's selection frame.

Independent controls that do not change layout or renderer topology can mutate
their owned settings directly during UI composition.

## Performance Presentation

Keep Performance as an independent top-level panel immediately above Settings,
with its own collapse state and the same smooth authored root-panel transition
used by Settings in Amp. Ogg remains immediate. Keep resolution,
submitted triangles, frame time, and frame rate on one slash-separated summary
line inside Performance. Keep its glyph baseline fixed at the ordinary expanded
row position and keep the one-line content surface fully opaque in every state;
do not make the remainder of the expanded body opaque.
The fully visible command interface uses that same opaque compact-body surface;
its existing whole-window appearance transform alone owns entry and exit fade.
In Amp, submit an opaque rounded inset frame after Performance content and after
the Settings scrolling child. The frame must fill all four outer-to-inner
corner wedges, leave the interactive center untouched, paint the fixed retained
row opaque, and finish with outer and inner depth outlines. Do not grade either
retained row with a root inset shadow.
Keep renderer identity in General. The compact unlabeled selector shows one
retained renderer timing view at a time. Give it the same inset and fixed width
as ordinary long General controls; do not reserve a same-row reset lane.
Selecting **Complete Renderer** is the direct route back to the default view.
Complete Renderer uses a striped two-column table for the full
retained stage list; an ordinary stage includes the complete frame for context.
Visibility, Directional Shadows, Temporal Reconstructive, Fast Approximate,
Conservative Morphological, and Multisample Adaptive use the same readable table language for their
retained breakdowns. Completed query availability gates every timing. Omit a
timing row until its first completed measurement, then retain that row for the
session and display `--` with a zero unavailable value whenever its current
query is absent. Retain `--`, Unsupported, or other direct unavailable text for
meaningful non-time resource, count, format, and status rows. Directional Shadows uses **Shadow Ray Dispatch**, **Shadow
Denoise**, **Sky Visibility Ray Dispatch**, and **Sky Visibility Denoise**.
Visibility keeps **Ambient Occlusion Denoise** and **Diffuse Illumination
Denoise** separate from its trace. Multisample base lighting used only to feed
Visibility appears as **Visibility Lighting Preparation**, separate from both
**Direct Lighting** and **Screen Space Visibility**. Complete Renderer also
exposes **Auto Exposure**. Never repopulate this panel with retired planners, benchmarks,
shadow techniques, or shader taxonomies.

## Scrolling and Input

Settings has one scrolling body. Lay out the detached Performance and Settings
panels with exactly one Tight top-level drawer gap while either panel opens or
closes.
At 100 percent scale, target 23.44 font heights for both authored root panels,
exactly 20 percent narrower than the preceding layout. Enforce a lower bound
that contains the longest intentional control, root and drawer padding, and the
scrollbar, and retain the viewport/right-side picker-lane cap.
Keep both root silhouettes fully rounded and keep the Settings title and footer
positions stable while drawers animate. Cancel the root window's duplicate
leading padding before the scrolling child begins; submit one explicit Regular
gap inside the zero-padded child's clip rectangle before the first drawer.
Use one authored corner radius for root bodies, drawer bodies, controls, and the
Settings scrollbar. Use a 12-pixel authored channel at 100 percent scale and keep
both axes of its grab one pixel inside the frame, yielding the minimum 10-pixel
visible grab that meets the inset outline while retaining the true 4-pixel outer
radius and 3-pixel inset-outline radius.
Every submitted transition frame keeps that channel and visible grab, including
the first opening frame. The fully collapsed endpoint submits no scrolling child
and therefore no scrollbar. Never toggle `NoScrollbar` during root motion,
because removing its layout reservation changes every full-width drawer.
Submit the opaque ring, retained-row fill, and depth outlines on the last visible
Settings child draw list: nested child windows render after their parent, so a
parent-only decoration is not a true foreground layer. Interpolate one Settings
inner rectangle from the full scrolling viewport to the retained hash perimeter,
clamp it to the animated root padding, and draw exactly one outline. Reversing
the root motion must reverse that same geometry without crossfading outlines.
General must use the same
`DrawCollapsingHeader`, `BeginDrawerBody`, `EndDrawerBody`, and Tight spacing
path as every ordinary drawer and must never own root chrome or clipping.
Backdrop composites must follow the independent title and body silhouettes;
they must not fill the panel gap, rounded-corner pockets, or exterior margins,
and root panels must not cast blur into those empty areas. Same-frame
scroll-anchor correction must block
interaction until submitted geometry and hit rectangles agree. The command bar
owns its permanently reserved one-row bottom lane; separate it from Settings by
the same Tight gap used between Performance and Settings. Neither stacked panel
may overlap or resize that lane. Its authored entrance scales uniformly from the
bottom center, preserving both the bottom edge and horizontal center while its
width and height expand together. Guidance belongs in the empty input hint and
disappears when typing begins. Separate guidance with slashes. After submission, the same empty
input shows `Success` with the configured positive accent or `Error` with the
configured negative accent until editing resumes. Never add a floating result
window above the command row. Up and Down continue
to recall command history. A long or multiline result may expose a trailing
details button; only an explicit click may open its bounded, scrollable,
selectable read only popup. The catalog mirrors all visible lighting,
representation, producer, and denoising choices. A `list` result uses `/`
between each row's supported verbs and value domain.

Clamp repeated outward wheel input at the Settings top or bottom to that exact
endpoint before applying scroll-anchor correction. A pending navigation or
programmatic scroll target still owns its frame. Constrain authored color
pickers to the lane beginning at the Settings content-right edge and center each
picker body vertically on the source row whenever space permits. Clamp that
centered position at the Settings margins, including the existing Settings-root
bottom limit, instead of locking every picker there.
Apply the picker-local zoom/fade transform once, then the managed-stack
appearance once. Give only that picker a translucent popup base plus two
translucent interior layers. Fill its entire outer-to-inner frame band and
pointer frame with the same inherited-alpha panel-inset role as Settings and
Performance without changing generic popup colors. Append the rounded source pointer after transforming the
body so its tip remains fixed on the canonical Settings content edge at the
origin row's vertical center. An actual contiguous-frame Settings scroll requests the same
retained close transition, updates one shared live source rectangle before both
popup placement and pointer drawing, blocks picker input during the transition,
and closes immediately only when authored motion is disabled. Wheel input over
the picker itself must remain picker input.

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
  sun's history. A positive emitter radius uses four shared-noise rays. A
  dedicated sphere resolves penetration and continuously sweeps the authored
  mount through the same static collision hierarchy used by the camera.
  Collision safety is independent from receiver depth and lens sway; no
  camera-centering controller or receiver feedback is retained.

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

## Pixel Zoom and Material Drawer

Pixel zoom uses exact integer texel replication. Material is a normal Settings
drawer and does not own the zoom panel's placement or renderer texture. Opening
Material forces the center crosshair, requests a center pick, and keeps aiming
guidance visible after a miss. Its Amp transition and Ogg endpoint must preserve
stable hit testing and focus.

The M shortcut opens or refreshes the Material drawer; the drawer header closes
it. Material picking must distinguish center inspection from camera-focus
picking.

## Required Checks

After any Settings, animation, dropdown, scrolling, loading, pixel-zoom, or
Material drawer change, run:

```powershell
cmake --build build --config Release --target uvsr_ui_source_contract_tests uvsr_ui_animation_tests uvsr_imgui_dropdown_roll_tests
ctest --test-dir build -C Release -R "uvsr_ui_source_contract|uvsr_ui_animation_reference|uvsr_imgui_dropdown_roll_lifecycle" --output-on-failure
```

Also run `uvsr_renderer_source_contract` and the affected feature tests when a
choice changes renderer resources or pass order. Run the complete Release build
and CTest suite before handoff.

## Live Exercise

Use the exact candidate executable and a bundled scene. Exercise:

- opening, closing, scrolling, and resetting Performance and Settings in Amp
  and Ogg, including all four independent collapse combinations,
  the first moving frame, midpoint, rapid direction reversal, endpoint, and
  following settled frame. Verify the retained text baseline and full-opacity
  surface never change, the Settings full-menu inner outline visibly compacts
  into the hash-row outline, the root outline moves continuously, drawer widths
  and scrollbar presence remain stable in both directions, the endpoint has no
  scrollbar, and Settings has no viewport shadows;
- copying the settings code expanded and collapsed, decoding it as text and
  JSON, and rejecting an unknown code, unknown version, and mismatched catalog
  entry;
- editing and resetting every Interface color, switching skins to verify Amp
  Primary Accent and the directly visible Font/Background roles, confirming
  the Ogg unavailable state, verifying Secondary/Tertiary routing through CLI,
  toggles, and Material, and checking alpha at zero, half, and full opacity;
- opening representative RGB and RGBA Settings and Material color pickers at
  ordinary and narrow viewport widths to confirm the shared four-lane layout,
  checkerboard gated alpha lane, one continuous outline around both empty gated
  fourth numeric slots with no middle seam, absent
  sub-row preview squares and popup title,
  fourth-column bar alignment, shared intermediate marker size, opaque color
  assets, visible inner/outer hue-wheel gradients, four-sided bright control
  margins, translucent base/depth layers, and the full Settings-matched frame
  band; verify the popup may cover the scrollbar but never Settings content,
  centers on high and middle source rows when space permits, clamps at the
  Settings bottom for low rows, targets the canonical
  content edge for inset swatches, zooms and fades reversibly,
  closes on Settings wheel or scrollbar movement, and preserves picker-local
  wheel input;
- hovering short, 120-character, over-limit dynamic, top-level, and nested
  control copy to confirm every authored tooltip keeps the same outer dimensions,
  five-pixel text inset, 120-code-point cap, and no vertical scrollbar;
- editing Amp Primary Accent to an ultra-bright translucent value over dark,
  bright, and detailed scenes to confirm transmission and automatic dark depth
  edges;
- toggling Settings with Escape, grave accent, and shifted tilde while
  preserving text-input ownership;
- leveling camera roll with V while a stationary trackpad touch remains held,
  then confirming a real camera-look delta cancels the leveling motion;
- Q/E vertical camera motion with Space/Shift confirmed inert;
- all three Adaptive Sync choices, reset behavior, and capability/vendor
  unavailable states;
- the Diffuse, Occlusion, Illumination, and three estimator labels in both
  skins, with no Reconstruction group or selectable spatial filter;
- the Diffuse sample default, Distribution and Occlusion endpoints, and both
  Output Hit Distance switches;
- all global Noise patterns and resolutions, Animate Samples, inheritance, each
  Specify Noise override, centered clipping, override isolation, and the final
  collapsible Accumulate Samples section;
- all four Denoising groups, Raw, both first-party bilateral methods and radius,
  supported third-party lists, stored controls, and the single final backend
  notice;
- every changed control at both endpoints and its unavailable state;
- disabling and re-enabling both sample-count gates while a nonminimum value is
  stored, confirming the disabled value and knob move to one sample, the stored
  preference returns on enable, the Primary Accent knob retains depth, the
  grayscale treatment reverses smoothly, and its numeric lane accepts an exact
  value without changing the control width;
- Representation rebuild/refit transitions, staged status, and **Allow Ray
  Traversal** with sky, sun, and flashlight effects selected;
- both sun Ratio Estimator states, both Output Hit Distance states, and the
  MSAA unavailable state;
- flashlight ray traced shadows, horizontal and vertical offset endpoints,
  beam and Angular Size defaults/endpoints, increasing penumbra width, pure-white
  Color reset, initial overlap repair, radius growth at contact, fast-motion and
  sliding collision safety, scene-depth-independent mount and aim across rows of
  pillars, direction-only sway, and independent SIGMA history;
- alpha-tested foliage silhouettes under sun, sky, and flashlight ray queries;
- Auto Exposure off with only Enable visible and exact color parity with the
  established manual AgX path; then bright, dark, neutral, and saturated views
  while enabled, both Maximum Brightening and Maximum Darkening endpoints, both
  Exposure Compensation endpoints, both Adjustment Period endpoints, and
  symmetric bright/dark settling;
- affected dropdowns while their dependent layout is open and clipped, plus
  World Materials from Default to every White mode while verifying that the
  authored popup finishes roll-up and a stable Settings frame is presented
  before material or render-pass work begins;
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

- `2026-08-14.4`: Made the Performance summary and Settings hash use permanent
  full-opacity retained surfaces and fixed text baselines, removed the root
  inset shadow, eliminated the duplicate animated hash glyph run, and made one
  Settings inner outline continuously morph into the compact hash perimeter.

- `2026-08-14.3`: Preserved the Settings scrollbar channel through root motion,
  strengthened the single settled Settings outline, enclosed both gated alpha
  numeric slots with one perimeter, opened Pathing once on each Path Tracing
  transition, and added fingerprint-owned concurrent schema allocation.

- `2026-08-14.3`: Excluded Material drawer presentation from settings snapshot
  schema `0003`, separated logical collapse from temporary ImGui visibility,
  and made compact Performance and Settings bodies use one renderer.
- `2026-08-14.2`: Excluded root collapse presentation from settings snapshot
  schema `0002`, unified collapsed Settings chrome on animated root geometry,
  removed the rejected fixed viewport shadows, hid gated alpha numeric widgets,
  and completed the corrected Interface and Material copy.

- `2026-08-14.1`: Added the retained versioned settings snapshot line and
  decoder catalog, rebuilt fixed Settings viewport edge shadows, moved Material
  and Interface to bounded side-labeled controls, retained checkerboard alpha
  lanes while gated, removed selectable Diffuse reconstruction, added
  first-party bilateral methods to every Ray Marching signal, simplified
  Denoising status copy, and made Accumulate Samples the final collapsible Noise
  section.

- `2026-08-12.4`: Added Lighting Solution as the first General decision,
  inserted the Pathing drawer, defined Lighting Solution drawer/body gating and
  state retention, and kept renderer topology changes on the established
  deferred combo barrier.

- `2026-08-12.3`: Centered the scoped picker body on its source row before
  applying Settings-bound clamps, expanded the shared panel-inset role across
  the full frame band, added a four-pixel bright control margin and visible
  wheel-edge gradients, and retained once-observed Performance timing rows.

- `2026-08-12.2`: Made the scoped picker frame opaque independently of caller
  alpha, restored source-following vertical placement with the existing bottom
  clamp, aimed inset-source pointers at the canonical Settings edge, expanded
  popup padding, outlined both hue-wheel edges, and capped tooltips at 120
  Unicode code points.

- `2026-08-12.1`: Restored uniform authored tooltip dimensions; routed every
  first-party color editor through one RGB/RGBA policy; made all authored
  pickers use four fourth-column-aligned lanes with a disabled RGB alpha lane;
  removed subordinate preview squares, popup headings, and Advanced Accents;
  restored a translucent popup base with only a two-pixel opaque rim; and
  unified visible marker radii at the midpoint between the compact and prior
  snap-circle sizes.

- `2026-08-11.4`: Forward-ported the accepted UI contract onto the UVSR Engine
  mainline and retained the canonical dropdown-roll target and lifecycle name
  required by current repository policy. This revision changes no UI behavior.

- `2026-08-11.3`: Matched the command surface to compact Performance, restored
  the Performance selector's ordinary control inset, replaced the scoped Amp
  picker with a hue wheel plus rounded hue/transparency bars, removed the
  unfinished Hardware view and backend, and made Reset restore Interface and
  Performance defaults as well as renderer settings.

- `2026-08-11.2`: Reduced the authored panel target width by 20 percent, narrowed
  the Settings scrollbar without changing its fillet or inset, balanced and
  opacity-stabilized the collapsed Performance summary, made color pickers obey
  stack appearance and bottom bounds, use an opaque-strengthened Primary
  Background, and close on Settings scrolling, matched the Settings-to-command
  gap to the ordinary panel gap, and clamped repeated outward wheel input at
  Settings endpoints.

- `2026-08-11.1`: Unified authored panel, drawer, control, and scrollbar
  rounding; made the Settings scrollbar flush with its outline; moved shared
  Settings chrome above the last visible child; rebuilt General on the ordinary
  drawer path; and expanded the Performance selector to the full content width.

- `2026-08-10.7`: Added the fixed four-fillet Settings inner silhouette and
  child-layer shadow continuation over General, split authored sliders into
  toggle-width twin bubbles, and restored a transition-gated 180-millisecond
  authored combo roll without moving renderer mutation into popup code.

- `2026-08-10.6`: Matched root-body fillets to Window Rounding, added a fixed
  clipped top-inset shadow, retained the Performance summary while collapsed,
  made popup selection glide between compact rows, replaced moving slider copy
  with a fixed exact-input lane, routed Primary Accent to raised slider knobs,
  and eased gated presentation over 280 milliseconds.

- `2026-08-10.5`: Made the Settings root own its persistent title-to-General
  inset, aligned the child scrollbar with General, added the Interface animation
  control, consolidated authored palettes to Primary Accent, Font Color, and
  Primary Background Color, and rebuilt authored combo popups with compact
  uniformly rounded option rows.
- `2026-08-10.4`: Restored the ordinary drawer-sized Performance-to-Settings
  gap, separated root backdrop silhouettes from empty margins, made Interface
  the final drawer after General-led renderer controls, added alpha-aware
  primary, semantic, font, and background roles, and constrained ColorEdit
  pickers outside Settings content.
- `2026-08-10.3`: Added the Interface drawer and live per-skin main plus shared
  negative and positive accents, routed semantic colors through CLI, toggles,
  and Material, made bright-header depth selection procedural, restored fully
  rounded tangent root panels, removed the Settings top band, and gave
  Performance the authored root-panel transition.
- `2026-08-10.2`: Removed the gap between the detached Performance and Settings
  panels, repaired authored opening and reversal motion, corrected scrollbar and
  dark-surface depth polarity, added bright-surface darkening outlines and true
  local Neo header translucency, restored opened selectors to stock ImGui
  behavior, and added the footer-toned Noir skin.
- `2026-08-10.1`: Detached Performance into its own independently collapsible
  panel above Settings, made Material the final Settings drawer, renamed the
  visible White and OG skins to Neo and Ogg while retaining command aliases,
  restored rounded anti-aliased depth outlines, reduced cutout highlights, and
  made Neo headers subtly scene-translucent.
- `2026-08-09.9`: Renamed the root Statistics drawer to Performance, removed the
  redundant renderer and loose status block above it, placed it directly beneath
  the Settings title, restored carved dark-control and raised-header depth,
  made White headers subtly translucent and graded, and gave authored headers
  explicit bold type with pitch-black White copy.
- `2026-08-09.8`: Added the White skin, embedded Material drawer with crosshair
  ownership, fixed root Statistics and Hardware view, four-field per-window
  status, uniform slider/toggle geometry, fixed-geometry dropdown fades, and
  command-interface-dependent Settings height.
- `2026-08-09.7`: Removed the rejected flashlight camera-centering experiment,
  including receiver rays, proximity retraction, temporal controls, optical
  state, recovery state, and Camera Movement Diagnostics. Retained only the
  emitter-aware collision sphere and continuous sweep for wall safety, plus the
  pure-white factory Color. Revisions `2026-08-09.3` through `2026-08-09.6` are
  historical experiment records and do not describe current behavior.
- `2026-08-09.6`: Added configurable flashlight Time to Action and Adjustment
  Speed, made materially different receiver depths earn independent evidence,
  reset stale optical state when the receiver changes, removed released receiver
  aim carry, and added cached Camera Movement Diagnostics.
- `2026-08-09.5`: Added temporal stability for flashlight receiver-depth
  discontinuities, coupled the transient aim to the final mount extension,
  locked physical retraction independently from lens sway, and changed the
  factory beam color to pure white.
- `2026-08-09.4`: Replaced the rejected fixed-distance flashlight proximity
  fade with camera-center receiver feedback, distance-proportional physical
  mount retraction, emitter-to-receiver visibility validation, and exact
  receiver aiming.
- `2026-08-09.3`: Replaced the flashlight's contact-only mount correction with
  an early cubic proximity fade using mount-direction and hard-safe forward
  probes while retaining immediate physical collision limits. Revision
  `2026-08-09.4` supersedes this rejected behavior.
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
