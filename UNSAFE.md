# unsafe Rust policy and audit

**read this before adding, changing, or reviewing unsafe code.** this is the authoritative project policy and visible registry, linked from the root README, agent instructions, specification, and checkpoint prompt.

## current audit status

| area | observed state |
| --- | --- |
| first-party Theta Rust | one diagnostic Rust test module, with `forbid(unsafe_code)`, and no Cargo workspace or runtime implementation. no first-party Rust unsafe exception exists |
| proposed native and shader boundaries | candidates only, listed below. none has implementation approval by implication |
| dependencies, generated code, macros, external hosts | pinned NGAPI C++ and Vulkan SDK 1.4.357.0 were used by the diagnostic below. these are native dependencies, not safe Rust or an audited finished Rust port |
| RustGPU contribution | the tracked rspirv prerequisite patch repairs instruction placement using safe Rust. separate RustGPU inference and SPIR-T grammar candidates are still under test. no verified unsafe shader boundary is claimed |
| runtime and soundness evidence | actual NGAPI device/command-context probes and non-executed SPIRV-Tools fixtures passed at E-007. no Rust shader execution or Rust boundary soundness result exists |

source pins are in [research](docs/specs/001-theta-prototype/research.md), and current evidence is in [execution](docs/execution.md). **zero Rust implementation is not evidence of a safe completed system.** replace these rows with revision-specific Rust inventories as code is added.

the [RustGPU diagnostic module](tools/theta/rustgpu-instruction-probes.rs) uses upstream parser/linker/test APIs and standard owned vectors and strings. its only added lint is `#![forbid(unsafe_code)]`. it contains no unsafe operation, native call, or shader dispatch. the upstream compiler and SPIRV-Tools remain dependency boundaries with their own unsafe code. this probe does not audit those complete implementations or prove runtime soundness.

E-009 patch audit: the [rspirv loader patch](patches/rustgpu-prerequisites/rspirv-untyped-global.patch) changes safe enum matching and adds a safe serialization/placement regression. no unsafe operation, public memory-access API, native call, or lint exception is added. coordinator self-review checked the complete patch against its pinned source and the full rspirv test results. no independent review or complete dependency safety audit is claimed. the registry still needs no new U record.

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

current implemented entries: **none**. add one `U-nnn` entry here for each distinct implemented boundary. this file owns the overview and complete review record. large supporting proofs may be linked, but the necessity, obligations, status, and source sites must remain visible here.

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

inline template for the first implementation:

```text
### U-001. operation name
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
