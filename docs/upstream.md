# RustGPU contribution boundary

the primary project deliverable is a focused local RustGPU PR draft enabling Rust shaders in actual NoGraphicsAPI on native Windows Vulkan. the AGFX port is a reusable testbed. complete testbed parity and future backends do not gate the contribution.

## compiler scope

the inspected RustGPU baseline uses logical addressing and 32-bit logical pointers. [draft PR 237](https://github.com/Rust-GPU/rust-gpu/pull/237) supplies physical-pointer groundwork and unresolved review concerns around casts, alignment, aliasing, operations, width/overflow, tests, and scope. it is evidence, not an accepted implementation.

[issue 524](https://github.com/Rust-GPU/rust-gpu/issues/524) tracks native heaps. merged [PR 534](https://github.com/Rust-GPU/rust-gpu/pull/534) provides entry-point groundwork. prove the actual descriptor-heap/untyped instructions through the whole tool pipeline and actual Windows Vulkan NGAPI consumer.

use the [upstream crosswalk](specs/001-theta-prototype/upstream-tests.md) for exact obligations and [research](specs/001-theta-prototype/research.md) for dated pins/status. preserve existing upstream platform regressions even though this testbed prioritizes Windows Vulkan.

## patch discipline

keep AGFX, ShaderToHuman, and NGAPI host changes outside generic RustGPU commits. split necessary compiler prerequisites, physical pointers, and heap APIs into reviewable changes when useful. reuse upstream compiletests/difftests and minimal Vulkan fixtures. do not add testbed dependencies to the compiler.

every promised capability needs a test, exact result, and remaining limitation. retain ordinary integer/logical-pointer semantics. copy the relevant safety and ABI explanations into upstream docs, with precise local contracts, so the patch can be understood independently of [Theta's unsafe registry](../UNSAFE.md).

the local PR text leads with the concrete compiler/consumer failure and resulting behavior. include reproduction, relevant tests, dependency justification, attribution, and risks. explain design tradeoffs that remain relevant, without reciting abandoned work or conversation history.

## delivery states

a useful local draft requires focused compiler evidence and actual Windows NGAPI proof. record the complete current CI crosswalk, run the applicable checks available for the candidate, and keep unavailable jobs pending. a fully passing public gate requires all its dependencies to pass at that revision.

local draft preparation, public CI success, publication, maintainer review, and merge are distinct states. publication follows actual task authority. no full framework, report, Metal, DirectX, or additional-language implementation is required before drafting.

record meaningful contribution checkpoints in [execution](execution.md), and reusable findings in [lessons](lessons.md). [tasks](specs/001-theta-prototype/tasks.md) keeps supporting work separate from the primary milestone.
