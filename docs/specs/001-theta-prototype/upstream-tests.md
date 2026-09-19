# RustGPU tests and upstream crosswalk

this page is the primary contribution crosswalk for [US6 and FR-015](spec.md). [tasks T010-T016 and T033-T035](tasks.md) own the work. every row needs an implementation/test pointer, observed result, source/configuration identity, and remaining limitation in the contribution evidence. no implementation row is currently complete.

three different authorities are recorded: the draft author's checklist, a maintainer's concrete review concerns, and repository CI. none alone establishes eventual acceptance.

## PR #237 checklist

the [draft, refreshed on 2026-09-19](https://github.com/Rust-GPU/rust-gpu/pull/237) was open at `e14a70d9260c7df7fab1542810f2ca0276233331`. its checked boxes refer to that draft, not proof in current main or the new implementation. all checklist items are mapped below.

| id | original obligation | required proof in this contribution |
|---|---|---|
| P01 | explicit storage classes, including work from #236 | passing compiletests for physical versus logical storage, inference across calls, and rejected incompatible classes |
| P02 | enable `PhysicalStorageBuffer64` with the physical-address capability | disassembly/capability checks and validation; ordinary logical shaders retain their existing behavior |
| P03 | pointer casts, including `u64 as *mut T` and reverse | address round-trip and typed-cast compiletests plus execution on real allocated addresses; document the target-width policy |
| P04 | defer cast errors until storage-class inference | a cast that becomes valid after inference passes; genuinely invalid casts diagnose cleanly after inference |
| P05 | generate required `Aligned` memory operands | loads/stores of scalar and aggregate layouts validate and execute in optimized and unoptimized forms |
| P06 | strip alignment from non-physical operations as needed | SPIR-T regression tests for mixed physical/logical modules and inspection of final memory operands |
| P07 | carry memory operands through qptr load/store lowering/lifting | focused pass regression covering preservation of applicable operands and full-pipeline execution |
| P08 | restriction/alias decorations where necessary | a documented alias policy, correct emitted decorations and placement, and tests with valid aliasing; no marker type claiming semantics it does not emit |
| P09 | raw pointer operations correct or rejected | operation inventory with supported tests and unsupported-operation diagnostics; no compiler crash or silently invalid SPIR-V |
| P10 | `spirv-std` physical-pointer utilities and `PhysicalPtr<T>` wrapper | size/alignment/offset tests, usable value loads/stores/indexing, host/shader ABI, unsafe obligations and examples |
| P11 | parity with `*mut` | enumerate cast, offset/add/sub, equality/null, reads/writes/copies, references, and other exposed methods; implement sound operations or explicitly reject/document unsupported ones and retain the scope question for review |

P03/P04 cover the parent casts item and both children. P05/P06/P07 cover the parent alignment item and all three children. P10/P11 cover the parent utilities item and both children. the draft spells P08 `RestrictedPointer`; determine the actual applicable SPIR-V `RestrictPointer`/alias decorations from the specification instead of inventing an opcode.

a narrow initial API is useful for the prototype. it does not silently close P11 or settle the draft's safety/open pointer-width questions. document any proposed reduced upstream scope and obtain maintainer agreement during review; meanwhile keep the contribution honest about unsupported operations.

## maintainer review concerns

[Firestar99's comment](https://github.com/Rust-GPU/rust-gpu/pull/237#issuecomment-5063782587) is a public issue-conversation review, not a formal approving review. the repository lists Firestar99 among its [CODEOWNERS](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.github/CODEOWNERS).

| id | review concern | acceptance evidence |
|---|---|---|
| R01 | work must be reviewable as a real focused patch | clean feature diff against the chosen upstream base; locally prepared PR text and reproducer |
| R02 | avoid unnecessary unstable `f16` | baseline tests use `f32`/ordinary integer types; any narrower type has a feature-specific reason and separate coverage |
| R03 | examples belong in compiletests/difftests | every compiler semantic case is in the upstream harness; demo is additional consumer evidence |
| R04 | unrelated raytracing changes | exclude them from physical-pointer/heap patches; use separate fixes only if independently necessary |
| R05 | arbitrary `u32` to `u64` widening is unsound | regression for `u32::overflowing_add()`, wrapping/truncating casts, mixed-width comparisons, and pointer conversions; no post-output widening workaround |
| R06 | a restriction marker without lowering does nothing | prove final decoration semantics or omit the misleading marker/API |
| R07 | code must be understood and reviewed | explain the lowering, ABI, alias invariants, tests, and each reused patch; inspect generated SPIR-V rather than trusting an experimental fork |

the fork author's [follow-up](https://github.com/Rust-GPU/rust-gpu/pull/237#issuecomment-5079412612) describes a consumer-specific, non-mergeable experiment. use it as feasibility/design evidence, with attribution, not an accepted baseline.

## additional feature tests required by this plan

these are engineering requirements for this consumer, not a checklist quoted from a maintainer. prioritize Vulkan directly and through actual NGAPI on Windows. future APIs and shader languages do not create additional gates here.

| id | case family | oracle |
|---|---|---|
| H01 | native resource/sampler heap declarations and untyped pointers | final SPIR-V, extension/capability/interface validation, no ordinary binding substituted |
| H02 | texture/sampler access and descriptor strides | deterministic textures and distinct samplers; queried size/stride rules, nonzero slots |
| H03 | uniform and divergent resource indices | lane-dependent expected values; extension-specific non-uniform rules |
| H04 | pointer and heap use in one module | full optimized/unoptimized pipeline plus GPU output |
| H05 | mixed resource types and supported access modes | sampled images, storage images, reads/writes as exposed; deliberate unsupported cases diagnose |
| H06 | stage and interface preservation | compute, vertex/fragment, then task/mesh compiletests and supported-device execution |
| H07 | tool/dependency compatibility | parser/linker/optimizer/serializer/validator probes in installed-tools and compiled-tools paths |
| H08 | feature gating | enabled and missing-capability configurations; explicit test skips with reasons, no success-shaped empty result |
| A01 | root and aggregate ABI | eight-byte addresses, high-bit preservation, nested address-containing structures, offsets, strides, scalar vectors, row-major matrix interpretation, 256-byte root boundary |
| A02 | arithmetic, bounds, alignment, aliasing | correct defined operations and diagnostics/contracts for unsupported ones; never test undefined GPU dereferences by expecting a graceful error |
| A03 | actual consumer integration | Rust compute/pointer probe and cube in actual NoGraphicsAPI on native Windows Vulkan, with exact host/shader hashes. a small direct Windows Vulkan fixture isolates failures. the full AGFX native-heap profile is supporting evidence, not a draft prerequisite |
| A04 | descriptor/resource lifecycle | valid reuse after completion, multiple submissions, barriers and readback; host-side rejection of invalid supported API use |

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

| id / job | coverage to retain and run |
|---|---|
| C01 `test` | Ubuntu, Windows, macOS; codegen release build/test; workspace nextest with workflow exclusions/features; ash/wgpu/CPU examples; shader builds in release and dev; separate compiletest and difftest preparation |
| C02 `compiletest` | all three OSes and `vulkan1.1`, `vulkan1.2`, `vulkan1.3`, `vulkan1.4`, `spv1.3`, `spv1.4`, `spv1.5`, `spv1.6`; preserve meaningful per-case feature restrictions |
| C03 `difftest` | all three OSes with the workflow's nextest profile and software/device setup; report new-feature skips separately |
| C04 `android` | the existing aarch64 Android wgpu example build; this is compiler regression coverage, not an AGFX Android port |
| C05 `lint` | main workspace, compiletest UI, and difftest formatting; warning-free docs; stable docs for spirv-std/builder; clippy; custom lint script |
| C06 `cargo-deny` | the repository's cargo-deny action and policy |
| C07 `release-dry-run` | locked dependency fetch and `cargo publish --dry-run`; it does not publish a release |
| C08 `cargo-gpu-os` | three-OS cargo-gpu/install tests, template dependency fetch, and `cargo xtask test-build` |
| C09 `cargo-gpu-backwards-compat` | all six active pinned Rust-GPU/glam configurations in the workflow; do not revive commented-out matrix cells as invented requirements |
| C10 aggregate | every `test_success` dependency succeeds at the exact proposed revision |

the [custom lint script](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.github/workflows/lint.sh) also checks compiler environment-variable discipline, minimal dependencies for spirv-std, and compilation with the unpatched codegen SSA configuration. include these; `cargo test` alone is not the project gate.

C01 uses `use-installed-tools`, with workspace features `clap,bytemuck`; C02/C03 likewise use the workflow tool path. H07 additionally probes the compiled-tools configuration when changing instruction support. new extensions may require a narrowly justified tool update; do not disable validation or relax the old matrix to make them pass.

during edits, run only affected focused checks. at the final patch checkpoint, run the complete applicable public workflow and supported-device fixtures. keep unavailable remote jobs visibly pending, with exact reproduction instructions, until results exist. refresh CI and upstream comments once before submission and account for changes. never describe a local subset as all upstream checks.

## PR-ready evidence

prepare the feature diff, docs, unsafe contract, pointer-width decision, dependency justification, test inventory/results, native Windows Vulkan NGAPI consumer recipe, attribution, and known limits. mirror necessary contracts from [UNSAFE.md](../../../UNSAFE.md) into upstream-facing documentation so the RustGPU patch stands alone. public [contribution guidance](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/CONTRIBUTING.md) calls for a feature branch, successful local build, descriptive PR, and maintainer review. there is no discovered universal PR template replacing this source-based crosswalk.

feature patches may be split into prerequisites, physical pointers, and native heaps. keep each independently reviewable. all promised capabilities retain their tests in the appropriate patch, and the combined actual-consumer claim still needs the complete path.

a local PR draft may list unavailable CI jobs as pending after its promised features and actual-consumer proof are established. distinguish this state from a fully passing upstream gate or an opened PR. complete AGFX/ShaderToHuman parity, Linux testbed portability, report polish, Metal, and DirectX do not block drafting. do not claim successful public CI or maintainer acceptance before those events occur. append the evidence and remaining gaps to [execution](../../execution.md), and reusable findings to [lessons](../../lessons.md).
