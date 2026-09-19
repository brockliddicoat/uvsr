# shader and native-host contracts

## safe Rust by default

**[UNSAFE.md](../../../../UNSAFE.md) owns the complete policy, visible registry, source-local documentation rules, and checkpoint audit.** read it before changing native bindings, mapped memory, physical-pointer APIs, or safety-sensitive callers. no blanket unsafe allowance exists for the AGFX port or compiler.

safe address transport and ordinary logic remain safe. operations with unenforced caller obligations must have honest unsafe contracts. safe wrappers must enforce all preconditions, including lifetime, aliasing, and synchronization, rather than hiding obligations.

## artifact boundary

the active route is RustGPU to SPIR-V to Vulkan, directly and through actual NGAPI. each artifact records authored language/source hash, compiler chain/options and dependency hashes, target environment/payload type/hash, stage, entry, workgroup metadata, binding/layout profile, required capabilities, and original-source diagnostics.

reject incompatible payloads or profiles before pipeline creation. explicit data boundaries allow later shader languages and APIs, without active adapter implementations or speculative abstractions. no future compiler path is assumed to preserve this ABI.

## Windows and Linux Vulkan

native Windows Vulkan is the primary local path, including actual NGAPI. select the Vulkan backend explicitly instead of inheriting source OS routing to DirectX. record loader, adapter/driver, extension properties, enabled features, surface/platform path when applicable, and host/tool identity.

distinguish offscreen shader execution from native Windows presentation. both use Vulkan when tested. Linux portability uses the same shader/profile contracts with separately recorded native evidence. a Linux result does not establish Windows support, and a Vulkan API version does not establish descriptor-heap extension availability.

## ordinary AGFX profile

preserve source handle widths and sentinels by field, offsets/strides, root/push constants, descriptor namespaces, sampler behavior, coordinate conversion, stage I/O, matrix convention, and completion-based resource lifetime. pinned AGFX Vulkan uses resources at set 0 binding 0, samplers at binding 1, and optional acceleration structures at binding 2.

inspect actual generated modules. source-compatible layout is not implied by compiler defaults. task/mesh and ray-query behavior need feature-specific proof when exposed. keep future native backend limits from silently changing the Vulkan profile.

## NGAPI native-heap profile

pinned NGAPI uses full physical buffer addresses, separate resource/sampler heaps, scalar-compatible layout, row-major matrices, and root data at most 256 bytes with a size divisible by four. preserve `vertexMain`, `fragmentMain`, `computeMain`, `taskMain`, and `meshMain` where corresponding entries are exposed. the host accepts SPIR-V and uses `vkCmdPushDataEXT` on the native root path.

emit and preserve actual descriptor-heap/untyped-pointer instructions through code generation, SPIR-T, linking, optimization, serialization, and validation. conventional bindings may help isolate a failure, but cannot complete this profile.

actual NGAPI on Windows Vulkan is the primary consumer proof. a small direct Vulkan fixture isolates compiler/host behavior. adding the exact native-heap profile to the AGFX port is supporting work unless a concrete primary-path failure requires it. when added, expose a focused optional Vulkan profile without a second RHI or forcing the extension onto ordinary AGFX operation.

## physical addresses

the selected compiler foundation adds an explicit Vulkan `-physical64` target with eight-byte Rust pointers and `usize`, while existing targets retain their four-byte ABI. [E-016](../../../execution.md#e-016-2026-09-19-proved-explicit-rust-pointer-width-layouts) records the layout decision. [E-017](../../../execution.md#e-017-2026-09-19-lowered-physical-address-conversions) adds PhysicalStorageBuffer64 and storage-class-constrained integer/raw-pointer conversions on SPIR-V 1.3 or newer. narrow casts truncate after a 64-bit address conversion, and integer-to-pointer casts use the source integer's signedness. logical-pointer address conversion remains rejected. T010 remains open until the physical-address API and its supported operation boundary are proved.

use explicit `u64` fields for device-address transport. do not share pointer/`usize`-containing layouts between target ABIs or reuse dependencies built for the other target. never widen arbitrary `u32` operations or patch emitted output instead of correct typed lowering. the upstream-facing target documentation is included in the [ABI patch](../../../../patches/rustgpu-prerequisites/README.md#explicit-rust-pointer-width-foundation).

validate eight-byte address representation, nested address-containing structures, offsets, array strides, scalar/vector/matrix layout, conversions, supported pointer operations, alignment operands, and alias decorations. preserve logical-pointer behavior and diagnose unsupported operations cleanly.

[E-018](../../../execution.md#e-018-2026-09-19-compiled-aligned-physical-accesses) verifies scalar physical u32 read/write code generation with Aligned 4 and removal of alignment from logical accesses while retaining other memory flags and scopes. [E-019](../../../execution.md#e-019-2026-09-19-preserved-qptr-memory-effects) adds actual qptr memory-operand lowering/lifting and optimized effect checks with a native optimizer prerequisite. this is compile/validation evidence only. aggregate accesses, complete pointer operations, Rust volatile intrinsics and runtime host obligations remain open.

raw physical-address APIs state allocation validity, range, alignment, lifetime, aliasing, and synchronization obligations under the central unsafe policy. host ownership and queue completion establish when referenced resources and descriptors may be reused. synthetic addresses are never dereferenced. enable memory-model/int64 features only where the artifact and API require them, not simply because addresses contain 64 bits.

## minimum cases

use exact arithmetic/buffer readback, nonzero descriptor slots, distinct samplers/textures, uniform/divergent indices, root aggregates, matrix orientation, aligned access, mixed logical/physical operations, optimized/unoptimized modules, missing-feature diagnostics, and valid lifecycle sequences. add stage cases for every promised exposed stage.

record the first failing phase and emitted capabilities alongside output. [research](../research.md) owns references and tool constraints. neither Vulkan profile proves another API, and compiler-only validation does not prove device execution.
