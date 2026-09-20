//! Zoom2D shared source bodies with explicit pre/render/post ordering.
//! Copyright Electronic Arts 2024-2025, BSD-3-Clause, translated from d6f98b7d.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
#[path = "../../crates/shader-to-human/programs/zoom.rs"]
mod zoom;
use shader_to_human::{UVec2, UVec3, UVec4};
use spirv_std::{spirv, Image};
use zoom::{Inputs, State};
type Output = Image!(2D, format = rgba8, sampled = false);

/// # Safety
/// U-023: root dimensions equal the initialized exclusive RGBA8_UNORM view in
/// GENERAL, one z plane. Binding0 is an initialized112-byte immutable state.
/// Finite reviewed roots/state keep all source arithmetic within its domain.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn zoom_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] state: &[UVec4; 7],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.dimensions.x && id.y < root.dimensions.y {
        let pixel = UVec2::new(id.x, id.y);
        // SAFETY: U-023. Exact guarded distinct texels, one z plane, exclusive
        // image and immutable state. No renderer invocation writes shared state.
        unsafe { output.write(pixel, zoom::render(root, pixel, State(*state))) };
    }
}

/// Exactly one invocation, exclusive initialized state, no image access.
#[spirv(compute(threads(1)))]
pub fn zoom_pre_cs(
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] state: &mut [UVec4; 7],
) {
    let mut value = State(*state);
    zoom::pre(root, &mut value);
    *state = value.0;
}

/// Exactly one invocation after the image completes, exclusive state.
#[spirv(compute(threads(1)))]
pub fn zoom_post_cs(
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] state: &mut [UVec4; 7],
) {
    let mut value = State(*state);
    zoom::post(root, &mut value);
    *state = value.0;
}
