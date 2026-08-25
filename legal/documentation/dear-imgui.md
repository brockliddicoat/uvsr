# Dear `ImGui`

## Record

- Relationship: Dependency Integration and Incorporated Upstream Material
- Status: Current
- Confidence: Confirmed
- Upstream: [Dear ImGui](https://github.com/ocornut/imgui)
- Revision: `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`
- Governing Terms: [MIT License](https://github.com/ocornut/imgui/blob/45acd5e0e82f4c954432533ae9985ff0e1aad6d5/LICENSE.txt)

## UVSR Relationship

Dear ImGui supplies the permanent immediate-mode UI implementation. The
unmodified upstream boundary is 15 directly vendored files under
`third_party/imgui`: the license, core implementation and headers, embedded stb
headers, and Win32 and DirectX 12 backends. The boundary is pinned to the
revision above. Its CMake manifest verifies every file's SHA-256 and rejects
missing or extra files. It contains no demo, documentation, example, or
standalone font file.

The active pre-detachment build stages copies of the core sources and applies
five reviewed product layout and interaction patches outside that boundary.
Those modified copies, the patches, and first-party private-API consumers remain
transitional; direct vendoring does not complete UI detachment. UVSR's settings
model and visual composition are first-party. Donut's nested ImGui copy is not
an active build source.

Dear ImGui also embeds Tristan Grimmer's ProggyClean font under the MIT License.
UVSR's `Ogg (ProggyClean)` interface-font option uses that existing embedded
data at 13 pixels for stock/Ogg controls and registers it at 16 pixels for Amp
body text. Authored Amp headings retain Noto Sans Bold because ProggyClean has
no Bold face. No additional ProggyClean font binary is staged. New renderer
packages install the complete separate copyright and MIT notice as
`bin/licenses/ProggyClean-MIT.txt`.

## Evidence

- [Exact Manifest and Validation](../../cmake/DirectImGui.cmake)
- [Pre-Detachment Build Integration](../../cmake/DirectDonut.cmake)
- [Transitional Patch Set](../../CMakeLists.txt)
- [Vendored Dear ImGui License](../../third_party/imgui/LICENSE.txt)
- [Embedded ProggyClean Attribution](../../third_party/imgui/imgui_draw.cpp)
- [ProggyClean Source Record](proggy-clean-font.md)

## Commercial Clearance

Every distributed modified copy must retain Dear ImGui's MIT copyright and
permission notice. Packages using the embedded ProggyClean font must also retain
Tristan Grimmer's copyright and MIT terms. No separate external UI design source
was substantiated. Renderer packages install Dear ImGui's terms as
`bin/licenses/Dear-ImGui-MIT.txt`.
