//! Source Features/2D_example.hlsl, Electronic Arts 2024-2025, BSD-3-Clause.
use super::{Inputs, State};
use shader_to_human::{
    index_to_color, linear_to_srgb, text, ContextGather, UVec2, Vec2, Vec3, Vec4,
};

fn eye(ui: &mut ContextGather, center: Vec2) {
    let mut offset = ui.mouse_input.truncate().truncate() - center;
    if offset.length() > 11.0 {
        offset = offset.normalize() * 11.0;
    }
    ui.draw_disc(center, 30.0, Vec3::ZERO.extend(1.0));
    ui.draw_disc(center, 25.0, Vec4::ONE);
    ui.draw_disc(center + offset, 15.0, Vec3::ZERO.extend(1.0));
}

fn blending(ui: &mut ContextGather) {
    let background = Vec3::new(0.3, 0.6, 0.9);
    let mut over = background;
    let mut under = Vec4::ZERO;
    for layer in 0..5 {
        let angle = layer as f32 * 0.6;
        let color = index_to_color(layer);
        let center =
            Vec2::new(libm::sinf(angle), libm::cosf(angle)) * 60.0 + Vec2::new(105.0, 450.0);
        let alpha = (2.0 - (ui.pixel - center).length() / 25.0).clamp(0.0, 1.0);
        over = over * (1.0 - alpha) + color * alpha;
        under = (under.truncate() + color * alpha * (1.0 - under.w))
            .extend(1.0 - (1.0 - alpha) * (1.0 - under.w));
    }
    let under_color = background * (1.0 - under.w) + under.truncate();
    let min = Vec2::new(10.0, 300.0);
    let max = Vec2::new(290.0, 590.0);
    let mut color = Vec3::X;
    ui.draw_rectangle(
        min - Vec2::splat(3.0),
        max + Vec2::splat(3.0),
        Vec3::ZERO.extend(1.0),
    );
    ui.text_color = Vec4::ZERO;
    if ui.pixel.cmpge(min).all() && ui.pixel.cmplt(max).all() {
        ui.text_color = Vec3::ZERO.extend(1.0);
        let compare = libm::floorf(ui.pixel.x) - libm::floorf(ui.mouse_input.x);
        if compare < -2.0 {
            color = under_color;
        } else if compare > 2.0 {
            color = over;
        } else if compare == 0.0 {
            color = Vec3::new(1.0, 1.0, 0.0);
        }
    }
    ui.draw_rectangle(min, max, color.extend(1.0));
    ui.set_scale(2.0);
    ui.set_cursor(Vec2::new(ui.mouse_input.x - 8.0 * 5.4 * ui.scale, 310.0));
    ui.print_text(&text!("under over"));
    ui.text_color = Vec4::ONE;
}

pub fn draw(inputs: &Inputs, pixel: UVec2, state: &mut State) -> Vec4 {
    // The HLSL mainCS/mainImage flips cancel to (x+1,y), not (x+.5,y+.5).
    let mut ui = inputs.ui(pixel.as_vec2() + Vec2::X, *state);
    let red = Vec3::X.extend(1.0);
    let green = Vec3::Y.extend(1.0);
    let blue = Vec3::Z.extend(1.0);
    ui.set_scale(3.0);
    ui.print_text(&text!("2DTest"));
    ui.print_lf();
    ui.print_lf();
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.set_scale(2.0);
    ui.print_text(&text!("with AA"));
    ui.set_cursor(Vec2::new(200.0, 5.0));
    ui.slider_rgba(8, &mut state.color0);
    ui.print_space(1.0);
    ui.print_text(&text!("top layer"));
    for _ in 0..5 {
        ui.print_lf();
    }
    ui.slider_rgba(8, &mut state.color1);
    ui.print_space(1.0);
    ui.print_text(&text!("bottom layer"));
    for _ in 0..5 {
        ui.print_lf();
    }
    ui.slider_float(8, &mut state.sizes.x, 0.0, 20.0);
    ui.print_text(&text!(" top border"));
    ui.print_lf();
    ui.slider_float(8, &mut state.sizes.y, 0.0, 20.0);
    ui.print_text(&text!(" bottom border"));
    ui.print_lf();
    ui.draw_rectangle_aa(
        Vec2::new(250.0, 240.0),
        Vec2::new(350.0, 300.0),
        Vec4::ONE,
        state.color1,
        state.sizes.y,
    );
    ui.draw_rectangle_aa(
        Vec2::new(220.0, 210.0),
        Vec2::new(280.0, 280.0),
        Vec4::ONE,
        state.color0,
        state.sizes.x,
    );
    let backup = ui.line_width;
    ui.line_width = 2.0;
    ui.draw_circle(Vec2::new(50.0, 120.0), 20.0, red);
    ui.line_width = 4.0;
    ui.draw_circle(Vec2::new(50.0, 120.0), 30.0, green);
    ui.line_width = 3.0;
    ui.draw_crosshair(Vec2::new(50.0, 120.0), 10.0, blue);
    ui.line_width = backup;
    let center = Vec2::new(50.0, 200.0);
    let sc = Vec2::new(libm::sinf(inputs.time()), libm::cosf(inputs.time())) * 20.0;
    ui.line_width = 12.0;
    ui.draw_line(center + sc, center - sc, blue);
    ui.line_width = backup;
    ui.line_width = 3.0;
    ui.draw_crosshair(Vec2::new(100.5, 120.5), 10.0, Vec3::ZERO.extend(1.0));
    ui.line_width = 1.0;
    ui.draw_crosshair(Vec2::new(100.5, 120.5), 10.0, Vec4::ONE);
    ui.line_width = backup;
    ui.line_width = 4.0;
    ui.draw_crosshair(Vec2::new(130.0, 120.0), 10.0, Vec3::ZERO.extend(1.0));
    ui.line_width = 2.0;
    ui.draw_crosshair(Vec2::new(130.0, 120.0), 10.0, Vec4::ONE);
    ui.line_width = backup;
    eye(&mut ui, Vec2::new(450.0, 240.0));
    eye(&mut ui, Vec2::new(510.0, 240.0));
    ui.draw_srgb_ramp(Vec2::new(520.0, 10.0));
    for i in 0..3 {
        let angle = i as f32 * core::f32::consts::PI * 2.0 / 3.0;
        let mut plane = Vec3::new(libm::sinf(angle), libm::cosf(angle), 0.0);
        plane.z -= plane.dot(Vec3::new(450.0, 340.0, 1.0));
        ui.draw_half_space(
            plane,
            ui.mouse_input.truncate().truncate(),
            index_to_color(i + 1).extend(1.0),
            20.0,
            40.0,
        );
    }
    blending(&mut ui);
    let linear = Vec4::new(0.7, 0.4, 0.4, 1.0) * (1.0 - ui.color.w) + ui.color;
    state.capture = ui.deinit();
    linear_to_srgb(linear.truncate()).extend(linear.w)
}
