# ShaderToHuman Features

source: Electronic Arts ShaderToHuman `d6f98b7d67da802053cd9c702082fa741dec42e7`, under the unchanged [BSD-3-Clause notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt). the [manifest](features-sources.json) freezes all16 Features files, the shared drawing library and declared test inputs. [programs/features](../../crates/shader-to-human/programs/features/mod.rs) contains safe CPU/RustGPU bodies. the [shader module](../../shaders/rust/shader_to_human_features.rs) supplies eight entries. the [native example](../../crates/shader-to-human/examples/features.rs) uses existing AGFX owners and explicit storage/sampled/attachment views.

## source mapping

| original file | Rust behavior and graph output |
| --- | --- |
| 3D_example.hlsl | world::draw, A. source3x3 sample pattern, checkerboard, segments, shadows and animated basis |
| Gather_example.hlsl | gather::draw, B before DebugZoom. formatted text, radio/clear/checkbox/progress and alpha/RGB sliders |
| DebugZoom_example.hlsl | images::debug_zoom, B after Gather. source-selected texel, numeric channels and magnifier |
| Clear_example.hlsl | world::clear, background before C and G. active skybox path and raw UNORM write |
| Scatter_example.hlsl | shared fixtures/scatter body, C after Clear. complete source drawing callback, ordered writes |
| Table_example.hlsl | table::draw, D. integer and float tables, callbacks and range formatting |
| 2D_example.hlsl | two_d::draw, E. RGBA/size controls, animated shapes, lines, eyes, ramps and blending |
| 2D_Arrow.hlsl | images::arrows, F. width/head variants, radial arrows and mouse-direction arrow |
| QuadCommon.hlsl | quad::quad_vertex and post corner values. original UV flip and position offset |
| QuadVSPS_example.hlsl | quad_vertex/quad_fragment, G after Clear. six vertices, camera labels, window depth and clip W |
| QuadPost_example.hlsl | quad::quad_post, G after raster. four corner spheres and object/view/homogeneous/NDC/pixel tables |
| GenUserFont_example.hlsl | images::font_atlas,768x8 source HSV-colored embedded glyphs |
| UseUserFont_example.hlsl | images::use_font, H. source UserFont text with sampled sRGB atlas, scale4 and black background |
| 2D_CoordinateSystem.hlsl | images::coordinates, I. both coordinate systems and source label |
| s2h_features.gg | fixed A-I execution, dependencies,800x600 targets, font dimensions, view formats and persistent UI fields |
| s2h_features.gguser | camera0 retains saved angles and the source hosting fallback described below |

Scatter's example and original unit-test bodies are equal after removing comments, includes and whitespace. one shared Rust body maps both. source inactive COLOR clear branch and commented alternatives remain inactive. stale SplatVSPS/4To1 graph definitions have no active implementation or dependency and are not invented as new output programs. unused quad varyings are omitted without changing the active formulas.

all image invocations read an immutable state snapshot and update private copies. one post invocation commits the source UI action after image completion. this explicitly replaces the source's shared-state writes and does not claim to reproduce every outcome of its race. the selected invocation follows the source mouse hit test, including fractional positions. Gather and 2D pass float4 mouse input. Table and Arrows explicitly cast to int4. capture coordinates truncate independently. consequently a Gather mouse x of -100.9 releases capture, while the integer-cast programs preserve the -100 sentinel.

the source World example reads context.ro before initializing the context. both the Rust port and controlled HLSL comparison explicitly use the declared camera origin. this correction is visible here and is not described as unmodified source execution. DebugZoom preserves its selected source texel by returning without a write. other reads/writes address each invocation's own texel. an offscreen selected texel explicitly loads zero. Scatter uses one writer, and its complete source coordinate enumeration fits inside800x600.

## interface, camera and color behavior

