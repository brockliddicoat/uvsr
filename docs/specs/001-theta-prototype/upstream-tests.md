# RustGPU tests and upstream crosswalk

this is the current contribution crosswalk for [US6 and FR-015](spec.md). [execution](../../execution.md) owns chronological results and exact historical revisions. [the patch manifest](../../../patches/rustgpu-prerequisites/sources.json) owns candidate source/dependency pins. a row with bounded evidence does not claim every operation, device or CI configuration passes.

the final upstream refresh at 2026-09-20 00:43:32 UTC found main unchanged at e6394e08eb3356083b12f732a01906f8e49f7c4a. CI, lint, contribution guidance and aliases match the pinned source. PR #237 remains open at e14a70d9260c7df7fab1542810f2ca0276233331, with nine comments and no newly inferred maintainer agreement. the links below distinguish its author's checklist, a maintainer's review concerns and repository CI.

current local evidence spans E-015 through E-034. E-033's source gate passes 51 required pairs, seven logical and 44 physical64. E-034 adds the generic CPU/Vulkan difftest and real high-address execution on Intel Arc. actual NGAPI heap/compute/cube/storage/task-mesh execution remains separate NVIDIA Windows evidence. production compiler/library code is unchanged since E-030. the complete upstream gate, broader raw-pointer scope and Linux/device portability remain open.

## PR #237 checklist

