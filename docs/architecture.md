# architecture contract

## intended shape

the product is a lightweight explicit Rust graphics framework with native D3D12, Vulkan, and Metal 4 backends. AGFX is the concrete compatibility and architecture reference for devices, queues, command recording, resource states, handles, capabilities, backend separation, shader ABI, and test-facing behavior. merge duplicate reference concepts only when their observable behavior is equivalent.

use explicit ownership and small unsafe boundaries around D3D12, Vulkan, Metal, windowing, and compiler interfaces. resource destruction follows backend completion, not Rust lexical scope alone. document every deliberate difference from the pinned AGFX behavior with its reason and test.

## backend targets

| platform | required backend | decisive proof |
| --- | --- | --- |
| Windows | D3D12 | native execution and readback on D3D12 |
| Linux | Vulkan | native execution and readback on Vulkan |
| macOS | Metal 4 | native execution and readback on Metal |

the Vulkan backend must support AGFX's ordinary bindless descriptor-array profile. NoGraphicsAPI's native descriptor-heap and untyped-pointer profile is a second explicit Vulkan contract. sharing storage concepts does not make their shader ABIs interchangeable.

## shader boundary

every shader artifact records source language, stage, entry point, compiled bytes, target environment, binding/layout metadata, and compiler identity. Rust, Slang, and HLSL each need an adapter to this boundary. every applicable language/backend cell remains open until it compiles, validates, executes, and matches its reference behavior.

Rust-to-D3D12 and Rust-to-Metal translation, Slang binding adaptation, and several advanced stages are research risks, not assumed capabilities. bounded feasibility probes precede broad implementation. a failed compiler route changes the implementation plan but does not silently remove a required cell.

## physical pointers and native heaps

the NoGraphicsAPI prototype requires full-width GPU addresses, correct target layout, pointer arithmetic and casts, aligned loads and stores, optimizer survival, and a narrow unsafe shader API. tests must catch address truncation, overflow, layout mismatch, and invalid alignment.

native heap support requires SPIR-V descriptor-heap and untyped-pointer instructions, validation through the compiler pipeline, deterministic texture and sampler access, and host feature agreement. a conventional binding-to-heap mapping may be used as a measured intermediate step, but it does not complete native heap support.

## excluded architecture

do not add a render graph, ECS, universal RHI layer, mandatory shared ownership, or broad runtime abstraction without a concrete requirement and evidence. mobile, consoles, sparse resources, production packaging, and performance tuning are later scope unless a baseline depends on them.
