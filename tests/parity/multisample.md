# multisample color and depth

this is the native Vulkan dependency for ShaderToHuman GaussianSplatting, not an additional source AGFX test or a completed Gaussian port. pinned AGFX `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7` fixes [image samples](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx/agfx_vulkan.cpp#L1937) and [raster samples](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx/agfx_vulkan.cpp#L3373) to one. ShaderToHuman `d6f98b7d67da802053cd9c702082fa741dec42e7` instead declares eight-sample color and D32 targets in `examples/GaussianSplatting/s2h_splat.gg`. its `CustomResolveMSAA.hlsl` loads all eight color samples individually. substituting a one-sample target cannot preserve that behavior.

## implemented contract

the existing Texture and RenderPipeline owners now carry an explicit SampleCount. old constructors and default pipelines remain One. `Device::texture_with_samples` queries support for the exact base format, usage, flags and compatible view list. the byte bound includes all samples. no new native owner, descriptor mechanism, dependency or compiler patch is introduced.

pipeline construction checks color/depth framebuffer sample capabilities. recording requires every attachment and pipeline to agree. buffer/image copies reject multiple samples before recording. complete clears initialize all samples, while DontCare's unsafe coverage contract explicitly includes each sample. sampled-image fetch callers must match the shader image type and supply valid sample indices. multisample storage is explicitly unsupported until shaderStorageImageMultisample is queried and enabled. that restriction does not affect this source example's attachment and sampled-image path.

the [Vulkan clear rules](https://docs.vulkan.org/spec/latest/chapters/clears.html) define clearing every sample. the [copy rules](https://docs.vulkan.org/spec/latest/chapters/copies.html) require one sample for buffer/image transfers. [U-026](../../UNSAFE.md#u-026-owned-multisample-attachments-and-per-sample-readback) owns the complete safety renewal and review limits. [A-018](../../docs/agfx-port-notes.md) records the inherited restriction and disposition.

## numeric evidence

the [shader](../../shaders/rust/multisample.rs) forbids unsafe. it has a vertex/fragment pair with a48-byte root, one-word SampleMask output, and two compute entries for one-sample and eight-sample fetches. each compute entry uses one8x8x1 group and a disjoint512-float4 output. only Shader/VulkanMemoryModel capabilities are required. the [native caller](../../crates/agfx/src/bin/multisample.rs) has six explicit unsafe blocks, checks exact private payload/metadata identities before Device creation, and uses the existing synchronous owners.

three configurations cover one-sample RGBA32_FLOAT/D32, eight-sample RGBA32_FLOAT/D32 and eight-sample sRGB/D32. each checks five phases independently:

1. full color and depth clear.
2. writes to only the first half of the samples, preserving the remainder.
3. distinct values in every sample.
4. a full mask whose depth fails the reverse-Z Greater test.
5. a passing depth with a zero sample mask.

every phase reads all64 texels and every color/depth sample. expected float values are exactly representable. the sRGB colors use distinct endpoint triples to isolate sample identity from conversion precision, which has its separate [format-view gate](format-views.md). only the defined first depth component is compared. one-sample dispatches must also preserve the remaining448 output elements. the two readbacks per phase yield30 cases per shader level,60 per host configuration. no comparison tolerance is used.

Debug and Release each pass all60 cases and24 native rejection controls with zero Khronos core/synchronization diagnostics on NVIDIA GeForce RTX4090 Laptop GPU. all60 corresponding host captures agree. one Rust capability test and eight Python evidence controls cover rejected counts, missing/zero/duplicate cases, incorrect samples/channels, stale source/root/format identity, NaN depth, reordered samples, altered output suffixes and missing validation. this is bounded correctness evidence, not complete Vulkan conformance or arbitrary-module soundness.

## reproduction and remaining scope

```text
python tools/theta/compile_agfx_graphics.py --multisample --rustgpu-source <owned-rust-gpu> --codegen-backend <rustc_codegen_spirv.dll> --output-dir <ignored-shaders>
cargo build -p agfx --bin multisample --locked
python tools/theta/run_agfx_multisample.py --executable <host-target>/debug/multisample.exe --sdk <Vulkan-SDK> --shader-dir <ignored-shaders> --output-dir <ignored-evidence> --review <current-pre-execution-review.json>
python -m unittest discover -s tools/theta -p test_agfx_multisample.py
```

repeat the build and native runner for Release using a separately reviewed executable. the runner requires NumPy and a current source/executable/module review record before native entry. unsupported samples fail explicitly. ignored `msaa-compile`, `msaa-review-debug/release.json`, `msaa-debug/release` and `msaa-regression-verification.json` retain exact source, compiler, payload, command, adapter, diagnostic and capture evidence.

Gaussian PLY parsing, covariance/conic math, compute/raster programs, source resolve behavior and complete original-HLSL comparisons remain required. sample shading, multisample storage, fixed-function resolve and general mip/array/cube/3D views are outside this checkpoint. T018/T024-T029 remain open, as do separate Linux and upstream acceptance requirements. no performance claim is made.
