//! Active compute, vertex, fragment and resolve programs at d6f98b7d.
use super::{math, Inputs};
use shader_to_human::{
    linear_to_srgb, srgb_to_linear, text, Context3D, ContextGather, UVec2, Vec2, Vec3, Vec4,
};

pub fn base_image(kind: u32, pixel: UVec2, inputs: &Inputs) -> Vec4 {
    let mut output = (Vec3::new(0.1, 0.2, 0.3) * 0.7).extend(1.0);
    let mut ui = ContextGather::new(pixel.as_vec2());
    ui.set_cursor(Vec2::new(10.0, 40.0));
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    ui.text_color = Vec3::ONE.extend(ui.text_color.w);
    ui.set_scale(2.0);
    ui.print_lf();
    match kind {
        0 => {
            ui.print_text(&text!("CS Raster"));
            ui.print_lf();
        }
        1 => {
            ui.print_text(&text!("CS Ray"));
            ui.print_lf();
            ui.print_text(&text!("PerspectiveCorrect"));
            ui.print_lf();
        }
        2 => {
            ui.print_text(&text!("VSPS Raster"));
            ui.print_lf();
        }
        3 => {
            ui.print_text(&text!("CS Ray"));
            ui.print_lf();
            ui.print_text(&text!("ManySplats"));
            ui.print_lf();
        }
        _ => {}
    }
    ui.draw_srgb_ramp(Vec2::splat(2.0));
    output = output.lerp(ui.color.truncate().extend(1.0), ui.color.w);
    linear_to_srgb(output.truncate()).extend(output.w)
}

fn scene(context: &mut Context3D, inputs: &Inputs) {
    context.draw_checker_board(Vec3::new(0.0, -1.0, 0.0));
    context.draw_basis(
        math::splat_base(math::procedural_splat(0, inputs.offset.truncate())),
        math::cutoff_scale(),
    );
}

fn stochastic(context: &mut Context3D, pixel: UVec2, sample: u32, inputs: &Inputs) {
    let phase = sample.wrapping_mul(12345);
    let seed = pixel
        .x
        .wrapping_mul(phase.wrapping_add(82927))
        .wrapping_add(pixel.y.wrapping_mul(phase.wrapping_add(21313)));
    let mut random =
        math::init_random(seed, 0x12345678_u32.wrapping_add(inputs.random_frame()), 16);
    let origin = context.origin + context.direction * inputs.ray_bounds.x;
    let max_t = inputs.ray_bounds.y.min(context.depth) - inputs.ray_bounds.x;
    for id in 0..6 {
        let splat = math::procedural_splat(id, inputs.offset.truncate());
        let mut depth = math::next_random(&mut random);
        let color = math::ray_cast(origin, context.direction, splat, &mut depth, max_t);
        if color.w > math::next_random(&mut random) && depth < context.depth {
            context.depth = depth;
            context.color = srgb_to_linear(color.truncate()).extend(1.0);
        }
    }
}

pub fn compute_image(kind: u32, pixel: UVec2, prior: Vec4, inputs: &Inputs) -> Vec4 {
    let center = pixel.as_vec2() + Vec2::splat(0.5);
    let mut context = Context3D::new(inputs.camera_near.truncate(), inputs.ray(center));
    let mut output = srgb_to_linear(prior.truncate()).extend(prior.w);
    if kind == 1 {
        scene(&mut context, inputs);
        output = output.lerp(context.color.truncate().extend(1.0), context.color.w);
    }
    let splat = math::procedural_splat(0, inputs.offset.truncate());
    let (params, visible) = math::rasterize(splat, &inputs.camera);
    // Retain source TESTID3's visibility gate on the first procedural splat.
    if visible {
        if kind == 0 {
            let color = params.evaluate(center);
            output = output.lerp(srgb_to_linear(color.truncate()).extend(1.0), color.w);
            let conic = params.original_conic();
            let d = center - params.center;
            let error = conic.x * d.x * d.x + 2.0 * conic.y * d.x * d.y + conic.z * d.y * d.y
                - math::cutoff_scale() * math::cutoff_scale();
            if error.abs() < 0.1 {
                output = output.lerp(Vec4::new(0.0, 1.0, 0.0, 1.0), 0.5);
            }
            let mut ui = ContextGather::new(pixel.as_vec2());
            ui.set_cursor(Vec2::splat(10.0));
            let bounds = params.aabb();
            ui.draw_rectangle_aa(
                bounds.truncate().truncate(),
                Vec2::new(bounds.z, bounds.w),
                Vec4::new(0.0, 1.0, 1.0, 0.5),
                Vec4::ZERO,
                3.0,
            );
            output =
                (output.truncate() * (1.0 - ui.color.w) + ui.color.truncate()).extend(output.w);
        } else if kind == 1 {
            let origin = context.origin + context.direction * inputs.ray_bounds.x;
            let max_t = inputs.ray_bounds.y.min(context.depth) - inputs.ray_bounds.x;
            let mut depth = f32::MAX;
            let color = math::ray_cast(origin, context.direction, splat, &mut depth, max_t);
            output = output.lerp(srgb_to_linear(color.truncate()).extend(1.0), color.w);
        } else if kind == 3 {
            scene(&mut context, inputs);
            let mut sum = Vec4::ZERO;
            for sample in 0..8 {
                let mut inner = context;
                stochastic(&mut inner, pixel, sample, inputs);
                sum += (inner.color.truncate() * inner.color.w).extend(inner.color.w);
            }
            context.color = sum / 8.0;
            if context.color.w > 0.0001 {
                context.color =
                    (context.color.truncate() / context.color.w).extend(context.color.w);
            }
            output = output.lerp(context.color.truncate().extend(1.0), context.color.w);
        }
    }
    linear_to_srgb(output.truncate()).extend(output.w)
}