the [draft, refreshed on 2026-09-19](https://github.com/Rust-GPU/rust-gpu/pull/237) was open at `e14a70d9260c7df7fab1542810f2ca0276233331`. its checked boxes refer to that draft, not proof in current main or the new implementation. all checklist items are mapped below.

| id | original obligation | required proof in this contribution | current evidence and limitation |
|---|---|---|---|
| P01 | explicit storage classes, including work from #236 | passing compiletests for physical versus logical storage, inference across calls, and rejected incompatible classes | bounded pass. `spirv_type_constraints`, storage inference and `physical_address` linker tests, with physical/logical cast fixtures. no new general source annotation API is claimed. |
| P02 | enable `PhysicalStorageBuffer64` with the physical-address capability | disassembly/capability checks and validation; ordinary logical shaders retain their existing behavior | pass for the explicit Vulkan `-physical64` target in target specs and `builder_spirv`. layout/capability snapshots and E-021 onward execute it. ordinary targets retain logical32. |
| P03 | pointer casts, including `u64 as *mut T` and reverse | address round-trip and typed-cast compiletests plus execution on real allocated addresses; document the target-width policy | bounded pass. `cast_physical64`, integer signedness and PhysicalPtr tests preserve u64 transport. E-034 executes real addresses above 4 GiB, while NGAPI allocations remain below it. |
| P04 | defer cast errors until storage-class inference | a cast that becomes valid after inference passes; genuinely invalid casts diagnose cleanly after inference | bounded pass. post-inference physical conversion and mixed-storage rejection in `simple_passes` and cast fixtures. unlisted casts remain outside the supported initial API. |
| P05 | generate required `Aligned` memory operands | loads/stores of scalar and aggregate layouts validate and execute in optimized and unoptimized forms | bounded pass. aligned scalar, array and nested aggregate source cases, E-030 actual NGAPI and E-034 generic runtime. not an all-layout or all-Copy-type guarantee. |
| P06 | strip alignment from non-physical operations as needed | SPIR-T regression tests for mixed physical/logical modules and inspection of final memory operands | bounded pass. logical Aligned stripping after inference and SPIR-T mixed-memory regressions retain other flags/scope IDs. final logical/physical source streams validate. |
| P07 | carry memory operands through qptr load/store lowering/lifting | focused pass regression covering preservation of applicable operands and full-pipeline execution | bounded pass. SPIR-T qptr memory regressions, RustGPU effects/mem2reg regressions, patched native optimizer and four `operations_*` variants. E-030/E-034 execute the full path. effectful/scoped copies remain unsupported. |
| P08 | restriction/alias decorations where necessary | a documented alias policy, correct emitted decorations and placement, and tests with valid aliasing; no marker type claiming semantics it does not emit | bounded pass. conservative `Aliased`/`AliasedPointer` placement is covered by linker/source tests. separately loaded aliases execute in E-030/E-034. no restriction marker, physical reference constructor or concurrent-alias guarantee. |
| P09 | raw pointer operations correct or rejected | operation inventory with supported tests and unsupported-operation diagnostics; no compiler crash or silently invalid SPIR-V | partial. `platform-support.md` and E-020/E-031 enumerate tested raw comparisons, wrapping element offsets, constness casts and clean helper rejections. unlisted operations are not a blanket diagnostic guarantee. |
| P10 | `spirv-std` physical-pointer utilities and `PhysicalPtr<T>` wrapper | size/alignment/offset tests, usable value loads/stores/indexing, host/shader ABI, unsafe obligations and examples | bounded pass. `spirv-std/src/physical_ptr.rs`, native layout/wrapping tests, logical-target rejections and scalar/aggregate execution. read/write remain unsafe with complete host obligations. |
| P11 | parity with `*mut` | enumerate cast, offset/add/sub, equality/null, reads/writes/copies, references, and other exposed methods; implement sound operations or explicitly reject/document unsupported ones and retain the scope question for review | open broader scope and maintainer decision. the proposed initial API omits Deref/references, restriction markers, atomics, volatile/copy-range APIs and allocation-dependent methods. the documented inventory is not full `*mut` parity. |

P03/P04 cover the parent casts item and both children. P05/P06/P07 cover the parent alignment item and all three children. P10/P11 cover the parent utilities item and both children. the draft spells P08 `RestrictedPointer`; determine the actual applicable SPIR-V `RestrictPointer`/alias decorations from the specification instead of inventing an opcode.

a narrow initial API is useful for the prototype. it does not silently close P11 or settle the draft's safety/open pointer-width questions. document any proposed reduced upstream scope and obtain maintainer agreement during review; meanwhile keep the contribution honest about unsupported operations.

## maintainer review concerns

[Firestar99's comment](https://github.com/Rust-GPU/rust-gpu/pull/237#issuecomment-5063782587) is a public issue-conversation review, not a formal approving review. the repository lists Firestar99 among its [CODEOWNERS](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.github/CODEOWNERS).

| id | review concern | acceptance evidence | current evidence and limitation |
|---|---|---|---|
| R01 | work must be reviewable as a real focused patch | clean feature diff against the chosen upstream base; locally prepared PR text and reproducer | generic commits and reconstructable patch exports are separate from consumer hosts. the [local title/body, size review and primary audit](../../rustgpu-contribution.md) complete T035/T036 for the proposed bounded scope. no upstream PR has been opened. |
| R02 | avoid unnecessary unstable `f16` | baseline tests use `f32`/ordinary integer types; any narrower type has a feature-specific reason and separate coverage | satisfied for this diff. fixtures use u32/u64, ordinary floats and existing vector types. no new f16 requirement. |
| R03 | examples belong in compiletests/difftests | every compiler semantic case is in the upstream harness; demo is additional consumer evidence | source cases live in compiletests, and E-034 adds CPU/Vulkan `physical_storage/operations` to the existing difftest workspace. heap GPU execution is still the external NGAPI supported-device gate. |
| R04 | unrelated raytracing changes | exclude them from physical-pointer/heap patches; use separate fixes only if independently necessary | satisfied for this diff. no raytracing feature implementation or testbed/NGAPI dependency is added to RustGPU. |
| R05 | arbitrary `u32` to `u64` widening is unsound | regression for `u32::overflowing_add()`, wrapping/truncating casts, mixed-width comparisons, and pointer conversions; no post-output widening workaround | bounded pass. explicit target ABI, layout/high-bit/overflow/signedness source tests and native PhysicalPtr arithmetic tests. ordinary u32 operations stay u32. no post-output shader widening. |
| R06 | a restriction marker without lowering does nothing | prove final decoration semantics or omit the misleading marker/API | satisfied by omission and explicit policy. no misleading restriction API. final alias markings and valid sequential alias execution are covered separately. |
| R07 | code must be understood and reviewed | explain the lowering, ABI, alias invariants, tests, and each reused patch; inspect generated SPIR-V rather than trusting an experimental fork | coordinator self-review covers complete changed lowering, ABI/alias contracts, emitted instructions and U-001/U-003/U-004/U-006/U-012-U-015 sites. independent maintainer/safety review remains pending. |

the fork author's [follow-up](https://github.com/Rust-GPU/rust-gpu/pull/237#issuecomment-5079412612) describes a consumer-specific, non-mergeable experiment. use it as feasibility/design evidence, with attribution, not an accepted baseline.

## additional feature tests required by this plan

these are engineering requirements for this consumer, not a checklist quoted from a maintainer. prioritize Vulkan directly and through actual NGAPI on Windows. future APIs and shader languages do not create additional gates here.

| id | case family | oracle | current evidence and limitation |
|---|---|---|---|
| H01 | native resource/sampler heap declarations and untyped pointers | final SPIR-V, extension/capability/interface validation, no ordinary binding substituted | bounded pass. grammar/untyped/ID metadata changes plus `native_heap.rs` constructors emit real ResourceHeapEXT/SamplerHeapEXT and untyped accesses. no ordinary descriptor-array replacement. |
| H02 | texture/sampler access and descriptor strides | deterministic textures and distinct samplers; queried size/stride rules, nonzero slots | bounded pass. E-024/E-026/E-027 distinguish nonzero resources and repeat/clamp samplers. queried resource and sampler sizes are both 32 bytes on the tested device. unequal-stride hardware remains untested. |
| H03 | uniform and divergent resource indices | lane-dependent expected values; extension-specific non-uniform rules | bounded pass. E-025 source cases and E-026 four-lane divergent resource/sampler permutations, queried/enabled sampled-image indexing support. divergent storage-image access is untested. |
| H04 | pointer and heap use in one module | full optimized/unoptimized pipeline plus GPU output | bounded pass. compute output and cube/task-mesh combine physical addresses and native heaps. optimized/unoptimized default/qptr source paths are checked. consumer case counts remain in execution. |
| H05 | mixed resource types and supported access modes | sampled images, storage images, reads/writes as exposed; deliberate unsupported cases diagnose | partial by resource/mode. float/integer sampled 2D images compile. float sampled images and rgba32ui storage reads/writes execute. untested formats, image parameters, storage atomics and other resource kinds are not promised. |
| H06 | stage and interface preservation | compute, vertex/fragment, then task/mesh compiletests and supported-device execution | bounded pass. compute, vertex/fragment and task/mesh source and actual NGAPI execution. E-033 uses six quad groups/fixed Output slots. dynamic u64 mesh Output indexing retains a driver-compiler fault reproducer. |
| H07 | tool/dependency compatibility | [E-015](../../execution.md#e-015-2026-09-19-completed-the-two-configuration-pipeline-gate): all 16 required assembly-input probes pass in each configuration at the pinned patched revisions. this does not establish Rust source code generation or execution | partial current integration. E-015's 16 assembly probes passed both tool configurations. later volatile Function behavior requires the separately patched compiled native tools. stock installed SDK optimization and full native GTest/current upstream CI are not passing evidence. |
| H08 | feature gating | enabled and missing-capability configurations; explicit test skips with reasons, no success-shaped empty result | bounded pass. missing capability/extension and unsafe-call source rejections, queried device profiles, native CPU rejection controls and failure-aware runners. the generic host records hardware skips. absent-capability hardware/device-loss execution is untested. |
| A01 | root and aggregate ABI | eight-byte addresses, high-bit preservation, nested address-containing structures, offsets, strides, scalar vectors, row-major matrix interpretation, 256-byte root boundary | bounded pass. eight-byte address and nested layout assertions, synthetic transport controls, E-034 real high addresses, 16/80-byte roots and row-major cube/task payload. no runtime claim for the full 256-byte root boundary. |
| A02 | arithmetic, bounds, alignment, aliasing | correct defined operations and diagnostics/contracts for unsupported ones; never test undefined GPU dereferences by expecting a graceful error | bounded pass. defined wrapping arithmetic, aligned scalar/aggregate loads/stores, valid sequential aliases and host bounds/lifecycle controls. no undefined negative dispatch, arbitrary reference or in-bounds arithmetic promise. |
| A03 | actual consumer integration | Rust compute/pointer probe and cube in actual NoGraphicsAPI on native Windows Vulkan, with exact host/shader hashes. a small direct Windows Vulkan fixture isolates failures. the full AGFX native-heap profile is supporting evidence, not a draft prerequisite | bounded primary consumer pass. E-021 through E-033 cover actual Windows NGAPI pointer/heap compute and numeric cube/storage/task-mesh oracles. E-028/E-029 direct Rust slice and E-034 generic host are independent cross-checks. Linux remains pending. |
| A04 | descriptor/resource lifecycle | valid reuse after completion, multiple submissions, barriers and readback; host-side rejection of invalid supported API use | bounded pass. real repeated submissions, barriers, fences/timelines, readback and completed retirement with host rejection controls. partial creation is reviewed. device loss, general asynchronous use and arbitrary external native mutation remain untested. |

never fabricate a dereferenceable address by adding 4 GiB to an allocation. use actual device addresses for memory access. synthetic high-bit patterns can test non-dereferencing representation/conversion. if real high-address allocation coverage is unavailable, record that limit explicitly.

## upstream test harnesses

[compiletests](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/tests/compiletests/README.md) validate compilation and SPIR-V; they do not execute shaders. use positive, negative, and disassembly cases. inspect expected output before blessing it.

[difftests](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/tests/difftests/README.md) compare multiple executable variants. add generic CPU/reference and low-level Vulkan variants in the test workspace, using existing helpers where compatible. do not pull AGFX or NoGraphicsAPI into the compiler's test dependencies. extend the minimal Vulkan fixture only as required.

feature detection may skip unavailable hardware. retain compile/validation coverage in ordinary CI and explicit supported-device execution evidence for the new extension. a skipped hardware variant is not its correctness proof.

focused commands, once the named tests exist:

```text
cargo compiletest physical_storage
cargo compiletest descriptor_heap
cargo difftest physical_storage
cargo difftest descriptor_heap
```

the [actual aliases](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.cargo/config.toml) are authoritative; the difftest alias uses nextest. copy target/features from current CI when reproducing its configuration.

## complete public CI gate

the pinned [workflow](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.github/workflows/ci.yaml) makes `test_success` depend on all nine jobs below. a snapshot is retained in ignored `work/theta/evidence/rust-gpu-ci.yaml`. this is verified public workflow evidence; private branch-protection settings were not inspected. these nine upstream CI jobs remain required for a full-CI claim. they are distinct from the retired nine language/backend implementation cells, and are not a requirement to port AGFX to every CI platform.

| id / job | coverage to retain and run | current result |
|---|---|---|
| C01 `test` | Ubuntu, Windows, macOS; codegen release build/test; workspace nextest with workflow exclusions/features; ash/wgpu/CPU examples; shader builds in release and dev; separate compiletest and difftest preparation | pending full job. E-030 passes 32 exported compiler tests plus 16 excluded local instruction probes, three shared-type tests and selected library checks. this is not the full workspace/examples or three-OS job. |
| C02 `compiletest` | all three OSes and `vulkan1.1`, `vulkan1.2`, `vulkan1.3`, `vulkan1.4`, `spv1.3`, `spv1.4`, `spv1.5`, `spv1.6`; preserve meaningful per-case feature restrictions | pending full job. E-033 passes all 51 focused pairs on Windows with compiled patched tools. the wider adjacent gate retains 120 passes, one pre-existing read_subpass failure and four ignores. stock CI does not yet select physical64. |
| C03 `difftest` | all three OSes with the workflow's nextest profile and software/device setup; report new-feature skips separately | pending full job. E-034's selected existing harness and independent 12-case execution cover this new test on Windows, not all difftests or all OSes. stock installed tools still need the optimizer prerequisite. |
| C04 `android` | the existing aarch64 Android wgpu example build; this is compiler regression coverage, not an AGFX Android port | pending. no Android toolchain/example build in this task. |
| C05 `lint` | main workspace, compiletest UI, and difftest formatting; warning-free docs; stable docs for spirv-std/builder; clippy; custom lint script | pending full job. E-030 compiler and E-034 fixture-only denied-warning Clippy plus selected formatting pass. dependency-inclusive fixture Clippy fails seven findings in unchanged upstream difftest helpers. complete warning-free docs, stable docs, minimal-version and unpatched-SSA configurations remain unrun. existing SPIR-T lint issues are separately recorded. |
| C06 `cargo-deny` | the repository's cargo-deny action and policy | pending. no complete cargo-deny action/policy result at the proposed revision. |
| C07 `release-dry-run` | locked dependency fetch and `cargo publish --dry-run`; it does not publish a release | pending. no publish dry-run result at the proposed revision. no release publication is authorized. |
| C08 `cargo-gpu-os` | three-OS cargo-gpu/install tests, template dependency fetch, and `cargo xtask test-build` | pending. no three-OS cargo-gpu/install/xtask gate at the proposed revision. |
| C09 `cargo-gpu-backwards-compat` | all six active pinned Rust-GPU/glam configurations in the workflow; do not revive commented-out matrix cells as invented requirements | pending. none of the six active backwards-compatibility configurations is claimed passing here. |
| C10 aggregate | every `test_success` dependency succeeds at the exact proposed revision | pending. no upstream workflow aggregate or maintainer acceptance. UVSR's source/document CI is a different gate. |

the [custom lint script](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.github/workflows/lint.sh) also checks compiler environment-variable discipline, minimal dependencies for spirv-std, and compilation with the unpatched codegen SSA configuration. include these; `cargo test` alone is not the project gate.

C01 uses `use-installed-tools`, with workspace features `clap,bytemuck`; C02/C03 likewise use the workflow tool path. H07 additionally probes the compiled-tools configuration when changing instruction support. new extensions may require a narrowly justified tool update; do not disable validation or relax the old matrix to make them pass.

during edits, run only affected focused checks. at the final patch checkpoint, run the complete applicable public workflow and supported-device fixtures. keep unavailable remote jobs visibly pending, with exact reproduction instructions, until results exist. refresh CI and upstream comments once before submission and account for changes. never describe a local subset as all upstream checks.

## PR-ready evidence

prepare the feature diff, docs, unsafe contract, pointer-width decision, dependency justification, test inventory/results, native Windows Vulkan NGAPI consumer recipe, attribution, and known limits. mirror necessary contracts from [UNSAFE.md](../../../UNSAFE.md) into upstream-facing documentation so the RustGPU patch stands alone. public [contribution guidance](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/CONTRIBUTING.md) calls for a feature branch, successful local build, descriptive PR, and maintainer review. there is no discovered universal PR template replacing this source-based crosswalk.

feature patches may be split into prerequisites, physical pointers, and native heaps. keep each independently reviewable. all promised capabilities retain their tests in the appropriate patch, and the combined actual-consumer claim still needs the complete path.

a local PR draft may list unavailable CI jobs as pending after its promised features and actual-consumer proof are established. distinguish this state from a fully passing upstream gate or an opened PR. complete AGFX/ShaderToHuman parity, Linux testbed portability, report polish, Metal, and DirectX do not block drafting. do not claim successful public CI or maintainer acceptance before those events occur. append the evidence and remaining gaps to [execution](../../execution.md), and reusable findings to [lessons](../../lessons.md).
