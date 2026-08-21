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

Dear ImGui also embeds Tristan Grimmer's ProggyClean font under the MIT License.
UVSR's `Ogg (ProggyClean)` interface-font option uses that existing embedded
data at 13 pixels for stock/Ogg controls and registers it at 16 pixels for Amp
body text. Authored Amp headings retain Noto Sans Bold because ProggyClean has
no Bold face. No additional ProggyClean font binary is staged. New renderer
packages install the complete separate copyright and MIT notice as
`licenses/ProggyClean-MIT.txt`.

## Evidence

- [Build Integration](../../CMakeLists.txt)
- [UI Patch](../../overrides/imgui-ui.patch)
- [Slider Controls Patch](../../overrides/imgui-slider-controls.patch)
- [Combo Roll Patch](../../overrides/imgui-combo-roll.patch)
- [Embedded ProggyClean Attribution](../../donut/thirdparty/imgui/imgui_draw.cpp)
- [ProggyClean Source Record](proggy-clean-font.md)

## Commercial Clearance

Every distributed modified copy must retain Dear ImGui's MIT copyright and
permission notice. Packages using the embedded ProggyClean font must also retain
Tristan Grimmer's copyright and MIT terms. No separate external UI design source
was substantiated.
