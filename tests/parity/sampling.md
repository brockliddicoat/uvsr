# AGFX sampler and 2D sampling mapping

source pin: `AmelieHeinrich/agfx` at `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7`, MIT. [the source manifest](sampling-sources.json) pins eleven original shader/test/oracle files and six untouched source PNGs. all sixteen native cases pass the source and added structural oracles in both Debug and Release, with zero Khronos core/synchronization diagnostics. all sixteen corresponding readbacks match between hosts. strict byte equality remains four passes and twelve differences, recorded separately.

the [sampler owner](../../crates/agfx/src/vulkan/sampler.rs) preserves the source Vulkan filter, address, comparison, LOD, normalized-coordinate and transparent-black border behavior. `Default` matches the Cpp create-info, including zero-initialized Never comparison. the source test helper explicitly selects Always for ordinary sampling. the two APIs must not be confused. [A-009/A-010/A-015](../../docs/agfx-port-notes.md) record the ambiguous comparison sentinel, disabled anisotropy and unused extra bias. Rust checks LOD validity and sampler allocation capacity before native creation. full source global sampler handles remain unimplemented.

one [existing compute owner](../../crates/agfx/src/vulkan/compute.rs) now binds storage buffers0, storage images1, sampled images2 and samplers3 in ordinary descriptor set0. each entry declares exact counts. this is explicitly separate from the source global mutable-resource heap and actual NGAPI heaps. [textures](../../crates/agfx/src/vulkan/texture.rs) declare storage/sampled usage before allocation, query exact support and retain linear-filter capability. no required storage feature is added to sampled-only images. current views remain same-format2D, one mip/layer.

| source behavior | Rust case and input | oracle |
| --- | --- | --- |
| texture_ops.hlsl Pattern/main_write_cs | seed_cs writes64x64 RGBA8, ramps x/63 and y/63, checker tiles8x8 | independent complete UNORM interval image, checker and alpha |
| SamplerFilterNearest/Linear, C/Cpp/Ez | filter_nearest/filter_linear, source GPU seed, UV scale.25 offset.375 | original sampler_filter_nearest/linear.png |
| SamplerAddressModeRepeat/MirroredRepeat/ClampToEdge, C/Cpp/Ez | address_repeat/mirrored_repeat/clamp_to_edge, source GPU seed, linear UV scale3 offset-1 | original sampler_address_mode_* PNGs |
| Sample2D, C/Cpp/Ez | sample_2d, original CPU upload with integer truncation x*255/63 and y*255/63, linear UV scale.25 offset.375 | original sample_2d.png |
| Vulkan transparent-black clamp border | address_border, nearest UV scale3 offset-1 | independent complete coordinate/address and RGBA oracle. added Vulkan case, no original cross-backend border golden |
| sampler creation | all eight comparison enum values create/drop | native validation only. depth-comparison execution and global handle tests remain pending |

the source families comprise18 C/Cpp/Ez image registrations mapped onto six Rust behavior groups, rather than three redundant language wrappers. the general source API and Ez contracts still require their own mappings and implementation. source sampler handle validity/reuse is not established by this ordinary descriptor profile.

the [Rust shader](../../shaders/rust/texture_sampling.rs) preserves source16-byte seed and48-byte sample roots. the source/sampler/destination indices become0 in the explicit ordinary bindings. sample root offsets are0/4/8/12/16/20/24/32/40. normalized coordinates use `(pixel+.5)/64*scale+offset`, explicit LOD0, and local8x8x1 with8x8x1 groups. sample input generation remains different between the five GPU-seeded cases and CPU-uploaded Sample2D. every complete image is read back after synchronous completion. numerical image checks include alpha.