binding0 is24 uint4 words,384 bytes. words0..6 retain the112-byte UI state from [Zoom2D](shader-to-human-zoom.md). words7..10,11..14 and15..18 hold world-from-clip, world-to-clip and world-to-view. word19 is camera position/near. words20/21 are current/previous float4 mouse. word22 is dimensions/time/far and word23 is camera angles/padding. image programs preserve unused fields, padding and immutable inputs. the root is16 bytes. storage output is binding1 and the custom font's sampled atlas is binding2, with no sampler.

source graph images use base RGBA8_SRGB and UNORM storage views. direct source UAV writes retain their encoded or linear byte values as written. the quad attachment and font sampled view use SRGB, so their conversions occur at those respective operations. Gather/Table retain the original raw background values. World/Coordinates explicitly encode sRGB. Clear writes its source skybox without adding an encoding step. QuadPost manually decodes the raw storage read, composites its source overlay and encodes once. alpha remains opaque for final targets, while the atlas preserves glyph transparency. the custom font ignores the source text color as its callback specifies.

the [three camera records](fixtures/shader-to-human/features-cameras.txt) come from unchanged Gigi Camera.cpp/h at `401386cfd7c6e39e549d939e44d99bd5b49cd14d`. camera0 retains Features' saved angles and default position(0,0,-10). its saved projection texture name FrameBuffer does not identify an active graph texture. pinned Gigi therefore retains its initial1x1 projection resolution, giving aspect1. cameras1/2 deliberately exercise visible quad/world output at800x600. each uses the source left-handed, reverse-Z perspective with near0.1, far1000 and FOV45. these are traced inputs, not an assertion that an unknown historical Gigi build had the same behavior.

quad positions retain the source uv*2+1 offset and UV Y flip. Vulkan fragment reciprocal W is inverted to preserve the source DirectX clip-W value. the post table retains source NDC.xy multiplied by dimensions without inserting a viewport offset or Y flip. the 2D program retains the combined source flips and pixel offset. correcting these unusual conventions would be a separate behavior change.

## verification status

the [54-step input sequence](fixtures/shader-to-human/features-inputs.json) covers all nine programs with three cameras, source-zero and seeded state, radio/checkbox/clear, held/released/fractional/offscreen mouse, slider capture/drag, sentinel release and degenerate arrows. it is declared input, not a replacement golden. nine CPU contracts cover packing, state preservation, source input casts, fractional sliders, font coordinates, quad conventions, Scatter bounds and NaN saturation. seventeen Python controls check state fields, immutable inputs, ABI stride, hashes, complete case counts, finite slider results, missing arrow rows and strict comparison failures.

Debug and Release each execute108 cases at opt0/opt3 with zero Khronos core/synchronization diagnostics. all384 corresponding image, intermediate, input and post-state artifacts agree between hosts. independent state/history/padding checks, source arrow centers/crosshairs, opaque alpha, camera response, Scatter preservation, atlas sampling and733,946 sky pixels per optimization level pass. all57 workspace Rust tests,107 Python controls, strict Clippy, formatting and both builds pass. the five earlier shader families were recompiled after the shared saturation correction. their Debug/Release native gates retain all340 compared readbacks per host, with the earlier four strict fixture failures and13 documentation differences unchanged.

the complete strict original-HLSL comparison executes108 cases per host. all108 post states and input buffers match exactly.60 final images match exactly, while48 remain strict failures.38 of those differ by at most one eight-bit channel value. ten cases, World with cameras1/2, Clear/Scatter with camera1 and Quad with cameras1/2, have larger differences at sensitive expressions, labels or raster coverage. maximum observed channel error is191. all alpha channels match. no threshold is relaxed and these results do not close T029. the [comparator](../../tools/theta/compare_s2h_features.py) preserves complete difference counts, first differing values and hashes for every input, post state, final image and intermediate.

