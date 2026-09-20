//! Source fixtures translated from ShaderToHuman d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
#![no_std]
#![forbid(unsafe_code)]

#[path = "../../crates/shader-to-human/fixtures/mod.rs"]
mod fixtures;

use shader_to_human::{Mat4, UVec2, UVec3, Vec4};
use spirv_std::spirv;

const PIXELS: usize = (fixtures::WIDTH * fixtures::HEIGHT) as usize;

#[repr(C)]
pub struct WorldRoot {
    pub origin_and_depth: Vec4,
    pub world_from_clip: Mat4,
}

fn index(id: UVec3) -> Option<usize> {
    if id.x < fixtures::WIDTH && id.y < fixtures::HEIGHT {
        Some((id.y * fixtures::WIDTH + id.x) as usize)
    } else {
        None
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn gather_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; PIXELS],
) {
    if let Some(index) = index(id) {
        output[index] = fixtures::gather(
            UVec2::new(id.x, id.y),
            &mut fixtures::UiState::default(),
            Vec4::ZERO,
            Vec4::ZERO,
        );
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn table_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; PIXELS],
) {
    if let Some(index) = index(id) {
        output[index] = fixtures::table(
            UVec2::new(id.x, id.y),
            &mut fixtures::UiState::default(),
            Vec4::ZERO,
        );
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn two_d_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; PIXELS],
) {
    if let Some(index) = index(id) {
        output[index] = fixtures::two_d(UVec2::new(id.x, id.y));
    }
}

#[spirv(compute(threads(8, 8, 1)))]
pub fn world_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &WorldRoot,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; PIXELS],
) {
    if let Some(index) = index(id) {
        let camera = fixtures::Camera {
            origin: root.origin_and_depth.truncate(),
            depth_near: root.origin_and_depth.w,
            inverse_view_projection: root.world_from_clip,
        };
        output[index] = fixtures::world(UVec2::new(id.x, id.y), camera, Vec4::ZERO);
    }
}

#[spirv(compute(threads(1)))]
pub fn scatter_cs(
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; PIXELS],
) {
    // One invocation, as in the source. The host clears untouched pixels first.
    fixtures::scatter(&mut |position, color| {
        if position.x >= 0
            && position.y >= 0
            && position.x < fixtures::WIDTH as i32
            && position.y < fixtures::HEIGHT as i32
        {
            output[(position.y as u32 * fixtures::WIDTH + position.x as u32) as usize] = color;
        }
    });
}
