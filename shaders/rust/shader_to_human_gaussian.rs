//! GaussianSplatting source programs. Electronic Arts 2024-2025, BSD-3-Clause.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
#[path = "../../crates/shader-to-human/programs/gaussian/mod.rs"]
mod gaussian;
use gaussian::{ply, Inputs, PLY_OFFSET, PLY_WORDS, WORDS};
use shader_to_human::{UVec3, UVec4, Vec3, Vec4};
use spirv_std::{
    image::{sample_with, ImageWithMethods},
    spirv, Image,
};
type Output = Image!(2D, format = rgba8, sampled = false);
type Samples = Image!(2D, type = f32, sampled, multisampled);

/// One invocation with exclusive initialized51520-byte binding0. Only the first
/// four words change. Invalid or truncated PLY input produces an empty header.
#[spirv(compute(threads(1)))]
pub fn gaussian_init_cs(
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &mut [u32; WORDS],
) {
    let header =
        ply::parse_header_with(PLY_WORDS as u32, |index| words[PLY_OFFSET + index as usize])
            .unwrap_or_default();
    header.write(words);
}

/// # Safety
/// U-027: initialized immutable51520-byte binding0 with reviewed finite inputs.
/// Root dimensions match those inputs and the exclusive initialized Rgba8 view.
/// One z plane. Root kind0/1/3 selects the corresponding source graph branch.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1), signed_zero_inf_nan_preserve = 32))]
pub unsafe fn gaussian_base_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[u32; WORDS],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.x && id.y < root.y {
        let value = gaussian::base_image(root.z, id.truncate(), &Inputs::read(words));
        // SAFETY: U-027. Distinct guarded texels and exactly one invocation plane.
        unsafe { output.write(id.truncate(), value) };
    }
}

/// # Safety
/// U-027: same binding/root/image contract as gaussian_base_cs, whose completed
/// writes initialize every texel. Each invocation reads and writes its own texel.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1), signed_zero_inf_nan_preserve = 32))]
pub unsafe fn gaussian_main_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[u32; WORDS],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.x && id.y < root.y {
        let prior = output.read(id.truncate());
        let value = gaussian::compute_image(root.z, id.truncate(), prior, &Inputs::read(words));
        // SAFETY: U-027. Guarded invocation-local read/modify/write after base.
        unsafe { output.write(id.truncate(), value) };
    }
}

#[spirv(vertex)]
pub fn gaussian_clear_vs(
    #[spirv(vertex_index)] vertex: u32,
    #[spirv(position)] position: &mut Vec4,
) {
    *position = gaussian::fullscreen_vertex(vertex);
}

#[spirv(fragment(signed_zero_inf_nan_preserve = 32))]
pub fn gaussian_clear_fs(
    #[spirv(frag_coord)] position: Vec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[u32; WORDS],
    output: &mut Vec4,
) {
    *output = gaussian::clear_fragment(
        position.truncate().truncate().as_uvec2(),
        &Inputs::read(words),
    );
}

#[spirv(vertex(signed_zero_inf_nan_preserve = 32))]
pub fn gaussian_splat_vs(
    #[spirv(vertex_index)] vertex: u32,
    #[spirv(instance_index)] instance: u32,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[u32; WORDS],
    #[spirv(position)] position: &mut Vec4,
    #[spirv(flat)] a: &mut Vec4,
    #[spirv(flat)] b: &mut Vec4,
    #[spirv(flat)] c: &mut Vec3,
    #[spirv(flat)] splat_id: &mut u32,
) {
    *splat_id = instance;
    let inputs = Inputs::read(words);
    if let Some(splat) = ply::splat_with(
        PLY_WORDS as u32,
        |index| words[PLY_OFFSET + index as usize],
        ply::Header::read(words),
        instance,
    ) {
        let (clip, params) = gaussian::splat_vertex(vertex, splat, &inputs.camera);
        *position = clip;
        (*a, *b, *c) = params.to_interpolators();
    } else {
        // Defined safe failure for out-of-range instances. The fixed source
        // graph draws exactly200 valid PLY splats and never takes this path.
        *position = Vec4::new(0.0, 0.0, -1.0, 1.0);
        *a = Vec4::ZERO;
        *b = Vec4::ZERO;
        *c = Vec3::ZERO;
    }
}

#[spirv(fragment(depth_replacing, signed_zero_inf_nan_preserve = 32))]
pub fn gaussian_splat_fs(
    #[spirv(frag_coord)] position: Vec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[u32; WORDS],
    #[spirv(flat)] a: Vec4,
    #[spirv(flat)] b: Vec4,
    #[spirv(flat)] c: Vec3,
    #[spirv(flat)] splat_id: u32,
    #[spirv(frag_depth)] depth: &mut f32,
    #[spirv(sample_mask)] mask: &mut [u32; 1],
    output: &mut Vec4,
) {
    let (color, z, coverage) = gaussian::splat_fragment(
        position.truncate().truncate(),
        gaussian::math::Raster::from_interpolators(a, b, c),
        splat_id,
        &Inputs::read(words),
    );
    *depth = z;
    *mask = [coverage];
    *output = color;
}

/// # Safety
/// U-027: root-sized exclusive initialized Rgba8 output and a distinct initialized
/// eight-sample RGBA8_SRGB sampled image of the same extent, after raster writes
/// complete. Exactly one z plane. No concurrent writer or descriptor mutation.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn gaussian_resolve_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
    #[spirv(descriptor_set = 0, binding = 2)] samples: &Samples,
) {
    if id.x < root.x && id.y < root.y {
        let value = gaussian::resolve(|sample| {
            samples.fetch_with(id.truncate(), sample_with::sample_index(sample))
        });
        // SAFETY: U-027. Fetch indices0..7 and coordinates are bounded. Output
        // is disjoint, with one store per guarded texel after source completion.
        unsafe { output.write(id.truncate(), value) };
    }
}

/// U-027 diagnostic only: one8x8x1 group, initialized512-float4 output and a
/// completed eight-sample source image. Root dimensions must match the image.
#[spirv(compute(threads(8, 8, 1)))]
pub fn gaussian_samples_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; 512],
    #[spirv(descriptor_set = 0, binding = 2)] samples: &Samples,
) {
    if id.x < 8 && id.y < 8 {
        let pixel =
            ((id.truncate() + shader_to_human::UVec2::ONE) * root.truncate().truncate()) / 9;
        for sample in 0..8 {
            output[((id.y * 8 + id.x) * 8 + sample) as usize] =
                samples.fetch_with(pixel, sample_with::sample_index(sample));
        }
    }
}
