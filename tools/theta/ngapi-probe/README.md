# native NGAPI baseline

E-007's T004 diagnostic used unchanged NGAPI `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38`. this small C++ launcher is an actual-host baseline, not the Rust AGFX port. the second target compiles NGAPI's existing `tests/command_context_test.cpp` without copying or modifying it. the later Rust consumer below uses a separately recorded host prerequisite.

from the repository root, with an isolated NGAPI checkout and extracted Vulkan SDK 1.4.357.0:

```powershell
$thetaRoot = (Get-Location).Path
$sdk = "$thetaRoot/work/theta/tools/vulkan-sdk-1.4.357.0"
cmake -S tools/theta/ngapi-probe -B work/theta/build/ngapi-owned `
  -G "Visual Studio 17 2022" -A x64 `
  "-DNGAPI_SOURCE_DIR=$thetaRoot/work/theta/upstream/NoGraphicsAPI" `
  "-DVulkan_INCLUDE_DIR=$sdk/Include" "-DVulkan_LIBRARY=$sdk/Lib/vulkan-1.lib"
cmake --build work/theta/build/ngapi-owned --config Debug `
  --target theta_ngapi_capabilities theta_ngapi_commands --parallel 1
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:VK_LAYER_PATH = "$sdk/Bin"
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
& work/theta/build/ngapi-owned/Debug/theta_ngapi_capabilities.exe
& work/theta/build/ngapi-owned/Debug/theta_ngapi_commands.exe
```

inspect each exit code. `77` is unsupported and is not a pass. the capability record counts zero shader cases. the upstream command-context test covers allocated GPU heaps, texture/sampler descriptors, placed textures, timeline completion, timestamp readback/capacity, and context reuse. it is not a Rust shader or image oracle. capture both streams and treat Vulkan validation errors as failure even if a native test returns zero.

Debug NGAPI enables its debug callback when available. `VK_LOADER_DEBUG=layer` can separately verify that the Khronos layer is inserted into both instance and device call chains. keep the environment changes process-local.

Windows headless device creation takes the default null window. presentation instead needs an HWND, appropriate Win32 surface/swapchain extensions, and the source thread-affinity/lifetime contract. presentation and Linux execution were not tested by this baseline.

SDK setup can extract the official installer archives into the ignored tools directory without changing the system loader or registry. obtain the exact filename and SHA-256 from LunarG's [SDK file manifest](https://vulkan.lunarg.com/sdk/files.json). the verified Windows file is `vulkansdk-windows-X64-1.4.357.0.exe`, SHA-256 `81f474711e9042f4cd22b31b2f7a8870db2e428b21586fb43dd80150be97310d`. retain its original license material with the extracted local SDK. no SDK binary is tracked here.

## Rust physical address readback

[physical_readback.rs](physical_readback.rs) runs in actual NGAPI's compute pipeline with `computeMain`, a 16-byte push-data root and real source/destination allocation addresses. its transparent PhysicalPtr<u32> root fields retain the native u64 layout. unsafe library calls read one u32, write the value plus seven and both address words, and leaves a fourth destination word untouched. the host also reads back the entire source allocation. three seeds include wrapping u32 arithmetic. each case submits one invocation and waits for completion before CPU inspection or reuse.

