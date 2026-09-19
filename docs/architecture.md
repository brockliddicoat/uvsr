# architecture contract

## purpose and priority

the primary product of this work is a RustGPU contribution enabling Rust shaders in actual NoGraphicsAPI. Vulkan is the design center, both as a native API and as the API wrapped by NGAPI. the close Rust AGFX port is a small, reusable testbed with [source parity](specs/001-theta-prototype/contracts/source-parity.md), not a prerequisite to complete before compiler work.

| path | role and decisive proof |
| --- | --- |
| native Windows Vulkan | primary local path on the current Windows machine. native loader, device, submission, readback and presentation where used |
| actual NGAPI on Windows Vulkan | primary consumer. Rust shaders execute with physical pointers and native descriptor heaps in the real host |
| Linux Vulkan | portability target for the same Vulkan contracts. independent evidence, not a replacement for Windows testing |
| Metal | second backend priority, later implementation. keep native differences explicit |
| DirectX | lowest priority, later implementation with the most acceptable documented compromises |

availability is queried, not assumed. unsupported local extension hardware is a visible blocker for the corresponding runtime proof. WSL, another backend, ordinary descriptor arrays, or a replacement host cannot be relabeled as the required Windows NGAPI result.

## small owned boundaries

preserve AGFX devices, queues, commands, resource states, handles, limits, native access, optional capabilities, and meaningful Ez behavior using ordinary Rust. adapt platform assumptions so Vulkan is usable on Windows. source platform routing must not make Windows select DirectX implicitly.

separate platform/window integration from graphics API selection only as much as the real Windows Vulkan path needs. share code for actual shared behavior. avoid a generic platform framework or empty backend implementations.

use safe Rust and explicit ownership. [UNSAFE.md](../UNSAFE.md) owns the safety policy and visible inventory. lexical lifetimes do not establish GPU completion. resource and descriptor retirement must follow the last completed use.

## shader boundary

the active route is RustGPU to SPIR-V to Vulkan. an artifact records authored language, stage, entry point, payload type/hash, target, compiler identity, binding/layout profile, and required capabilities. keep these data explicit and avoid compiler-specific objects in the public resource API. future languages or native APIs may provide different payloads and profiles without requiring placeholder adapters now.

ordinary AGFX descriptor bindings and NGAPI's native descriptor heaps are different [shader contracts](specs/001-theta-prototype/contracts/shader-abi.md). do not flatten them into a least-common-denominator API. where the AGFX testbed needs the NGAPI heap contract, expose a narrow Vulkan capability/profile. the actual NGAPI host remains separate consumer evidence.

future Metal or DirectX restrictions may require optional paths or reduced coverage there. they do not justify weakening Vulkan addressing, indexing, synchronization, diagnostics, or shader ABI. record each real compromise against a concrete feature, without speculative compatibility work.

## compiler and host correctness

prove full-width addresses, correct layout, casts, alignment, alias semantics, and optimizer survival. retain ordinary integer and logical-pointer behavior. raw shader operations with unenforced caller obligations remain explicitly unsafe.

native heap support requires the real descriptor-heap/untyped-pointer instructions to survive parsing, code generation, SPIR-T, linking, optimization, serialization, and validation. host capability queries, feature enablement, root data, descriptors, and lifetime rules must agree with the emitted module.

keep generic RustGPU fixes and regression tests independent of AGFX, ShaderToHuman, and NGAPI dependencies. reuse existing upstream Vulkan fixtures when useful. a broad framework is not needed to diagnose a compiler bug.

## excluded architecture

no render graph, ECS, second RHI, mandatory shared ownership, general compiler plugin system, new renderer, production packaging, or speculative backend scaffolding. preserve understandable contracts and meaningful tests while pursuing a small port. [size accounting](specs/001-theta-prototype/plan.md#size-and-complexity) compares equivalent behavior instead of rewarding omitted functionality.

[historical lessons](postmortems/architecture-and-dependencies.md) explain the ownership risks. [current lessons](lessons.md) record what this project actually learns.