initial runs exposed an incorrect integer mouse conversion in the port and an incorrect selected invocation in the HLSL control. both were corrected from source inspection and explicit fractional-input contracts. CPU NaN saturation was also insufficient without a GPU execution-mode contract: missing arrow rows and the centered-mouse failure disappeared when Features image/commit explicitly requested SignedZeroInfNanPreserve32. selected-device support is queried before pipeline creation. [L-018](../../docs/lessons.md#l-018-include-floating-point-execution-modes-in-source-parity) records this bounded finding. the final arrow cases differ from the strict control at only11 pixels by one channel value. complete pre-execution identities and source/caller reviews are recorded under [U-025](../../UNSAFE.md#u-025-shadertohuman-features-image-font-and-quad-programs). no independent safety audit is claimed.

the original HLSL control keeps all library bytes unchanged. explicit resources and row-major matrices adapt hosting. source image invocations use private state and one source-body post invocation commits it. unused raster varyings are removed, DirectX fragment W is requested explicitly, and the same World-origin correction applies. Scatter uses DXC Od after O3 exceeded its compile bound. a second control uses Gis floating-point strictness, requiring queried float32 signed-zero/Inf/NaN preservation support. neither control is an unmodified Gigi capture or a production HLSL adapter.

the independent double-precision sky oracle excludes |ray.y|<=1e-5 because the source pow(abs(y),0.2) amplifies tiny horizon rounding differences. those pixels remain in the complete original-HLSL image comparison. source image discrepancies are not waived by this numeric-oracle boundary. Linux, actual NGAPI integration, interactive input, GaussianSplatting and the earlier fixture/documentation discrepancies remain separate required work.

## reproduction

compile `tools/theta/compile_s2h_library.py --features` with the pinned RustGPU source/backend arguments from the [Hello recipe](shader-to-human-hello.md#reproduction-and-remaining-scope). build example features in Debug and Release. current complete source/module/host reviews must precede execution. the local review recipe is ignored work/theta/review_features.py, with features-preserve-review-debug.json/release.json records.

```powershell
python tools/theta/run_s2h_features.py --executable work/theta/build/agfx-host/debug/examples/features.exe --sdk work/theta/tools/vulkan-sdk-1.4.357.0 --shader-dir work/theta/evidence/full-port/features-compile-preserve --output-dir work/theta/evidence/full-port/features-preserve-debug --review work/theta/evidence/full-port/features-preserve-review-debug.json
python tools/theta/compare_s2h_features.py --candidate work/theta/evidence/full-port/features-preserve-debug --reference work/theta/evidence/full-port/features-reference-strict/native --output work/theta/evidence/full-port/features-source-exact-debug.json
```

the first command passes the native/state/structural gate. the second currently exits1 for the48 strict image differences. missing captures, changed hashes, incomplete sequences, changed source identities and missing validation fail separately. original and strict reference recipes/captures, camera extraction, failed attempts and final compiler identities remain under ignored features-source, features-reference and features-reference-strict. no source golden is replaced.

## inherited behavior for later review

all candidates below refer to the pinned files in the source table. they are observed source behavior, with no measured optimization claim.

| behavior | current disposition and evidence needed before changing |
| --- | --- |
| shared per-pixel UI state writes | changed to an immutable image snapshot and one post update. preserve exact interaction endpoints and images before considering a more general event interface |
| zero-area arrowheads and normalize(0) | retained geometric formulas. shared HLSL saturate(NaN)=0 is restored explicitly in Rust. any geometric early return needs shape/edge/alpha equivalence and measurements before claiming lower cost |
| float versus integer mouse input varies by program | retained with fractional and sentinel contracts. unify only through a declared interaction change with boundary/drag/release comparisons |
| uninitialized World origin and nonexistent saved projection texture | explicit origin correction and traced aspect1 fallback. future hosting should reject unresolved names or define initialization, with source reconstruction kept visible |
| per-pixel text/table evaluation and repeated callback arithmetic | retained for close parity. caching or scatter-based text needs image/alpha/state equivalence and measured CPU/GPU work before claiming an improvement |
| raw UNORM writes, sRGB sampling and manual quad post conversion | retained. future color-interface simplification must verify every intermediate format and conversion count |
