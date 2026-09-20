# ShaderToHuman Zoom2D

source: Electronic Arts ShaderToHuman `d6f98b7d67da802053cd9c702082fa741dec42e7`, with the unchanged [BSD-3-Clause notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt). the [manifest](zoom-sources.json) freezes Pre.hlsl, Render.hlsl, the graph/user settings and the shared library. [programs/zoom.rs](../../crates/shader-to-human/programs/zoom.rs) contains safe shared CPU/RustGPU bodies. the [entry module](../../shaders/rust/shader_to_human_zoom.rs) and [native example](../../crates/shader-to-human/examples/zoom.rs) use existing AGFX owners.

## source mapping and ordering

| source | Rust mapping | disposition |
| --- | --- | --- |
| Pre.hlsl mainCS and ScaleAroundCenter | pre and zoom_pre_cs | direct formulas, exactly one invocation before rendering |
| Render.hlsl gridTextureGradBox and coordinate systems | grid_texture and render | source grid integration, snapped coordinates, axes, text and premultiplied composition |
| Render.hlsl magnified pixel coordinates | render pixel_ui | original fractional position, scale-dependent opacity and integer coordinate labels |
| Render.hlsl screen overlay | overlay | unpanned numeric pan/scale display, red Reset button and mouse instructions |
| Render.hlsl shared deinit/reset writes | post and zoom_post_cs | explicit race removal. one update after rendering from an immutable state snapshot |
| s2h_zoom2D.gg persistent UIState and Pre/Render dependency | initialized state buffer and ordered synchronous passes | zero-initialized resource as in pinned Gigi hosting, retained between frames |

source Render writes shared capture and pan/scale while other invocations may still read them. reproducing that race would not define one expected image. the Rust image pass reads an immutable snapshot. one post invocation then commits deinit and the mouse-selected Reset action, making reset visible in the next frame. the original HLSL control uses this same documented hosting adaptation. exact image agreement does not claim reproduction of every possible outcome of the original race.

unlike UI_docs, this HLSL example actually calls s2h_deinit. its release rule clears the capture field when integer mouse x differs from the -100 sentinel and the left button is zero. the post pass preserves that behavior. unused radio, checkbox, slider and padding words are retained. declared graph field defaults do not replace Gigi's observed zero initialization.

the root is 48 bytes: dimensions uvec4 at 0, current mouse float4 at 16 and previous mouse float4 at 32. explicit seven-uvec4 state packing uses 112 bytes:

| byte offset | source value |
| --- | --- |
|0,4 | UIRadioState, UICheckboxState |
|8..15 | explicit reserved padding |
|16,32,48 | colorSlider0, colorSlider1, sizeSliders |
|64 | s2h_State int4 |
|80 | PanAndScale float3 |
|92 | explicit reserved padding |
|96 | MouseDragStart float4 |

this is an explicit Vulkan ABI adaptation, not a claim that the source HLSL structured-buffer byte packing was identical. the source fields and values map individually. state uses binding0, output binding1. the graph's RGBA8 sRGB texture has a UNORM UAV. Render explicitly applies accurate linear-to-sRGB conversion, then writes encoded bytes to RGBA8_UNORM. no second gamma conversion is added.

the port retains integer truncation of mouse coordinates, exact left-button comparisons to 1, nonzero right-button drag, right-drag start only on 0-to-1, simultaneous pan then zoom, and the original two-step pivot calculation. negative transformed coordinates between -1 and0 retain the source's truncation-to-zero framebuffer membership. unused alphaGrid arithmetic and unused time/camera macros have no output effect and are omitted. the source grid routine's original attribution remains in code.

## evidence

the [29-frame input sequence](fixtures/shader-to-human/zoom-inputs.json) covers idle, left drag/hold/release, right-drag pivot, scales 1/2,1/4 and1/16, return zoom, off-frame content, reset press/hold/release, fractional mouse coordinates, nonbinary buttons and simultaneous buttons. it is declared test input, not a replacement source golden.

