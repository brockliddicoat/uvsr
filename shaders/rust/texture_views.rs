//! Numeric controls for AGFX-compatible RGBA8 UNORM/sRGB view reinterpretation.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use spirv_std::{glam::{UVec3, Vec2, Vec4}, spirv, Image, Sampler};
type Storage = Image!(2D, format = rgba8, sampled = false);
type Sampled = Image!(2D, type = f32, sampled);

/// # Safety
/// U-024: one exclusive initialized8x8 RGBA8_UNORM storage view in GENERAL,
/// one z plane, and the fixed finite16-byte color root. Distinct guarded texels.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn store_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] color: &Vec4,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Storage,
) {
    if id.x < 8 && id.y < 8 {
        // SAFETY: U-024. Host provides the exact format/extent and one z plane.
        unsafe { output.write(id.truncate(), *color) };
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn sample_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; 64],
    #[spirv(descriptor_set = 0, binding = 2)] source: &Sampled,
    #[spirv(descriptor_set = 0, binding = 3)] sampler: &Sampler,
) {
    if id.x < 8 && id.y < 8 {
        let uv = (id.truncate().as_vec2() + Vec2::splat(0.5)) / 8.0;
        output[(id.y * 8 + id.x) as usize] = source.sample_by_lod(*sampler, uv, 0.0);
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn load_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; 64],
    #[spirv(descriptor_set = 0, binding = 1)] source: &Storage,
) {
    if id.x < 8 && id.y < 8 {
        output[(id.y * 8 + id.x) as usize] = source.read(id.truncate());
    }
}

#[spirv(vertex)]
pub fn view_vs(#[spirv(vertex_index)] id: u32, #[spirv(position)] out: &mut Vec4) {
    // Six vertices cover the full viewport with no vertex buffer.
    let uv = match id % 6 { 1 => Vec2::new(1.0,0.0), 2 | 3 => Vec2::ONE,
        4 => Vec2::new(0.0,1.0), _ => Vec2::ZERO };
    let xy = uv * 2.0 - Vec2::ONE;
    *out = Vec4::new(xy.x, xy.y, 0.5, 1.0);
}

#[spirv(fragment)]
pub fn view_fs(#[spirv(push_constant)] color: &Vec4, out: &mut Vec4) { *out = *color; }
