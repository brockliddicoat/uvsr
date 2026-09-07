# Dear `ImGui`

## record

- Relationship: Dependency Integration and Incorporated Upstream Material
- Status: Current
- Confidence: Confirmed
- Upstream: [Dear ImGui](https://github.com/ocornut/imgui)
- Revision: `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`
- Governing Terms: [MIT License](https://github.com/ocornut/imgui/blob/45acd5e0e82f4c954432533ae9985ff0e1aad6d5/LICENSE.txt)

## UVSR relationship

Dear ImGui supplies the permanent immediate-mode UI implementation. The
unmodified upstream boundary is 15 directly vendored files under
`third_party/imgui`: the license, core implementation and headers, embedded stb
headers, and Win32 and DirectX 12 backends. The boundary is pinned to the
revision above. Its CMake manifest verifies every file's SHA-256 and rejects
missing or extra files. It contains no demo, documentation, example, or
standalone font file.

The active retained build stages copies of the core sources and applies the reviewed
product layout and interaction patch outside that boundary. Those
modified copies, the patches, and first-party private-API consumers are part of
the supported integration. UVSR's settings model and visual composition are
first-party. Donut's nested ImGui copy is not an active build source.

Dear ImGui embeds Tristan Grimmer's ProggyClean under the MIT License.
it remains part of the upstream dependency. renderer packages retain the complete
notice in `bin/licenses/ProggyClean-MIT.txt`. the active interface uses installed
Segoe UI fonts.

## evidence

- [Exact Manifest and Validation](../../cmake/DirectImGui.cmake)
- [Retained Donut Build Integration](../../cmake/DirectDonut.cmake)
- [Reviewed Patch Set](../../CMakeLists.txt)
- [Vendored Dear ImGui License](../../third_party/imgui/LICENSE.txt)
- [Embedded ProggyClean Attribution](../../third_party/imgui/imgui_draw.cpp)
- [ProggyClean Source Record](proggy-clean-font.md)

## commercial clearance

Every distributed modified copy must retain Dear ImGui's MIT copyright and
permission notice. Packages using the embedded ProggyClean font must also retain
Tristan Grimmer's copyright and MIT terms. Renderer packages install Dear ImGui's terms as
`bin/licenses/Dear-ImGui-MIT.txt`.

## Cap reference

Cap uses stock ImGui Dark, as confirmed in Capsaicin
[`914b91596cd119eda85fbc1d3c7ee6ac391b1452`](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/tree/914b91596cd119eda85fbc1d3c7ee6ac391b1452).
its [viewer](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/src/scene_viewer/main_shared.cpp#L1292-L1294)
auto-sizes the window. pinned gfx
[`c65d6172500864b68988cdf64846520ebbb42e6f`](https://github.com/gboisse/gfx/blob/c65d6172500864b68988cdf64846520ebbb42e6f/gfx_imgui.cpp#L157-L175)
selects Dark, ImGui v1.91.9, a 13-pixel font, and DPI scaling. ImGui's auto-size
control width is 16 font heights, or 208 logical pixels. Cap retains that
control width with UVSR's font, labels, reset lane, and small corner radii.
no AMD palette, gfx code, or Capsaicin UI code is vendored.
