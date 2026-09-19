# compiler prerequisite patches

these are tested changes from separately owned upstream checkouts. [sources.json](sources.json) pins each base, local verified commit, patch hash and controlling license. they are not published upstream contributions or proof of complete RustGPU support.

latest checkpoint: [typed pointer operations](#typed-pointer-operations) adds address comparisons, wrapping offsets and explicit rejection controls. it retains the [native optimizer prerequisite](#qptr-memory-operands-and-volatile-loads) for volatile effects. the original 16 probes passed both tool configurations at the [two-configuration gate](#two-configuration-compiler-gate). the new volatile regression requires patched compiled tools. earlier results below preserve the evidence at each incremental patch.

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

## two-configuration compiler gate

the exact E-014 compiler/SPIR-T candidate also passes with the E-011 compiled tools. add the two wrapper paths to the existing ignored Cargo patch configuration:

```toml
spirv-tools = { path = '<spirv-tools-rs checkout>/spirv-tools' }
spirv-tools-sys = { path = '<spirv-tools-rs checkout>/spirv-tools-sys' }
```

resolve and inspect the local lockfile before running with `--locked`. the verified lock retained all package versions and dependency edges, changing only the two wrapper source/checksum entries relative to the installed-tools candidate. an automatic targeted update proposed unrelated dependency-edge changes, which were discarded. retain the exact resolved lock and configuration with run evidence.

```powershell
cargo test --config '<absolute configuration>' -p rustc_codegen_spirv --release --locked --no-default-features --features use-compiled-tools -j 1 -- --test-threads=1 -Z unstable-options --format=json
```

the local run additionally used `--offline` after dependencies were cached. it passed **35 tests, zero failures, four existing macOS-only ignores**. all 16 required probes passed with no skips. T005/H07's tool gate is complete, while Rust source compiletests, native execution and full upstream CI remain separate work. the installed and compiled tools have the distinct source identities recorded above. [E-015](../../docs/execution.md#e-015-2026-09-19-completed-the-two-configuration-pipeline-gate) owns the combined conclusion. a directory-local attribute keeps exported patch bytes and manifest hashes unchanged across Windows and Unix checkouts.

## explicit Rust pointer-width foundation

[rustgpu-physical64-abi.patch](rustgpu-physical64-abi.patch) applies after the RustGPU heap-metadata patch at the manifest base. it adds an opt-in `-physical64` Vulkan target with eight-byte Rust pointers and `usize`, exact client/backend target-JSON checks, upstream documentation, and reviewed compiletest controls. ordinary targets keep their four-byte ABI. capabilities alone do not change layouts. this patch does not yet enable physical memory operations or integer-pointer casts.

the source cases prove sizes/alignment, nested raw-pointer-containing layouts, explicit `u64` address fields, high-bit integer transport through `usize`, `u32` overflow/wrapping, truncation and mixed-width comparison. the cast control still expects the existing rejection on both targets and never dereferences an address. the harness now preserves hyphenated target identity in test selectors, using underscores in `only`/`ignore` directives. otherwise `compiletest_rs` conflates the two ABIs.

use the same compiled-tools overrides and local diagnostic-module installation as above:

```powershell
cargo test --release --locked -p rustc_codegen_spirv-types -p rustc_codegen_spirv --no-default-features --features use-compiled-tools -j 1 -- --test-threads=1 -Z unstable-options --format=json
cargo run --release --locked -p compiletests --no-default-features --features use-compiled-tools -j 1 -- --target-env vulkan1.3,vulkan1.3-physical64 physical_storage
cargo fmt --all -- --check
```

the full selected unit gate passed **37 compiler tests and three shared-type tests**, zero failures, with four pre-existing macOS-only ignores. all 16 instruction probes still passed. the source matrix passed four case/target pairs: the width-specific layout and cast rejection on each target. each target intentionally excludes the other ABI's layout case, and 331 unrelated compiletests are filtered. expected output was manually reviewed before blessing, then the complete selected matrix passed without blessing. the run used `--offline` after caching dependencies.

this is compiler/layout evidence on Windows, not GPU execution, complete upstream CI, raw-pointer parity or a completed physical-address API. [E-016](../../docs/execution.md#e-016-2026-09-19-proved-explicit-rust-pointer-width-layouts) records failed setup/selector attempts and the selected scope. the patch retains MIT OR Apache-2.0 terms and introduces no unsafe operation.

## physical address conversions

[spirt-physical-addresses.patch](spirt-physical-addresses.patch) preserves concrete PhysicalStorageBuffer types, conversions, access chains and function arguments through qptr. logical pointer legalization remains active. [rustgpu-physical-casts.patch](rustgpu-physical-casts.patch) enables the hybrid addressing model on explicit `-physical64` Vulkan targets, constrains raw address conversions to physical storage, preserves physical function parameters/returns, emits conservative alias decorations on final declarations, and reports incompatible storage classes through rustc. it also corrects integer extension to use rustc's source-signedness argument, with a separate unsigned intermediate where SPIR-V requires it.

apply after the preceding patches at the exact manifest bases, using `--unidiff-zero --index`. the source pins and licenses remain in [sources.json](sources.json). optional assembly diagnostics use the updated, version-independent [registration recipe](../../tests/compiler-probes/rustgpu.md). keep RustGPU's local dependency overrides out of a standalone SPIR-T run so its unchanged lockfile remains valid.

run SPIR-T's `cargo test --locked -j 1`, then the full compiler/shared-type unit command and two-target source command from the preceding section. additionally run:

```powershell
cargo run --release --locked -p compiletests --no-default-features --features use-compiled-tools -j 1 -- --target-env vulkan1.3 storage_class const-int-cast const-narrowing-cast const-from-cast u8-const-cast
```

[E-017](../../docs/execution.md#e-017-2026-09-19-lowered-physical-address-conversions) owns the complete counts, failed approaches and verification limits. the six required Rust source pairs now include physical casts, mixed-storage rejection and signed/unsigned integer controls. source oracles were reviewed before acceptance. alias tests assert the compiler's conservative raw-pointer policy, separately from instruction validity. [SPIR-V 1.6 revision 8](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#Aliasing) permits absent decorations, but requires alias markings for memory declarations that may alias. accepted absence is therefore not a validator defect. `Restrict` and `RestrictPointer` add no effect under the Vulkan memory model.

this checkpoint does not establish aligned raw memory access, raw-pointer operation parity, aggregate pointer storage, atomics, reference validity or GPU execution. physical instructions remain explicit through qptr, so this does not close memory-operand lowering/lifting requirement P07. T010-T013 and actual NGAPI shader execution remain open. all added Rust tests forbid unsafe, and the source probes never dereference addresses.

## aligned physical accesses

[rustgpu-physical-access.patch](rustgpu-physical-access.patch) follows the cast patch at the exact manifest base. apply with `--unidiff-zero --index`. it preserves rustc's known load/store alignment on physical64, removes only Aligned from logical accesses after inference, and retains other memory flags and scope IDs. it also fixes the panic decompiler's alignment handling, scope IDs in memory signatures, and mixed-width index merging exposed by the new safe array regression.

run the full compiler/shared-type unit command and the two-target `physical_storage` command above. include the existing panic cases in the adjacent regression command:

```powershell
cargo run --release --locked -p compiletests --no-default-features --features use-compiled-tools -j 1 -- --target-env vulkan1.3 storage_class const-int-cast const-narrowing-cast const-from-cast u8-const-cast panic
```

[E-018](../../docs/execution.md#e-018-2026-09-19-compiled-aligned-physical-accesses) owns exact counts and limits. the scalar raw-access source has an explicitly unsafe entry and a complete caller contract, registered as [U-001](../../UNSAFE.md#u-001-compile-only-physical-u32-access). compilation never dispatches a synthetic address. the panic fixture forbids unsafe and retains the upstream formatter's explicit unprintable 64-bit usize placeholders. no GPU execution, full operation parity or P07 qptr memory-operand lowering is claimed. MIT OR Apache-2.0 terms are unchanged.

## qptr memory operands and volatile loads

[spirt-qptr-memory.patch](spirt-qptr-memory.patch) follows the physical-address patch. QPtr loads/stores retain memory flags and literals, while scope IDs remain ordinary instruction inputs. printing uses the SPIR-V operand grammar, and lifting reconstructs their order. tests require actual qptr operations before lifting, exact operands afterward, and unchanged physical-pointer instructions. [rustgpu-qptr-memory.patch](rustgpu-qptr-memory.patch) follows the aligned-access patch and tests effects through ordinary/qptr linking, aggressive dead-code elimination and performance optimization. both retain MIT OR Apache-2.0 terms.

[spirv-tools-volatile-load.patch](spirv-tools-volatile-load.patch) is a separate native prerequisite under [Apache-2.0](../../legal/licenses/SPIRV-Tools-APACHE.txt). it prevents explicit Volatile OpLoad instructions from being classified as effect-free. the regression requires retention of an unused volatile load, flags, scopes and physical store alignment, while an ordinary unused load is removed. [SPIR-V memory operands](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#Memory_Operands) define the observable volatile behavior. validation alone did not catch its loss.

apply the SPIR-T and RustGPU patches at their exact manifest bases with `--unidiff-zero --index`. first apply the earlier wrapper/tool update and regenerate its tables as documented above. then apply the native patch **inside** that wrapper's owned `spirv-tools-sys/spirv-tools` checkout at `9a49b0883b9b635689a85b5647dbfcb223268151`, using the same flags. retain this patched native tree when building the wrapper. the wrapper commit and header pin stay unchanged. the generated tools version string still identifies the native base, so record the native patch hash as well.

run standalone SPIR-T tests, then the compiled-tools compiler/shared-type gate and two-target `physical_storage` source command above. the existing storage/cast/panic command checks adjacent behavior after the native dependency change. [E-019](../../docs/execution.md#e-019-2026-09-19-preserved-qptr-memory-effects) owns results, failed controls and source identities. RustGPU formatting passes, while SPIR-T retains its previously recorded whole-repository format/Clippy limitations.

the installed SDK's unpatched optimizer still fails the volatile reproducer. a patched CLI build and the full native C++/upstream CI suites remain pending. do not skip the new regression to make that configuration pass. these are non-executed assembly/compiler tests, not Rust volatile-intrinsic support, general pointer-operation parity or P07's GPU execution evidence.

## typed pointer operations

[rustgpu-pointer-operations.patch](rustgpu-pointer-operations.patch) follows the qptr regression patch at the manifest base, with `--unidiff-zero --index` and the existing MIT OR Apache-2.0 terms. physical64 equality/inequality compare full-width addresses because SPIR-V pointer comparisons exclude physical storage. after existing logical offset legalization, the remaining sized raw-pointer case uses byte-stride multiplication and addition. constant-offset probing uses checked multiplication. no memory access or unsafe site is added by these transformations.

six new source cases cover typed u32 comparisons, dynamic wrapping add/sub/offset, a negative offset with rustc optimization enabled, logical-address rejection, and the current `null_mut`/`is_null` typed-cast limitations. the upstream-facing document carries this operation inventory without claiming `*mut` parity. the optimized source fixture sets `-C opt-level=3 -C lto=off` because the backend's rustc ThinLTO method is unimplemented. these settings do not establish GPU execution or substitute for SPIR-V optimization checks.

rerun the compiled-tools compiler/shared-type gate and the two-target `physical_storage` command above. the adjacent source command for this change is:

```powershell
cargo run --release --locked -p compiletests --no-default-features --features use-compiled-tools -j 1 -- --target-env vulkan1.3 storage_class const-int-cast const-narrowing-cast const-from-cast u8-const-cast panic ptr_read ptr_write ptr_copy allocate_null offsets_vec3_vec3a
```

[E-020](../../docs/execution.md#e-020-2026-09-19-lowered-typed-pointer-operations) owns the exact counts, independent native Rust transport check and failed setup/probe results. physical aggregate memory, the shader-library access contract, allocation-dependent methods, full pointer parity and real Vulkan/NGAPI execution remain open.

## physical pointer library and aggregate alignment

[rustgpu-physical-library.patch](rustgpu-physical-library.patch) follows the typed-pointer patch at the manifest base with `--unidiff-zero --index`. it adds `spirv_std::PhysicalPtr<T>`, a transparent u64 with a non-owning marker. construction, casts, null tests, equality and wrapping arithmetic only transport bits. unsafe Copy-value reads/writes require the complete documented allocation, layout, initialization, aliasing, visibility and lifetime contract. ordinary 32-bit shaders lack those access methods, and native methods use the existing GPU-only panic stub. there is no reference, restriction marker or safe memory-access API. MIT OR Apache-2.0 terms remain unchanged.

the array fixture exposed lost alignment in whole typed copies. the compiler now retains the weaker source/destination alignment in a shared OpCopyMemory operand, transfers it to split accesses, and removes alignment from resulting logical accesses. only absent or pure Aligned copy operands are supported. effectful/scoped copies remain outside this change. default/qptr linker regressions validate a physical array copy before and after performance optimization.

run the full compiler/shared-type gate, two-target `physical_storage` matrix and adjacent source command above. library checks are:

```powershell
cargo test --release --locked -p spirv-std -j 1
cargo clippy --release --locked -p spirv-std --all-targets -j 1 -- -D warnings
$env:RUSTDOCFLAGS = '-D warnings'
cargo doc --release --locked -p spirv-std --no-deps -j 1
cargo fmt --all -- --check
```

also check UI source formatting directly because the workspace formatter excludes it. [E-022](../../docs/execution.md#e-022-2026-09-19-tested-the-physical-pointer-library) owns exact results and the separate actual NGAPI scalar consumer. [U-003](../../UNSAFE.md#u-003-physicalptr-shader-library-access) records the library/fixture audit, with self-contained contracts mirrored into the patch. aggregate runtime, general Copy-type support, complete pointer parity and full upstream CI remain open.

## native heap Rust assembly

[rustgpu-native-heap-asm.patch](rustgpu-native-heap-asm.patch) follows the physical-library patch at the manifest base with `--unidiff-zero --index`, retaining MIT OR Apache-2.0 terms. it registers explicit untyped pointer types, places untyped variables by storage class, and emits assembly constants in declaration order in the global type/value section. this lets descriptor-size constants precede the runtime arrays whose ID strides reference them. compiler changes remain safe Rust and introduce no consumer dependency or public heap utility.

five source wrappers share one compile-only unsafe body. it loads resource slot 1 and sampler slot 2 from native heaps, samples explicit LOD zero, multiplies the color by two in ordinary Rust and writes a physical Vec4. default, opt3 with LTO disabled, and qptr variants validate. missing DescriptorHeapEXT capability or extension produces a required diagnostic. complete expected streams retain both heap interfaces, size constants, ID strides, accesses, arithmetic and Aligned 16 output. [U-004](../../UNSAFE.md#u-004-compile-only-native-heap-source-probe) owns the local boundary review. no fixture is dispatched.

rerun the full compiler/shared-type gate, workspace/direct fixture formatting, and:

```powershell
$env:RUST_TEST_THREADS = '1'
cargo run --release --locked -p compiletests --no-default-features --features use-compiled-tools -j 1 -- --target-env vulkan1.3,vulkan1.3-physical64 physical_storage descriptor_heap
```

[E-023](../../docs/execution.md#e-023-2026-09-19-compiled-native-heap-rust-shaders) records 24 required source pairs and the broader adjacent gate's unresolved subpass-coordinate failure, reproduced with the previous compiler sources. keep that existing test enabled. native heap runtime, reusable interfaces, divergent indices, additional stages/resources and full upstream CI remain pending. malformed assembly also retains a pre-existing parser recovery limitation.

## ID constants during debug stripping

[spirv-tools-id-constants.patch](spirv-tools-id-constants.patch) follows the native volatile-load patch at its manifest base. dead-constant elimination now counts ID decoration operands as uses, while ignoring annotation targets and debug references as before. this preserves ArrayStrideIdEXT and OffsetIdEXT dependencies. an unused constant with a name and ordinary decoration still disappears. the native patch retains Apache-2.0 terms and includes a C++ regression whose full native test target remains pending.

[spirv-tools-id-constant-tests.patch](spirv-tools-id-constant-tests.patch) follows the wrapper's descriptor-heap tools patch and retains MIT OR Apache-2.0 terms. its compiled-tool regression validates the mixed heap module and a minimal array/member-ID fixture before and after dead-constant elimination plus debug stripping. all three selected wrapper cases pass, including the existing performance-optimization and missing-capability controls. from the owned wrapper checkout, with the pinned nightly and one build worker:

```powershell
cargo test --release --locked -p spirv-tools --no-default-features --features use-compiled-tools -j 1 --test descriptor_heap -- --test-threads=1
```

the unpatched SDK 1.4.357.0 optimizer independently reproduces missing constant IDs after `--eliminate-dead-const`, despite returning exit 0. validate its output explicitly. a patched standalone CLI and the full native suite remain pending. RustGPU's debug-stripped source and actual-consumer checks are separate gates. generated tool version text may still name the original native base, so use the manifest's explicit patch/commit identity as well.

[rustgpu-debug-strip-heaps.patch](rustgpu-debug-strip-heaps.patch) follows the native-heap assembly patch, retaining MIT OR Apache-2.0 terms. two additional wrappers run the shared source with debug stripping at opt0/opt3. the full two-ABI matrix passes 26 required pairs, with 46 compiler and three shared-type tests passing and four existing macOS ignores. its diagnostic snapshots are linked code before final tool passes. both actual final modules were separately inspected and validated. [E-024](../../docs/execution.md#e-024-2026-09-19-executed-native-heap-rust-shaders) records those gates and actual NGAPI execution. E-023's pre-existing subpass failure and broader upstream/native CI limits remain open.
