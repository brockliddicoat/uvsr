//! Source Hello screen, quad and compute shaders at d6f98b7d.
//! BSD-3-Clause, Electronic Arts 2024-2025.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

#[path = "../../crates/shader-to-human/programs/hello.rs"]
mod hello;

use hello::Inputs;
use shader_to_human::{UVec2, UVec3, Vec2, Vec4};
use spirv_std::{spirv, Image};

#[spirv(vertex)]
pub fn screen_vs(#[spirv(vertex_index)] vertex: u32, #[spirv(position)] position: &mut Vec4) {
    *position = hello::screen_vertex(vertex);
}

#[spirv(fragment)]
pub fn screen_fs(
    #[spirv(frag_coord)] position: Vec4,
    #[spirv(push_constant)] root: &Inputs,
    color: &mut Vec4,
) {
    *color = hello::screen(position.truncate().truncate(), root.dimensions.truncate().truncate(), false);
}

#[spirv(vertex)]
pub fn quad_vs(
    #[spirv(vertex_index)] vertex: u32,
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(position)] position: &mut Vec4,
    uv: &mut Vec2,
) {
    (*position, *uv) = hello::quad_vertex(vertex, root);
}

#[spirv(fragment)]
pub fn quad_fs(
    #[spirv(frag_coord)] position: Vec4,
    uv: Vec2,
    #[spirv(push_constant)] root: &Inputs,
    color: &mut Vec4,
) {
    // DirectX SV_Position.w is clip w, while Vulkan FragCoord.w is reciprocal w.
    // Match DXC's documented -fvk-use-dx-position-w host adaptation.
    let source_position = Vec4::new(position.x, position.y, position.z, 1.0 / position.w);
    *color = hello::quad(uv, source_position, root.camera.truncate());
}

type Output = Image!(2D, format = rgba8, sampled = false);

/// # Safety
/// U-022. Binding1 is an initialized, exclusively writable RGBA8_UNORM image
/// in GENERAL with exactly the positive integral root dimensions. Dispatch has
/// one z plane. Distinct guarded x/y invocations each write one complete texel.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn hello_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &Inputs,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    let dimensions = root.dimensions.truncate().truncate();
    if id.x < dimensions.x as u32 && id.y < dimensions.y as u32 {
        let xy = UVec2::new(id.x, id.y);
        let color = hello::screen(xy.as_vec2() + Vec2::splat(0.5), dimensions, true);
        // SAFETY: U-022. Exact image extent, distinct guarded texel and one z.
        unsafe { output.write(xy, color) };
    }
}
