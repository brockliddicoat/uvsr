//! Source: unittests/2D_test.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use shader_to_human::{
    linear_to_srgb, srgb_to_linear, text, ContextGather, UVec2, Vec2, Vec3, Vec4,
};

pub fn two_d(pixel: UVec2) -> Vec4 {
    let mut ui = ContextGather::new(pixel.as_vec2() + Vec2::splat(0.5));
    ui.set_cursor(Vec2::splat(10.0));
    ui.set_scale(3.0);
    ui.print_text(&text!("2DTest"));
    ui.print_lf();
    ui.print_lf();
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.set_scale(2.0);
    ui.print_text(&text!("with AA"));
    let red = Vec3::X.extend(1.0);
    let green = Vec3::Y.extend(1.0);
    let blue = Vec3::Z.extend(1.0);
    let white = Vec4::ONE;
    let black = Vec3::ZERO.extend(1.0);
    let backup = ui.line_width;
    ui.line_width = 2.0;
    ui.draw_circle(Vec2::new(50.0, 120.0), 20.0, red);
    ui.line_width = 4.0;
    ui.draw_circle(Vec2::new(50.0, 120.0), 30.0, green);
    for i in 0..13 {
        let center = Vec2::new(220.0, 120.0);
        let angle = i as f32 / 13.0 * 2.0 * core::f32::consts::PI;
        let d = Vec2::new(libm::sinf(angle), libm::cosf(angle));
        ui.line_width = 6.0;
        ui.draw_line(center + d * 20.0, center + d * 50.0, blue);
    }
    ui.draw_rectangle(Vec2::new(20.0, 210.0), Vec2::new(80.0, 280.0), blue);
    ui.draw_rectangle(Vec2::new(50.0, 240.0), Vec2::new(150.0, 300.0), blue * 0.5);
    ui.draw_rectangle_aa(
        Vec2::new(220.0, 210.0),
        Vec2::new(280.0, 280.0),
        blue,
        white,
        10.0,
    );
    ui.draw_rectangle_aa(
        Vec2::new(250.0, 240.0),
        Vec2::new(350.0, 300.0),
        blue * 0.5,
        white * 0.5,
        10.0,
    );
    ui.line_width = 3.0;
    ui.draw_crosshair(Vec2::new(50.0, 120.0), 10.0, blue);
    ui.draw_crosshair(Vec2::new(100.5, 120.5), 10.0, black);
    ui.line_width = 1.0;
    ui.draw_crosshair(Vec2::new(100.5, 120.5), 10.0, white);
    ui.line_width = 4.0;
    ui.draw_crosshair(Vec2::new(130.0, 120.0), 10.0, black);
    ui.line_width = 2.0;
    ui.draw_crosshair(Vec2::new(130.0, 120.0), 10.0, white);
    ui.line_width = backup;
    ui.draw_disc(Vec2::new(450.0, 220.0), 30.0, red);
    ui.draw_disc(Vec2::new(450.0, 220.0), 20.0, white);
    ui.draw_disc(Vec2::new(450.0, 220.0), 10.0, red);
    for i in 0..8 {
        let angle = core::f32::consts::PI * 2.0 * i as f32 / 8.0;
        let center = Vec2::new(100.0, 400.0);
        let mut plane = Vec3::new(libm::sinf(angle), libm::cosf(angle), 0.0);
        plane.z -= plane.dot(center.extend(1.0));
        plane.z += 80.0;
        plane *= (i + 1) as f32;
        ui.draw_half_space(plane, center, white, 20.0, 30.0);
    }
    let background = srgb_to_linear(Vec3::new(0.7, 0.5, 0.5));
    linear_to_srgb(background * (1.0 - ui.color.w) + ui.color.truncate()).extend(1.0)
}
