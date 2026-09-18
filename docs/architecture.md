# architecture contract

## intended shape

the product is a lightweight explicit Rust graphics framework with native DirectX 12, Vulkan 1.4, and Metal 4 backends. AGFX is an inspiration for explicit devices, queues, command recording, resource states, handles, capabilities, backend separation, shader ABI, and test-facing behavior. this project owns its public contracts and does not promise drop-in AGFX compatibility.

use explicit ownership and small unsafe boundaries around DirectX 12, Vulkan 1.4, Metal, windowing, and compiler interfaces. resource destruction follows backend completion, not Rust lexical scope alone. record each public behavior, native difference, and portability limit with its reason and test.

## backend targets

| platform | required backend | decisive proof |
| --- | --- | --- |
| Windows | DirectX 12 | native execution and readback on DirectX 12 |
| Linux | Vulkan 1.4 | native execution and readback on Vulkan 1.4 |
| MacOS | Metal 4 | native execution and readback on Metal |

ordinary bindless descriptor arrays and NoGraphicsAPI's native descriptor-heap and untyped-pointer model are separate explicit contracts. shared storage concepts do not make their shader ABIs interchangeable.

## shader boundary

every shader artifact records source language, stage, entry point, compiled bytes, target environment, binding and layout metadata, and compiler identity. Rust, Slang, and HLSL each need an adapter to this boundary. every applicable language and backend cell remains open until it compiles, validates, executes, and matches the project-owned expected behavior.

Rust-to-DirectX 12 and Rust-to-Metal translation, Slang binding adaptation, and several advanced stages are research risks, not assumed capabilities. bounded feasibility probes precede broad implementation. a failed compiler route changes the implementation plan but does not silently remove a required cell.

## physical pointers and native heaps

the NoGraphicsAPI prototype requires full-width GPU addresses, correct target layout, pointer arithmetic and casts, aligned loads and stores, optimizer survival, and a narrow unsafe shader API. tests must catch address truncation, overflow, layout mismatch, and invalid alignment.

native heap support requires SPIR-V descriptor-heap and untyped-pointer instructions, validation through the compiler pipeline, deterministic texture and sampler access, and host feature agreement. a conventional binding-to-heap mapping may be used as a measured intermediate step, but it does not complete native heap support.

## excluded architecture

do not add a render graph, ECS, universal RHI layer, mandatory shared ownership, or broad runtime abstraction without a concrete requirement and evidence. mobile, consoles, sparse resources, production packaging, and performance tuning are later scope unless a baseline depends on them.

historical reasons for keeping contracts small and owned are summarized in [architecture and dependencies](postmortems/architecture-and-dependencies.md).
