// Translated from AGFX f91b108a data/shaders/tests/multi_dispatch.hlsl.
// Copyright (c) 2026 Amélie Heinrich. See ../../legal/licenses/AGFX-MIT.txt.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use spirv_std::{glam::UVec3, spirv, RuntimeArray, TypedBuffer};

#[repr(C)]
pub struct Root {
    pub resource: u32,
    pub element_count: u32,
    pub pass_index: u32,
    pub padding: u32,
}

const _: () = {
    assert!(core::mem::size_of::<Root>() == 16);
    assert!(core::mem::offset_of!(Root, element_count) == 4);
    assert!(core::mem::offset_of!(Root, pass_index) == 8);
    assert!(core::mem::offset_of!(Root, padding) == 12);
};

/// One ordered pass of the source AGFX recurrence.
///
/// # Safety
/// U-009: set0/binding0 contains an initialized storage-buffer descriptor array.
/// The uniform root resource index selects a live writable buffer containing at
/// least element_count initialized u32 values. Dispatch exactly one 64-thread
/// group with element_count 64. Each invocation owns one disjoint word. No host
/// or other GPU access conflicts during the dispatch. The host orders all four
/// passes with shader-write to shader-read/write barriers, waits for completion
/// and retains all descriptors/resources before readback, reuse or retirement.
#[allow(unsafe_code)]
#[spirv(compute(threads(64)))]
pub unsafe fn main_cs(
    #[spirv(push_constant)] root: &Root,
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] buffers: &mut RuntimeArray<
        TypedBuffer<[u32]>,
    >,
) {
    if id.x >= root.element_count {
        return;
    }
    // SAFETY: U-009. The host validates the uniform descriptor index against
    // the fully initialized set and retains the selected buffer for this dispatch.
    // Invocations access disjoint words; the native owner excludes other users.
    let buffer = unsafe { buffers.index_mut(root.resource as usize) };
    let index = id.x as usize;
    buffer[index] = buffer[index]
        .wrapping_mul(2)
        .wrapping_add(root.pass_index)
        .wrapping_add(id.x);
}
