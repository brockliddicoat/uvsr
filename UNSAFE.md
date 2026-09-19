# unsafe Rust policy and audit

**read this before adding, changing, or reviewing unsafe code.** this is the authoritative project policy and visible registry, linked from the root README, agent instructions, specification, and checkpoint prompt.

## current audit status

| area | observed state |
| --- | --- |
| first-party Theta Rust | the diagnostic module uses `forbid(unsafe_code)`. no Cargo workspace or runtime port exists. the contributed compile-only shader fixture is recorded separately as U-001 |
| native and shader boundaries | U-001 is implemented for compile-only use. native runtime boundaries remain unimplemented and need their own complete records |
| dependencies, generated code, macros, external hosts | pinned NGAPI C++ and Vulkan SDK 1.4.357.0 were used by the diagnostic below. these are native dependencies, not safe Rust or an audited finished Rust port |
| RustGPU contribution | E-015 passed all 16 assembly-input probes in both tool configurations. E-018 adds aligned scalar accesses. E-019 adds qptr memory effects with a native optimizer prerequisite. U-001 has an unsafe entry and three raw accesses with one narrow lint exception. compilation does not establish its runtime obligations |
| runtime and soundness evidence | actual NGAPI device/command-context probes and non-executed SPIRV-Tools fixtures passed at E-007. no Rust shader execution or Rust boundary soundness result exists |

source pins are in [research](docs/specs/001-theta-prototype/research.md), and current evidence is in [execution](docs/execution.md). **zero Rust implementation is not evidence of a safe completed system.** replace these rows with revision-specific Rust inventories as code is added.

the [RustGPU diagnostic module](tools/theta/rustgpu-instruction-probes.rs) uses upstream parser/linker/test APIs and standard owned vectors and strings. its only added lint is `#![forbid(unsafe_code)]`. it contains no unsafe operation, native call, or shader dispatch. the upstream compiler and SPIRV-Tools remain dependency boundaries with their own unsafe code. this probe does not audit those complete implementations or prove runtime soundness.

E-009 patch audit: the [rspirv loader patch](patches/rustgpu-prerequisites/rspirv-untyped-global.patch) changes safe enum matching and adds a safe serialization/placement regression. no unsafe operation, public memory-access API, native call, or lint exception is added. coordinator self-review checked the complete patch against its pinned source and the full rspirv test results. no independent review or complete dependency safety audit is claimed. the registry still needs no new U record.

