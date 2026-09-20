# AGFX classic raster parity

source: AGFX `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7`. the [manifest](raster-sources.json) pins all38 relevant source files,47 original PNGs and50 case mappings. [AGFX MIT attribution](../../legal/licenses/AGFX-MIT.txt) applies to the translated source. the existing CPU [FLIP reference](sampling.md) retains NVIDIA's separate license and original algorithm.

## source mapping

| source family | Rust shader entries and native behavior | source groups |
| --- | --- | ---: |
| raster.hlsl, raster_common, cull mode, winding, wireframe, points, lines, discard and push constants | raster_vs/raster_fs, original six vertices and colors, root32, explicit one-pixel PointSize, real fragment kill, unchanged fill/cull/front/topology state | 10 |
| triangle.hlsl, draw triangle, viewport and scissor | triangle_vs/color3_fs, original three vertices, viewport64/64/64/64 or scissor24/40/64/64 when selected | 3 |
| indexed.hlsl, indexed u32/u16 and indexed lines | indexed_vs/color4_fs, original four stride32 vertices, original index streams and post-index vertex IDs, readonly ordinary buffer0 | 3 |
| depth.hlsl, depth_common, eight comparisons, clear value, clamp and write on/off | depth_vs/depth_fullscreen_vs/color3_fs, root32, original columns/floor/occluder, D32 depth attachment, ordered draws in one render pass | 13 |
| blend.hlsl, blend_common, ten factors, five operations and source-alpha blend | blend_vs/blend_fullscreen_vs/color4_fs, root48, original destination scales/alphas and source color, source AlphaEquivalent mapping | 16 |
| pass_actions.hlsl and pass_actions_common | pass_vs/pass_fs, root32, original XOR gradient upload, exact clear/draw byte colors, Load/Clear/DontCare with Store | 3 |

these48 source behavior groups retain their C/Cpp/Ez assertions through the one Rust owner. this does not close the separate complete API/Ez surface mapping. the two added cases import a just-exported same-device graphics pipeline cache and exercise firstIndex3 with signed baseVertex-4. they compare against the original triangle/indexed goldens. they are identified as added controls in the manifest.

