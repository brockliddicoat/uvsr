# ShaderToHuman Hello examples

source: Electronic Arts ShaderToHuman `d6f98b7d67da802053cd9c702082fa741dec42e7`, under the unchanged [BSD-3-Clause notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt). the [manifest](hello-sources.json) freezes all 14 shader, graph and user-input files for four example families. safe shared bodies live in [programs/hello.rs](../../crates/shader-to-human/programs/hello.rs), with separate RustGPU entry points and the native Vulkan host.

| source family | Rust behavior | disposition |
| --- | --- | --- |
| HelloScreenVSPS | screen_vs/screen_fs, six source vertices, pixel-centered Hello Screen text, sRGB ramp and UV background | direct shader translation with explicit Vulkan hosting |
| HelloWorldCS | hello_cs, original 8x8 group size, Hello Compute text and UV background | direct translation, one initialized UNORM storage image |
| HelloQuadVSPS | quad_vs/quad_fs, camera transform, source UV orientation, border, camera-position text, depth and fracW displays | direct translation with explicit fragment-coordinate adaptation |
| HelloQuadVSPS_Slang | the same quad entries and assertions | equivalent mapping. both stages are identical after removing comments/whitespace and normalizing three equivalent syntax differences. hashes are recorded. no Slang compiler or language adapter is claimed |

## preserved inputs and hosting

the source graphs use 800x600 RGBA8 sRGB textures. graphics writes use a native SRGB attachment and leave shader arithmetic linear. Vulkan applies the attachment conversion as specified in [framebuffer operations](https://docs.vulkan.org/spec/latest/chapters/framebuffer.html). the compute output uses the source UAV's UNORM interpretation, as in the existing ShaderToHuman image fixtures. transfer readbacks contain actual encoded bytes. no screenshot/display transform enters the comparison.

the active quad vertex expression is `uv * 2 + 1`, despite its nearby minus-one comment. its UV Y flip, camera text color, text cursor spacing, sRGB ramp, depth rectangle and fracW rectangle are unchanged. unused world-to-view/projection locals and the unconsumed object-position varying are omitted without changing output. the source uses no depth attachment, no blending and no culling.

Gigi's pinned resource implementation zero-initializes non-imported textures, including alpha. the draw defaults do not clear the target. the Rust host therefore initializes once and loads prior contents. the motion case preserves the previous silhouette instead of silently clearing it. a separate fresh moved-camera case distinguishes the two behaviors.

camera row0 retains the original .gguser position and altitude/azimuth. unchanged Gigi Camera.cpp/h at `401386cfd7c6e39e549d939e44d99bd5b49cd14d` and DirectXMath generate the frozen matrix words. settings are left-handed, perspective, reverse Z, 45-degree FOV, near0.1, far1000 and 800x600. two additional rows are a frontal camera and a translated source camera. these are declared controls, not replacement source defaults. extraction commands, executable, source hashes and exact settings are retained under ignored hello-source. the camera text file is input data, not an image golden.

the 96-byte root contains the column-major world-to-clip transform at0, camera float4 at64 and dimensions float4 at80. the source's transpose of Gigi's row-major view-projection matrix is preserved. native negative viewport height matches the source framebuffer orientation. Vulkan FragCoord.w is explicitly inverted before the shared quad body to preserve DirectX SV_Position.w. this matches [DXC's documented conversion](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst). depth remains window-space z. three CPU contracts check root layout, fixed cameras, the active quad offset/UVs, text and the two numeric displays.

## independent comparisons and results

each host executes six cases at opt0 and opt3: screen, compute, source quad, persistent-motion quad, frontal quad and fresh moved quad. Debug and Release each complete all 12 images, with confirmed Khronos core/synchronization validation and zero diagnostics.

an ignored reference compiles the original HLSL bodies and unchanged library using the installed DXC. only explicit root/binding declarations, the unused varying, unused matrix aliases and documented coordinate conversion adapt the source to Vulkan. five linked entries pass independent SPIR-V validation. a separate host accepts only the reviewed reference payload. it executes the same six cases, with its own pre-execution identity review and zero validation diagnostics. this is an original-source Vulkan control, not an original Gigi/D3D capture or a second production shader adapter.

all 12 outputs in each Rust host match the source control byte for byte, including alpha. no tolerance, rebaseline or candidate-generated golden is involved. the exact comparison checks record completeness, original source pin, shader language, root hashes, image lengths and capture hashes before comparing every channel. all corresponding Debug/Release outputs match as well.

independent structural checks cover full background gradients, SRGB versus UNORM encoding, source text presence, continuous and stepped ramp rows, camera-projected coverage, untouched transparent pixels, depth/W rectangle values and retained motion pixels. these checks use explicit numeric bounds and remain separate from exact source agreement. eight corruption controls reject stale runs/hosts, missing or zero cases, wrong roots, UNORM substituted for SRGB lost alpha and nonfinite numeric references.

[U-022](../../UNSAFE.md#u-022-shadertohuman-hello-raster-and-compute-examples) records the one shader image-write boundary and four existing-owner caller blocks. the first Debug run's automated review record was completed after execution because an incorrect image-depth operand assertion interrupted its creation while the next command still ran. source/caller inspection and module validation had preceded execution. this ordering failure is retained, not relabeled as a complete pre-execution gate. the original-source and Release runs used completed pre-execution identity gates.

## reproduction and remaining scope

with the pinned RustGPU environment and Python with numpy:

```powershell
python tools/theta/compile_s2h_library.py --hello --rustgpu-source work/theta/upstream/rust-gpu --codegen-backend work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll --output-dir work/theta/evidence/full-port/hello-compile-reviewed
cargo build --workspace --all-targets -j 1
python tools/theta/run_s2h_hello.py --executable work/theta/build/agfx-host/debug/examples/hello.exe --sdk work/theta/tools/vulkan-sdk-1.4.357.0 --shader-dir work/theta/evidence/full-port/hello-compile-reviewed --output-dir work/theta/evidence/full-port/hello-vulkan-debug
python tools/theta/compare_s2h_hello.py --candidate work/theta/evidence/full-port/hello-vulkan-debug --reference work/theta/evidence/full-port/hello-reference/native --output work/theta/evidence/full-port/hello-exact-debug.json
```

the comparison requires the independently captured reference and fails if it is missing or stale. ignored hello-source/hello-reference records retain its exact build, transformations, native host and complete captures. use the Release executable and a separate output directory for the second host configuration.

Features and GaussianSplatting remain required. [Zoom2D](shader-to-human-zoom.md) now has its separate source and interaction mapping. the previous four strict fixture failures and 13 documentation reference differences remain unresolved. full AGFX API/Ez behavior, actual NGAPI integration, Linux execution and upstream acceptance remain separate work. these four mapped Hello families do not close the complete port.