Debug and Release each execute 58 images and 116 pre/post state readbacks at shader opt0/opt3 on native Windows Vulkan. Khronos core and synchronization validation are confirmed active with zero diagnostics. all 174 corresponding readbacks agree between hosts. all 58 images in each host match the independent original HLSL control byte for byte, including alpha. every pre/post state also matches both the source control and independently enumerated endpoint words. no tolerance or rebaseline is used for source agreement.

the HLSL control compiles original Pre/Render and the unchanged library using DXC cs6_1 O3. explicit declarations adapt root/buffer/image binding. Render uses a private state copy. a one-invocation post entry reuses the source Render body at the selected mouse pixel, removes its image write and commits that private state. the reference passes independent SPIR-V validation and the same29-frame structural/state gate. this is a controlled source Vulkan comparison, not an original Gigi capture or a production HLSL adapter.

independent image checks cover full alpha, off-frame background, pan movement, stationary drag/release, magnification, reset visibility and restoration of the initial image. each optimization level additionally checks3,698,705 grid/background pixels against a numeric color-space oracle away from geometry and labels. the fractional-drag case checks the negative-coordinate truncation boundary. three CPU contracts check pivot invariance, integer inputs, untouched fields/padding, release sentinel and immutable frame/reset ordering. nine corruption controls reject empty runs, stale hosts, wrong roots, reordered phases, changed state with matching hashes, broken histories, truncated images and lost alpha, while accepting exact promoted float 32 mouse records.

[U-023](../../UNSAFE.md#u-023-zoom2d-image-and-ordered-persistent-state) registers one shader image write and three native caller blocks. complete source/executable/module reviews were persisted before all native runs. the runner requires matching review identities before entry. all 46 workspace Rust tests, 80 Python controls, strict Clippy, formatting and both host builds pass. the unchanged Hello comparator still passes its12 source comparisons. unchanged AGFX and older ShaderToHuman GPU gates were not repeated for this isolated addition.

the first native attempt completed29 frames, but its checker compared original decimal mouse values with JSON's promoted float 32 values. the checker was corrected and the existing captures re-evaluated successfully before the complete final runs. the first host build also required a mutable pipeline borrow. these are retained diagnostic failures, not shader formula changes or relaxed image/state requirements.

## reproduction and limits

use the pinned Rust environment and Python with numpy. compile with `tools/theta/compile_s2h_library.py --zoom`, using the same RustGPU source/backend arguments as the [Hello recipe](shader-to-human-hello.md#reproduction-and-remaining-scope). build example zoom in Debug and Release. the local pre-execution review recipe and its complete records remain under ignored work/theta/review_zoom.py and evidence/full-port/zoom-native-review-debug.json/release.json. a changed executable requires a current source/caller review and matching record.

```powershell
python tools/theta/run_s2h_zoom.py --executable work/theta/build/agfx-host/debug/examples/zoom.exe --sdk work/theta/tools/vulkan-sdk-1.4.357.0 --shader-dir work/theta/evidence/full-port/zoom-compile --output-dir work/theta/evidence/full-port/zoom-verified-debug --review work/theta/evidence/full-port/zoom-native-review-debug.json
python tools/theta/compare_s2h_hello.py --profile zoom --candidate work/theta/evidence/full-port/zoom-verified-debug --reference work/theta/evidence/full-port/zoom-reference/native --output work/theta/evidence/full-port/zoom-exact-debug.json
```

reference recipes, modules, hosts, pre-execution gates and captures live in ignored zoom-source/zoom-reference. missing reference captures fail. the current bounded sequence uses scales 1/16..1. unbounded interactive zoom, Linux, actual NGAPI integration and a window/input host remain separate requirements. the source has no zoom clamp. adding a finite range or changing the button/negative-coordinate rules would be a future behavior change needing an explicit compatibility decision and numeric/interaction evidence, not an assumed performance improvement.

Features and GaussianSplatting remain required example families. the earlier four strict fixture failures and 13 documentation reference differences remain unresolved. this increment does not complete T018/T024-T029 or the full AGFX port.
