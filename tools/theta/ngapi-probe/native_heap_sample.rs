#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use core::arch::asm;
use spirv_std::glam::{Vec2, Vec4};
use spirv_std::{spirv, PhysicalPtr};

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
    let mut color = Vec4::ZERO;
    // SAFETY: U-005. The entry contract supplies live visible native descriptors
    // at valid uniform indices. The initialized local operands are aligned and
    // live through this fallthrough block, which writes exactly one local Vec4.
    unsafe {
        asm!(
            "%u32 = OpTypeInt 32 0",
            "%f32 = OpTypeFloat 32",
            "%lod = OpConstant %f32 0",
            "%image = OpTypeImage %f32 Dim2D 0 0 0 1 Unknown",
            "%sampler = OpTypeSampler",
            "%sampled_image = OpTypeSampledImage %image",
            "%image_size = OpConstantSizeOfEXT %u32 %image",
            "%sampler_size = OpConstantSizeOfEXT %u32 %sampler",
            "%images = OpTypeRuntimeArray %image",
            "%samplers = OpTypeRuntimeArray %sampler",
            "OpDecorateId %images ArrayStrideIdEXT %image_size",
            "OpDecorateId %samplers ArrayStrideIdEXT %sampler_size",
            "%heap_pointer = OpTypeUntypedPointerKHR UniformConstant",
            "%resources = OpUntypedVariableKHR %heap_pointer UniformConstant",
            "%filters = OpUntypedVariableKHR %heap_pointer UniformConstant",
            "OpDecorate %resources BuiltIn ResourceHeapEXT",
            "OpDecorate %filters BuiltIn SamplerHeapEXT",
            "%image_slot = OpUntypedAccessChainKHR %heap_pointer %images %resources {resource}",
            "%sampler_slot = OpUntypedAccessChainKHR %heap_pointer %samplers %filters {sampler}",
            "%texture = OpLoad %image %image_slot",
            "%filter = OpLoad %sampler %sampler_slot",
            "%sampled = OpSampledImage %sampled_image %texture %filter",
            "%coords = OpLoad _ {coordinates}",
            "%color = OpImageSampleExplicitLod typeof*{color} %sampled %coords Lod %lod",
            "OpStore {color} %color",
            resource = in(reg) root.resource,
            sampler = in(reg) root.sampler,
            coordinates = in(reg) &coordinates,
            color = in(reg) &mut color,
        );
    }
    // SAFETY: U-005/U-003. The caller owns the complete aligned exclusive output
    // range and all initialization, visibility, completion and retirement rules.
    unsafe { root.output.write(color * 2.0) };
}
