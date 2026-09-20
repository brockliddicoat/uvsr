//! Per-sample color/depth controls for GaussianSplatting's eight-sample path.
#![no_std]
#![forbid(unsafe_code)]
use spirv_std::{
    glam::{UVec3, UVec4, Vec2, Vec4},
    image::{sample_with, ImageWithMethods},
    spirv, Image,
};

#[repr(C)]
pub struct Root {
    pub color: Vec4,
    pub depth: Vec4,
    pub mask: UVec4,
}

#[spirv(vertex)]
pub fn samples_vs(
    #[spirv(vertex_index)] id: u32,
    #[spirv(push_constant)] root: &Root,
    #[spirv(position)] out: &mut Vec4,
) {
    let uv = match id % 6 {
        1 => Vec2::new(1.0, 0.0),
        2 | 3 => Vec2::ONE,
        4 => Vec2::new(0.0, 1.0),
        _ => Vec2::ZERO,
    };
    let xy = uv * 2.0 - Vec2::ONE;
    *out = Vec4::new(xy.x, xy.y, root.depth.x, 1.0);
}

#[spirv(fragment)]
pub fn samples_fs(
    #[spirv(push_constant)] root: &Root,
    #[spirv(sample_mask)] mask: &mut [u32; 1],
    color: &mut Vec4,
) {
    *mask = [root.mask.x];
    *color = root.color;
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn read_ms_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; 512],
    #[spirv(descriptor_set = 0, binding = 2)] source: &Image!(2D, type=f32, sampled, multisampled),
) {
    if id.x < 8 && id.y < 8 {
        for sample in 0..8 {
            output[((id.y * 8 + id.x) * 8 + sample) as usize] =
                source.fetch_with(id.truncate(), sample_with::sample_index(sample));
        }
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn read_one_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; 512],
    #[spirv(descriptor_set = 0, binding = 2)] source: &Image!(2D, type=f32, sampled),
) {
    if id.x < 8 && id.y < 8 {
        output[(id.y * 8 + id.x) as usize] = source.fetch(id.truncate());
    }
}
