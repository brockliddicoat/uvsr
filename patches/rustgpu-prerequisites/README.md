# compiler prerequisite patches

these are tested changes from separately owned upstream checkouts. [sources.json](sources.json) pins each base, local verified commit, patch hash and controlling license. they are not published upstream contributions or proof of complete RustGPU support.

## rspirv untyped globals

[rspirv-untyped-global.patch](rspirv-untyped-global.patch) fixes the binary loader's placement of module-scope `OpUntypedVariableKHR`. the regression serializes and parses globals with and without the optional Data Type operand, and a function-local variable. it checks exact word round-trip and placement. this is a loader test, not shader validation or execution.

the one-line placement fix also exists in the pre-existing [upstream PR #264](https://github.com/gfx-rs/rspirv/pull/264). this patch was developed independently before finding that PR. treat the upstream work as a prerequisite to coordinate with, not a new contribution to duplicate. the added regression is local work. all patch code and context retain rspirv's [Apache-2.0 license](../../legal/licenses/rspirv-Apache-2.0.txt).

apply only to an owned checkout at the exact source pin:

```powershell
$theta = '<absolute Theta checkout>'
$rspirv = '<absolute separate rspirv checkout>'
if ((git -C $rspirv rev-parse HEAD) -ne '8afc3d0ac8e158128cd1410bb2e4b4c26ab11bb4') { throw 'wrong rspirv revision' }
$patch = Join-Path $theta 'patches/rustgpu-prerequisites/rspirv-untyped-global.patch'
git -C $rspirv apply --unidiff-zero --check $patch
if ($LASTEXITCODE -ne 0) { throw 'patch does not apply' }
git -C $rspirv apply --unidiff-zero $patch
if ($LASTEXITCODE -ne 0) { throw 'patch failed' }
Set-Location -LiteralPath $rspirv
cargo +nightly-2026-07-03 generate-lockfile
if ($LASTEXITCODE -ne 0) { throw 'lockfile generation failed' }
cargo +nightly-2026-07-03 test -p rspirv --locked -j 1
if ($LASTEXITCODE -ne 0) { throw 'rspirv tests failed' }
```

the pinned upstream repository does not track a lockfile. record the locally resolved lockfile and toolchain with each run. our Windows run passed 82 unit tests, one binary-fixture test and six documentation tests, with zero failures or ignored tests. the existing [RustGPU pipeline probes](../../tests/compiler-probes/rustgpu.md), using this local rspirv and its matching `spirv` crate, then passed all four parser cases. SPIR-T and RustGPU inference still fail later. [execution E-009](../../docs/execution.md#e-009-2026-09-19-repaired-untyped-global-loading) records that boundary.

## modern grammar and concrete interfaces

[spirt-modern-grammar.patch](spirt-modern-grammar.patch) updates SPIRV-Headers to `29981f65241605e08b0ede4cfeb999fe3b723c6a` and accommodates its aliases, provisional metadata, operand names, numeric ranges and optional operands. full-word enum values use sparse storage. initialization errors no longer format an operand kind through the same lazy grammar being initialized. the patch includes grammar regressions and preserves [SPIR-T's MIT](../../legal/licenses/SPIR-T-MIT.txt) and [Apache-2.0](../../legal/licenses/SPIR-T-APACHE.txt) license choices.

[rustgpu-grammar-controls.patch](rustgpu-grammar-controls.patch) adapts RustGPU's grammar lookups and retains already-concrete entry-point interface IDs during specialization. generic interfaces still follow the existing instance logic. it preserves [RustGPU's MIT](../../legal/licenses/RustGPU-MIT.txt) and [Apache-2.0](../../legal/licenses/RustGPU-APACHE.txt) license choices.

start from fresh, separately owned checkouts at the manifest's SPIR-T and RustGPU base commits. apply each patch with `git apply --unidiff-zero --index --check <patch>`, then `git apply --unidiff-zero --index <patch>`. `--index` deliberately stages these source changes and SPIR-T's submodule pin, so use a clean index. run `git submodule update --init` in SPIR-T after application. apply the rspirv patch above, and install the existing diagnostic module with the [pipeline recipe](../../tests/compiler-probes/rustgpu.md).

create an ignored Cargo configuration with absolute paths to these owned sources:

```toml
[patch.crates-io]
rspirv = { path = '<rspirv checkout>/rspirv' }
spirv = { path = '<rspirv checkout>/spirv' }
spirt = { path = '<SPIR-T checkout>' }
```

in RustGPU, run `cargo update --config <configuration> -p rspirv -p spirt` once and retain the resolved lockfile. the tested override changed only those three package sources. with the pinned nightly and SDK tools on PATH, run:

```powershell
cargo test --config '<absolute configuration>' -p rustc_codegen_spirv --release --locked --no-default-features --features use-installed-tools -j 1 -- --test-threads=1 -Z unstable-options --format=json
```

in SPIR-T, run `cargo +nightly-2026-07-03 test --locked -j 1`. all three grammar unit tests passed. the RustGPU run had 26 passed, six failed and four existing macOS-only skips. its 16 diagnostic cases comprise 10 passes and six failures, with no skips. all logical and physical-store stages pass, including optimization after both linker modes. untyped and native-heap parsing passes, but the later inference and IR stages remain unsupported. the run intentionally returns failure until those requirements are implemented.

RustGPU's full formatting check and formatting of the four changed SPIR-T files pass. SPIR-T's whole-repository formatting check reports unchanged legacy files, and its strict Clippy check stops in unchanged `build.rs` on `unnecessary_map_or`. those gates remain failed, not waived or claimed passed. these patches establish grammar/control progress only, with no shader execution or complete upstream CI claim. [execution E-010](../../docs/execution.md#e-010-2026-09-19-restored-logical-and-physical-pipeline-controls) records the evidence.
