# RustGPU Contribution Boundary

the local project may require RustGPU work for physical GPU pointers, target layout, native descriptor heaps, untyped pointers, and shader-library APIs. those changes should be generic, focused, and independently reviewable.

## Physical Pointers

the inspected RustGPU baseline uses logical addressing and a 32-bit pointer width. the open draft [physical pointer pull request](https://github.com/Rust-GPU/rust-gpu/pull/237) identifies useful groundwork but leaves review items around casts, alignment, aliasing, pointer operations, integer width and overflow, restriction markers, test placement, and unrelated changes. use it as evidence, not as an accepted implementation.

prove full-width representation, target ABI, arithmetic, casts, aligned access, decorations, optimizer survival, and safe library exposure before making a consumer claim.

## Descriptor Heaps

[descriptor-heap issue 524](https://github.com/Rust-GPU/rust-gpu/issues/524) tracks the missing native interface. assembly-based entry-point support from [pull request 534](https://github.com/Rust-GPU/rust-gpu/pull/534) may enable a narrow prototype. begin with compiler and `spirv-std` tests that emit and validate the required instructions, then connect the actual NoGraphicsAPI consumer.

## Patch Discipline

- keep framework integration inspired by AGFX and NoGraphicsAPI compatibility changes out of RustGPU commits.
- separate prerequisite compiler fixes from convenience APIs.
- include the exact upstream test placement and command for every promised behavior.
- run the applicable upstream CI matrix before proposing a pull request.
- distinguish a prepared local patch, opened pull request, maintainer review, and merged change.
- preserve rejected approaches and remaining checklist items in the pull request description.

the first contribution should be the smallest generic slice proven by both upstream tests and a real local consumer. do not bundle the entire graphics framework into a compiler pull request.
