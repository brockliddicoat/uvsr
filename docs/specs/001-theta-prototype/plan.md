# implementation plan: RustGPU on Windows Vulkan and NGAPI

## summary

deliver a focused local RustGPU PR draft enabling actual NoGraphicsAPI. start on native Windows Vulkan, the API path available for testing on the current development machine subject to capability probes. prioritize direct Vulkan and Vulkan through NGAPI. build only the close Rust AGFX slice needed to support useful experiments before widening the testbed.

complete AGFX and ShaderToHuman source parity remains valuable supporting work. Metal is the second backend direction and DirectX the lowest, with the most acceptable documented compromises. no additional-language or secondary-backend implementation belongs on the primary critical path.

## technical context

active shader route: RustGPU to SPIR-V to native Vulkan. primary host OS: Windows. additional Vulkan portability target: Linux. Vulkan 1.4 and every needed extension/feature must be queried explicitly. a Windows machine or Vulkan version string alone is not proof of native-heap support.

keep the RustGPU contribution checkout separate from Theta and the actual NGAPI host checkout. unchanged AGFX/ShaderToHuman references are read-only. record ownership, tools, patches, and hashes. adapt source platform assumptions so the AGFX Vulkan implementation can run on Windows, with native loader and window/surface handling where required. no DirectX fallback can close the Vulkan gate.

use [source pins](research.md), [shader contracts](contracts/shader-abi.md), and the [upstream checklist](upstream-tests.md). no implementation currently exists.

## constitution check

| constraint | application |
| --- | --- |
| contribution first | M3 depends on the real consumer and relevant compiler evidence, not full testbed parity |
| Vulkan priority | Windows direct Vulkan and actual NGAPI are explicit. Linux evidence stays separate. future API limitations do not weaken Vulkan |
| source parity | scoped mappings retain the complete inventory and deferred variants |
| safe default | [UNSAFE.md](../../../UNSAFE.md) is a required input and checkpoint audit, with local contracts and lints |
| small owners | no speculative adapters, second RHI, ECS, render graph, or mandatory shared ownership |
| evidence | each experiment names its oracle, source/configuration, result, and missing evidence |
| recoverability | ignored current state plus tracked [execution](../../execution.md) and [lessons](../../lessons.md) |
| provenance | translations retain source identity and controlling terms |

## proposed source owners

create these only when an actual slice needs them. modules may share one crate until a toolchain or consumer requires separation.

```text
Cargo.toml
crates/agfx/                 close Rust port, Vulkan first, Windows integration
crates/shader-to-human/      supporting Rust shader library translation
shaders/rust/               Rust-authored fixtures
tests/parity/               reviewed source mappings and fixture manifests
tools/test_report/          supporting AGFX-style report and queries
docs/specs/001-theta-prototype/
UNSAFE.md                   policy and complete first-party boundary registry
docs/execution.md           durable checkpoint and experiment summaries
docs/lessons.md             reusable evidence-qualified findings
work/theta/                ignored work card, notes, downloads, builds, raw evidence
```

artifact building and minimal result handling start as small modules or tools near their consumers. do not create a multi-language compiler service or a crate per proposed concept. upstream tests use RustGPU's existing harnesses. keep its shader toolchain separate from the host only when the pinned tools require it.

## primary milestones

| milestone | exit evidence | dependencies |
| --- | --- | --- |
| M0, baseline | pinned main/sources, Windows tool/device facts, reference slice, physical-pointer/heap tool probes | activation |
| M1, compiler and slice | generic lowering/library changes with focused regressions, minimal native Windows Vulkan Rust AGFX readback and lifetime proof | relevant M0 evidence. compiler work and the slice can proceed independently |
| M2, actual NGAPI | combined Rust pointer/heap readback and cube in actual NGAPI on Windows Vulkan, with deterministic oracles | compiler tests, host feature agreement, explicit ABI |
| M3, local PR draft | reviewable RustGPU diff, docs, safety contracts, crosswalk, reproducible consumer, PR text, current CI status | M1/M2 evidence for promised features. no dependency on full parity or report polish |

