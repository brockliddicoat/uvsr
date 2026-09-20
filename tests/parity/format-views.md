# AGFX texture format views

source: AGFX `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7`. [image creation](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx/agfx_vulkan.cpp#L1927) always enables mutable format. [agfxTextureViewCreate](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx/agfx_vulkan.cpp#L2567) chooses a requested view format or the image format, along with mip/layer ranges and descriptor slots. [AGFX MIT attribution](../../legal/licenses/AGFX-MIT.txt) applies.

this increment implements compatible whole-image format reinterpretation for the existing single-mip, single-layer2D texture. `Device::texture_with_views` accepts explicit storage, sampled and attachment formats. `TextureViewFormats::same` retains earlier behavior. the safe constructor accepts equal formats and RGBA8_UNORM/sRGB pairs, rejecting other reinterpretations. it does not complete the source's independent view-handle API, array/mip cases or depth-to-color view behavior. those remain in T024/T025.

one Texture owns the image, dedicated allocation and at most three native views. equal active formats share one view. partial creation failure and normal destruction release every distinct view once. the existing device lease, initialization, image identity, exclusive writes and synchronous completion remain unchanged. transfers use the base format. descriptors select storage or sampled views, and graphics validates and uses the attachment view's format. [U-024](../../UNSAFE.md#u-024-owned-format-views-and-conversion-controls) owns the boundary and caller review.

the constructor queries base transfer support and every active view's usage/filter support. it supplies an identical explicit format list, union usage and flags to the [image support query](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceImageFormatInfo2.html) and creation. only reinterpretation requests enable MUTABLE_FORMAT/EXTENDED_USAGE. each [view usage](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewUsageCreateInfo.html) contains only the roles assigned to that view's format. this permits an sRGB base with a UNORM storage view without claiming sRGB storage support. [A-014](../../docs/agfx-port-notes.md) records the source flag policy and the unmeasured optimization candidate.

## independent conversion checks

the fixed Rust shader has five explicit entries: storage write, sampled read, storage read, generated vertex and fragment color. it uses only Shader/VulkanMemoryModel, ordinary descriptors and a16-byte color root. no physical address or new compiler capability is needed. the private native caller validates exact payload and source identities before creating Device.

three configurations each execute storage and raster writes, at opt0 and opt3:

| configuration | base | storage | sampled and attachment |
| --- | --- | --- | --- |
| unorm | UNORM | UNORM | UNORM |
| srgb_views | UNORM | UNORM | sRGB |
| srgb_base | sRGB | UNORM | sRGB |

each8x8 case writes `[0.2,0.4,0.6,0.8]`, then reads all raw bytes, sampled float4s and UNORM storage float4s. independent expected bytes are `[51,102,153,204]` for storage/UNORM attachment writes and `[124,170,203,204]` for sRGB attachments. every byte must match. UNORM reads and alpha must agree within1e-7. sRGB RGB reads must re-encode to the exact expected byte using the independent [Khronos inverse EOTF](https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#TRANSFER_SRGB_INVEOTF), with finite values in[0,1]. maximum error against the ideal CPU decode is reported separately. this verifies preservation of the source's eight-bit information and one conversion, not Vulkan precision conformance or float-exact hardware decoding.

eight native controls per optimization level reject incompatible sampled/storage/attachment formats, empty usage, depth storage and three pipeline/view mismatches before recording. two Rust contracts cover format compatibility and disabled roles. ten Python controls reject stale/missing/duplicate cases, changed bytes, omitted or extra conversion, alpha errors, NaNs, incomplete readbacks and a decoded adjacent code even when its capture hash is updated.

Debug and Release each pass all12 cases and16 rejection controls with zero Khronos core/synchronization diagnostics. all36 corresponding byte/float readbacks match between hosts. the largest observed sRGB absolute error from ideal CPU decode is0.00045446163623663605. alpha and UNORM reads remain within2.4e-8. the first opt0 native run completed all six cases without diagnostics, but an initial arbitrary2e-4 global float threshold rejected sampled sRGB. that failed record is retained. the exact encoded roundtrip rule above replaces that assumption, and the unchanged first captures also pass it. neither source bytes, shader math nor native format selection changed.

## reproduction and limits

use the pinned toolchain, Vulkan SDK and a Python environment with NumPy. `compile_agfx_graphics.py --views` emits both optimization levels and independently validates them. its default raster profile remains unchanged. build the workspace's `texture_views` binary, review U-024's owner/caller and final modules, then persist a review JSON containing status `reviewed-before-dispatch`, the runner's complete `host_sources` map, executable SHA-256 and `payloads` keyed by `0` and `3`. the runner rejects stale or missing review identities before Vulkan entry.

```powershell
python tools/theta/compile_agfx_graphics.py --views --rustgpu-source work/theta/upstream/rust-gpu --codegen-backend work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll --output-dir <ignored-shader-dir>
python tools/theta/run_agfx_views.py --executable <host-target>/debug/texture_views.exe --sdk <Vulkan-SDK-root> --shader-dir <ignored-shader-dir> --output-dir <ignored-output-dir> --review <review.json>
python -m unittest discover -s tools/theta -p test_agfx_views.py
```

ignored `work/theta/evidence/full-port/views-*` retains exact source/module/host identities, original failed record, complete captures, numeric comparisons and Debug/Release regression evidence. complete AGFX view handles and subresources, ShaderToHuman Features/GaussianSplatting and actual NGAPI testbed integration remain required. no Linux execution, allocation savings or performance improvement is inferred.
