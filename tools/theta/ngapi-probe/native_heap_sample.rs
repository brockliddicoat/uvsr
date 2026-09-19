#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use spirv_std::glam::{Vec2, Vec4};
use spirv_std::{image::Image2d, spirv, PhysicalPtr, Sampler};

#[repr(C)]
pub struct Root {
    pub output: PhysicalPtr<Vec4>,
    pub resource: u32,
    pub sampler: u32,
}

const _: () = {
    assert!(core::mem::size_of::<Root>() == 16);
    assert!(core::mem::align_of::<Root>() == 8);
    assert!(core::mem::offset_of!(Root, output) == 0);
    assert!(core::mem::offset_of!(Root, resource) == 8);
    assert!(core::mem::offset_of!(Root, sampler) == 12);
};

/// Samples native heaps and writes the doubled color through a physical address.
///
/// # Safety
/// U-005: the host binds live native resource/sampler heaps, using the queried
/// image/sampler descriptor sizes. Both indices select initialized compatible
/// descriptors within those heaps. The image is sampled 2D RGBA8 UNORM, with
/// initialized visible contents in GENERAL layout. The sampler uses nearest
/// filtering and repeat or clamp-to-edge addressing, without comparison.
/// Output is a nonzero 16-byte-aligned, exclusive complete 16-byte device range,
/// disjoint from descriptors, textures and other allocations. Dispatch exactly
/// one invocation. Keep every allocation and descriptor unchanged and live until
/// queue completion. Establish transfer/compute/host visibility before reading,
/// reusing or retiring storage. The root occupies 16 bytes at offsets 0/8/12.
#[allow(unsafe_code, non_snake_case)]
#[spirv(compute(threads(1)))]
pub unsafe fn computeMain(#[spirv(push_constant)] root: &Root) {
    let coordinates = Vec2::new(1.25, 0.25);
    // SAFETY: U-005/U-006. The host initializes complete native-size descriptors
    // at these uniform indices and keeps both heaps and compatible resources
    // live, visible and unchanged until the dispatch has completed.
    let (image, sampler) = unsafe {
        (
            Image2d::from_resource_heap(root.resource),
            Sampler::from_sampler_heap(root.sampler),
        )
    };
    let color: Vec4 = image.sample_by_lod(sampler, coordinates, 0.0);
    // SAFETY: U-005/U-003. The caller owns the complete aligned exclusive output
    // range and all initialization, visibility, completion and retirement rules.
    unsafe { root.output.write(color * 2.0) };
}
