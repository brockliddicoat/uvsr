# UI contract

Cap is the only active interface. the [user guide](user-guide.md#startup-and-interface)
owns visible behavior. [settings](settings.md) owns values and transactions.

`src/uvsr_ui_panels.cpp` owns the fixed style and panel layout.
`src/uvsr_ui_internal.h` contains shared controls. `overrides/imgui-ui.patch`
contains the few hooks that require ImGui internals; apply it only to build copies.
prefer normal ImGui controls and style values. do not add a skin abstraction,
parallel widget framework, or duplicate settings state.

preserve retained controls, labels, spacing, resets, input precision, and snapshot
compatibility. hide unavailable controls without clearing stored values. test
interaction and inspect the exact candidate at representative viewport sizes and
DPI scales. source checks alone do not prove appearance.

drawing owns a two-level settings-tree stack, matching FXAA and FXAA Tuning.
checked slider formats use the longest retained control format as their bound.
performance text publishes complete snapshots within the existing formatter limits.
texture labels borrow the published scene only through synchronous drawing;
material edits preserve that string storage. the 25/117-code-point labels retain
ImGui's decoder and the former embedded-NUL display boundary.

material names borrow scene text while drawing. clipped labels and hovered tooltip
text use checked temporary storage, keeping the compiled ImGui tooltip rules.
startup snapshot codes have checked ownership through their terminal log; failure
to retain a code closes startup before staging begins.

legacy skins live in [compressed recovery material](postmortem/archive/legacy-ui-skins.zip).
leave it out of routine context and extract it only for requested recovery.
