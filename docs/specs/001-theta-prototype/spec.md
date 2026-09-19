# feature specification: RustGPU on Vulkan through NGAPI

feature: `001-theta-prototype`. revision: 2.0. status: implementation active. [tasks](tasks.md) owns completion.

primary outcome: a focused local RustGPU PR draft enabling Rust-authored shaders in actual NoGraphicsAPI. **Vulkan on Windows is the primary local implementation and test path.** direct Vulkan and Vulkan wrapped by NGAPI receive the most design attention. Linux Vulkan remains a portability target.

the AGFX Rust port is a high-quality, lightweight reusable testbed. its complete source parity and ShaderToHuman parity remain supporting outcomes, not barriers to the primary contribution. Metal is the second backend direction. DirectX is lowest priority and may incur the largest documented compromises. keep adaptable boundaries, without implementing secondary APIs or Slang/HLSL support in this plan.

baseline and decisions: [research](research.md). principles: [constitution](../../../.specify/memory/constitution.md). **unsafe policy and complete audit: [UNSAFE.md](../../../UNSAFE.md).**

## user scenarios and testing

### US6. draft a useful RustGPU contribution, priority P1

as an upstream contributor, i can review focused generic compiler/library changes that enable the actual NGAPI consumer, with regression tests, understandable safety and ABI contracts, and reproducible evidence.

independent test: the [upstream crosswalk](upstream-tests.md) links each promised capability to a test and observed result. the local PR draft includes the consumer recipe, exact candidate revision, CI outcomes and unrun jobs, attribution, and remaining limitations. drafting, public CI success, publication, and maintainer acceptance are separate states. full framework parity is not a dependency.

### US1. execute Rust shaders through NGAPI on Windows Vulkan, priority P1

as a developer using the current Windows machine, i can run Rust-authored physical-pointer and native-heap shaders in actual NGAPI over native Vulkan. a minimal direct Vulkan slice helps isolate host and compiler failures.

independent test: deterministic buffer readback and a textured cube exercise real addresses, nonzero resource/sampler indices, root layout, and completion in optimized and unoptimized variants. record the Windows Vulkan loader, adapter, driver, enabled features, shaders, and host identities. ordinary descriptors, a replacement host, Linux-only evidence, or DirectX execution do not satisfy this Windows NGAPI story.

### US2. retain AGFX behavior in a small Rust testbed, priority P2

as a testbed user, i retain the implemented AGFX devices, queues, commands, handles, states, capabilities, native access, and meaningful Ez behavior in ordinary Rust. Vulkan on Windows is usable independently of the source project's platform/backend routing.

independent test: map source behavior to Rust owners and preserved assertions. compare matched fixtures and report missing/deferred variants. build the smallest useful Vulkan slice before expanding the port. compare line counts only over equivalent behavior. a C++ wrapper is not the finished Rust port.

### US3. retain room for future pivots, priority P2

as a future user, i can add another shader compiler or graphics backend at explicit artifact and native boundaries without redesigning the Vulkan path.

independent test: inspect whether stage, entry, payload, target, profile, required capabilities, and compiler identity are explicit data. keep native capability differences visible. this is a boundary review, not a requirement to build placeholder adapters or prove future backends now.

### US4. preserve ShaderToHuman source behavior, priority P2

as a shader author, i can use a source-faithful Rust translation of ShaderToHuman's library, regressions, and distinct documentation/example behavior.

independent test: preserve the five golden groups and account for all source library functions and distinct examples with equivalent portable fixtures. replacing Gigi hosting is allowed, deleting hosted behavior unnoticed is not. this supporting parity track does not gate the RustGPU PR.

### US5. audit testbed failures as a human or agent, priority P3

as a reviewer, i see AGFX's report appearance with separate **AGFX** and **ShaderToHuman** tabs, and can retrieve bounded structured failure evidence.

independent test: verify counts, filters, expected/actual/difference artifacts, keyboard access, deep links, and agreement with JSON. missing goldens, stale identity, empty runs, and alpha-only defects must not become passes. full report polish and measured usability are supporting deliverables. the primary contribution needs only useful, truthful reproduction records.

## functional requirements