pub fn clear_fragment(pixel: UVec2, inputs: &Inputs) -> Vec4 {
    let background = Vec3::new(0.1, 0.2, 0.3) * 0.7;
    let mut ui = ContextGather::new(pixel.as_vec2());
    ui.draw_srgb_ramp(Vec2::splat(2.0));
    let linear = background.lerp(ui.color.truncate(), ui.color.w);
    // Source uses integer pixel coordinates here, unlike the compute half pixel.
    let mut context = Context3D::new(inputs.camera_near.truncate(), inputs.ray(pixel.as_vec2()));
    context.color = linear.extend(context.color.w);
    scene(&mut context, inputs);
    linear_to_srgb(context.color.truncate()).extend(0.5)
}

fn quad_uv(vertex: u32) -> Vec2 {
    match vertex % 6 {
        1 => Vec2::new(1.0, 0.0),
        2 | 3 => Vec2::ONE,
        4 => Vec2::new(0.0, 1.0),
        _ => Vec2::ZERO,
    }
}

pub fn fullscreen_vertex(vertex: u32) -> Vec4 {
    (quad_uv(vertex) * 2.0 - Vec2::ONE).extend(0.5).extend(1.0)
}

pub fn splat_vertex(
    vertex: u32,
    splat: math::Splat,
    camera: &math::Camera,
) -> (Vec4, math::Raster) {
    // The source computes visibility but does not use it to suppress vertices.
    let (params, _) = math::rasterize(splat, camera);
    let ndc = (params.center / camera.dimensions * Vec2::new(2.0, -2.0)) - Vec2::new(1.0, -1.0);
    let depth = math::device_depth(-params.splat_z.x, camera.view_to_clip);
    // Source VS passes (a,2b,c) to computeCornerPs, which halves its middle
    // coefficient. The debug/AABB helper returns (a,b,c), a different contract.
    let conic = params.original_conic() * Vec3::new(1.0, 2.0, 1.0);
    let corner = math::corner_pixels(quad_uv(vertex) * 2.0 - Vec2::ONE, conic) / camera.dimensions
        * Vec2::new(2.0, -2.0);
    (
        (ndc + corner * math::cutoff_scale())
            .extend(depth)
            .extend(1.0),
        params,
    )
}

pub fn splat_fragment(
    pixel: Vec2,
    params: math::Raster,
    splat_id: u32,
    inputs: &Inputs,
) -> (Vec4, f32, u32) {
    let value = params.evaluate(pixel);
    let depth = math::device_depth(-params.splat_z.x, inputs.camera.view_to_clip);
    // WEIGHT_EXPERIMENT=0, as in the pinned source. RGB is not decoded here.
    let coverage = math::coverage(
        value.w,
        pixel.as_uvec2().to_array(),
        splat_id,
        inputs.random_frame(),
    );
    (value.truncate().extend(1.0), depth, coverage)
}

pub fn resolve(mut fetch: impl FnMut(u32) -> Vec4) -> Vec4 {
    let mut value = Vec4::new(0.0, 0.0, 0.0, 0.0001);
    for sample in 0..8 {
        // The active source replaces every sample's alpha with one, after the
        // unused FIXUP_MUL division. Keep the extra manual sRGB decode and bias.
        value += srgb_to_linear(fetch(sample).truncate()).extend(1.0);
    }
    linear_to_srgb(value.truncate() / value.w).extend(1.0)
}