[U-002](../../../UNSAFE.md#u-002-actual-ngapi-physical-u32-readback) owns the caller and [U-003](../../../UNSAFE.md#u-003-physicalptr-shader-library-access) owns the library. they record the access, allocation, alignment, bounds, aliasing, visibility and lifetime contract. the C++ diagnostic is an external consumer, not the finished Rust host. no unsafe entry wrapper claims to enforce the host's obligations.

prepare the [compiler prerequisites](../../../patches/rustgpu-prerequisites/README.md) through E-022 and the separate [NGAPI feature patch](../../../patches/ngapi-physical-readback/README.md). the verified RustGPU commit is `ff7883bd22981f0a67ffddb19d159f09542a14bd`, with the E-019 patched compiled tools. use its pinned `nightly-2026-07-03` and the owned Cargo source overrides. do not reuse libraries from the ordinary 32-bit target. first build and test the physical64 sysroot from the owned RustGPU checkout:

```powershell
$thetaRoot = (Get-Location).Path
$env:CARGO_BUILD_JOBS = '1'
$env:CARGO_TARGET_DIR = "$thetaRoot/work/theta/build/rust-gpu"
$env:RUSTUP_TOOLCHAIN = 'nightly-2026-07-03'
Push-Location work/theta/upstream/rust-gpu
cargo run --release --locked -p compiletests --no-default-features `
  --features use-compiled-tools -j1 -- --target-env vulkan1.3-physical64 physical_storage
Pop-Location
if ($LASTEXITCODE -ne 0) { throw 'compiler prerequisite failed' }
```

with that same nightly's `rustc` and SDK `spirv-val`/`spirv-dis` on PATH, compile the two consumer variants. the Windows script uses only the uniquely identified libraries from the prepared target. it retains argument arrays, compiler/library/source/module hashes, capabilities and disassembly. it invalidates the old generated include before rebuilding, validates each module and checks the reviewed profile before embedding it.

```powershell
python tools/theta/ngapi-probe/compile_readback.py `
  --rustgpu-source work/theta/upstream/rust-gpu `
  --codegen-backend work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll `
  --output-dir work/theta/build/ngapi-physical-shaders
if ($LASTEXITCODE -ne 0) { throw 'shader compilation failed' }
$sdk = "$thetaRoot/work/theta/tools/vulkan-sdk-1.4.357.0"
cmake -S tools/theta/ngapi-probe -B work/theta/build/ngapi-owned `
  -G "Visual Studio 17 2022" -A x64 `
  "-DNGAPI_SOURCE_DIR=$thetaRoot/work/theta/upstream/NoGraphicsAPI" `
  "-DTHETA_SHADER_DIR=$thetaRoot/work/theta/build/ngapi-physical-shaders" `
  "-DVulkan_INCLUDE_DIR=$sdk/Include" "-DVulkan_LIBRARY=$sdk/Lib/vulkan-1.lib"
if ($LASTEXITCODE -ne 0) { throw 'native configure failed' }
foreach ($configuration in @('Debug', 'Release')) {
  cmake --build work/theta/build/ngapi-owned --config $configuration `
    --target theta_ngapi_physical_readback theta_ngapi_capabilities theta_ngapi_commands --parallel 1
  if ($LASTEXITCODE -ne 0) { throw 'native build failed' }
  python tools/theta/ngapi-probe/run_readback.py `
    --executable "work/theta/build/ngapi-owned/$configuration/theta_ngapi_physical_readback.exe" `
    --sdk $sdk --shader-dir work/theta/build/ngapi-physical-shaders `
    --output-dir "work/theta/evidence/physical-readback-$configuration"
  if ($LASTEXITCODE -ne 0) { throw 'actual NGAPI consumer failed' }
}
python -m unittest discover -s tools/theta/ngapi-probe -p test_records.py -v
```

the runner enables explicit Khronos core/synchronization validation with process-local settings, disables incidental implicit layers, captures both streams, and requires instance/device layer insertion. it checks all six unique GPU case IDs plus the CPU controls, exact hashes and readback values. warnings/errors from native validation fail the result. known loader notices about deliberately disabled implicit layers remain in the log. interruption, unsupported devices and incomplete counts are failures of this required gate, not shader passes. `--self-test` on the native executable runs only its 16 range/oracle controls, without creating a device.

[E-022](../../../docs/execution.md#e-022-2026-09-19-tested-the-physical-pointer-library) passes six library cases each in Debug and Release, with both Rust opt0 and opt3 modules. these tiny driver allocations had zero upper address words. the records explicitly report `nonzero_high_address_bits_covered: false`. compiler/native-CPU high-bit transport tests remain separate evidence. do not allocate excessive GPU memory or fabricate a high address to disguise this coverage limit.

this proves scalar physical reads/writes and a two-u64 root through NGAPI. the modules contain no resource/sampler heap lookup, so native descriptor shader support, textured cube, aggregate runtime, complete pointer parity, direct Rust Vulkan host, presentation, Linux and full upstream CI remain open. the [compute pipeline rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkComputePipelineCreateInfo.html) permit NGAPI's heap pipeline with null layout and no conventional resource bindings. the [Vulkan 1.2 feature definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan12Features.html) distinguishes the memory-model feature from device-scope support.

## native resource/sampler heap readback

[native_heap_sample.rs](native_heap_sample.rs) uses actual native heaps and dynamic uniform indices from a 16-byte root. [the native caller](native_heap_sample.cpp) initializes all four slots per heap, then tests resource slots 1/3 with sampler slots 2/3. two 2 x 2 RGBA8 images and nearest repeat/clamp addressing at UV (1.25, 0.25) produce four distinct exact colors. the Rust shader doubles the sampled Vec4 and writes it through PhysicalPtr. the host checks all output bits, a 16-byte destination guard and unchanged source textures after barriers and timeline completion.

apply the compiler prerequisites through the ID-constant/debug-stripping patches. verified source pins and observed results belong to [E-024](../../../docs/execution.md#e-024-2026-09-19-executed-native-heap-rust-shaders). NGAPI retains the same separate physical-readback feature patch. after preparing the physical64 sysroot above, use the owned nightly and SDK environment:

```powershell
python tools/theta/ngapi-probe/compile_readback.py --fixture native_heap_sample `
  --rustgpu-source work/theta/upstream/rust-gpu `
  --codegen-backend work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll `
  --output-dir work/theta/build/ngapi-heap-shaders
if ($LASTEXITCODE -ne 0) { throw 'heap shader compilation failed' }
cmake -S tools/theta/ngapi-probe -B work/theta/build/ngapi-owned `
  -G "Visual Studio 17 2022" -A x64 `
  "-DNGAPI_SOURCE_DIR=$thetaRoot/work/theta/upstream/NoGraphicsAPI" `
  "-DTHETA_HEAP_SHADER_DIR=$thetaRoot/work/theta/build/ngapi-heap-shaders" `
  "-DVulkan_INCLUDE_DIR=$sdk/Include" "-DVulkan_LIBRARY=$sdk/Lib/vulkan-1.lib"
if ($LASTEXITCODE -ne 0) { throw 'native configure failed' }
foreach ($configuration in @('Debug', 'Release')) {
  cmake --build work/theta/build/ngapi-owned --config $configuration `
    --target theta_ngapi_native_heap_sample --parallel 1
  if ($LASTEXITCODE -ne 0) { throw 'native build failed' }
  python tools/theta/ngapi-probe/run_readback.py --fixture native_heap_sample `
    --executable "work/theta/build/ngapi-owned/$configuration/theta_ngapi_native_heap_sample.exe" `
    --sdk $sdk --shader-dir work/theta/build/ngapi-heap-shaders `
    --output-dir "work/theta/evidence/native-heap-$configuration"
  if ($LASTEXITCODE -ne 0) { throw 'native heap consumer failed' }
}
```

the runner requires eight unique GPU cases, one CPU-control record, current source/payload hashes, exact bytes and native validation insertion without diagnostics. the native `--self-test` runs 74 CPU checks without creating a device. all 14 Python record tests run with the earlier unittest command. [U-005](../../../UNSAFE.md#u-005-actual-ngapi-native-heap-sample) records the reviewed boundary. this compute fixture leaves divergent indices, unequal descriptor sizes, nonzero high address bits, additional resources/stages, cube, direct Rust Vulkan and Linux coverage open.
