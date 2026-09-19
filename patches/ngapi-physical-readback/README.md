# NGAPI physical-readback host prerequisite

the [four-line patch](ngapi-rust-features.patch) requires and enables `shaderInt64` and `vulkanMemoryModel` in NGAPI's existing device-selection/creation path. the [source manifest](source.json) pins the unmodified base, verified local commit, reconstructed tree and patch hash. source retains Sebastian Aaltonen's complete [MIT license](../../legal/licenses/NoGraphicsAPI-MIT.txt).

this is a separate experimental NGAPI profile for the actual [Rust readback consumer](../../tools/theta/ngapi-probe/README.md#rust-physical-address-readback). its emitted module requires Int64, VulkanMemoryModel and PhysicalStorageBufferAddresses. the original NGAPI already enables bufferDeviceAddress. neither VulkanMemoryModelDeviceScope nor another optional capability is added. the change deliberately narrows device eligibility for this fixture instead of adding a public feature-negotiation API or implying every physical address needs Int64 arithmetic.

apply only to a separately owned, clean NGAPI checkout at `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38`:

```powershell
$ngapiPatch = (Resolve-Path patches/ngapi-physical-readback/ngapi-rust-features.patch).Path
git -C work/theta/upstream/NoGraphicsAPI apply --check $ngapiPatch
if ($LASTEXITCODE -ne 0) { throw 'NGAPI prerequisite does not apply' }
git -C work/theta/upstream/NoGraphicsAPI apply $ngapiPatch
if ($LASTEXITCODE -ne 0) { throw 'NGAPI prerequisite failed' }
```

run from this repository's root. no RustGPU patch depends on NGAPI or this file.

[E-021](../../docs/execution.md#e-021-2026-09-19-executed-rust-physical-readback-through-ngapi) records the actual Windows Debug/Release tests, limitations and exact shader identity. the change has not been submitted to NGAPI upstream. it changes neither device-address allocation nor descriptor-heap/pipeline/root-data implementation.


## divergent sampled-image access

apply [ngapi-divergent-sampling.patch](ngapi-divergent-sampling.patch) after the physical-readback patch. [divergent-source.json](divergent-source.json) pins this separate increment, its prerequisite and reconstructed tree. the two added lines query/require and enable `shaderSampledImageArrayNonUniformIndexing` in the same fixed experimental profile. there is no public negotiation API or fallback.

```powershell
$ngapiPatch = (Resolve-Path patches/ngapi-physical-readback/ngapi-divergent-sampling.patch).Path
git -C work/theta/upstream/NoGraphicsAPI apply --check $ngapiPatch
if ($LASTEXITCODE -ne 0) { throw 'divergent NGAPI prerequisite does not apply' }
git -C work/theta/upstream/NoGraphicsAPI apply $ngapiPatch
if ($LASTEXITCODE -ne 0) { throw 'divergent NGAPI prerequisite failed' }
```

[E-026](../../docs/execution.md#e-026-2026-09-19-tested-divergent-native-heap-access) records the four-lane actual consumer and unchanged scalar/uniform regression gates. native heaps permit non-uniform indexing by default, while the [Vulkan SPIR-V environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html) still requires the sampled-image capability and its device feature for this artifact. this patch is not part of the generic RustGPU contribution and has not been published to NGAPI upstream.

## storage-image profile

apply [ngapi-storage-images.patch](ngapi-storage-images.patch) after the divergent-sampling patch, using `git apply --unidiff-zero`. [storage-source.json](storage-source.json) pins its base, reconstructed tree, exact hash and MIT license. the two added lines query/require and enable shaderStorageImageArrayNonUniformIndexing for the fixture's declared StorageImageArrayNonUniformIndexing capability, as required by the [Vulkan feature contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDescriptorIndexingFeatures.html). this preserves the fixed experimental profile, with no new negotiation API or fallback. it does not claim that every uniform storage-image shader needs this feature.

[E-032](../../docs/execution.md#e-032-2026-09-19-executed-native-storage-image-operations) records the actual storage-image cases. no descriptor writing, image allocation, pipeline, root or command implementation changed. this prerequisite remains separate from RustGPU and has not been published upstream.
