# UVSR UI Design and Integration Reference

UI reference version: `2026-07-22.1`.

## Purpose

This is the single canonical reference for UVSR's UI. It defines the accepted
visual language, capitalization, content formatting, information hierarchy,
control behavior, animation and scrolling model, renderer-facing transaction
boundaries, implementation procedure, and verification evidence. It applies
whenever an agent adds, imports, merges, or changes Settings, Statistics,
loading, benchmark, magnifier, or material-editor UI.

Functional preservation is necessary but not sufficient. A Settings change is
incomplete until it uses the established animated control composition, retains
authored spacing, opens safely in a live Release build, and passes the
mechanical UI and animation source contracts.

The failure that established the renderer-affecting dropdown rules is recorded
inside this document under
[Reference Incident: Aliasing Method Dropdown Stutter](#reference-incident-aliasing-method-dropdown-stutter).
Read that section before changing staged presentation, the shared dropdown
commit barrier, AA topology refresh, or any control that changes both UI
structure and renderer resources. Do not create a competing UI style guide,
copy guide, animation guide, or implementation procedure; amend this reference
and its contracts instead.

## Reference Versioning

The UI reference version uses `YYYY-MM-DD.N` in the repository's
America/Chicago time zone. Increment `N` for every later normative revision on
the same date. Bump the version whenever accepted visual, content, control,
animation, scrolling, transaction, implementation, or verification behavior
changes; typo-only prose corrections that cannot alter implementation may keep
the current version.

Before UI implementation begins, record both this exact version and the current
`AGENTS.md` policy version in the task plan, assignment, or working notes. Read
the file itself rather than trusting a cached excerpt or earlier handoff. Recheck
the version immediately before integration review and record it in the final
handoff. If the value changed at either checkpoint, stop using the older summary,
reread the current reference, reconcile the implementation and contracts, and
report both the stale and current versions. A UI reference with no version is
stale by definition.

When an element does not match current UI behavior, compare the version recorded
by its implementation task with this line before inventing a local exception.
A mismatch is evidence that old guidance may have caused the defect; it does not
waive source inspection, runtime diagnosis, or repair against the current
version.

## Document Map

- [Reference Versioning](#reference-versioning) defines the version format,
  checkpoints, and stale-guidance diagnosis.
- [Visual and Content System](#visual-and-content-system) defines what the UI
  looks like, how it is worded, and how information is organized.
- [Mandatory New-Element Intake Checklist](#mandatory-new-element-intake-checklist)
  classifies ownership, state, consumers, animation, and renderer cost before
  implementation.
- [Required Composition](#required-composition) defines the accepted helpers,
  lifetimes, layout, scrolling, controls, loading screen, and magnifier.
- [Reference Incident: Aliasing Method Dropdown Stutter](#reference-incident-aliasing-method-dropdown-stutter)
  records the complete failure, final transaction, renderer ownership repair,
  and regression boundary.
- [Integration Review](#integration-review),
  [Required Checks](#required-checks), and
  [Handoff Evidence](#handoff-evidence) define completion.
- [Reference Revision History](#reference-revision-history) records normative
  revisions without replacing source-control history.

## Visual and Content System

### Information Architecture

- Keep the Settings hierarchy in this order: title bar; renderer and performance
  status; separator; the single scrolling body; General, Visibility, Buffers,
  Statistics, Aliasing, Sky, and Lights; then Reset, Screenshot, Zoom, and
  Restart in one footer row.
- Launch with Settings hidden. The first Escape press opens General only. Do not
  introduce automatic first-frame opening, a splash-owned opening timer, or a
  second settings window. Keep ImGui `.ini` persistence disabled so a previous
  run cannot override that launch contract.
- Put ordinary configuration in its owning drawer, advanced subdivisions in
  animated nested sections, effect telemetry in Statistics, and run controls
  beside the statistics they measure. Do not duplicate the same state or
  telemetry in multiple drawers.
- Organize controls by user decision order: enablement or profile first,
  fundamental method and quality next, dependent tuning after them, diagnostics
  last. Keep a gate outside the region it owns so disabling it never removes the
  only control capable of restoring that region.
- Show only usable product behavior. Do not expose dormant experiments,
  redundant aliases, provenance, source-project names, or controls that are
  always off and did not produce measured value. Use technical language only
  when it helps the user make the decision. Preserve necessary implementation
  provenance in code and technical documentation, not in visible UI copy.

### Typography and Capitalization

- Use the packaged 16 px semibold UI face at every Settings nesting depth. On
  Windows it follows the accepted Segoe UI Semibold presentation and expanded
  word spacing; the packaged Geist fallback preserves the same hierarchy on
  other platforms. Do not introduce a second font, size hierarchy, or local
  spacing override for a new control.
- Write every visible window title, drawer title, nested-section title, control
  label, action label, table heading, and dropdown option in conventional
  English Title Case. Keep short articles, conjunctions, and prepositions
  lowercase unless they are the first or last word. Preserve required casing
  for product names, acronyms, units, scene asset identifiers such as
  `hdri_sky_1`, and mathematical notation.
- Write tooltips, transient status text, empty states, warnings, and errors in
  sentence case with terminal punctuation. A progress counter or compact
  telemetry value may omit punctuation when it is not a sentence.
- Apply the repository's same Title Case rule to every Markdown heading and
  standalone bold heading in UI documentation. Run
  `tools/check_document_title_case.cmd`; do not hand-wave a checker failure.
- Use one semantically correct term everywhere. Search selectors, reset
  tooltips, Statistics labels, benchmark copy, and documentation before
  renaming a concept. Do not leave two labels for one setting.

### Labels, Options, Values, and Units

- Prefer a short noun phrase that states the user's decision, such as
  `Distribution Exponent`, `Depth Normal`, `Geometry`, `Multisample Reference`,
  or `Temporal Reconstructive`. Put explanation in the tooltip, not the label.
- Keep every label and dropdown option readable at the current minimum Settings
  width. Shorten the wording before widening one control or clipping text. Do
  not append the technique's origin, an implementation citation, or redundant
  words such as `Noise` when the owning label already supplies that context.
- A dropdown preview must exactly match its selectable option except for a
  deliberate state suffix. Aliasing does not show `(Preset)`. Visibility keeps
  its originating quality when edited, for example `Medium (Custom)`. A
  temporary unavailable candidate may use `(Mutex)` inside the open popup. The
  selected preview may retain `(Mutex)` only when an external renderer mode has
  made its stored choice unavailable; dependent setting rows collapse instead
  of remaining as gray `(Mutex)` rows.
- Use `###HiddenId` or `##HiddenId` only for stable ImGui identity; hidden ID
  text must never become visible. Every repeated label needs a unique enclosing
  `PushID` or hidden suffix.
- Format numbers with one stable precision and a compact unit: `%`, `ms`, `fps`,
  `MiB`, `gb/s`, `tflops`, or `x` as established by the neighboring values.
  Keep the performance line ordered as resolution, bandwidth, compute, frame
  time, then frame rate. Do not report a timer that is stale, frozen, or not
  derived from the work it claims to measure.
- Distinguish a magnification factor from its pixel-area descriptor: the control
  cycles 2x through 5x, while the overlay reports `4x`, `9x`, `16x`, or `25x`.
  Preserve those literal forms rather than substituting a multiplication glyph
  or fractional representation.

### Tooltips, Status, and Progress Copy

- Give every UVSR-owned interactive control one concise plain-English hover
  tooltip that says what changes or why a choice is unavailable. Attach the
  tooltip immediately after the actual interactive item or animated header.
- Keep the patched tooltip envelope consistent at every nesting depth: at most
  20 em or 42 percent of the work width, at most 4.75 em or 25 percent of the
  work height, at least 8 em of wrapping width, and the accepted 5 px inner
  inset. Do not create a one-off tooltip window or allow a long line to resize
  Settings.
- Describe outcome first. Avoid implementation jargon, marketing claims,
  provenance, duplicated labels, and paragraphs that merely restate every
  option. Put safety or restart consequences in the tooltip when they affect the
  choice.
- Use status copy only for actionable or genuinely changing state. Do not show
  idle readiness prose such as `Ready to test...`, an inert Cancel button, or
  export guidance for functionality that is not present.
- Render inline unavailable guidance with the shared disabled-text token and
  wrap it within its owning drawer. Keep each temporal anti-aliasing
  unavailability explanation on two authored lines at the standard Settings
  width so neither message clips or changes to ordinary text color.
- Loading copy is exactly one scene line followed by real object, import-step,
  decoded-texture, and GPU-ready counts. The scene line ends with
  `please wait.`, `please wait..`, or `please wait...` at two updates per second.
  Do not add a progress bar or elapsed timer.
- A pending dropdown preview changes immediately, but it must not claim that
  renderer telemetry already belongs to the staged choice. Use `--` until the
  committed renderer produces compatible data.

### Layout, Spacing, and Responsive Width

- Derive the Settings edge margin from 60 percent of the current UI font size,
  rounded to a whole pixel and clamped to at least one pixel. Use that same
  physical margin for Settings and the top-right magnifier.
- Derive Settings width from the widest renderer/status line and the widest
  complete control-plus-label row, including padding, scrollbar, footer, and a
  small readability allowance. Clamp it to the renderer width minus both edge
  margins. Do not replace the content-based width with a percentage or tune one
  drawer independently.
- Use one shared control width and the established full-row alignment. Labels
  follow their fields on labeled slider rows; full-width dropdowns and buttons
  keep the same left and right bounds. Reserve the reset-icon lane so an icon
  appearing does not squeeze or move its owner.
- Keep the reset icon trailing on every top-level dropdown and on every
  non-dropdown control at every nesting depth. Only a dropdown inside an
  animated nested section places its reset icon in the immediately preceding
  tree-indent gutter. The gutter placement must not move or resize the dropdown,
  its label, its popup, the row height, or any sibling, and its hidden and
  visible endpoints must submit identical layout. This recovers the indent
  width for the nested dropdown label while leaving every un-nested dropdown
  exactly where it was.
- Preserve ImGui's authored `WindowPadding`, `FramePadding`, `ItemSpacing`, and
  `ItemInnerSpacing`. Insert one explicit `ImGui::Spacing()` between every pair
  of top-level drawer headers. Do not borrow the final spacing from the previous
  drawer body or eliminate it when that drawer is collapsed.
- Use one scrollbar only, owned by `##SettingsBody`, and reserve its width even
  before scrolling is needed. The title, status, and separator remain pinned;
  drawers and their four-button footer scroll together inside SettingsBody.
- Keep the four footer buttons equally wide with visually centered text. Do not
  add a fifth footer action without revisiting minimum width, keyboard access,
  and this hierarchy.

### Surfaces, Blur, Outlines, and Shadows

- Reapply `ApplyReferenceStyle` because the host can restore ImGui defaults
  after display-scale changes. The accepted radii are 8 px for windows, child
  surfaces, popups, and scrollbars; 5 px for drawer bodies; and 4 px for frames,
  grabs, tabs, title disclosure hover, and row highlights.
- The main panel uses neutral tinted black `(0.018, 0.016, 0.020)` at 0.60
  opacity. Fields, buttons, dropdown arrow buttons, and slider tracks use the
  same RGB at 0.72 opacity with opacity-led hover and active states. Drawer
  bodies and footer buttons use transparent graphite
  `(0.66, 0.67, 0.69, 0.13)`, rising to `(0.74, 0.75, 0.77, 0.20)` on hover and
  `(0.80, 0.81, 0.83, 0.26)` while active. The Settings title surface remains
  the distinct `(0.146, 0.146, 0.154, 0.652)`. Top-level headers use the accepted
  blue `(0.26, 0.59, 0.98)` at 0.31, 0.48, and 0.65 alpha for normal, hover, and
  active states. Popup background is `(0.04, 0.04, 0.045, 0.92)`; standard text
  is `(0.94, 0.95, 0.98, 1.0)` and disabled text is
  `(0.58, 0.59, 0.61, 1.0)` under the shared 0.38 disabled alpha. Reuse these
  tokens through helpers; do not create a nearly matching local palette.
- Expanded Settings consists of separate rounded title and body surfaces;
  collapsed Settings consists of separate rounded title and status surfaces.
  Submit one 4 px backdrop-blur mask for each real surface. Never blur the
  rectangular union or the empty wedges between rounded surfaces.
- Draw exactly one one-pixel translucent vertical-gradient outline per visible
  edge, inset by half a pixel. The gradient rises from roughly 0.10 to 0.30
  alpha while its RGB rises from `(0.88, 0.90, 0.94)` to
  `(0.96, 0.97, 1.00)`. Suppress ImGui's ordinary border wherever the custom
  outline owns the edge; compact and expanded states must have the same apparent
  weight. Preserve the drawer body's 2 px clipped top gap so its outline does
  not double the header seam.
- Draw the analytic shadow before the translucent panel, with 10 px softness,
  0.34 opacity, and a 3 px downward offset. Cut the shadow away from the panel
  interior. Settings and magnifier use the same shadow and outline vocabulary.
- Clip and transform background blur, outlines, shadows, child vertices, and
  draw-command clip rectangles with the parent appearance animation. A child or
  effect that remains full-size during a parent transition violates the surface
  contract.

### Controls, Defaults, and Reset Feedback

- Use the repository helpers for headers, combos, sliders, checkboxes, reset
  icons, folder actions, centered actions, tables, and animated regions. A raw
  ImGui primitive is acceptable only when no helper owns that semantic and the
  exception is documented and tested.
- Use animated two-state toggles. Collapse toggle-owned controls into nothing
  after the renderer consumes a disable, and reverse the motion after enable.
  Grayscale is reserved for genuine external unavailability or benchmark locks,
  not ordinary gating.
- Make the complete clickable area use the accepted hover, active, focus, and
  disabled treatments. Reset recycled popup highlight state so one dropdown's
  cursor animation cannot appear in another.
- Show the small animated reset icon only when a safely resettable value differs
  from its owner-defined preset or factory/scene default. Reset only the state
  that control owns and reconcile the profile afterward. Do not attach reset
  icons to adapters, scene destinations, folder buttons, run/cancel commands,
  Screenshot, Restart, or Zoom.
- Defaults are product behavior. Aliasing presets keep Stable Interior and
  Sharpness off; changing morphology changes only the CMAA2 morphology override;
  Visibility custom labels retain their originating profile. Do not silently
  redefine a preset while adding its control.
- Preserve the factory baseline unless the user explicitly changes it: PBR with
  Deferred shading, enabled Temporal Reconstructive Medium AA, Sharpness off,
  Visibility High, procedural sky on, White World off, Freelook camera,
  Complete Renderer Statistics, zoom off, and 120 warmup plus 240 measured
  benchmark frames. Footer Reset restores renderer settings but intentionally
  leaves the current camera and scene unchanged.

### Tables and Statistics

- Put all effect telemetry in Statistics, selected by effect, using the shared
  two-column striped table with inner horizontal borders and proportional
  columns. Use concise Title Case metric headings and stable value formatting.
- Use `BordersInnerH | RowBg | SizingStretchProp` for these tables. Renderer,
  visibility, and resource tables use a 3:1 label/value ratio; Anti-Aliasing uses
  3:2; the last benchmark result uses 2.5:1:1. A new table must justify a
  different schema in this reference rather than tuning columns ad hoc.
- Begin a table only inside an active drawer or nested child lifetime. Bound
  selector indices before array access and keep every `BeginTable` /
  `EndTable` path balanced, including closing animation frames.
- Keep method-dependent Statistics structure synchronized with its source
  drawer. During a staged structure swap, draw the staged row schema but show
  `--` until matching renderer timings exist. Never label committed-method data
  as though it came from the staged method.
- Keep benchmark actions and their live state together. Cancel exists only while
  a run is queued or active and enters/exits through the same animated-region
  system. Removed export UI and idle readiness copy stay removed.

### Input, Focus, and Accessibility

- Escape owns Settings visibility; Z and the footer Zoom action share one zoom
  cycle. Preserve X/C horizontal camera rotation, Space/Shift vertical motion,
  and V rotation reset. A new shortcut must not shadow text input, popup
  navigation, or an existing renderer command.
- Block input while Settings geometry is scaled, clipped, structurally staged,
  or waiting to commit, but keep its visual alpha unchanged. The window must
  continue capturing the pointer so clicks cannot leak into camera control.
- Preserve keyboard focus, default focus for the current dropdown choice, and a
  complete clickable target. Do not encode state through color alone; labels,
  disclosure direction, check state, pending preview, and reset visibility must
  remain sufficient.
- Validate at native pixels and at every supported display scale. Text may not
  clip, outlines may not double, controls may not overlap the scrollbar, and
  hover targets must match transformed geometry.

## Before Editing

1. Record the UI reference version printed below this document's title and the
   current `AGENTS.md` policy version in the task plan, assignment, or notes.
   Resolve any mismatch with an earlier assignment or handoff before editing.
2. Read the current renderer baseline in `README.md`, the UI helper
   implementations in `src/uvsr.cpp`, and this complete procedure.
3. Capture the destination's existing drawer and control patterns before
   importing another branch. Compare behavior, not only data flow.
4. Record every incoming top-level drawer, nested section, dropdown, slider,
   toggle, button, table, and progress indicator in the active execution plan or
   task notes.
5. Assign each incoming element to the matching UVSR-owned presentation path
   below. A raw ImGui call from another lineage is not evidence that the
   destination intends the raw presentation.

## Mandatory New-Element Intake Checklist

Complete this checklist for every new or materially changed UI control before
editing. Record the answers in the execution plan or task notes; no item may
remain unknown when implementation begins.

- [ ] Record UI reference version `2026-07-22.1` for this revision and confirm
  that no cached assignment, excerpt, or handoff names an older version.

### Ownership and State

- [ ] Name the owning top-level drawer, nested section, toggle, preset, default,
  and reset behavior.
- [ ] Assign a unique visible label, hidden ImGui ID, and body ID where
  applicable.
- [ ] Decide whether the control owns a value, a command, a disruptive
  destination, or a renderer restart. Commands and disruptive selectors do not
  receive reset icons unless the product explicitly requires one.

### Control Classification

- [ ] **Static or Immediate Control:** Use the established checkbox, slider,
  reset-icon, or action-button helper. Prove it cannot add, remove, or resize
  another row and cannot begin expensive synchronous work.
- [ ] **Toggle-Gated Control:** Keep the toggle outside one balanced
  `BeginAnimatedToggleRegion` / `EndAnimatedToggleRegion` envelope containing
  every owned dependent row. Preserve slider presentation values throughout
  collapse.
- [ ] **Nonstructural Dropdown:** Use `BeginRoundedCombo` and
  `DrawDeferredDropdownOption`, the shared keyed queue, and the pending preview.
  If the selection is expensive, identify its exact synchronous path and rely
  on `TryApplyDeferredDropdownUiActions` only to place that work after a stable
  frame.
- [ ] **Structural Dropdown:** Use `DeferredUiStructuralPresentation<T>`,
  `PresentSelectors`, `PresentStructuralBody`, `ShowStructuralBody`, `Advance`,
  and `ReadyForCommit`. Collapse committed structure completely, swap while
  hidden, expand staged structure, present one stable frame, and only then
  commit.
- [ ] **Renderer-Topology Selector:** Complete both the structural-dropdown
  transaction and a pass/resource ownership audit. Separate truly target-bound
  objects from reusable pipelines, caches, and compatible allocations.

Treat render-target allocation, pass recreation, PSO creation, shader reload,
file I/O, scene changes, adapter restart, and history-layout changes as
expensive. UI deferral can relocate a synchronous pause; it cannot eliminate
one. The Aliasing Method failure reached `RenderTargets::Init`, broad
`CreateRenderPasses`, and first-use `CreateCmaa2Pass`. The final repair also
needed renderer-side ownership changes.

### Consumer Inventory

- [ ] Search the mutated field and every derived helper that reads it,
  including `GetResolved...`, `Uses...`, availability checks, pass/timing
  getters, Statistics, benchmark controls, status text, footer controls, and
  other drawers.
- [ ] Classify every consumer as selector-only, value-only, or structural.
- [ ] Feed every selector from the same staged presentation.
- [ ] Give every structural consumer either the same staged
  collapse/swap/expand phase or a permanently fixed envelope.
- [ ] Show stable placeholders such as `--` until staged renderer data exists.
- [ ] Prove the expensive commit changes values only and cannot add, remove,
  reorder, or resize UI anywhere.

Aliasing Method is the reference failure. Staging only the Aliasing drawer left
the Anti-Aliasing Statistics row set as a cross-drawer post-commit reflow.
`##StatisticsAliasingMethodBreakdown` now shares the structural phase and holds
target values at `--` until renderer data is ready.

### Presentation Purity and Lifetime

- [ ] UI composition performs no normalization, reconciliation, renderer
  mutation, allocation, file I/O, or scene mutation.
- [ ] Do not treat `BeginDisabled` as a purity guard. It blocks input, not
  ordinary C++ assignments.
- [ ] Perform normalization only in the explicit staged/apply mutation, as with
  `NormalizeRedundantAntiAliasingOverrides`.
- [ ] Deferred callbacks capture stable values or durable objects only. Never
  capture build-frame references, local helper lambdas, or pointers into a
  staged optional that commit will destroy.
- [ ] Scene-load cancellation clears both queued actions and staged
  presentation.

### Animation and Scroll Ownership

- [ ] Give each changing body exactly one clip-height animation owner.
- [ ] Use `DrawCollapsingHeader` with `BeginDrawerBody` / `EndDrawerBody`,
  `BeginAnimatedTreeNode` / `EndAnimatedTreeNode`, or
  `BeginAnimatedToggleRegion` / `EndAnimatedToggleRegion`; do not mix raw
  alternatives into the same ownership path.
- [ ] Keep `##SettingsBody` as the only scroll owner.
- [ ] Register stable boundaries with `TrackSettingsScrollAnchor`, mark every
  transition frame with `MarkSettingsLayoutAnimationActive`, and call
  `EnsureAnimatedChildLayoutSubmission` for clipped animated children.
- [ ] Verify every begin/end, ID, style, disabled, child, combo, table, and tree
  scope remains balanced during both opening and closing frames.

### Required Runtime Stress Gate

- [ ] Test every dropdown choice cold and warmed, forward and backward, with
  every affected drawer both open and closed.
- [ ] For structural choices, keep every cross-drawer consumer open, especially
  Statistics, while switching.
- [ ] Repeat while scrolled above, through, and below the changing body. Reverse
  wheel direction before motion settles and exercise partly clipped and fully
  offscreen content.
- [ ] Move the pointer, close Settings, toggle zoom, and begin a scene transition
  while a choice is pending.
- [ ] Confirm the pending label appears immediately, committed renderer state
  stays unchanged through popup roll-up, collapse, and expansion, the hidden
  swap is invisible, one stable frame is presented, and commit causes no second
  reflow.
- [ ] Open every changed dropdown and confirm its fixed-layout contents roll
  into view at full alpha. Click once during roll-down and verify the blocked
  click is discarded rather than replayed; select after opening and verify the
  popup rolls fully closed before dependent layout begins to collapse.
- [ ] Distinguish animation correctness from renderer performance. If the UI is
  motionless but the process pauses after commit, trace and reduce the
  synchronous renderer path instead of adding another UI timer.
- [ ] Run the focused animation, UI source, renderer source, and feature
  contracts; the Release build; the full CTest suite; and live stress checks
  before calling the element complete.

This intake gate closes the three gaps exposed by Aliasing Method: incomplete
consumer inventory, presentation-time mutation despite disabled input, and
treating deferred commit timing as a substitute for renderer-side optimization.

## Required Composition

### Main Settings Window

- Launch with Settings hidden. The first Escape press opens the outer Settings
  window with General open and Visibility, Buffers, Statistics, Aliasing, Sky,
  and Lights collapsed. Later Escape presses close and reopen the complete
  Settings composition without changing drawer state.
- Animate the complete parent and body composition through the shared 180 ms
  86-percent-to-full magnifier grow-and-fade in both directions. Continue
  submitting the noninteractive Settings draw list until a close animation
  reaches zero. Register every Settings-owned child draw list created by a
  drawer, nested tree, toggle region, or scroll body and transform it with the
  root draw list. A child surface must never remain full-sized while the parent
  zooms.
- Keep ImGui's title-bar compact/expand reshape on its patched 200 ms smoothstep.
  It is separate from the 180 ms whole-window appearance, drawer, nested, gated,
  and zoom clocks; do not force them onto one duration or advance one from
  another's state.
- Transform the blur bounds with the same center, scale, and opacity so the
  backdrop never extends beyond the visible animated panel. Expanded Settings
  consists of separate rounded title and body surfaces; collapsed Settings uses
  separate rounded title and status surfaces. Submit one backdrop mask for each
  real surface in both states. Do not blur either rectangular union: every empty
  corner wedge between surfaces must remain untouched.
- Render the Settings shadow analytically before ImGui composition with the
  magnifier's 10 px blur, 0.34 opacity, and 3 px downward offset. Cut the shadow
  away from the rounded panel interior so translucent WindowBg does not reveal
  a dark fill beneath the menu.
- When the Settings appearance transform scales draw vertices, apply the same
  transform to every draw command clip rectangle. Block Settings input until the
  appearance transform reaches its stable endpoint so hit testing cannot target
  unscaled geometry.
- Keep one outline owner at every edge. Settings uses its one-pixel window
  outline; ImGui frame borders must not add a second seam between the rounded
  title and status surfaces when the window is collapsed. Use the same
  half-pixel inset and gradient-outline path for expanded and collapsed
  surfaces, and suppress the standard ImGui border wherever that custom outline
  is present.

### Host Window and Activity Overlay

- Fit a windowed launch inside the primary monitor's usable work area. On
  Windows, measure the visible DWM frame separately from the invisible resize
  border, then make the visible top, taskbar, left, and right gaps equal. The
  initial client may become slightly taller than 16:9 to preserve those visible
  margins; do not force the requested aspect after native-frame correction.
- Keep the title diagnostic and reproducible: active graphics API, one-word
  experiment label, seven-character embedded source revision, and local launch
  time in 24-hour `HHmm` form. The required launcher validates a lowercase
  letters-only experiment label. Do not use an unlabeled executable for visual
  acceptance or performance evidence.
- While a visibility benchmark is queued or active, show the independent
  top-right activity overlay even when Settings is hidden. It is undecorated,
  noninteractive, always auto-sized, positioned 12 px from the top-right, and
  uses 0.82 background opacity with the shared UI font.
- Cycle that overlay through `Benchmarking.`, `Benchmarking..`, and
  `Benchmarking...` at two states per second, followed by collected measured
  frames over requested frames. Warmup frames do not increment the displayed
  count. This overlay reports activity only; detailed stage results and Cancel
  remain in Statistics.

### Material Editor

- M opens and closes the one `Material Editor` window. Selecting a surface must
  never open it automatically. Place it at the top-right using the shared
  font-derived edge margin, auto-size it, and capture one 8 px rounded backdrop
  blur surface through the common style.
- Show `Material <id>: <name>` for the selected material. With no selection,
  show the sentence-case empty state `Click a scene surface to select a
  material.` without inventing a placeholder material.
- Route the Material Domain selector through `BeginRoundedCombo`,
  `DrawDeferredDropdownOption`, and the shared commit barrier. Capture the
  material and scene by durable shared ownership, mark the material dirty, and
  invalidate the scene graph only at commit. The embedded Donut material editor
  is the accepted external-control exception; do not use that exception to add
  raw UVSR-owned controls elsewhere.
- Block Material Editor input during a visibility benchmark and while any
  deferred dropdown is pending. Preserve full visual alpha during the pending
  lock so the window does not flash gray, and keep its begin/end and disabled
  scopes balanced even when the native close button is used.

### Top-Level Drawers

- Create every Settings sibling with `DrawCollapsingHeader`.
- Put all drawer content between `BeginDrawerBody` and `EndDrawerBody`. This is
  required even when the body contains only text or a table; the pair owns the
  measured-height animation, child-window lifetime, surface, outline, clipping,
  and control width.
- Give each animated body exactly one clip-height animation envelope. Measure
  the complete submitted content directly while its target is open; do not
  smooth a measurement and then apply a second height interpolation to the same
  body. Freeze the last trustworthy expanded height throughout close motion so
  changing or disappearing children cannot deform the exit envelope.
- Before a body's first open animation, submit one alpha-zero measurement frame
  at its natural expanded height. Start visible height and alpha motion only
  after that measurement exists; never substitute a one-row estimate that
  jumps to the complete body on the following frame.
- Continue logical layout submission when an animated child is fully clipped
  offscreen. Let item-level clipping suppress rendering, but never let a skipped
  parent make an open nested header report false, advance toward closed, discard
  its trustworthy expanded height, or emit an anchor from stale item bounds.
- Advance drawers, nested sections, and gated regions with the same capped
  180 ms normalized clock. Never use content-size-dependent completion or raw
  frame delta for layout motion; a long renderer frame must not snap one
  envelope while its child continues at a different rate.
- Preserve scrolling across animated top-level height changes. Outside a
  bottom-pinned viewport, adjust scroll only by deltas from bodies whose larger
  previous-or-current envelope is wholly above the viewport. When the viewport
  was already pinned to the bottom, preserve that endpoint with the total
  displayed-height delta.
- Retain the previous Settings viewport height as a transient minimum only while
  a nonzero scroll offset, wheel input, or scrollbar drag needs protection.
  Layout animation alone must not pin the old height: an unscrolled SettingsBody
  follows the animated drawer envelopes so the outer background contracts
  continuously. Permit growth immediately and release scroll retention after
  correction settles so the compact panel leaves no permanent empty surface.
- Give each body a unique hidden ID and give every substantial drawer a unique
  `PushID` scope. `CollapsingHeader` does not create an ID scope for its body.
- Call `ImGui::Spacing()` after every top-level drawer body or collapsed header,
  before constructing the next sibling. Never rely on incidental spacing inside
  a previous body.

### Nested Sections

- Use `BeginAnimatedTreeNode` and `EndAnimatedTreeNode` for expandable content.
  Do not import raw `TreeNode`, `TreeNodeEx`, or manual `TreePop` pairs into
  Settings.
- Set the nested-section tooltip immediately after its animated header is
  submitted. Do not attach the tooltip to the last child control.
- Keep `BeginAnimatedTreeNode` and `EndAnimatedTreeNode` balanced on every
  rendered animation frame, including closing frames where the target is shut
  but measured content is still visible.
- Store nested animation contexts in a stack. Opening a nested section inside
  another animated section must never overwrite the outer section's
  measurement, style, input, or lifetime state.
- Register the child draw list created by every animated nested section with
  the parent Settings appearance transform. Clipping a child is not sufficient;
  its vertices, clip rectangles, opacity, and blur geometry must all follow the
  root zoom.

### Smooth Scrolling and Layout Stability

The Settings panel has one scroll owner: `##SettingsBody`. Top-level drawer,
nested-tree, and toggle-region children must use `NoScrollbar` and
`NoScrollWithMouse`; they may clip their contents but must never capture wheel
movement or create an independent scroll target. Keep the SettingsBody vertical
scrollbar structurally present so its available width does not change when
content crosses the scrollable threshold.

Construct and finish the SettingsBody in this order:

1. Call `PrepareSettingsScrollStability` before setting the child size
   constraints, then use `GetSettingsBodyMinimumHeight` as its temporary minimum.
2. Begin SettingsBody, register its draw list with the Settings appearance
   transform, and call `BeginSettingsScrollStability` before submitting the
   first drawer.
3. Submit every top-level header, nested-tree header, and toggle-region boundary
   through the shared helpers. Those helpers record stable, uniquely scoped
   content anchors automatically. A custom layout-changing helper must call
   `TrackSettingsScrollAnchor` at its fixed header or start boundary and must
   call `MarkSettingsLayoutAnimationActive` on every intermediate frame.
4. Preserve the footer anchor after the final drawer. Call
   `EndSettingsScrollStability` after the footer controls but before scroll-edge
   fades and `EndChild`.

The scroll correction compares current anchors with the previous frame, selects
the first surviving anchor at or below the old viewport top, and adds that
anchor's content-space movement to the requested scroll position. Compose this
correction with ImGui's pending `ScrollTarget.y` when one exists; replacing that
target discards wheel input already queued in the same frame. If no stable
anchor survives, use only the conservative top-level height delta from bodies
wholly above the viewport. A view already pinned to the bottom follows the total
content-height delta.

Keep logical expanded height separate from displayed clip height. Measure
complete submitted content directly every open frame, use
`ResolveUiExpandedMeasurement` to freeze the last trustworthy height while
closing, and animate only the displayed envelope. The first open submits a
zero-alpha, near-zero-height child to obtain the complete natural measurement
before visible progress. `EnsureAnimatedChildLayoutSubmission` must run after
every animated `BeginChild`, including top-level, nested-tree, and toggle-region
children. It clears ImGui's fully clipped `SkipItems` state so an offscreen open
header cannot be misread as closed and cannot emit stale item bounds. Item-level
clipping still prevents invisible geometry from being drawn.

Use `UiLayoutAnimationDurationSeconds`, `AdvanceUiLayoutAnimation`, and
`SmoothUiLayoutAnimation` for every layout-changing envelope. The shared clock
is capped against long renderer frames and completes in 180 ms independent of
body size. Do not add wheel- or drag-triggered measurement quiet periods; the
removed `measurementFreezeUntil` delay allowed heights and `ScrollMaxY` to
change after scrolling stopped, which caused the original lurch.

SettingsBody temporarily retains its previous viewport height while a nonzero
scroll offset, wheel input, or left-button scrollbar drag is active. Do not use
`layoutAnimatingLastFrame` as an independent retention reason; doing so holds
the outer background still while drawers collapse and then snaps it to the final
height. An unscrolled body must follow the animated content height every frame.
It may grow immediately and releases scroll retention after correction settles.
Clear prior anchors, retained height, and scroll state when SettingsBody was
absent for a frame gap so reopening cannot apply a correction from an obsolete
layout.

Store nested-tree and toggle-region contexts in stacks and balance every
begin/end pair through opening and closing frames. Block child interaction while
its geometry is moving or clipped, but keep disabled alpha at one so gated rows
do not gray out during their exit. The complete Settings window is likewise
noninteractive during its grow-and-fade, and its appearance transform must scale
draw-command clip rectangles with vertices.

`DrawSliderFloat` and `DrawSliderInt` cache a stable presentation value under
the slider's scoped ImGui ID. While any enclosing toggle region closes, submit a
temporary copy of that value and leave the renderer-facing value untouched.
Update the cache only outside a close transition. Never resolve a still-visible
slider through a disabled runtime path that substitutes zero; Temporal AA
history presentation deliberately resolves an enabled copy so its 3, 6, 9, or
12-frame value and strength remain visible until clipping removes the row.

#### Dropdown Popup Motion

Every Settings combo uses one bounded 180 ms, full-alpha geometric roll. On
open, submit the popup immediately at its complete measured size and final
position, keep every row, scrollbar, navigation target, and internal cursor at
that final layout, and reveal only its draw-command clip rectangles with the
shared smoothstep clock. Reveal from the combo edge toward the popup's free edge:
top to bottom when the popup is below the field and bottom to top when it is
above. Never animate popup window size, row height, content scale, alpha, or
layout; those alternatives move targets, remeasure Settings, or make text appear
to stretch. Choose that reveal edge once when the popup lifecycle begins and
retain it through roll-down and roll-up. Never recompute direction while its
owner scrolls or moves because crossing the placement threshold would reverse
the clip in mid-transition.

Block popup selection until roll-down reaches its fully visible endpoint. A
pointer or navigation activation received while blocked is discarded. Do not
latch it, synthesize it later, or store a pending selectable ID: delayed click
replay can apply an option after the user's pointer has moved and after the
visible event that appeared to cause it. Mark clipped rows non-hoverable before
their button behavior runs, do not submit that behavior while blocked, and
clear any retained active ID. Holding a pointer down from the hidden part of
roll-down through the ready endpoint must not turn its later release into a
selection.

After a valid selection stages its deferred action and pending preview, retain
that combo popup and run the same geometry backward to a zero-height clip before
removing it from the popup stack. This selected-popup roll-up is part of the UI
transaction, not permission to mutate renderer state. At the zero endpoint,
close it exactly once and clear popup-local elapsed time, direction, interaction,
and highlight state so a recycled ImGui popup cannot inherit motion from another
combo.

Escape, click-away, programmatic cancellation, and non-combo popups may keep
their native immediate dismissal. Track the scoped originating combo ID with the
deferred request. `ImGui::IsComboPopupTransitionActive(comboId)` must report only
whether that combo is actively rolling down or rolling up; an unrelated or
stable popup must return idle and must never strand the action. When the owner
becomes invisible, is clipped or no longer submitted for the current frame, or
its work is canceled, call
`ImGui::FinishComboPopupTransition(comboId)` to close and clear only that popup's
roll state without waiting for motion that can no longer be composed. Record
the exact originating combo's last submitted frame with the deferred request;
never infer owner presence from the Settings window alone.

#### Deferred Dropdown Commit Barrier

Dropdown selections never mutate renderer-facing state inside the popup frame.
Queue them by scoped combo ID, show the pending label immediately, close the
selected popup through the full-alpha roll above, and block further Settings
input without dimming it. The
shared `TryApplyDeferredDropdownUiActions` queue owns the renderer-facing commit
barrier. It may drain only when all of these conditions are true:

- at least `UiDropdownSelectionSettleSeconds` (250 ms) has elapsed since the
  latest request;
- the request frame has ended;
- Settings layout, Settings scrolling, Settings appearance, and pixel-zoom
  appearance are all idle;
- `ImGui::IsComboPopupTransitionActive(comboId)` reports that the originating
  combo has no roll-down or selected roll-up in progress;
- `DeferredUiStructuralPresentation<T>::ReadyForCommit` is true;
- no item, wheel motion, pointer drag, or other tracked interaction is active;
- `UpdateUiDropdownIdleStartFrame` has observed that fully composed idle state
  for one complete frame and the current frame is later than that frame.

This global barrier is separate from the structural phase machine below. A
layout phase reaching `ReadyToCommit` does not authorize immediate renderer
mutation, and the 250 ms/global-idle barrier must not advance or fake a layout
phase. A visible selected combo must reach its zero-clip close endpoint. Stable
or unrelated combos, generic popups, and native generic dismissals do not
participate and must never starve a queued choice. Repeated requests for one
combo replace its older action; different combo IDs retain their order. When a
scene-load transition begins, finish the originating combo transition and cancel
both the queue and staged presentation.

#### Structural Dropdown State Machine

Aliasing Method uses `DeferredUiStructuralPresentation<T>`. `Stage` receives a
`structural` Boolean whose meaning is exact: `true` means the selection changes
dependent UI topology; it does not mean that renderer work is expensive. Method
selection and Method reset pass `true`. Quality selection and Quality reset pass
`false`, even though they share the same presentation snapshot and commit path.
A nonstructural edit made while a structural snapshot is pending must compose
into that same snapshot without restarting `AwaitPopupRollUp` or
`CollapseCommitted`.

Normalize or sanitize the copied snapshot inside the explicit `Stage` mutator.
`CommitTo` then copies that exact staged snapshot into committed state unchanged
and clears the phase. Never normalize again during composition or commit: a
second normalization can make staged and committed layouts differ and cause the
post-commit reflow this system exists to prevent.

| Phase | Selector Presentation | Structural Body Presentation | Renderer State | Advance Condition |
| --- | --- | --- | --- | --- |
| `Inactive` | Committed | Committed | Committed | A request calls `Stage` |
| `AwaitPopupRollUp` | Staged | Committed, fully presented | Committed | The originating combo reaches its zero-clip close endpoint on a later frame; a non-popup request observes idle |
| `CollapseCommitted` | Staged | Committed, collapsing | Committed | A later frame after every owned region, Settings layout, and scroll are stable |
| `ExpandStaged` | Staged | Staged, expanding | Committed | A later frame after every owned region, Settings layout, and scroll are stable |
| `ReadyToCommit` | Staged | Staged, fully presented | Committed | The separate global barrier presents a later fully idle frame |
| After `CommitTo` | Committed staged value | Identical staged body | Updated | Phase returns to `Inactive` with no layout change |

`PresentSelectors` supplies the immediate pending preview.
`PresentStructuralBody` keeps committed contents through collapse and supplies
staged contents only after the hidden swap. `ShowStructuralBody` drives one
outer `BeginAnimatedToggleRegion` envelope per dependent consumer; it does not
authorize per-row swaps. `Advance` owns only the structural phases and requires
the originating combo transition to be idle and Settings layout and scrolling
to be stable before it leaves `AwaitPopupRollUp`, then that same layout and
scroll stability plus
`frame > m_PhaseFrame` for every layout phase. A reset or other non-popup stage
has no originating active combo and observes idle on that later frame. A target
flip must call
`MarkSettingsLayoutAnimationActive` before its animated amount leaves the
endpoint so the request frame cannot advance itself. The drawer must not begin
collapsing behind a still-visible popup.

#### Dependent Consumer Synchronization

Inventory every structural consumer across the entire UI. Aliasing uses two
separate envelopes driven by the same `ShowStructuralBody` result:
`##AliasingMethodDependentControls` in the Aliasing drawer and
`##StatisticsAliasingMethodBreakdown` in Statistics. Both read the same
`PresentStructuralBody` snapshot and therefore collapse, swap while hidden, and
expand together.

Statistics may present staged row structure before the renderer commit, but it
must not relabel old telemetry. `statisticsRendererReady` is true only when no
presentation is pending or the phase is `AwaitPopupRollUp` or
`CollapseCommitted`. During `ExpandStaged` and `ReadyToCommit`, every value
requiring the new renderer topology is `--`.

#### Nonstructural Choices and Hidden Settings

A choice with no dependent topology still uses the shared deferred queue. If it
is already owned by `DeferredUiStructuralPresentation<T>`, call
`Stage(..., false, ...)` so it composes with any existing snapshot; do not bypass
the helper and rely only on a pending string. Its expensive renderer frame may
occur while the UI is motionless, but UI deferral is not renderer optimization.

Only `!m_ui.ShowUI && m_SettingsAppearance <= 0.f` is fully hidden. In that
state `SkipInvisibleAnimation` may move a pending structural presentation to
`ReadyToCommit` because no motion can be observed. It must not call `CommitTo`,
drain the queue, or bypass the same timing, composition-idle, and later-frame
barrier. Before that skip, call
`ImGui::FinishComboPopupTransition(originatingComboId)` so the invisible owner
cannot leave an active popup lifecycle behind. Reset recycled combo highlight
and roll state at every popup lifecycle so a previous popup's absolute row
position or elapsed time cannot animate through the next popup.

The helper tests validate expanded-height retention, conservative top-level
deltas, and bottom anchoring. Source contracts protect the integration calls,
stacked contexts, clipped-child submission, retained slider presentation, and
removed failure patterns. They do not simulate real ImGui wheel composition,
anchor identity, or rapid nested reversals, so the live scroll stress test in
Required Checks remains mandatory.

### Controls

- Use `BeginRoundedCombo` for dropdowns and pair it with `ImGui::EndCombo` only
  when it opens. Its popup uses the fixed-layout, full-alpha roll defined above;
  do not add a second popup animation in a drawer-local wrapper.
- Submit every mutable candidate through `DrawDeferredDropdownOption`. Never
  change renderer state directly from `ImGui::Selectable`, and never flush a
  queued choice from inside its owning drawer. The one shared flush belongs
  after `EndSettingsScrollStability`; it must also remain reachable after the
  Settings close animation so a hidden drawer cannot strand a choice. Capture
  only stable object pointers and values, never references to build-frame local
  variables or reference-capturing helper lambdas. Treat activation of the
  already selected row as a no-op so a redundant click cannot trigger renderer
  work or reset state that the row does not own.
- Route a reset icon owned by a deferred dropdown through the same staged or
  deferred path as that dropdown. Toggle-owned reset icons retain the toggle
  timing rule below; lightweight value reset icons remain immediate.
- Keep UI composition presentation-pure. Never normalize, reconcile, or write
  renderer-facing settings merely because a combo or nested section was drawn.
  Normalize the copied snapshot inside the staged mutation; `CommitTo` must copy
  that exact value unchanged so the renderer observes one intentional state
  transition.
- Use `DrawSliderFloat` and `DrawSliderInt` for sliders so panel track styling
  stays consistent.
- Show the shared animated reset icon beside every safely resettable Settings
  value only while its effective value differs from its owner-defined default.
  Aliasing, Visibility, and Buffer controls use their selected preset for
  individual values; non-preset drawers such as Statistics, Sky, General, and
  Lights use factory or scene-loaded defaults. Reset only that control and all
  state it directly owns, then reconcile the complete profile so the custom
  suffix disappears automatically after the final deviation is removed. A
  profile-row reset may restore the complete factory profile when its tooltip
  says so.
- Do not add reset icons to commands or disruptive destination selectors.
  Graphics Adapter, World Scenes, folder buttons, benchmark actions, Cancel,
  Screenshot, Restart, and the footer Zoom action are not ordinary values and
  remain icon-free.
- Use the repository-patched `ImGui::Checkbox` for two-state controls; it owns
  the accepted endpoint animation. Every interactive control still needs a
  concise tooltip.
- Put controls whose availability is directly owned by a toggle inside
  `BeginAnimatedToggleRegion` and `EndAnimatedToggleRegion`. Submit the toggle
  itself outside the region. Preserve the old visibility endpoint for the
  complete UI frame in which the value changes, start the 180 ms collapse or
  expansion on the next rendered UI frame after the renderer has consumed the
  setting, and block interaction during motion without applying the grayscale
  disabled alpha. Nested gated controls require unique hidden IDs and balanced
  regions at every visible animation frame.
- Freeze a slider's presentation value at its last enabled endpoint throughout
  a toggle-owned exit animation. Apply the renderer-facing disabled state
  normally, but do not derive the retained slider label or grab position from a
  disabled resolver that substitutes zero while the row remains visible.
- When a dropdown selection makes another setting irrelevant or mutually
  exclusive, stage the desired settings without changing renderer-facing
  values. Keep the complete dependent body on committed presentation while one
  enclosing region collapses, swap to staged presentation only at the hidden
  endpoint, and expand that same region before commit. Advance structural
  phases only after both Settings layout and scroll are idle, and include the
  final presentation-ready phase in the common dropdown commit gate. Replacing
  staged state with the committed identical state must not reverse or restart
  the region. Do not leave a gray `(Mutex)` dependent-setting row behind.
  Dropdown candidates that cannot be selected may remain immediately disabled
  inside the open popup, and a selected preview may report external
  unavailability as defined by the copy rules above.
- Reset popup-local highlight position, roll direction and elapsed time,
  close state, and interaction readiness for every new dropdown lifecycle.
  Recycled ImGui popup windows must not animate from the previously opened
  dropdown's absolute row position. Input blocked during roll-down is discarded,
  never replayed. Keep non-selection dismissals native and immediate. If an
  owning UI path disappears or cancels queued work, finish that scoped combo ID
  through `ImGui::FinishComboPopupTransition`.
- Reserve grayscale disabled presentation for genuine external availability
  and benchmark locks. Never use `BeginDisabled` as the resting presentation
  for controls directly gated by an enabled/disabled toggle or a current
  dropdown selection.
- Use `DrawCenteredActionButton` for text action buttons and
  `DrawFolderButton` for folder actions unless an accepted neighboring control
  establishes a more specific pattern.
- Put transient actions such as Cancel inside an animated toggle region owned
  by the actual running state. Do not render an idle disabled button or
  readiness sentence. Start the control's entrance only after the run begins,
  and keep its exit animation balanced after cancellation or completion.
- Tables may be created only after the owning drawer body or nested body has
  begun. Do not render a table directly from a top-level collapsing header.
- Preserve disabled-scope, style, ID, combo, child, table, and tree begin/end
  balance in every branch. Validate closing-animation frames, not only the fully
  open endpoint.

### Renderer-Affecting Dropdown Transactions

A dropdown that changes UI structure and renderer topology is one transaction
across presentation, layout, renderer state, and GPU-resource ownership. Do not
implement or review those planes independently.

1. Create one staged settings snapshot. Selectors read it immediately through
   `PresentSelectors`; committed renderer state remains untouched.
2. Inventory every structural consumer, not only the selector's drawer. Bind
   each consumer to `PresentStructuralBody` and one region driven by
   `ShowStructuralBody`, or give it a fixed-height schema whose values can
   change without reflow.
3. Stage the pending preview, finish the selected popup's roll-up, collapse
   committed consumers, swap to staged contents only at the hidden endpoint,
   expand all staged consumers, and wait for a fully presented stable frame.
   `ReadyForCommit` and
   `ImGui::IsComboPopupTransitionActive(originatingComboId)` both belong in the
   global composition-idle predicate, not in a drawer-local branch.
4. Make composition read-only. If a choice needs sanitization or normalization,
   perform it inside the explicit `Stage` mutator and test it as a pure settings
   transition.
5. Trace the committed value through render-target selection, pass creation,
   binding caches, history, timers, and first-use pipelines. Classify each
   resource as recreated, retained with invalidation, retained with rebinding,
   or unrelated. Preserve an object only after proving that it owns no deleted
   target handle or incompatible format/sample topology.
6. Keep complete reconstruction for initial creation, resize, renderer-mode or
   visibility-resource ownership changes, view-topology changes, and shader
   reload. Use a narrow refresh only when its preconditions are explicit and
   source-contracted.
7. Test cold and warmed transitions in both directions with every structural
   consumer open. The commit frame may update values, but it must not start an
   animation, change row count, change panel height, or reveal target data from
   the old renderer configuration.

Aliasing Method is the reference implementation. Its UI transaction is owned by
`DeferredUiStructuralPresentation<AntiAliasingSettings>`. The Aliasing body and
Anti-Aliasing Statistics breakdown share the phase; Statistics uses `--` until
target timings exist. `RefreshAntiAliasingTargetPasses` then limits an AA-only
target change to target-bound work, clears deferred/visibility caches, resets
visibility history, rebinds compatible CMAA2 resources, and retains compatible
MSAA visibility-resolve pipelines. Read
[Reference Incident: Aliasing Method Dropdown Stutter](#reference-incident-aliasing-method-dropdown-stutter)
before changing this transaction or copying it for another renderer selector.

Do not state that a deferred structural dropdown is fixed merely because its
animation is smooth. A stationary post-commit pause still requires renderer
profiling. Conversely, do not retain every pass merely to remove a pause; stale
bindings or history are correctness failures.

### Statistics and Developer Controls

- Keep effect-specific statistics in the Statistics drawer behind its effect
  selector. Present every effect breakdown, including anti-aliasing history,
  permutation, timing, CMAA2, and MSAA telemetry, in the shared two-column
  striped table layout. Do not duplicate that telemetry in the Aliasing drawer.
- Keep benchmark controls together in Statistics. Idle state contains the run
  actions only; running status and Cancel appear only while their run state
  exists.
- Do not restore removed TAA execution-path, kernel, LDS, reuse, early-reject,
  fusion, cache-blocking, or debug dropdowns. Do not create a separate developer
  performance drawer. Stable Interior is the only retained performance control,
  appears immediately above Sharpness, and remains off in every preset unless
  new measured evidence and an explicit product decision authorize a change.
- Do not append `(Preset)` to inherited Aliasing controls. Keep Sharpness
  disabled by default for every Aliasing preset, but preserve the user's stored
  strength while the toggle is off.
- Keep temporal-history sharpening and resolved presentation sharpening as
  separate shader permutations. Temporal history is premultiplied by confidence;
  CMAA2 output is resolved RGB with non-semantic alpha and must never be divided
  by that alpha.
- When a visibility preset becomes custom, preserve its originating quality in
  the selector label, such as `Medium (Custom)`. Do not collapse every edited
  preset into an originless `Custom` label.
- Treat presentation morphology as an independent CMAA2 setting. Its quality
  selector must never write the main Temporal or Multisample quality; selecting
  the inherited strength normalizes the morphology override back to the preset.

### Build Override Requirements

- When an override changes `imgui.h` or `imgui_internal.h`, stage and replace
  every ImGui translation unit in the target, including `imgui_tables.cpp` and
  `imgui_demo.cpp`, even when those files have no textual patch.
- ImGui translation units use quoted sibling-header includes. Leaving one source
  in the dependency directory compiles it against the original context layout,
  while staged sources compile against the patched layout. The mismatch can
  remain latent until a newly imported widget path dereferences the context.
- Keep the staged-source list and replacement-source list identical. The
  `uvsr_ui_source_contract` test enforces this for all ImGui translation units.

### Loading Screen

- The launch loading screen displays status text and cycles the `please wait`
  suffix through one, two, and three dots.
- Preserve the requested first-line punctuation:
  `Loading scene: <name>, please wait...`.
- Do not add a progress bar, track, grab, percentage, simulated completion
  curve, elapsed-time counter, or launch timer unless the user explicitly
  changes this product behavior.
- Keep real object, import-step, decoded-texture, and GPU-ready texture counts
  truthful and independent of the dot animation.

### Pixel Zoom Overlays

- Keep pixel zoom outside ImGui image sampling. Capture the untouched presented
  scene before UI composition and use integer `Texture2D.Load` coordinates with
  no sampler, independent filtering, or overlay AA.
- Preserve the Off, 2x, 3x, 4x, 5x, Off cycle for both Z and the footer button.
  The footer button text remains `Zoom` in every state, and crosshair rendering
  must use the same active-state condition.
- Derive the panel width from 28 percent of the current renderer width. Derive
  its height from that width and the renderer aspect ratio, then use the same
  edge margin calculation as Settings at the required corner.
- Cut the 8 px rounded silhouette away from the magnified image. Apply only the
  full-weight one-pixel translucent vertical-gradient outline after the exact
  texel load; do not filter or soften the magnified interior. The zoom shader's
  1.5 px signed-distance coverage is the calibrated implementation that matches
  Settings' perceived one-pixel outline; do not replace it with an uncalibrated
  literal band.
- Fade and scale the complete zoom surface and its analytic outside-only drop
  shadow through one shared eased visibility value. Quantize every animated
  rectangle to whole destination pixels, keep the fully visible endpoint at
  the exact 28-percent aspect-matched bounds, and never let shadow softness
  enter the rounded magnified cutout.
- Animate enabled level changes with a symmetric whole-pixel scale pulse. Keep
  the outgoing integer factor through the first half, switch factors at the
  midpoint without crossfading or fractional sampling, and expand the incoming
  integer factor through the second half.
- Draw the bottom-center pixel-area descriptor as `4x`, `9x`, `16x`, or `25x`
  using the renderer UI font and a slight one-pixel shadow. Position it from the
  same animated panel bounds, inset it from the panel bottom by the shared edge
  margin, multiply both text layers by the panel opacity, and switch its value
  at the same exact midpoint as the magnified source.
- Guard both the capture and composite submissions with the active zoom state.
  A normal Off transition may render only until its fade reaches zero.
  Controlled benchmark runs must bypass the transition and submit no zoom GPU
  work.

## Reference Incident: Aliasing Method Dropdown Stutter

This incident is the worked example for any future control that changes visible
layout and renderer topology. It records the failed boundaries as well as the
final repair so an agent does not repeat a superficially smooth but incomplete
fix.

### User-Visible Failure

Selecting a different Aliasing Method closed the popup and then visibly froze or
stuttered every active animation. The failure was most apparent when switching
among Temporal Reconstructive, Conservative Morphological, and Multisample
Reference. Opening Statistics exposed an additional defect: the Aliasing drawer
could finish its transition while the Anti-Aliasing Statistics rows changed
after commit, causing a second panel reflow.

The symptom had two independent contributors:

1. The committed method was allowed to alter dependent UI structure before all
   consumers had presented the same staged structure.
2. The renderer treated an AA-only render-target topology change as a reason to
   rebuild nearly every pass, so the post-commit frame still paused even after
   popup and drawer animation were delayed correctly.

### Failed Boundaries

- Removing the old alpha-based closing fade fixed its popup-lifetime
  interference but did not move renderer mutation out of layout motion. The
  accepted later roll is safe only because it clips fixed full-alpha geometry,
  exposes an exact transition state, and participates in the global barrier.
- Adding a delay moved the hitch later; it did not make the UI stable or reduce
  the renderer work.
- Staging only the Aliasing drawer missed
  `##StatisticsAliasingMethodBreakdown`, so Statistics still changed row count
  after the renderer commit.
- Wrapping rows in `BeginDisabled` blocked input but did not stop ordinary C++
  normalization or assignments during composition.
- The original `RenderTargets::IsUpdateRequired` path rebuilt targets and then
  called broad `CreateRenderPasses`, recreating Forward, G-buffer, Material ID,
  pixel readback, deferred or PBR lighting, MSAA visibility resolve,
  screen-space visibility, TAA, CMAA2, Sky, and tone mapping. A cold CMAA2
  recreation also constructed its full 16-PSO quality set.

These attempts established the central rule: animation timing, presentation
purity, consumer synchronization, and renderer resource ownership must be
repaired together.

### Final UI Transaction

`DeferredUiStructuralPresentation<AntiAliasingSettings>` owns one staged
snapshot and the phase table defined under
[Structural Dropdown State Machine](#structural-dropdown-state-machine).
Method selection and reset stage with `structural = true`; Quality selection and
reset stage with `structural = false` so a quality edit never impersonates a
method topology change.

A valid Method selection first stages its preview and enters
`AwaitPopupRollUp`. The committed Aliasing and Statistics bodies remain fully
presented while the fixed-layout popup rolls to its zero-clip close endpoint.
Only then may `Advance` begin `CollapseCommitted`; no drawer motion occurs behind
the popup, and no click received during roll-down can be replayed into this
transaction.

Both `##AliasingMethodDependentControls` and
`##StatisticsAliasingMethodBreakdown` read `PresentStructuralBody` and use
`ShowStructuralBody` as the target of one outer animated region. The selectors
read `PresentSelectors`. Normalization happens in the `Stage` mutator, and
`CommitTo` copies the resulting snapshot unchanged. During staged Statistics
presentation, `statisticsRendererReady` allows renderer values only when there
is no pending presentation or the phase remains `CollapseCommitted`; later
phases display `--`.

After `EndSettingsScrollStability`, `Advance` may leave
`AwaitPopupRollUp` only when
`ImGui::IsComboPopupTransitionActive(originatingComboId)` is false. It may move
one later structural phase only when Settings layout and scroll are stable and
the request phase is at least one frame old. Once `ReadyToCommit` is reached,
the independent shared queue still waits for the 250 ms selection settle time,
idle originating combo, Settings, and zoom appearance, idle pointer and scroll
input, and a later fully presented frame. The queue is drained once after all UI
windows compose. Fully hidden Settings may skip only invisible structural
animation after finishing its scoped combo transition; it does not skip the
global barrier.

The resulting commit changes renderer values while every visible consumer
already has its final row set, height, opacity, and labels. It therefore cannot
start a second UI animation or reflow.

### Renderer Ownership Repair

`RefreshAntiAliasingTargetPasses` is eligible only when render targets already
exist; width and height, PBR ownership, visibility-resource ownership, and
visibility source-radiance ownership are unchanged; and only sample count or
motion-vector topology changed. Render-target replacement clears the global
binding cache and previous view before the narrow refresh.

The AA-only refresh uses this ownership boundary:

| Resource or Pass | AA-Only Action | Reason |
| --- | --- | --- |
| `RenderTargets` | Recreate | Sample count or motion-vector attachments changed |
| MiniEngine TAA | Recreate when required | Owns target-bound color, depth, motion, and history resources |
| G-buffer pass | Recreate | Its topology names the motion-vector attachment |
| Forward pass | Recreate only when sample count changes | Its framebuffer/sample topology changed |
| Pixel readback | Recreate | It owns the replacement Material ID target binding |
| Sky and AgX tone mapping | Recreate | They own replacement framebuffer bindings |
| Legacy and PBR deferred lighting | Retain and clear binding caches | Pipelines remain compatible; target bindings do not |
| Screen-space visibility | Retain, clear bindings, and reset history | Independent pipelines survive, but bindings and temporal history do not |
| CMAA2 | Retain and call `UpdateSourceColor` | Same-sized single-sample RGBA16F intermediates and 16 quality PSOs remain compatible |
| MSAA visibility resolve | Retain | All supported sample-count PSOs are created with the visibility renderer |
| Material ID pass | Retain | Its pipeline is not target-owned by the AA-only swap |

If a visibility benchmark is active when topology changes, fail that run rather
than silently mixing configurations. Initial creation, resize, PBR changes,
visibility resource or source-radiance ownership changes, view-topology changes,
and shader reload still require complete `CreateRenderPasses`. A change only to
TAA pass presence or Stable Interior's moment-history layout uses
`CreateMiniEngineTemporalAAPass` without rebuilding unrelated passes.

### Remaining Synchronous Work

The repair deliberately does not claim a stall-free method change. A sample or
motion topology change still allocates replacement render targets, recreates
target-bound passes, resets history, discards the previous view, and may create
the required TAA or first-use presentation path. Returning to Temporal cannot
preserve history across incompatible topology. Those are correctness costs.

The proven outcome is narrower: all visible UI motion finishes before commit,
the commit causes no second reflow, and unrelated renderer subsystems are no
longer destroyed and rebuilt. If a stationary pause remains, profile the commit
path and change renderer ownership only with explicit compatibility evidence;
do not add another delay or weaken cache/history invalidation.

### Regression Guardrails

- `uvsr_ui_animation_reference` protects structural phase ordering, request-
  frame guards, selected-popup roll-up waiting, hidden presentation, global
  idle timing, and nonstructural composition.
- `uvsr_imgui_dropdown_roll_lifecycle` drives the actual patched ImGui widgets
  without a renderer backend. It protects blocked press/hold/release discard,
  roll-down and roll-up query endpoints, the zero-visible close frame, latched
  direction, wrong-owner isolation, and exact clipped or skipped owner cleanup.
- `uvsr_ui_source_contract` protects the two synchronized consumer envelopes,
  `ShowStructuralBody`, Method's `true` staging, Quality's `false` staging,
  exact Statistics readiness predicate, fixed-layout full-alpha combo roll,
  absence of delayed click replay, the scoped transition query and finish APIs,
  one shared flush, and scene-load cancellation.
- `uvsr_renderer_source_contract` protects the narrow refresh preconditions,
  resource recreation/retention boundary, binding invalidation, visibility
  history reset, and full-rebuild fallbacks.
- `uvsr_taa_miniengine_reference` protects TAA history and moment-layout
  behavior.
- Live verification keeps Aliasing and Statistics open, switches every Method
  cold and warmed in both directions, confirms roll-up completes before the
  synchronized collapse/hidden swap/expand, verifies `--` before matching
  telemetry exists, confirms Quality does not collapse the structure, closes
  Settings during a pending transition, and checks that no second reflow follows
  commit.

## Integration Review

After composing incoming behavior:

1. Search the changed Settings sections for raw `ImGui::BeginCombo`,
   `ImGui::TreeNodeEx`, `ImGui::SliderFloat`, `ImGui::SliderInt`, and
   `ImGui::Button` calls. Replace them with the established path or document the
   accepted exception in source and in the execution plan.
2. Verify that every top-level drawer has a unique body ID, a balanced animated
   body, and sibling spacing after its body. General alone must have the
   launch-time DefaultOpen flag.
3. Verify that every Settings-owned child draw list participates in the parent
   appearance transform. Inspect a frame during open and close motion; nested
   bodies must share the root scale and opacity.
4. Verify that expanded Settings submits separate blur masks for its title and
   body and collapsed Settings submits separate masks for its title and status.
   Both states must leave their empty corner wedges sharp and use exactly one
   outline path per edge at the same visible weight.
5. Verify that tables are inside a current drawer or nested child window and
   that every selector index is bounded before array access.
6. Verify that toggle-owned dependent controls use delayed animated regions,
   never a resting gray disabled scope. Exercise nested toggle regions and
   confirm their renderer-facing value changes one frame before their motion.
7. Verify that every Settings combo opens through one 180 ms full-alpha
   geometric roll with a fixed popup size, location, row layout, and scrollbar.
   Confirm an activation during roll-down is discarded and never reappears as a
   delayed selection. A valid selection must show its pending label immediately,
   roll the selected popup completely to its zero-clip close endpoint, and defer
   renderer mutation until at least 250 ms since selection, the exact combo
   transition, scroll motion, Settings/zoom transitions, and one complete idle
   frame have finished. Confirm the shared commit runs outside every drawer;
   stable or unrelated combos, generic popups, and native generic dismissals
   cannot strand it;
   newer requests for one combo replace its older queued action; and a
   scene-load transition cancels stale work. For a choice that changes
   composition, confirm the committed method-dependent body remains fully
   presented through popup roll-up, then collapses; the body swaps only while
   fully hidden; the staged body expands completely; and a stable frame occurs
   before renderer commit. Confirm renderer state stays unchanged throughout
   every phase, the commit causes no second reflow, hidden Settings can bypass
   invisible structural motion without stranding the action, popup alpha never
   fades, and there is no resting mutex row.
8. For every renderer-affecting dropdown, trace the target setting through
   render-target allocation, pass construction, bindings, history, timers, and
   first-use PSOs. Verify that a narrow refresh has explicit topology
   preconditions, recreates every target-bound object, resets or rebinds every
   retained cache/history owner, and leaves unrelated passes allocated. Confirm
   a cold transition and a warmed reverse transition; do not confuse a smooth
   UI with an eliminated renderer hitch.
9. Verify that every ImGui translation unit is compiled beside the same patched
   public and internal headers.
10. Verify that pixel inspection tools use integer source loads, exact group
   mapping, aspect-matched 28-percent bounds, exact-factor midpoint switching,
   conditional GPU submission, benchmark suspension, matched border weight,
   outside-only shadows, and balanced appearance/disappearance and level
   transitions.
11. Review the semantic diff against both lineages. Do not replace
   `src/uvsr.cpp` wholesale and do not remove controls or renderer behavior to
   simplify presentation repair.

## Required Checks

1. Build and run the focused UI, renderer, animation, and TAA contracts:

   ```powershell
   cmake --build build --config Release --target uvsr_ui_source_contract_tests uvsr_renderer_source_contract_tests uvsr_ui_animation_tests uvsr_imgui_dropdown_roll_tests uvsr_taa_miniengine_tests
   ctest --test-dir build -C Release -R "uvsr_ui_(source_contract|animation_reference)|uvsr_imgui_dropdown_roll_lifecycle|uvsr_renderer_source_contract|uvsr_taa_miniengine_reference" --output-on-failure
   ```

2. Rebuild shaders and the Release renderer, then run the full CTest suite.
3. Launch only through `tools/launch_uvsr.ps1` with a valid lowercase
   letters-only experiment label.
4. Open and close every changed top-level drawer at least twice. Observe the
   opening, closing, and sibling reflow rather than checking only endpoints.
5. Exercise every changed nested section, dropdown, slider, toggle, and action.
   Confirm each toggle-owned region remains present for the setting-change
   frame, then collapses after the renderer applies the disabled state and
   reverses that motion after the enabled state is applied. No gated region may
   settle into a gray disabled state. Open every dropdown and confirm its
   contents roll from the combo edge at full alpha while the popup bounds, rows,
   scrollbar, and text retain their final geometry. Click during roll-down,
   hold through its endpoint, and then release; confirm the blocked click is
   discarded rather than replayed later and hidden rows do not hover or show
   tooltips. Scroll the originating field fully offscreen during a separate
   selected roll-up and confirm its exact popup is finished without stranding
   the action. Select
   every dropdown choice that changes dependencies and confirm the pending label
   updates immediately, the popup rolls completely closed before the complete
   committed body begins collapsing, the staged replacement appears only after
   the hidden swap and expands smoothly, and the renderer change waits until
   `ImGui::IsComboPopupTransitionActive(originatingComboId)` is false and through
   a fully presented composition-idle frame. The commit must cause no second
   movement or alpha-based popup fade. Confirm Escape, click-away, an unrelated
   combo, and a generic popup retain scoped or native dismissal and cannot strand
   the queued selection. Hide the owner and cancel work during separate trials;
   verify `ImGui::FinishComboPopupTransition` clears only its originating combo.
   For a structural renderer choice, keep
   every dependent drawer open and repeat the transition cold, warmed, forward,
   and backward. If the UI is stationary but the process still pauses, capture the
   remaining synchronous renderer path; do not conceal it with another delay.
   While a long drawer is expanded, repeatedly scroll past a nested section,
   reverse direction before motion settles, and open or close that section both
   partly clipped and fully offscreen. Confirm the visible content anchor does
   not jump, the Settings viewport does not compact beneath the pointer, the
   scrollbar range updates immediately, and the user can always return to the
   section and the top of the drawer.
   Collapse every nonzero AA slider through its owning toggle or dropdown and
   confirm its label and grab retain the last enabled value until clipping
   removes the row; neither may travel toward zero during the exit animation.
   For reset placement changes, modify one un-nested dropdown, every affected
   nested dropdown, and one nested non-dropdown control. Confirm only the nested
   dropdown reset icons occupy the immediately preceding tree-indent gutter.
   Compare the un-nested dropdown against its pre-change position, confirm the
   nested dropdown and label do not move when the icon appears or disappears,
   click the former trailing-icon location to prove it is inert, and click the
   gutter icon to prove it resets only its owner without opening the dropdown.
   Repeat while the nested section opens, closes, and scrolls through clipping;
   the icon must transform and clip with its owning child.
   For Statistics, select every effect, open Resource Footprint when available,
   inspect benchmark controls, confirm idle state has no readiness sentence or
   Cancel button, then verify Cancel animates in only while a test runs and the
   process remains responsive.
6. Observe at least two launch-dot states and confirm that no loading bar or
   launch timer is visible.
7. Capture a Settings screenshot at the task's relevant resolution and verify
   Settings remained absent until Escape, the first-open General-only state,
   deliberate gaps between all sibling drawer headers, the complete
   grow-and-fade in both directions, nested children moving with the parent,
   sharp empty wedges between the expanded title and body and between the
   collapsed title and status surfaces, the single-weight outline in both
   states, and the outside-only magnifier-matched shadow.
8. When zoom changes, cycle Z and the button through every state, resize the
   renderer, verify crosshair gating, and inspect the grow/shrink motion, both
   fade endpoints, the rounded cutout, full-weight outline, and outside-only
   shadow at native pixels.
9. Run the documentation checker self-test and full scan, then run
   `git diff --check`.

## Handoff Evidence

The handoff must identify:

- the UI reference version and `AGENTS.md` policy version recorded before
  implementation, the versions rechecked during integration review, and any
  stale-version mismatch that required reconciliation;
- the exact source revision and labeled executable;
- the completed new-element classification, owner/default/reset decisions, and
  cross-consumer inventory;
- the drawers and controls exercised;
- the observed animation, spacing, loading-dot, and Statistics results;
- renderer work caused by each disruptive choice, the resource-ownership
  boundary used, and any remaining synchronous work;
- focused and full test outcomes;
- any raw ImGui exception and its accepted reason;
- whether the candidate was committed, pushed, merged, or intentionally kept
  local.

Do not describe a UI candidate as verified when only compilation or endpoint
screenshots were checked.

## Reference Revision History

- `2026-07-22.1`: Standardized inline unavailable guidance on the shared
  disabled-text token with owner-width wrapping. Required both temporal
  anti-aliasing availability explanations to use an authored two-line layout
  and required Statistics benchmark gating guidance to use the same muted
  presentation instead of ordinary text color.
- `2026-07-21.2`: Moved reset icons for nested dropdowns only into the
  immediately preceding tree-indent gutter, preserving the established
  trailing placement for un-nested dropdowns and all non-dropdown controls.
  Required fixed dropdown, label, popup, row, and sibling geometry across reset
  visibility, plus scope-matrix, source-allowlist, and live hit-target checks.
- `2026-07-21.1`: Established explicit reference-version tracking and
  stale-guidance diagnosis. Defined the fixed-layout, full-alpha combo
  roll-down/selected roll-up lifecycle, discarded input during roll-down,
  prohibited delayed click replay, latched reveal direction per lifecycle,
  required clipped-owner cleanup and joint popup/layout stability, added
  `AwaitPopupRollUp`, and placed the originating combo ID's exact transition
  query and finish path ahead of the existing composition-idle renderer commit
  barrier. Added a backend-free lifecycle test for the actual patched ImGui
  input and popup state. This revision supersedes the unversioned reference.