use a small direct Vulkan fixture to locate compiler versus host failures. reuse an existing upstream fixture when it suffices. adding the exact NGAPI native-heap profile to the AGFX port is supporting work unless a specific failure needs it. avoid implementing two complete frameworks to prove one compiler feature.

M3 can be prepared while unavailable upstream CI is clearly pending. successful full CI, external publication, review, and merge have their own evidence and authority. do not call a partial test run the complete upstream gate.

## bounded experiments

first inspect the native Windows Vulkan toolchain, loader, adapter, required extension features, and NGAPI host build/ABI. distinguish source feasibility, build, offscreen readback, presentation, and actual consumer execution. Linux-only success does not satisfy Windows proof.

probe address representation and mixed-width arithmetic, then physical-pointer and heap/untyped instructions through the actual parser, SPIR-T, linker, optimizer, serializer, and validator. cover installed-tools and compiled-tools configurations where relevant. use simple Rust-authored compute, then vertex/fragment fixtures with known descriptors and root data. add task/mesh evidence only for promised exposed behavior.

the active route does not require a cross-compiler to another graphics API. preserve explicit artifact metadata and native capabilities so future routes have a clear boundary. do not weaken Vulkan's pointer, heap, layout, or synchronization contract for speculative portability.

if local hardware lacks required heap features, keep that runtime proof blocked and continue eligible compiler, ABI, or reference work. record the exact missing capability and a reproducible supported-device recipe. never substitute ordinary arrays or another backend for native heaps.

## supporting parity track

after the decisive slice, expand AGFX owner by owner and the ShaderToHuman Rust library and portable fixtures. keep source public/native/Ez behaviors, assertions, ownership, examples, and all variant dispositions traceable. reference goldens and algorithms are preserved.

implement full report tabs and bounded queries when real results from both suites justify them. use the same minimal result records already serving compiler diagnosis. measured report usability is separate from correctness and contribution readiness.

Linux Vulkan portability belongs to this track unless an upstream regression requires it earlier. Metal remains the second backend direction, DirectX last. no native implementation tasks for either are created by this plan. complete parity with the entire upstream backend set remains unclaimed while those variants are deferred.

## size and complexity

aim for less handwritten code than the equivalent source behavior through ordinary Rust types, explicit ownership, shared real consumers, and removal of duplicate C/C++ wrappers. this is a design preference to measure, not a forecast or a reason to compress code.

before a size claim, freeze a behavior manifest and file lists for both source and port. use the same named/versioned counting tool and options. report nonblank, noncomment handwritten implementation LOC separately for host, shaders, and tests/harness. report generated/vendor code, dependency count/identity, build scripts, and documentation separately so work moved outside the main tree remains visible.

compare only matched capabilities and behavior. a Windows Vulkan subset cannot be compared with all AGFX backends and called a full-port reduction. record deferred APIs/examples, code shared with unmeasured features, and any limits on separating the source. publish totals and labeled estimates at coherent checkpoints with the raw manifest in ignored evidence. keep the durable summary in execution.

preserve meaningful tests, diagnostics, explicit invariants, license notices, readability, and safety documentation. do not introduce macros, code generation, dense formatting, hidden dependencies, or unsafe shortcuts to reduce LOC. if an extra owner or abstraction has no current consumer or measurable reason, omit it.

## validation and recording

use pure ABI checks and compiler positive/negative/disassembly tests before device work. then deterministic readback, image oracles, source comparisons, and relevant lifecycle sequences. keep alpha, stale-artifact, missing-golden, empty-run, and interruption controls appropriate to the current slice.

run focused checks while editing and the relevant gate at a coherent checkpoint. preserve the repository workflow. ordinary hosted checks do not require nonexistent local GPUs, and do not establish extension runtime correctness. exact upstream CI obligations remain in their [crosswalk](upstream-tests.md).

each checkpoint updates task evidence, the unsafe audit, execution history, and reusable lessons. raw logs stay ignored. record failed approaches and what would justify retrying. [quickstart](quickstart.md) defines bounded reading, work-card state, and recovery.
