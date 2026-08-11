# Dear `ImGui`

## Record

- Relationship: Dependency Integration and Incorporated Upstream Material
- Status: Current
- Confidence: Confirmed
- Upstream: [Dear ImGui](https://github.com/ocornut/imgui)
- Revision: `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`
- Governing Terms: [MIT License](https://github.com/ocornut/imgui/blob/45acd5e0e82f4c954432533ae9985ff0e1aad6d5/LICENSE.txt)

## UVSR Relationship

Dear ImGui supplies the immediate-mode UI implementation. UVSR stages selected
upstream files and applies product-specific layout, interaction, and runtime
policy patches before compiling them. Those staged files remain modified ImGui
source; UVSR's settings model and visual composition are first-party.

## Evidence

- [Build Integration](../../CMakeLists.txt)
- [UI Patch](../../overrides/imgui-ui.patch)
- [Slider Controls Patch](../../overrides/imgui-slider-controls.patch)
- [Combo Roll Patch](../../overrides/imgui-combo-roll.patch)

## Commercial Clearance

Every distributed modified copy must retain Dear ImGui's MIT copyright and
permission notice. No separate external UI design source was substantiated.
