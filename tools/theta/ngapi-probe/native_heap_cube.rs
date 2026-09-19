// Vertex layout and entry names follow NoGraphicsAPI's cube example.
// Copyright (c) 2026 Sebastian Aaltonen. See ../../../legal/licenses/NoGraphicsAPI-MIT.txt.
// This diagnostic uses unlit generated textures and an explicitly extended root.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use spirv_std::glam::{Vec2, Vec4};
use spirv_std::{image::Image2d, spirv, PhysicalPtr, Sampler};

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Vertex {
    pub position: [f32; 4],
    pub uv: [f32; 2],
}

#[repr(C)]
pub struct Root {
    pub vertices: PhysicalPtr<Vertex>,
    pub transform: [[f32; 4]; 4],
    pub resource: u32,
    pub sampler: u32,
}

const _: () = {
    assert!(core::mem::size_of::<Vertex>() == 24);
    assert!(core::mem::align_of::<Vertex>() == 4);
    assert!(core::mem::offset_of!(Vertex, uv) == 16);
    assert!(core::mem::size_of::<Root>() == 80);
    assert!(core::mem::align_of::<Root>() == 8);
    assert!(core::mem::offset_of!(Root, vertices) == 0);
    assert!(core::mem::offset_of!(Root, transform) == 8);
    assert!(core::mem::offset_of!(Root, resource) == 72);
    assert!(core::mem::offset_of!(Root, sampler) == 76);
};

/// Transform a vertex fetched through an actual physical GPU address.
///
/// # Safety
/// U-008: vertices addresses 24 initialized Vertex values, stride 24 and alignment
/// four, in one live 576-byte allocation. Indices are 0..23, with vertex_offset
/// zero. The host supplies finite positions/UVs and a finite row-major transform.
/// The allocation is visible, immutable during the draw and retained until
/// completion. It is separate from writable color/depth attachments. The root
/// is 80 bytes with offsets 0/8/72/76. No Rust reference to GPU memory is created.
#[allow(unsafe_code, non_snake_case)]
#[spirv(vertex)]
pub unsafe fn vertexMain(
    #[spirv(push_constant)] root: &Root,
    #[spirv(vertex_index)] vertex_id: i32,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] uv: &mut Vec2,
) {
    // SAFETY: U-008/U-003. The host's fixed validated indices and zero base vertex
    // select a complete aligned initialized element of the live vertex range.
    let vertex = unsafe {
        root.vertices
            .wrapping_add(u64::from(vertex_id as u32))
            .read()
    };
    let p = Vec4::from_array(vertex.position);
    *position = Vec4::new(
        Vec4::from_array(root.transform[0]).dot(p),
        Vec4::from_array(root.transform[1]).dot(p),
        Vec4::from_array(root.transform[2]).dot(p),
        Vec4::from_array(root.transform[3]).dot(p),
    );
    *uv = Vec2::from_array(vertex.uv) * 1.5 + Vec2::splat(0.125);
}

/// Sample the native descriptor heaps in the fragment stage.
///
/// # Safety
/// U-008: each heap has four initialized native-size slots. The uniform root
/// selects sampled RGBA8 2D images at resource 1/3 and compatible non-comparison
/// nearest repeat/clamp samplers at 2/3. Descriptors and image contents remain
/// initialized, visible and unchanged until completion. Images are in GENERAL
/// layout and separate from all attachments. No handle extends native lifetime.
#[allow(unsafe_code, non_snake_case)]
#[spirv(fragment)]
pub unsafe fn fragmentMain(
    #[spirv(push_constant)] root: &Root,
    #[spirv(location = 0)] uv: Vec2,
    #[spirv(location = 0)] color: &mut Vec4,
) {
    // SAFETY: U-008/U-006. The host checks both uniform indices, initializes the
    // complete compatible descriptor ranges and retains all objects through use.
    let (image, sampler) = unsafe {
        (
            Image2d::from_resource_heap(root.resource),
            Sampler::from_sampler_heap(root.sampler),
        )
    };
    *color = image.sample(sampler, uv);
}
