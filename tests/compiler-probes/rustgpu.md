# RustGPU pipeline probes

[the diagnostic test module](../../tools/theta/rustgpu-instruction-probes.rs) runs the four adjacent assembly fixtures through the actual pinned RustGPU parser, SPIR-T serializer and compiler linker. it uses existing upstream test helpers and no AGFX or NGAPI dependency. it forbids unsafe Rust and dispatches no shader.

each fixture has four stable test suffixes: `parser_roundtrip`, `spirt_roundtrip`, `linker_default` and `linker_qptr`. both linker variants enable storage-class inference and structurization, then validate the unoptimized result and the performance-optimized result. each stage checks required instructions and reparses serialized output. the required denominator is 16 per tools configuration, with no ignored cases.

## reproduce in a separate checkout

use RustGPU `e6394e08eb3356083b12f732a01906f8e49f7c4a`, its `nightly-2026-07-03` toolchain including rustc-dev, rust-src and llvm-tools, and a separately owned writable checkout. the installed-tools run needs Vulkan SDK 1.4.357.0 `Bin` on PATH. keep generated output outside tracked source. the commands below add only diagnostic tests, not compiler semantics.

```powershell
$theta = '<absolute Theta checkout>'
$rustgpu = '<absolute separate RustGPU checkout>'
$probeTests = Join-Path $rustgpu 'crates/rustc_codegen_spirv/src/linker/test'
git -C $rustgpu apply --check (Join-Path $theta 'tools/theta/rustgpu-probe-registration.patch')
if ($LASTEXITCODE -ne 0) { throw 'probe registration does not apply cleanly' }
$probeFiles = @('instruction_compatibility.rs', 'logical_store.spvasm', 'physical_store.spvasm', 'untyped_store.spvasm', 'descriptor_heaps.spvasm')
foreach ($probeFile in $probeFiles) {
    if (Test-Path -LiteralPath (Join-Path $probeTests $probeFile)) { throw "probe destination already exists: $probeFile" }
}
New-Item -ItemType Directory -Path $probeTests -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $theta 'tools/theta/rustgpu-instruction-probes.rs') -Destination (Join-Path $probeTests 'instruction_compatibility.rs')
foreach ($fixture in @('logical_store', 'physical_store', 'untyped_store', 'descriptor_heaps')) {
    Copy-Item -LiteralPath (Join-Path $theta "tests/compiler-probes/$fixture.spvasm") -Destination (Join-Path $probeTests "$fixture.spvasm")
}
git -C $rustgpu apply (Join-Path $theta 'tools/theta/rustgpu-probe-registration.patch')
if ($LASTEXITCODE -ne 0) { throw 'probe registration failed' }
Set-Location -LiteralPath $rustgpu
cargo test -p rustc_codegen_spirv --release --locked --no-default-features --features use-installed-tools -j 1 instruction_compatibility -- --test-threads=1 -Z unstable-options --format=json
cargo test -p rustc_codegen_spirv --release --locked --no-default-features --features use-compiled-tools -j 1 instruction_compatibility -- --test-threads=1 -Z unstable-options --format=json
```

run the two configurations sequentially and capture each exit, JSON stdout and stderr separately. the compiled-tools feature takes precedence over installed tools, so that run exercises the bundled C++ library even with SDK tools on PATH. a failing test is a compatibility failure, not a pass or hardware skip. the JSON suite result retains the full required count. preserve baseline failures before applying candidate compiler/dependency patches.

these fixtures are fully typed SPIR-V tool inputs, rather than Rust source compiletests. they expose pipeline assumptions and the first failing stage. they do not establish raw Rust pointer semantics, a shader-library API, GPU correctness, or upstream acceptance. [execution](../../docs/execution.md) owns observed results and exact source/configuration identities.
