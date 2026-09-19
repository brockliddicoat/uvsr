# compiler prerequisite patches

these are tested changes from separately owned upstream checkouts. [sources.json](sources.json) pins each base, local verified commit, patch hash and controlling license. they are not published upstream contributions or proof of complete RustGPU support.

latest checkpoint: all 16 required pipeline probes pass with installed tools after the [heap metadata patches](#heap-metadata-and-dependency-retention). compiled-tools integration remains pending. earlier results below preserve the evidence at each incremental patch.

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

## compiled descriptor-heap tools

[spirv-tools-descriptor-heaps.patch](spirv-tools-descriptor-heaps.patch) updates the wrapper's SPIRV-Tools and SPIRV-Headers pins to SDK 1.4.357 sources, adds the extension grammars required by their generator, and tests heap assembly, validation, optimization and serialization through both tool implementations. the regression also requires rejection when `DescriptorHeapEXT` is absent. the wrapper retains its [MIT](../../legal/licenses/SPIRV-Tools-rs-MIT.txt) OR [Apache-2.0](../../legal/licenses/SPIRV-Tools-rs-APACHE.txt) terms. the native tools and headers remain pinned upstream submodules under their own licenses.

apply to an owned clean checkout at the manifest's exact base using `git apply --unidiff-zero --index --check <patch>` and `git apply --unidiff-zero --index <patch>`. then run `git submodule update --init`. generated tables are deliberately omitted from this repository. regenerate them in the owned upstream checkout before compiling:

```powershell
cargo +nightly-2026-07-03 run --locked -p generate -j 1
cargo +nightly-2026-07-03 test -p spirv-tools --all-features --locked -j 1
cargo +nightly-2026-07-03 fmt --all -- --check
```

Python and the installed SDK's `spirv-as`, `spirv-val`, `spirv-opt` and `spirv-dis` must be on PATH. the manifest records SHA-256 hashes of the four changed generated files with LF line endings. source patch application plus those verified outputs reproduces the recorded commit tree. retain the generator's output and the resolved lockfile with each reproduction.

the Windows run passed all eight integration tests, with zero failures or skips. four are new heap cases, and four are existing assembler, optimizer, validator and issue regressions. there are zero unit and documentation cases. formatting passed. compiled SPIRV-Tools is `v2026.3`, source `9a49b0883b9b635689a85b5647dbfcb223268151`. installed SDK tools are `v2026.3rc1`, source `b707790a`; these are distinct implementations/revisions, not byte-identical builds. this standalone wrapper result does not establish RustGPU integration or shader execution. [execution E-011](../../docs/execution.md#e-011-2026-09-19-enabled-compiled-descriptor-heap-tools) records the boundary.

## untyped pointers and static type operands

[spirt-untyped-pointers.patch](spirt-untyped-pointers.patch) builds on the SPIR-T grammar patch. it preserves optional global Data Type operands and instruction type IDs separately from runtime values, including traversal, transformation and emission. qptr preserves existing untyped globals and memory operations while still legalizing typed pointers. [rustgpu-untyped-pointers.patch](rustgpu-untyped-pointers.patch) builds on the RustGPU grammar/control patch, adds untyped storage-class constraints and retains untyped globals in entry-point interface collection. both retain their existing upstream licenses above.

apply these patches after their preceding patches, at the exact manifest bases, using the same `--unidiff-zero --index` flags. rerun the SPIR-T tests and installed-tools RustGPU command above. SPIR-T passes three grammar tests and five new structural cases, with no skips. the new cases cover global/local placement, optional Data Type and initializer, type/value operand order, qptr preservation and a typed Private base. one negative case preserves qptr's existing whole-buffer input diagnostic. typed whole-buffer inputs remain unsupported by that optional pass, and the fixture tests are structural rather than GPU evidence.

the RustGPU suite now reports 29 passed, three failed and four existing macOS-only skips. all four stages pass for logical, physical and untyped stores. native-heap parsing passes, while its remaining three stages fail on ID decorations. the required matrix is **13/16 passing, zero ignored**, so the overall command still fails intentionally. these patches do not establish complete untyped-operation coverage, physical Rust pointer code generation or shader execution. [execution E-012](../../docs/execution.md#e-012-2026-09-19-preserved-untyped-pointer-pipeline-controls) records the exact boundary and prior failed attempts.

## heap metadata and dependency retention

[spirt-heap-metadata.patch](spirt-heap-metadata.patch) preserves `OpDecorateId`, `OpMemberDecorateIdEXT` and `OpConstantSizeOfEXT` through lowering, dependency traversal, transformation, printing and serialization. decoration operands are resolved before their target, as required by SPIR-V. regressions cover descriptor size/stride/member offset, ID renumbering and definition order, duplicate-decoration diagnostics and rejected forward references.

[rustgpu-heap-metadata.patch](rustgpu-heap-metadata.patch) retains constants referenced only by live ID decorations and removes decorations whose targets are dead. it also includes member ID offsets in type deduplication, preventing descriptor structs with different offsets from being merged. three direct regressions cover these cases. RustGPU's SPIR-T validator recognizes the new size constant representation.

apply each after its untyped-pointer patch at the exact manifest base, with `--unidiff-zero --index`. both retain their existing MIT OR Apache-2.0 terms. rerun the SPIR-T and installed-tools commands above. the verified Windows result is three grammar plus eight structural SPIR-T tests passing, and RustGPU **35 passed, zero failed, four existing macOS-only ignores**. all 16 required diagnostic cases pass, with no skips, including default/qptr linking and performance optimization for logical stores, physical stores, untyped stores and native resource/sampler heaps.

RustGPU full formatting passes. SPIR-T formatting remains limited to changed ranges, with the prior unrelated full-format/Clippy failures still open. compiled-tools integration and Rust source code generation remain separate pending gates. no fixture was dispatched. [execution E-014](../../docs/execution.md#e-014-2026-09-19-preserved-native-heap-metadata-through-the-linker) records exact commits, failed attempts and the boundary of this result.
