//! Original RGBA8 storage-image hosting for ShaderToHuman d6f98b7d fixtures.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

#[path = "../../crates/shader-to-human/fixtures/mod.rs"]
mod fixtures;

use shader_to_human::{Mat4, UVec2, UVec3, Vec4};
use spirv_std::{spirv, Image};

type Output = Image!(2D, format = rgba8, sampled = false);

#[repr(C)]
pub struct WorldRoot {
    pub origin_and_depth: Vec4,
    pub world_from_clip: Mat4,
}

fn in_bounds(id: UVec3) -> bool {
    id.x < fixtures::WIDTH && id.y < fixtures::HEIGHT
}

/// # Safety
/// U-018: bind a live initialized 800x600 RGBA8_UNORM storage view in GENERAL.
/// Dispatch exactly 100x75x1 groups, with exclusive completed image ownership.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn gather_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if in_bounds(id) {
        let position = UVec2::new(id.x, id.y);
        let color = fixtures::gather(
            position,
            &mut fixtures::UiState::default(),
            Vec4::ZERO,
            Vec4::ZERO,
        );
        // SAFETY: U-018. The entry contract supplies the exact bound image.
        // Guarded x/y and one z plane give each invocation a distinct texel.
        unsafe { output.write(position, color) };
    }
}

/// # Safety
/// U-018: the same image, group and exclusive-use contract as gather_cs.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn table_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if in_bounds(id) {
        let position = UVec2::new(id.x, id.y);
        let color = fixtures::table(position, &mut fixtures::UiState::default(), Vec4::ZERO);
        // SAFETY: U-018. Exact entry image, guarded unique x/y texel, one z plane.
        unsafe { output.write(position, color) };
    }
}

/// # Safety
/// U-018: the same image, group and exclusive-use contract as gather_cs.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn two_d_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if in_bounds(id) {
        let position = UVec2::new(id.x, id.y);
        // SAFETY: U-018. Exact entry image, guarded unique x/y texel, one z plane.
        unsafe { output.write(position, fixtures::two_d(position)) };
    }
}

/// # Safety
/// U-018: gather_cs image/group contract and a live finite 80-byte WorldRoot,
/// with the reviewed source camera matrix in column order.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn world_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &WorldRoot,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if in_bounds(id) {
        let camera = fixtures::Camera {
            origin: root.origin_and_depth.truncate(),
            depth_near: root.origin_and_depth.w,
            inverse_view_projection: root.world_from_clip,
        };
        let position = UVec2::new(id.x, id.y);
        let color = fixtures::world(position, camera, Vec4::ZERO);
        // SAFETY: U-018. Exact entry image, guarded unique x/y texel, one z plane.
        unsafe { output.write(position, color) };
    }
}

/// # Safety
/// U-018: the same image contract as gather_cs, but exactly one invocation.
/// Clear all untouched texels before the source's first execution.
#[allow(unsafe_code)]
#[spirv(compute(threads(1)))]
pub unsafe fn scatter_cs(#[spirv(descriptor_set = 0, binding = 1)] output: &Output) {
    fixtures::scatter(&mut |position, color| {
        if position.x >= 0
            && position.y >= 0
            && position.x < fixtures::WIDTH as i32
            && position.y < fixtures::HEIGHT as i32
        {
            // SAFETY: U-018. Entry contract supplies the exact image and one
            // invocation. Clipping bounds every ordered write, including repeats.
            unsafe { output.write(position, color) };
        }
    });
}
