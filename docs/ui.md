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

legacy skins live in [compressed recovery material](postmortem/archive/legacy-ui-skins.zip).
leave it out of routine context and extract it only for requested recovery.
