//! AGFX f91b108a texture_ops::main_write_cs and sampling::main_sample_2d_cs.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use spirv_std::{
    glam::{UVec3, Vec2, Vec4},
    spirv, Image, Sampler,
};
type Output = Image!(2D, format = rgba8, sampled = false);
type Source = Image!(2D, type = f32, sampled);

#[repr(C)]
pub struct SeedRoot {
    pub source: u32,
    pub destination: u32,
    pub width: u32,
    pub height: u32,
}

#[repr(C)]
pub struct SamplingRoot {
    pub source: u32,
    pub sampler: u32,
    pub destination: u32,
    pub width: u32,
    pub height: u32,
    pub slice_count: u32,
    pub uv_scale: Vec2,
    pub uv_offset: Vec2,
    pub padding: Vec2,
}

/// # Safety
/// U-020: bind an initialized 64x64 RGBA8_UNORM output, root [0,0,64,64],
/// and dispatch8x8x1 groups. Exclusive image use lasts through completion.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn seed_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &SeedRoot,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x >= root.width || id.y >= root.height {
        return;
    }
    let u = id.x as f32 / (root.width - 1).max(1) as f32;
    let v = id.y as f32 / (root.height - 1).max(1) as f32;
    let checker = ((id.x / 8 + id.y / 8) % 2) as f32;
    // SAFETY: U-020. Guarded unique xy, one z plane, exact host image extent.
    unsafe { output.write(id.truncate(), Vec4::new(u, v, checker, 1.0)) };
}

/// # Safety
/// U-020: initialized distinct64x64 RGBA8_UNORM source/output, a compatible
/// normalized non-comparison sampler, root indices0, dimensions64, slice1,
/// finite UV transform, and8x8x1 groups. Source remains immutable and output
/// exclusive until completion. These ordinary bindings replace source heap slots.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn sample_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &SamplingRoot,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
    #[spirv(descriptor_set = 0, binding = 2)] source: &Source,
    #[spirv(descriptor_set = 0, binding = 3)] sampler: &Sampler,
) {
    if id.x >= root.width || id.y >= root.height {
        return;
    }
    let uv = (id.truncate().as_vec2() + Vec2::splat(0.5))
        / Vec2::new(root.width as f32, root.height as f32);
    let uv = uv * root.uv_scale + root.uv_offset;
    let color: Vec4 = source.sample_by_lod(*sampler, uv, 0.0);
    // SAFETY: U-020. Guarded unique xy, one z plane, exact host image extent.
    unsafe { output.write(id.truncate(), color) };
}