`crates/agfx/src/vulkan/graphics.rs` owns classic pipeline creation and ordered render passes. the existing single-threaded Device, buffer/texture allocations, recording, timeline and forgotten-child retention remain in use. private `bindings.rs` now serves compute and graphics with one descriptor layout/pool owner. public graphics resources are readonly during a pass. pipeline construction and arbitrary shader draws retain explicit unsafe contracts under [U-021](../../UNSAFE.md#u-021-classic-graphics-pipelines-and-render-attachments).

the source shader handle root in indexed.hlsl maps to explicit ordinary binding0. its reserved16-byte root is unused and removed from the emitted entry. the complete vertex layout remains position0/padding8/color16, stride32. this is not a native NGAPI heap or a finished source global-handle implementation. the other push layouts preserve source byte offsets exactly.

## coordinates, attachments and lifetime

the source viewport conversion is preserved: native y becomes y+height and native height becomes -height. both winding directions, culling, asymmetric geometry and viewport/scissor offsets are exercised. reverse depth ranges and signed heights retain valid source/Vulkan behavior in CPU controls. native bounds follow [VkViewport](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html). [A-011](../../docs/agfx-port-notes.md) records the convention and possible future clarification.

dynamicRendering, fillModeNonSolid, depthClamp and independentBlend are queried before being enabled. unavailable requested features fail explicitly. the source has one raster sample and no vertex-input layout. vertices come from VertexIndex or shader reads. colors and optional depth use the exact matching full extent and format, with queried attachment/blend support. D32 views, copies and clears use the depth aspect. no unsafe Rust implementation is needed inside the translated shader bodies.

Load requires initialized contents. Clear initializes the complete attachment. DontCare followed by Store has an explicit unsafe full-write obligation. the source test meets it with its oversized triangle and no blend, cull or discard. Store DontCare invalidates the contents and subsequent safe readback or Load is rejected. every image returns to GENERAL after synchronous completion. all attachment and descriptor owners remain borrowed until then.

## oracles and observed results

the source acceptance remains FLIP mean at most0.05, with source float32 accumulation/threshold comparison and raw RGB byte normalization. alpha is checked separately. no PNG, source tolerance, geometry, camera or shader formula is rewritten. exact-byte differences remain visible as diagnostics.

additional checks cover all image alpha, independent blend arithmetic for all channels within one UNORM code point, complete depth-test color regions, every depth texel within1e-6 of its independent expected value, source sparse-output counts, forbidden viewport/scissor/discard regions, push colors and the source load/clear/DontCare regions. the original47 images satisfy these independent checks before GPU execution. ten CPU controls deliberately corrupt these properties, including blank points/lines, wrong depth and alpha omitted by RGB FLIP.

E-043 Debug and Release each execute100 images and26 depth readbacks at shader opt0/opt3. all100 pass the source and additional oracles. all126 corresponding host outputs are byte-identical. 44/100 are also exactly equal to the original PNG bytes. the largest FLIP mean is0.0277969874 for source-color blending. the largest individual byte difference is254 in sparse line coverage, whose source oracle permits backend rasterization differences and separately requires nonempty coverage. this does not imply every image differs by only one byte.

each host passes19 rejected-use controls plus one exact full D32 clear/readback control. all runs require confirmed Khronos core/synchronization validation, with zero diagnostics. the controls cover foreign/uninitialized/index-overrun inputs, invalid viewport/scissor/root, incorrect attachment format/usage/extent, invalid depth clears and discarded-store reuse. source handles never escape. passing these bounded cases is not a general soundness proof.

the first source compile exposed PushConstant pointer OpPhi from indexing a borrowed glam Vec4. indexing its copied value array preserves every source value and compiles without VariablePointers or a compiler patch. the rejected module is retained. the first GPU capture finished, then the report parser stalled on a quadratic regex over a long JSON line. its raw captures remain marked incomplete because the process exit result had not been persisted. a line-by-line parser retains the same diagnostic rules and passes a long-record control. fresh final gates passed in19–20seconds, including image comparisons. neither issue was repaired by altering a shader result or suppressing a diagnostic.

evidence: ignored `work/theta/evidence/full-port/raster-native-review.json`, `raster-compile`, `raster-compile-pointer-phi`, `raster-vulkan-debug`, `raster-verified-debug`, `raster-verified-release` and `raster-configuration-comparison.json` retain exact sources, builds, commands, payloads, driver, metrics and first differences. the final payloads use only Shader/VulkanMemoryModel and contain12 named entries.

## reproduction and remaining scope

with the configured pinned Rust environment and CPU FLIP reference:

```powershell
python tools/theta/compile_agfx_graphics.py --rustgpu-source work/theta/upstream/rust-gpu --codegen-backend work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll --output-dir work/theta/evidence/full-port/raster-compile
cargo build --workspace --all-targets -j 1
python tools/theta/run_agfx_raster.py --executable work/theta/build/agfx-host/debug/raster.exe --sdk work/theta/tools/vulkan-sdk-1.4.357.0 --shader-dir work/theta/evidence/full-port/raster-compile --flip-reference work/theta/evidence/full-port/flip-reference/flip_reference.exe --output-dir work/theta/evidence/full-port/raster-verified-debug
```

the runner needs Pillow for unchanged source PNGs. use the corresponding Release executable for the second host configuration. metadata and executable source hashes must agree before Vulkan is loaded. cached bytes are imported only from a compatible native cache, under the constructor's explicit contract.

remaining work includes the ShaderToHuman example families, graphics sampled-resource execution, source draw-parameter/indirect/mesh cases, multi-attachment tests, complete texture shapes/formats/views, global handles and API/Ez behavior. only classic vertex/fragment cache reuse is exercised here. actual NGAPI integration and Linux execution remain separate required evidence. this graphics checkpoint does not complete the full ports.
