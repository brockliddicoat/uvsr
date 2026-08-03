# UVSR UI Design and Integration Reference

UI reference version: `2026-08-03.4`.

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
2. Visibility
3. Buffers
4. Statistics
5. Aliasing
6. Debug
7. Sky
8. Lights
9. Shadows

The order is product behavior. Add a top-level drawer only when the feature has
a distinct user goal and enough retained controls to justify it. Effect-specific
diagnostics belong in Debug, grouped under the effect they explain.

## Visual and Copy System

- Use Title Case for visible drawer, section, and control headings.
- Use the displayed product name **UVSR** and lowercase executable slug `uvsr`.
- Prefer the shortest label that identifies the user's decision.
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
changed. The Visibility profile selector continues to show its originating Low,
Medium, High, or Ultra recipe and appends **(Custom)** when an owned value
differs. Its adjacent circular reset restores the complete High recipe, while
each owned control can return to its originating recipe value.

## Control Composition

Use the established UVSR helpers for drawer bodies, rounded combos, animated
tree and toggle regions, reset icons, tooltips, and footer actions. Visibility,
Aliasing, Debug, and Advanced groups retain animated disclosure. Every retained
setting has a concise hover explanation, and dropdown width must leave its label
and reset lane visible. Maintain balanced ImGui ID, style, disabled, tree,
table, child, and popup lifetimes on every branch.

Debug and its World, Visibility, Physically Based Lighting, and Screen-Space
Shadows groups start expanded, then preserve user-owned disclosure state. Their
ordinary rendering choices, including the initial World material choice, are
labeled **Default**. Visibility Reconstruction starts collapsed for a full-
resolution trace and expanded for a reduced-resolution trace; its stored manual
state takes precedence after interaction.

An inherited dropdown previews its resolved concrete value but lists that value
only once. Its reset icon is the route back to recipe ownership. Do not append
owner text in parentheses; **(Automatic)** and the modified-profile status
**(Custom)** are the only exceptions. Temporal Advanced begins with Stationary
Bypass under **Algorithm** and places its remaining cost policies under **Cost**.
Any Algorithm change appends **(Custom)** to the selected Quality preview; any
Cost change appends it to the selected Temporal Cost preview. The marker clears
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

Keep the six general values on one dash-separated summary line. The labeled
Effect selector shows one retained renderer effect at a time and keeps its reset
beside the label. Complete Renderer uses a striped two-column table for the full
retained stage list; an ordinary stage includes the complete frame for context.
Visibility, Screen-Space Shadows, Temporal Reconstructive, Conservative
Morphological, and Multisample use the same readable table language for their
retained breakdowns. Completed query availability gates every timing. Dormant,
unsupported, or newly enabled work displays `--` or a direct status instead of
a fabricated zero. Never repopulate this panel with retired planners,
benchmarks, shadow techniques, or shader taxonomies.

## Scrolling and Input

Settings has one scrolling body. Keep title/status/footer positions stable while
drawers open, close, or animate. Same-frame scroll-anchor correction must block
interaction until submitted geometry and hit rectangles agree. The command bar
owns its permanently reserved one-row bottom lane; Settings must not overlap or
resize that lane. Guidance belongs in the empty input hint and disappears when
typing begins. Separate guidance with slashes. After submission, the same empty
input shows a green `Success` or red `Error` message until editing resumes.
Never add a floating result window above the command row. Up and Down continue
to recall command history. A long or multiline result may expose a trailing
details button; only an explicit click may open its bounded, scrollable,
selectable read-only popup. The catalog contains 122 entries: 118 values and
four actions.

Escape toggles Settings unless an active edit or popup owns it. `/` toggles the
command interface when text input does not already own the key. M, F, V, and Z
shortcuts must respect active text/popup ownership.

## Renderer Boundary

Renderer-affecting changes cross after ImGui composition, at a point where no
control or popup still references the previous structure. Scene-loading resource
ownership rejects renderer mutations while leaving interface-only commands
usable.

Controls must remain decoupled unless their actual resource contract requires a
dependency. In particular:

- Visibility changes only visibility-owned state and resources.
- TAA, CMAA2, and MSAA are independent states with deterministic pass order.
- World appearance, Visibility views, Physically Based Lighting filters, and
  shadow isolation are separate Debug states. A Physically Based Lighting
  filter preserves Visibility execution but suppresses its ordinary composite;
  an explicit Visibility view wins. The retired Edge Overlay must not return as
  hidden shadow state.
- Screen-Space Directional Shadows require a primary directional light but do
  not enable unrelated lighting features.

Changing renderer topology must invalidate only the affected passes/history.
Do not force a scene reload when a narrower pass or render-target refresh is
sufficient.

## Loading and Error Presentation

Loading keeps presenting frames while staged scene and renderer work proceeds.
The UI must never expose a partially prepared renderer state as interactive.
Errors name the unavailable feature and the missing condition in user language.
Do not expose internal planner, factory-profile, or permutation terminology.

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
- every changed control at both endpoints and its unavailable state;
- affected dropdowns while their dependent layout is open and clipped;
- scene loading and command completion;
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

- `2026-08-03.4`: Added group-owned Custom markers to Quality after Algorithm
  changes and to Temporal Cost after Cost changes.
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
