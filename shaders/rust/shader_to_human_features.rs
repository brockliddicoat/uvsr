//! Features source programs. Electronic Arts 2024-2025, BSD-3-Clause.
#![no_std]
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
#[path = "../../crates/shader-to-human/programs/features/mod.rs"]
mod features;
use features::{Inputs, State, WORDS};
use shader_to_human::{IVec2, UVec2, UVec3, UVec4, Vec2, Vec4};
use spirv_std::{Image, spirv};
type Output = Image!(2D, format = rgba8, sampled = false);
type Font = Image!(2D, type = f32, sampled);

/// # Safety
/// U-025: binding0 is an initialized immutable384-byte state/input buffer.
/// Root width/height equal the exclusive initialized Rgba8 storage view and
/// input dimensions, with one z plane. Root kind0..7 and inputs are reviewed.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1), signed_zero_inf_nan_preserve = 32))]
pub unsafe fn features_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[UVec4; WORDS],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.x && id.y < root.y {
        let mut private = State::read(words);
        let color = features::image(root.z, &Inputs::read(words), id.truncate(), &mut private);
        // SAFETY: U-025. Unique guarded texels, one z plane. State is private.
        unsafe { output.write(id.truncate(), color) };
    }
}

/// Exactly one invocation after the image, exclusive384-byte initialized state.
#[spirv(compute(threads(1), signed_zero_inf_nan_preserve = 32))]
pub fn features_commit_cs(
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &mut [UVec4; WORDS],
) {
    let mut value = State::read(words);
    features::commit(root.z, &Inputs::read(words), &mut value);
    value.write(words);
}

/// # Safety
/// U-025: exact initialized image/root extent and immutable input buffer as in
/// features_cs. All invocations read only their own texel and the mouse texel.
/// The mouse invocation must not write, even when writing the same value.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn features_debug_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[UVec4; WORDS],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.x && id.y < root.y {
        let inputs = Inputs::read(words);
        let mouse = inputs.mouse.truncate().truncate().as_ivec2();
        if mouse == id.truncate().as_ivec2() {
            return;
        }
        // Explicit zero for the source's out-of-range D3D texture-load result.
        let selected = if mouse.cmpge(IVec2::ZERO).all()
            && mouse.cmplt(UVec2::new(root.x, root.y).as_ivec2()).all()
        {
            output.read(mouse)
        } else {
            Vec4::ZERO
        };
        let own = output.read(id.truncate());
        let color = features::debug_zoom(&inputs, id.truncate(), own, selected);
        // SAFETY: U-025. Selected texel is immutable. All other reads/writes are
        // invocation-local at distinct guarded coordinates in the exclusive view.
        unsafe { output.write(id.truncate(), color) };
    }
}

/// # Safety
/// U-025: initialized exclusive root-sized output, one z plane, and a distinct
/// initialized768x8 RGBA8_SRGB sampled font view. No concurrent atlas writer.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn features_font_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
    #[spirv(descriptor_set = 0, binding = 2)] atlas: &Font,
) {
    if id.x < root.x && id.y < root.y {
        let color = features::use_font(id.truncate(), |pixel| atlas.fetch(pixel));
        // SAFETY: U-025. Constant ASCII UserFont and guarded8x8 local glyph
        // coordinates bound sampled loads. The output texel is unique/in bounds.
        unsafe { output.write(id.truncate(), color) };
    }
}

/// # Safety
/// U-025: exclusive initialized800x600 Rgba8 view, one8x8x1 group. Only invocation
/// zero writes, ordered and explicitly clipped, preserving untouched clear RGB.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn features_scatter_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id == UVec3::ZERO {
        features::scatter(&mut |pixel, color| {
            if pixel.cmpge(IVec2::ZERO).all() && pixel.cmplt(IVec2::new(800, 600)).all() {
                // SAFETY: U-025. One writer with bounded sequential stores.
                unsafe { output.write(pixel, color) };
            }
        });
    }
}

#[spirv(vertex)]
pub fn features_quad_vs(
    #[spirv(vertex_index)] vertex: u32,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[UVec4; WORDS],
    #[spirv(position)] position: &mut Vec4,
    uv: &mut Vec2,
) {
    (*position, *uv) = features::quad_vertex(vertex, &Inputs::read(words));
}

#[spirv(fragment)]
pub fn features_quad_fs(
    #[spirv(frag_coord)] position: Vec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[UVec4; WORDS],
    uv: Vec2,
    output: &mut Vec4,
) {
    *output = features::quad_fragment(
        &Inputs::read(words),
        uv,
        position.truncate().extend(1.0 / position.w),
    );
}

/// # Safety
/// U-025: initialized exclusive root-sized Rgba8 view and immutable384-byte
/// inputs. One z plane. Each invocation reads and writes only its own texel.
#[allow(unsafe_code)]
#[spirv(compute(threads(8, 8, 1)))]
pub unsafe fn features_quad_post_cs(
    #[spirv(global_invocation_id)] id: UVec3,
    #[spirv(push_constant)] root: &UVec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] words: &[UVec4; WORDS],
    #[spirv(descriptor_set = 0, binding = 1)] output: &Output,
) {
    if id.x < root.x && id.y < root.y {
        let own = output.read(id.truncate());
        let color = features::quad_post(&Inputs::read(words), id.truncate(), own);
        // SAFETY: U-025. Unique guarded read/modify/write after raster completion.
        unsafe { output.write(id.truncate(), color) };
    }
}