the [native executable](../../crates/agfx/src/bin/texture_sampling.rs) admits only the two source-reviewed, independently validated payload hashes in [U-020](../../UNSAFE.md#u-020-owned-samplers-and-source-texture-sampling). both optimization levels execute eight cases each. seventeen native rejection controls cover invalid LOD/usage and, per payload, missing/foreign samplers, foreign/uninitialized/missing-usage sampled images, comparison samplers on color images and missing storage usage. eight additional comparison creations do not imply depth-sampling parity. no invalid GPU access is submitted.

the [runner](../../tools/theta/run_agfx_sampling.py) checks current host/source/payload identities before Vulkan, requires SDK core/synchronization validation, exact16-case execution, native controls, completion order and fixed root hashes. it retains strict byte differences, then applies the original AGFX LDR-FLIP mean threshold0.05 and additional full-image structural and alpha checks. the [CPU oracle](../../tools/theta/flip_reference.cpp) compiles against the exact unchanged source FLIP header. it mirrors RunFlip: bytes divided by255, RGB only, no extra sRGB decode, default FLIP parameters, no HDR. this external test dependency has no graphics API and does not replace any Rust graphics implementation. its [builder](../../tools/theta/build_flip_reference.py) and [caller](../../tools/theta/source_flip.py) check header/source/compiler/executable identities and complete finite error maps. equal-image and black/white CPU controls produce mean0 and0.967345238. the original float32 reduction is retained when checking the metric against its map.

[Vulkan UNORM conversion](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-fpfixedconv) permits either neighboring integer and recommends nearest. the first added seed oracle incorrectly required nearest everywhere. the corrected independent oracle accepts floor/ceil of the known ramp while checking exact checker and alpha. each subsequent sample is checked over the entire image using independently computed integer coordinates, address modes and bilinear weights. the input is the separately validated seed readback, or the original integer-truncated CPU upload for Sample2D. the oracle never derives an expected sampled output from that output. this separates permitted conversion choices from wrong filters, addressing, channel order or missing writes. original goldens and source threshold are unchanged. the initial exact-only failed result is preserved.

| case | exact differing pixels, both opt levels | largest byte difference | source FLIP mean |
| --- | ---: | ---: | ---: |
| seed | 252 | 1 | added structural case |
| filter_nearest | 496 | 1 | 0.00539851561 |
| filter_linear | 3072 | 1 | 0.0254705474 |
| address_repeat | 252 | 1 | 0.0029903492 |
| address_mirrored_repeat | 252 | 1 | 0.00299708382 |
| address_clamp_to_edge | 0 | 0 | 0 |
| address_border | 0 | 0 | added structural case |
| sample_2d | 3007 | 1 | 0.0248968247 |

all twelve source FLIP comparisons pass0.05 and all sixteen structural/alpha checks pass. [ten evidence controls](../../tools/theta/test_agfx_sampling.py) reject missing or duplicate cases, wrong roots/completion/identities, invalid thresholds, alpha changes, wrong filters/address modes, out-of-interval texels and absent source oracles. exact-byte diagnostics continue to report their own failures.

```text
python tools/theta/compile_agfx_shader.py --sampling --rustgpu-source work/theta/upstream/rust-gpu --codegen-backend work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll --output-dir work/theta/evidence/full-port/sampling-compile
python tools/theta/run_agfx_sampling.py --executable work/theta/build/agfx-host/debug/texture_sampling.exe --sdk work/theta/tools/vulkan-sdk-1.4.357.0 --shader-dir work/theta/evidence/full-port/sampling-compile --flip-reference work/theta/evidence/full-port/flip-reference/flip_reference.exe --output-dir work/theta/evidence/full-port/sampling-vulkan-debug-final
```

use the Pillow-equipped Python for image comparisons. first build the CPU reference with `build_flip_reference.py --agfx-source SOURCE --output-dir work/theta/evidence/full-port/flip-reference`, from a Visual Studio developer environment on Windows or with a C++17 compiler on Linux. Linux compilation/execution has not been tested here. [E-042](../../docs/execution.md#e-042-2026-09-20-ported-samplers-and-source-sampling-oracles) owns the checkpoint outcome. no graphics-pipeline, full AGFX API/Ez, depth/mip/layer/format-view, Linux Vulkan, full ShaderToHuman example or actual-NGAPI integration result is implied by these bounded cases.
