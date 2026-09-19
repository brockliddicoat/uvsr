#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use spirv_std::glam::{UVec3, Vec2, Vec4};
use spirv_std::{image::Image2d, spirv, PhysicalPtr, Sampler};

#[repr(C)]
pub struct Root {
    pub output: PhysicalPtr<Vec4>,
    pub resource_xor: u32,
    pub sampler_xor: u32,
}

const _: () = {
    assert!(core::mem::size_of::<Root>() == 16);
    assert!(core::mem::align_of::<Root>() == 8);
    assert!(core::mem::offset_of!(Root, output) == 0);
    assert!(core::mem::offset_of!(Root, resource_xor) == 8);
    assert!(core::mem::offset_of!(Root, sampler_xor) == 12);
};

/// Each of four lanes samples a distinct native image/sampler pair.
///
/// # Safety
/// U-007: dispatch exactly one workgroup of four invocations. Both heaps contain
/// four complete initialized native-size descriptors, with sampled 2D RGBA8
/// images in resource slots 1/3 and compatible nearest repeat/clamp samplers in
/// slots 2/3. Image contents and descriptors are visible and remain unchanged
/// through completion. Enable the declared non-uniform indexing device feature.
/// Output is one nonzero 16-byte-aligned device allocation with 64 exclusively
/// writable bytes, disjoint from textures, descriptors and other allocations.
/// Each lane owns one 16-byte range. The caller establishes transfer/compute/host
/// visibility, waits for completion and retains all resources before reuse or
/// retirement. The root occupies 16 bytes with offsets 0/8/12.
#[allow(unsafe_code, non_snake_case)]
#[spirv(compute(threads(4)))]
pub unsafe fn computeMain(
    #[spirv(push_constant)] root: &Root,
    #[spirv(local_invocation_id)] local: UVec3,
) {
    let lane = local.x;
    let resource = 1 + 2 * (((lane >> 1) ^ root.resource_xor) & 1);
    let filter = 2 + ((lane ^ root.sampler_xor) & 1);
    // SAFETY: U-007/U-006. Masking selects only the two valid resource and
    // sampler slots whose native layout, compatibility and lifetimes the host
    // establishes. Every invocation has visible initialized descriptors/images.
    let (image, sampler) = unsafe {
        (
            Image2d::from_resource_heap(resource),
            Sampler::from_sampler_heap(filter),
        )
    };
    let color: Vec4 = image.sample_by_lod(sampler, Vec2::new(1.25, 0.25), 0.0);
    // SAFETY: U-007/U-003. LocalSize 4 and one workgroup give lane values 0..3.
    // The four aligned Vec4 ranges are disjoint and lie within the complete
    // caller-owned output allocation, with lifetime and visibility as above.
    unsafe { root.output.wrapping_add(u64::from(lane)).write(color * 2.0) };
}
