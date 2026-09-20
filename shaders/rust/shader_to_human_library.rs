//! Compile-only coverage probe for the translated ShaderToHuman library.
//! Source-derived calls retain the BSD-3-Clause terms in NOTICES.md.
//! No runtime or golden-image claim follows from this module.
#![no_std]
#![forbid(unsafe_code)]
use shader_to_human::{
    Context3D, ContextGather, ContextScatter, IVec2, Triangle, Vec2, Vec3, Vec4,
};
use spirv_std::spirv;
#[spirv(compute(threads(1)))]
pub fn main_cs(
    #[spirv(push_constant)] input: &Vec4,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] output: &mut [Vec4; 1],
) {
    let p = Vec2::new(input.x, input.y);
    let mut ui = ContextGather::new(p);
    ui.set_cursor(Vec2::new(2.0, 3.0));
    ui.print_text(&shader_to_human::text!("S2H"));
    ui.print_float(input.z);
    ui.print_int(input.w as i32);
    ui.print_hex(input.w.to_bits());
    ui.draw_disc(p * 0.5, 8.0, Vec4::ONE);
    ui.draw_circle(p * 0.25, 6.0, Vec4::ONE);
    ui.draw_line(Vec2::ZERO, p, Vec4::ONE);
    ui.draw_arrow(Vec2::ZERO, p, Vec4::ONE, 3.0, 2.0);
    ui.draw_triangle(
        Triangle {
            a: Vec2::ZERO,
            b: p,
            c: Vec2::new(p.y, p.x),
        },
        Vec4::ONE,
    );
    ui.draw_half_space(Vec3::new(1.0, 0.0, input.z), p, Vec4::ONE, 4.0, 10.0);
    ui.draw_rectangle_aa(Vec2::ZERO, p, Vec4::ONE, Vec4::ZERO, 2.0);
    ui.draw_srgb_ramp(p);
    ui.coordinate_system(
        Vec2::ZERO,
        Vec4::new(-1.0, -1.0, 1.0, 1.0),
        4.0,
        4.0,
        Vec4::ONE,
        3,
    );
    ui.print_box(Vec4::ONE);
    ui.print_disc(Vec4::ONE);
    ui.frame(4);
    ui.button(4);
    ui.radio_button(true);
    ui.check_box(false);
    ui.progress(4, input.z);
    let mut scalar = input.z;
    ui.slider_float(6, &mut scalar, 0.0, 1.0);
    let mut rgb = Vec3::splat(input.z);
    ui.slider_rgb(6, &mut rgb);
    let mut rgba = *input;
    ui.slider_rgba(6, &mut rgba);
    ui.table_int(0, Vec4::ONE, IVec2::new(4, 4), true, |column, row| {
        Some((column + row) as i32)
    });
    ui.table_float(0, Vec4::ONE, IVec2::new(4, 4), false, |_, row| {
        Some(row as f32)
    });
    ui.function(
        0,
        Vec4::ONE,
        IVec2::new(4, 4),
        Vec2::new(0.0, 1.0),
        Vec2::new(0.0, 1.0),
        |_, x| x * x,
    );
    let mut accumulated = Vec4::ZERO;
    let mut sink = |position: IVec2, color: Vec4| {
        if position.x == input.x as i32 {
            accumulated += color;
        }
    };
    let mut scatter = ContextScatter::default();
    scatter.print_text(&shader_to_human::text!("Rust"), &mut sink);
    scatter.print_float(input.z, &mut sink);
    scatter.print_block(Vec4::ONE, &mut sink);
    scatter.print_disc(Vec4::ONE, &mut sink);
    scatter.draw_crosshair(p, 4.0, Vec4::ONE, &mut sink);
    let mut world = Context3D::new(Vec3::new(input.x, input.y, -3.0), Vec3::Z);
    world.draw_aabb(Vec3::ZERO, Vec3::ONE, Vec4::ONE);
    world.draw_line(Vec3::ZERO, Vec3::Y, Vec4::ONE, 0.1);
    world.draw_arrow(Vec3::ZERO, Vec3::Y, Vec4::ONE, 0.1);
    world.draw_sphere(Vec3::ZERO, Vec4::ONE, 1.0);
    world.draw_basis(shader_to_human::Mat4::IDENTITY, 1.0);
    world.draw_checker_board(Vec3::ZERO);
    world.draw_skybox();
    world.scene_with_shadows(|ray| ray.draw_sphere(Vec3::ZERO, Vec4::ONE, 1.0));
    let distance = shader_to_human::distance_to_aabb(p, *input);
    output[0] = Vec4::splat(distance)
        + ui.color
        + world.color
        + accumulated
        + shader_to_human::linear_to_srgb(rgb).extend(rgba.w);
}