E-010 patch audit: the [grammar/control patches](patches/rustgpu-prerequisites/README.md#modern-grammar-and-concrete-interfaces) use safe collections, checked name-index bounds, enum lookups and existing specialization ownership. they add no unsafe site, lint waiver, native ABI or runtime memory-access API. self-review covered complete diffs, source pins, regression outcomes and remaining failed gates. patched dependencies still contain unrelated unsafe code. no independent safety review or new U record is claimed.

E-011 patch audit: the [compiled-tools update](patches/rustgpu-prerequisites/README.md#compiled-descriptor-heap-tools) adds safe generator arguments and regressions with `forbid(unsafe_code)`. no FFI declaration, native wrapper, unsafe operation or lint exception changes. the pinned native library advances to SPIRV-Tools `9a49b0883b9b635689a85b5647dbfcb223268151`; existing wrapper/native boundaries remain dependencies, not a first-party soundness proof. coordinator self-review inspected source changes, generated-file identities, public C target-environment compatibility, build source selection and all eight integration results. no GPU dispatch, complete native safety audit, independent review or new U record is claimed.

E-012 patch audit: the [untyped-pointer changes](patches/rustgpu-prerequisites/README.md#untyped-pointers-and-static-type-operands) add safe IR fields, enum variants, traversal and inference logic. regression code forbids unsafe. no unsafe site, FFI, lint waiver or runtime memory-access API is added. coordinator self-review checked static type/value separation, dependency traversal, placement, interface collection and qptr behavior against the recorded tests. whole-buffer qptr usage remains diagnosed, and heap ID decorations remain failing. no independent review, GPU soundness proof or new U record is claimed.

E-014 patch audit: the [heap metadata changes](patches/rustgpu-prerequisites/README.md#heap-metadata-and-dependency-retention) use safe IR variants, dependency traversal and collection operations. no unsafe operation, FFI, lint exception or runtime access API is added. coordinator self-review checked annotation ordering, constant/type remapping, liveness, member-offset deduplication, diagnostic paths and the complete passing installed-tools matrix. SPIR-T structural tests retain `forbid(unsafe_code)`. no independent review, full dependency safety audit, GPU soundness result or new U record is claimed.

E-015 audit: the same compiler sources pass the compiled-tools configuration against the E-011 native dependency pin. no source boundary changed for this run. the unsafe inventory, self-review limits and absence of a GPU soundness claim remain as above. new AGFX A-006/A-007 notes are source observations and future contract requirements, not implemented native calls.

E-016 audit: the [explicit pointer-width foundation](patches/rustgpu-prerequisites/README.md#explicit-rust-pointer-width-foundation) changes target selection, JSON generation and test selection using safe Rust. source probes use `forbid(unsafe_code)` and no raw-pointer dereference. synthetic high bits test integer representation only. raw casts retain rejection. coordinator self-review checked the complete diff, strict target agreement, layout assertions and all emitted semantic instructions before approving expectations. no unsafe site, native call, FFI or lint waiver is added, and no U record is needed. independent review, physical access and GPU soundness remain unproved.

E-017 audit: the [physical conversion patches](patches/rustgpu-prerequisites/README.md#physical-address-conversions) use safe compiler transformations and non-executed test fixtures. source probes forbid unsafe and only transport addresses. coordinator self-review checked source-signed extension, narrowing after 64-bit conversion, storage-class conflicts, final alias placement, preservation of explicit restrictions, qptr's concrete-address boundary and exact test output. no unsafe operation, FFI, reference fabrication, safe memory-access API or lint waiver is added. pointer-slot/array annotation checks are structural evidence, not a complete aggregate-access guarantee. no new U record, independent review or GPU soundness claim is warranted.

E-018 audit: [U-001](#u-001-compile-only-physical-u32-access) registers the first compile-only shader access boundary. all newly changed compiler transformations and the other new source fixture remain safe Rust. self-review checked the narrow exception, all three operation contracts, exact emitted alignment and logical interfaces, successful deny lints, and the full compiler/source regression gates. native calls, FFI declarations and runtime resource owners did not change. the dependency/compiler/macro implementations are not thereby fully audited, and no GPU execution or independent safety review is claimed.

E-019 audit: the [qptr memory patches](patches/rustgpu-prerequisites/README.md#qptr-memory-operands-and-volatile-loads) use safe Rust IR fields, traversal and regressions. no unsafe site, lint exception or public access API is added. the native SPIRV-Tools change reads an existing instruction's memory mask only after checking its opcode and operand count. it changes optimizer classification inside an existing C++ dependency, with no FFI or wrapper change. self-review covered the complete diffs, scope-ID traversal, effect retention and ordinary-load elimination controls. no new U record, full native safety audit or independent review is claimed. U-001 remains compile-only.

E-007 diagnostic audit: [capabilities.cpp](tools/theta/ngapi-probe/capabilities.cpp) checks device creation before querying borrowed caps, waits idle and destroys the same device. it exposes no Rust API. the other target compiles unchanged NGAPI `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38` command-context tests, which own their allocations and completion. Vulkan SDK 1.4.357.0 supplies headers/import libraries and the explicit validation layer. these dependencies contain native code and raw memory operations, excluded from the first-party Rust count, not claimed safe through that exclusion. review: coordinator self-review, 2026-09-19, source/control-flow inspection plus Debug/Release execution. no independent review.

the [SPIR-V fixtures](tests/compiler-probes/README.md) are assembly-only tool inputs with raw-address and descriptor assumptions documented at their entry. none was dispatched. compiler validation does not discharge their allocation, bounds, alignment, aliasing, visibility or lifetime obligations for future execution. no safe shader API or Rust lint exception is introduced by these fixtures.

## safe default and exceptions

ordinary ownership, collections, state machines, shader math, formatting, fixtures, reporting, and compiler transformations use safe Rust. a close source port preserves behavior, not unsafe C/C++ pointer idioms.

an exception needs a specific required operation that a suitable safe operation cannot provide. convenience, silencing the borrow checker, copying upstream idioms, reducing line count, and unmeasured performance claims are insufficient. first try a simpler representation or ownership model. using a dependency merely to hide unsafe code does not remove the obligation to understand its exposed guarantees.

| candidate boundary | why unsafe may be necessary | what remains safe |
| --- | --- | --- |
| native Vulkan, OS, or compiler FFI | the foreign API cannot express all lifetime, ABI, threading, and synchronization obligations in Rust's type system | validated parameters, ordinary bookkeeping, state transitions, and ownership outside the call |
| mapped native memory | constructing or accessing Rust values over a foreign allocation requires validity, bounds, layout, and lifetime proof | bounds calculations and safe views whose complete obligations are enforced |
| physical GPU-pointer operations | the shader may access a raw device address whose validity, aliasing, and lifetime depend on the host and GPU work | address transport, checked arithmetic, and shader logic that do not themselves require unsafe operations |
| native escape hatches | a caller can bypass owned resource and synchronization rules | the ordinary API, when it actually prevents such bypasses from invalidating safe operations |

these are investigation candidates, not permission to mark whole modules unsafe. no exception is justified until it has a completed record and source-local explanation.

## enforcement and source documentation

use `#![forbid(unsafe_code)]` in first-party crates or modules needing no exception. in a crate with necessary boundary code, use `#![deny(unsafe_code)]` and the smallest documented `#[allow(unsafe_code)]` scopes. do not put `forbid` on an ancestor that must contain an exception. use `#![deny(unsafe_op_in_unsafe_fn)]` so an unsafe function still identifies its unsafe operations explicitly. adapt equivalent enforcement to the supported upstream RustGPU toolchain without unrelated policy churn.

place a concrete `SAFETY:` comment beside every unsafe operation. cite its registry ID and explain how this call establishes the applicable invariants, rather than saying only “the caller guarantees safety.” public unsafe functions and traits need a `# Safety` contract. unsafe trait implementations, foreign interfaces, and unsafe attributes need equivalent local justification. a shared record may cover several sites only when it enumerates them and each site explains its local proof.

a safe signature promises that safe callers cannot violate the implementation's memory-safety preconditions. use it only when the implementation enforces that promise. raw pointer access, arbitrary shader dispatch, foreign resource ownership, and synchronization bypasses must not acquire safe signatures merely to reduce the unsafe count. if caller obligations remain unenforceable, expose a narrow, explicit unsafe contract.

## boundary registry

current implemented entries: **U-001, a compile-only physical-access shader fixture**. it has not been dispatched. add one `U-nnn` entry here for each distinct implemented boundary. this file owns the overview and complete review record. large supporting proofs may be linked, but the necessity, obligations, status, and source sites must remain visible here.

each entry must contain all fields below. mark an inapplicable field with a reason.

| field | required content |
| --- | --- |
| identity and scope | stable U ID, concise operation, host/shader/compiler classification, source files and symbols, exact revision or dirty-diff identity, all relevant call sites |
| necessity | required behavior, why safe Rust cannot express this operation, alternatives considered, and why a simpler or existing safe operation is insufficient |
| interface and owners | safe or unsafe public boundary, who creates/validates/uses/retires the resource, and exactly which obligations belong to callers |
| pointer and value validity | allocation/provenance, byte range, overflow, alignment, initialization, valid value/bit patterns, layouts and ABI, allowed casts, and reference creation if any |
| lifetime and aliasing | allocation lifetime, outstanding CPU/GPU users, mutable/shared aliases, resource movement, mapping/unmapping, and descriptor/address invalidation |
| concurrency and completion | thread affinity, Send/Sync claims, host/device synchronization, queue completion and visibility, and descriptor/resource retirement |
| failure paths | partial initialization, cleanup, device loss, allocation failure, panic/unwind or foreign exceptions, and guarantees after errors |
| proof at each site | the checks, types, ownership rules, or external specification that establish each precondition. identify assumptions the program cannot validate |
| verification | meaningful positive and negative tests, compiler/lint results, native validation where relevant, tools/configuration, evidence links, and untested conditions |
| review | reviewer identity, date, revision, findings, resolution, and whether review was self-review or independent. no invented independent approval |
| remaining risk and status | proposed, implemented-unreviewed, reviewed, or retired. list open assumptions and blockers without implying tests prove soundness |
| change history | record contract changes, added/removed sites, changed callers or dependencies, and the reason to repeat review |

### U-001. compile-only physical u32 access

status: reviewed for compile-only use. execution requires a host that establishes the contract below. no runtime safety result is claimed.

| field | record |
| --- | --- |
| identity and scope | `tests/compiletests/ui/physical_storage/access_physical64.rs::main` in the [aligned-access patch](patches/rustgpu-prerequisites/rustgpu-physical-access.patch), based on `0983e4e07ca382804c9f0084b36e049f8613d211`. fixture SHA-256 `18722cce7665957041773f09eeb42b6e343d8dc3a8ebbe6fc86868c8e8e33243`. the patch manifest pins the verified compiler commit. sites are the unsafe entry declaration and its two reads and one write. |
| necessity | T011 needs actual Rust load/store lowering through a physical address. safe address transport cannot generate those memory operations, and an assembly-only fixture cannot prove Rust lowering. a conventional descriptor access would exercise a different pointer class. |
| interface and owners | `pub unsafe fn main` states the caller obligations. no safe wrapper is exposed. the compiletest harness compiles only and never invokes the entry. a future runtime host must allocate, initialize, validate, synchronize and retire the allocation before this fixture may execute. |
| pointer and value validity | caller supplies a real device address for a live four-byte allocation containing an initialized u32, with alignment at least four. all u32 representations are valid. address transport is u64 and does not perform offset arithmetic. no Rust reference is fabricated from the raw address. logical output is a separate two-element u32 array. |
| lifetime and aliasing | physical allocation remains live and exclusively accessible by the one invocation until queue completion. it is disjoint from push constants and output, with no concurrent CPU/GPU aliases. the fixture does not move, map, free or reuse resources. |
| concurrency and completion | dispatch exactly one invocation. host establishes prior initialization visibility and waits for completion plus visibility before reading or retiring resources. the fixture provides no Send/Sync implementation, queue API or completion primitive. |
| failure paths | fixture owns no allocation and creates no native object. compilation/validation failure prevents dispatch. it does not attempt to recover from an invalid pointer. a future host must handle allocation/pipeline/submit failure, device loss and retirement separately. |
| proof at each site | first read relies on the entry's initialization/alignment/exclusivity contract. write uses the same valid allocation and wrapping u32 arithmetic. final read observes this invocation's initialized value. local SAFETY comments enumerate these obligations. the compiler cannot establish host allocation validity or completion. |
| verification | the baseline failed SPIR-V validation for missing Aligned operands. the corrected Rust source compiles and validates with the pinned nightly and compiled tools. manually reviewed output has two physical loads and one store with Aligned 4, while logical accesses carry no alignment. the two-target source gate passes without blessing. no CPU call or GPU dispatch of this fixture occurred. synthetic addresses are never dereferenced. |
| review | coordinator self-review, 2026-09-19, fixture hash above and E-018 patch. source inventory found one unsafe function, three explicit unsafe blocks and one narrow `allow(unsafe_code)`, all covered here. crate-level `deny(unsafe_code)` and `deny(unsafe_op_in_unsafe_fn)` compile successfully. reviewed the spirv entry macro's emitted interfaces and complete instruction stream. no independent review or complete dependency/macro safety audit. |
| remaining risk | compile-only use avoids execution but does not prove GPU safety. the host obligations are currently unenforced because no runtime host for this fixture exists. aggregate access, unaligned access, reference creation, races, atomics and arbitrary dispatch remain outside this record. |
| change history | introduced for the aligned scalar-access probe after E-017. repeat review when adding sites, changing data types/layout, exporting a callable library API or introducing a runtime caller. |

template for another boundary:

```text
### U-nnn. operation name
status / revision / scope / source sites:
required behavior and insufficient safe alternatives:
public interface and owner/caller obligations:
validity, layout, bounds, alignment, initialization, provenance:
lifetime, aliasing, synchronization, threading, completion:
failure and cleanup behavior:
how each source site establishes the contract:
tests, validation, evidence, and limits:
reviewer, review type, date, findings:
remaining risks and next action:
change history:
```

## checkpoint audit

inventory all first-party unsafe blocks, unsafe functions/traits/impls, foreign declarations/interfaces, unsafe attributes, and lint exceptions across every owned target and feature configuration. inspect relevant macro expansions and generated bindings separately. text search is a starting aid, not proof of complete coverage. record the method, tool versions, source scope, and exclusions.

map every site to its U record and reconcile the actual source inventory with this page. list dependency-provided unsafe boundaries separately with exact dependency identity, used APIs, trusted guarantees, and known limitations. distinguish first-party sites from dependency/generated totals instead of hiding one in the other. mirror the relevant safety contracts into the RustGPU patch so an upstream reviewer does not need this repository to understand it.

a checkpoint cannot close with an unlisted site, stale contract, unexplained lint waiver, or unreviewed boundary it relies on. a changed caller, layout, synchronization rule, feature configuration, or dependency can invalidate a prior review even when the unsafe line is unchanged. retire removed records with a pointer to the removal instead of leaving stale active claims.

run checks appropriate to the actual risk. CPU tools such as Miri or sanitizers can help supported host logic, but cannot establish native GPU correctness. use deterministic readback and Vulkan validation where applicable. passing tests and validation do not prove soundness. do not run undefined GPU accesses as negative tests expecting a recoverable error.

append the audit result, unresolved issues, and evidence scope to [execution](docs/execution.md). promote any reusable finding to [lessons](docs/lessons.md). reducing unsafe sites is useful only when the remaining contracts become simpler and more credible.

## references

the [Rust book](https://doc.rust-lang.org/book/ch20-01-unsafe-rust.html), [Rustonomicon](https://doc.rust-lang.org/nomicon/safe-unsafe-meaning.html), and [unsafe-operation lint guide](https://doc.rust-lang.org/edition-guide/rust-2024/unsafe-op-in-unsafe-fn.html) explain Rust's obligations. [Vulkan queue submission in ash](https://docs.rs/ash/latest/ash/struct.Device.html#method.queue_submit) is an example of a binding with caller obligations. use exact toolchain/dependency versions for implementation reviews.
