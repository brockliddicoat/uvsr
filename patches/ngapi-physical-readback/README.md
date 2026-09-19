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
