//! Source documentation demos with separate image and persistent-state passes.
//! BSD-3-Clause, Electronic Arts 2024-2025, translated from d6f98b7d.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

#[path = "../../crates/shader-to-human/demos/mod.rs"]
mod demos;

use demos::{Inputs, State};
use shader_to_human::{UVec2, UVec3, UVec4};
use spirv_std::{spirv, Image};

type Output = Image!(2D, format = rgba8, sampled = false);

/// # Safety
/// U-019: root dimensions must equal the initialized RGBA8_UNORM storage view
/// in GENERAL, with one z plane and exclusive image ownership. Binding0 is a
/// live initialized 80-byte state buffer, read-only throughout this dispatch.
/// Root/state contain the reviewed finite source inputs and a valid case ID.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn demos_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] state: &[UVec4; 5],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.control.x && id.y < root.control.y {
        let position = UVec2::new(id.x, id.y);
        let mut private = State::from_words(*state);
        let color = demos::render(root, position, &mut private);
        // SAFETY: U-019. Exact image/root extent, guarded distinct x/y and one
        // z plane. UI modifications affect private values, never shared state.
        unsafe { output.write(position, color) };
    }
}

/// U-019: exactly one invocation, with exclusive initialized 80-byte state
/// ownership after the image pass completes. No image or physical access.
#[spirv(compute(threads(1)))]
pub fn update_ui_cs(
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] state: &mut [UVec4; 5],
) {
    let mut value = State::from_words(*state);
    demos::update(root, &mut value);
    *state = value.to_words();
}
