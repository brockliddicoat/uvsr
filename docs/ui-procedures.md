# UVSR UI Integration

## Purpose

This is the maintained contract for UVSR-owned ImGui presentation. It governs
Settings, Performance, commands, loading, diagnostics, pixel zoom, and Material
editing. Feature ownership and defaults remain authoritative in renderer
settings, not this document. Update this file only for reusable UI behavior.

## Structure

Performance is an independently collapsible panel above and detached from
Settings. Both belong to one vertical stack that yields to the command surface.
Settings contains one title, fixed identity row, scrolling body, and footer.
Keep the identity visible while collapsed. The body owns its scrollbar and must
reserve that channel throughout open/close animation; submit no child after the
logical collapsed endpoint.

Order drawers by user goal. Keep dependent controls together and place uncommon
implementation policy in a default-closed Advanced group. Debug views belong in
Debug under the effect they inspect. Interface remains last. Add a drawer only
when multiple retained controls serve a distinct user goal.

Use stable ImGui IDs. Every `Begin`, child, table, tree, popup, style, disabled,
font, and ID scope must balance on every branch. Submit persistent controls every
frame and animate visibility around stable content rather than creating a second
state registry.

## Visual and Copy Rules

- Use Title Case for visible headings and the product name **UVSR**.
- Use the shortest unambiguous label. Put units, range, effect, and important
  side effects in a concise tooltip no longer than 120 Unicode code points.
- Use one scaled 4-pixel spacing base: Tight is 1x, Regular is 2x, and Section
  is 4x. Widget geometry and one-pixel depth strokes are not spacing tokens.
- Preserve complete rounded silhouettes for top-level panels. Closed and open
  rows must not jump in baseline, inset, width, or scrollbar allocation.
- Never present an enabled control with no runtime effect. Disable it with a
  direct reason, or remove it.
- Keep normal product copy free of benchmark, implementation, and developer
  terminology.

Amp is the authored animated skin; Ogg uses stock ImGui widgets and immediate
motion endpoints. Both expose identical renderer state. Interface owns skin,
font family, animation, bounded numeric-entry preference, and role colors.
Installed Segoe UI files are never copied. Bundled Noto Sans is the default;
embedded ProggyClean remains the Ogg body face. Primary Accent owns emphasis and
raised controls, Secondary owns negative/off state, Tertiary owns positive/on
state, Font Color owns copy, and Background Color owns resting surfaces.

Use the shared UVSR wrappers for authored sliders, toggles, combos, color edits,
tooltips, reset icons, drawers, and popups. Do not paint duplicate arrows,
backgrounds, labels, or input surfaces over native controls. Ogg keeps native
ImGui behavior. Authored popups and animated regions must remain reversible
without losing selection, focus, or stored values.

## Controls, Defaults, and Resets

Every user-facing control requires:

- one state owner and authoritative default;
- an active renderer consumer;
- range, units, accepted command values, and unavailable-state behavior;
- reset ownership and persistence policy;
- topology/resource impact;
- command or diagnostic coverage when applicable; and
- focused automated and live verification.

Keep internal implementation policy out of Settings. A setting may remain only
when it changes a retained behavior and offers a proven user decision.

Factory defaults live with the owning settings type or preset and are shared by
startup, renderer creation, commands, tests, and UI reset. Never duplicate
numeric defaults in presentation code. A local reset restores the smallest
coherent ownership group. Editing a preset-owned value may show Custom, but must
not erase the preset origin needed by reset. Global reset restores renderer and
interface defaults while preserving camera, scene, adapter, and shell navigation
unless their owner explicitly defines otherwise.

Deferred controls are required only when a mutation changes pipelines,
resources, device state, or another topology. Apply them after popup roll-up,
settling, and one complete idle composition frame. Ordinary constants mutate
directly. Never maintain two mutation queues.

Semantic gating preserves the stored preference while presenting the effective
value and reason. Turning a parent option off must not silently overwrite child
choices. Hidden values remain inert. Exact numeric entry may exceed a compact
visual track only within the established safe logical range; disabling that
preference never clamps an accepted value silently.

## Renderer Boundary

UI code may request settings changes, commands, scene loads, diagnostics, and
presentation state. It must not create renderer resources, compile shaders,
invent fallback policy, or duplicate canonical settings/version hashing.
Renderer status is authoritative for pending, unavailable, rebuilding, ready,
and failed states.

All represented values participate in the canonical settings contract according
to their persistence rule. Copy/display diagnostics must identify the exact
settings hash and derived engine version without maintaining another version.
Scene, camera, resolution, material, lighting, and renderer changes must trigger
the reset behavior owned by the affected feature.

During loading or device work, keep the shell responsive, show truthful progress
and failure text, and disable only actions that would violate the active
transaction. Never report completion before renderer ownership has settled.

## Verification

For a changed control, prove pointer and keyboard input, popup open/close,
scrolling, collapse, focus, disable/enable, reset, save/load, command parity,
unavailable state, and the actual renderer effect. Exercise both skins and
representative DPI/window sizes. Check loading, resize, scene change, camera cut,
and clean shutdown when relevant.

Visible changes require representative captures from the exact executable and
settings. Renderer-affecting changes also require the affected protected-feature
matrix, debug-layer cleanliness, finite output, full developer gate, and fresh
production package smoke. Compiling or launching alone is not evidence. Bind the
handoff to exact source, settings hash/version, executable, and SHA-256.
