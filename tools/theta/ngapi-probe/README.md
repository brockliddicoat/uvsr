# native NGAPI baseline

T004 diagnostic tools consume unchanged NGAPI `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38`. this small C++ launcher is an actual-host baseline, not the Rust AGFX port. the second target compiles NGAPI's existing `tests/command_context_test.cpp` without copying or modifying it.

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