| id | requirement |
| --- | --- |
| FR-001 | use the pinned current Theta main as the starting point, preserving workflows, assets, provenance, history, and unrelated work |
| FR-002 | make a close ordinary Rust AGFX port as a supporting testbed, beginning with native Windows Vulkan and a minimal useful slice |
| FR-003 | account for AGFX APIs, meaningful C/Cpp/Ez behavior, registrations, shader helpers, and examples, separating implemented, pending, and user-deferred scope |
| FR-004 | prioritize native Windows Vulkan and actual NGAPI on Windows Vulkan, retain Linux Vulkan portability, and keep future backend/language boundaries explicit without an implementation matrix |
| FR-005 | keep shader payload/hash, authored language, stage, entry, target, binding/layout profile, capabilities, and tool identity explicit. the active route is RustGPU to SPIR-V to Vulkan |
| FR-006 | preserve full-width physical addresses, target layout, casts, arithmetic, alignment, alias rules, and optimizer survival without widening unrelated integers |
| FR-007 | support real native descriptor heaps and required untyped pointers with correct resource/sampler indexing and full compiler-pipeline handling |
| FR-008 | prove combined pointer/heap behavior with Rust shaders in actual NGAPI on Windows Vulkan. use a small direct Vulkan fixture to diagnose compiler/host boundaries |
| FR-009 | preserve ShaderToHuman library behavior and account for every regression, documentation branch, and distinct example in the supporting parity track |
| FR-010 | preserve source fixtures and oracle semantics. correct demonstrated harness defects separately without silently relaxing thresholds |
| FR-011 | preserve AGFX report appearance with separate AGFX and ShaderToHuman tabs and isolated suite counts/filters in the supporting report |
| FR-012 | use one versioned result model with stable IDs, exact provenance, first mismatches, and reproduction argument arrays. add bounded queries as the report grows |
| FR-013 | required missing cases, absent hardware, empty/all-skipped runs, interruption, and stale artifacts are non-passes |
| FR-014 | retain explicit native capabilities, resource transitions, submission completion, descriptor lifetime, and retirement. future backends must not force avoidable Vulkan restrictions |
| FR-015 | separately account for draft-author, maintainer-review, feature-specific, and current public CI obligations in the RustGPU contribution |
| FR-016 | prepare focused generic RustGPU patches, docs, and local PR text independent of AGFX/NGAPI host dependencies. primary completion does not await full testbed parity |
| FR-017 | retain exact source mappings, licenses, copyright notices, and third-party restrictions for translations and imports |
| FR-018 | preserve execution settings, a small current work card, useful experiment notes, and bounded recovery |
| FR-019 | measure human/agent report usefulness before claiming an improvement. this evaluation is supporting work, not a compiler-contribution gate |
| FR-020 | distinguish actual-consumer proof, local PR draft, public CI, publication/acceptance, scoped testbed parity, and deferred future compatibility |
| FR-021 | safe Rust is the default. enforce lints and the prominent [unsafe registry](../../../UNSAFE.md), with necessity, obligations, source sites, review, evidence, and limits for every exception |
| FR-022 | maintain tracked [execution](../../execution.md) and [lessons](../../lessons.md), including failed approaches, evidence scope, uncertainty, reusable findings, and next actions |
| FR-023 | keep the port lightweight, preferably smaller than matched source behavior. use fixed scope/counting rules and preserve correctness, tests, clarity, and safety documentation |

## edge cases

cover full-width address transport and nested layouts, mixed logical/physical pointers, nonuniform heap indices, distinct nonzero resource/sampler slots, wrong profiles, absent enabled features, premature descriptor reuse, readback pitch, matrix orientation, alpha, sRGB boundaries, and interrupted runs. ShaderToHuman adds persistent UI state and deterministic input sequences.

synthetic high address bits are for non-dereferencing representation tests only. memory access uses actual allocated addresses. undefined GPU access is not a diagnostic oracle. Vulkan API version alone does not establish extension support. Windows presentation requires the appropriate native surface path, and offscreen proof does not imply presentation has passed.

## measurable success criteria

| id | completion condition and scope |
| --- | --- |
| SC-001 | primary: actual NGAPI on Windows Vulkan passes combined Rust pointer/heap readback and cube oracles with optimized/unoptimized variants and exact source/tool/device/shader identity |
| SC-002 | supporting: the complete source inventories have dispositions, and every behavior required for the declared Vulkan parity scope has a passing mapping. deferred backends remain visible and prevent an unqualified full-upstream-parity claim |
| SC-003 | primary: a minimal Rust AGFX slice executes directly on Windows Vulkan, artifact/native boundaries are explicit, and the Windows and Linux Vulkan evidence rows remain separate. Linux runtime portability remains pending until executed |
| SC-004 | supporting: all five ShaderToHuman golden groups pass their source contract and additional library/documentation/example behavior is accounted for |
| SC-005 | primary: minimal records and negative controls propagate failure truthfully. supporting: all report/query counts and statuses agree and required report negative controls are detected |
| SC-006 | primary: focused RustGPU diff, safety/ABI docs, promised-feature tests, real-consumer recipe, and local PR text exist at the proposed revision. all CI rows have results or explicit pending status. a full-CI claim requires every applicable job to pass |
| SC-007 | primary: another agent can recover the task, owners, source identity, decisive check, failed hypotheses, and next action from the entry and work card |
| SC-008 | supporting: human navigation and seeded-fault agent evaluations have measured outcomes. no improvement is claimed from format alone |
| SC-009 | primary and supporting: safe-only owners enforce the prohibition and all necessary exceptions have complete current registry records, local contracts, relevant checks, and honest review status |
| SC-010 | every meaningful checkpoint records execution and considers reusable lessons, with evidence classifications and correction history. preparation alone does not complete future recording duties |
| SC-011 | the delivered slice has a matched-scope size/dependency accounting and a simplicity review. no unsupported promise that Rust must reduce LOC, and no sacrificed behavior or documentation |

## completion boundaries

the primary milestone is the local RustGPU contribution draft after Windows Vulkan and actual-consumer proof. it may truthfully list unavailable public CI jobs as pending. it cannot claim the complete upstream gate or acceptance before those results exist. external publication follows actual authority.

supporting AGFX, ShaderToHuman, reports, and Linux runtime portability continue under their own tasks. Metal and DirectX implementation and additional shader-language adapters are future work outside this implementation plan. their inventory entries are deferred, never passed or silently erased.

source parity is equivalent supported behavior and traceability, not binary C++ ABI compatibility or identical pointer idioms. [the parity contract](contracts/source-parity.md) defines the denominators. retained UVSR scenes, production packaging, a new renderer, and speculative performance work are not prerequisites for the compiler contribution.
