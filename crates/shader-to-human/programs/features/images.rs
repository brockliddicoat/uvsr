//! Features image helpers, Electronic Arts 2024-2025, BSD-3-Clause.
use super::{Inputs, State};
use shader_to_human::{linear_to_srgb, text, ContextGather, UVec2, Vec2, Vec3, Vec4};

fn arrow(ui: &mut ContextGather, from: Vec2, to: Vec2, length: f32, width: f32) {
    ui.draw_arrow(
        from,
        to,
        Vec3::ZERO.extend(1.0),
        ui.line_width * length,
        ui.line_width * width,
    );
    let backup = ui.line_width;
    ui.line_width = 1.0;
    ui.draw_crosshair(from, 5.0, Vec3::X.extend(0.5));
    ui.draw_crosshair(to, 5.0, Vec3::Y.extend(0.5));
    ui.line_width = backup;
}

pub fn arrows(inputs: &Inputs, pixel: UVec2, state: &mut State) -> Vec4 {
    let mut ui = inputs.ui(pixel.as_vec2(), *state);
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    for r in 0..8 {
        ui.line_width = r as f32;
        let y = 50.0 + r as f32 * 20.0;
        arrow(&mut ui, Vec2::new(20.0, y), Vec2::new(60.0, y), 0.0, 0.0);
        arrow(&mut ui, Vec2::new(120.0, y), Vec2::new(160.0, y), 4.0, 1.5);
        arrow(&mut ui, Vec2::new(220.0, y), Vec2::new(260.0, y), 8.0, 1.5);
        arrow(&mut ui, Vec2::new(320.0, y), Vec2::new(360.0, y), 8.0, 0.5);
        arrow(&mut ui, Vec2::new(420.0, y), Vec2::new(560.0, y), 8.0, 1.5);
    }
    ui.line_width = 5.0;
    for i in 0..12 {
        let angle = i as f32 / 12.0 * core::f32::consts::PI * 2.0;
        let center = Vec2::new(700.0, 120.0);
        let sc = Vec2::new(libm::sinf(angle), libm::cosf(angle)) * 80.0;
        arrow(&mut ui, center + sc * 0.3, center + sc, 8.0, 1.5);
    }
    ui.set_cursor(Vec2::new(20.0, 20.0));
    ui.print_text(&text!("0, 0"));
    ui.set_cursor(Vec2::new(120.0, 20.0));
    ui.print_text(&text!("4, 1.5"));
    ui.set_cursor(Vec2::new(220.0, 20.0));
    ui.print_text(&text!("8, 1.5"));
    ui.set_cursor(Vec2::new(320.0, 20.0));
    ui.print_text(&text!("8, 0.5"));
    ui.set_cursor(Vec2::new(470.0, 20.0));
    ui.print_text(&text!("8, 1.5"));
    ui.set_cursor(Vec2::new(670.0, 20.0));
    ui.print_text(&text!("8, 1.5"));
    ui.set_cursor(Vec2::ZERO);
    ui.line_width = 10.0;
    let center = inputs.dimensions() / 2.0;
    let mouse = ui.mouse_input.truncate().truncate();
    let length = (mouse - center).length();
    let head_length = (0.25 * length).max(40.0);
    let head_width = (0.5 * head_length).max(20.0);
    // Source calls this color "blue", but its actual components are green.
    ui.draw_arrow(center, mouse, Vec3::Y.extend(1.0), head_length, head_width);
    let opposite = center + (center - mouse).normalize() * length;
    ui.draw_arrow(
        center,
        opposite,
        Vec3::X.extend(1.0),
        head_length,
        head_width,
    );
    let linear = Vec3::splat(0.5).extend(1.0) * (1.0 - ui.color.w) + ui.color;
    state.capture = ui.deinit();
    linear_to_srgb(linear.truncate()).extend(linear.w)
}

pub fn coordinates(pixel: UVec2) -> Vec4 {
    let mut ui = ContextGather::new(pixel.as_vec2() + Vec2::splat(0.5));
    ui.set_scale(2.0);
    ui.set_cursor(Vec2::splat(10.0));
    ui.coordinate_system(
        Vec2::new(50.0, 130.0),
        Vec4::new(-30.0, -30.0, 250.0, 250.0),
        1.0,
        20.0,
        Vec3::ONE.extend(0.25),
        0,
    );
    ui.line_width = 1.0;
    ui.coordinate_system(
        Vec2::new(440.0, 150.0),
        Vec4::new(-10.0, -120.0, 150.0, 10.0),
        1.0,
        20.0,
        Vec3::ONE.extend(0.25),
        3,
    );
    ui.print_text(&text!("s2h_coordinateSystem"));
    let color = Vec4::new(0.01, 0.01, 0.1, 1.0) * (1.0 - ui.color.w) + ui.color;
    linear_to_srgb(color.truncate()).extend(color.w)
}

pub fn font_atlas(pixel: UVec2, time: f32) -> Vec4 {
    let hue = time + (pixel.x + pixel.y) as f32 / 16.0 * 0.1;
    let value = Vec3::splat(hue * 6.0) + Vec3::new(0.0, 4.0, 2.0);
    let divided = value / 6.0;
    let modulo = (divided - divided.floor()) * 6.0;
    let rgb = ((modulo - Vec3::splat(3.0)).abs() - Vec3::ONE).clamp(Vec3::ZERO, Vec3::ONE);
    let rgb = rgb * rgb * (Vec3::splat(3.0) - 2.0 * rgb);
    let mut ui = ContextGather::new(Vec2::new((pixel.x % 8) as f32, pixel.y as f32));
    ui.text_color = rgb.extend(1.0);
    ui.print_character(pixel.x / 8 + 32);
    ui.color
}

/// Source custom printCharacter reads colored, sRGB-decoded atlas texels.
/// It intentionally ignores textColor and has a different local pixel offset
/// from the embedded monochrome font. The caller supplies the sampled load.
pub fn use_font(pixel: UVec2, mut load: impl FnMut(UVec2) -> Vec4) -> Vec4 {
    let mut ui = ContextGather::new(pixel.as_vec2());
    ui.set_cursor(Vec2::splat(10.0));
    ui.scale = 4.0;
    ui.color = Vec3::ZERO.extend(1.0);
    let characters = text!("UserFont");
    let mut index = 0;
    while index < characters.len() {
        let local = ((ui.pixel - ui.cursor + Vec2::splat(0.5)) / ui.scale)
            .floor()
            .as_ivec2();
        if local.as_uvec2().cmplt(UVec2::splat(8)).all() {
            let texel = UVec2::new(
                (characters[index] - 32) * 8 + local.x as u32,
                local.y as u32,
            );
            let font = load(texel);
            ui.color = ui.color * (1.0 - font.w) + font.truncate().extend(1.0) * font.w;
        }
        ui.cursor.x += 8.0 * ui.scale;
        index += 1;
    }
    ui.color
}

/// The shader leaves the selected mouse texel untouched, so all invocations
/// may read that immutable texel while updating only their own destination.
pub fn debug_zoom(inputs: &Inputs, pixel: UVec2, original: Vec4, mouse_color: Vec4) -> Vec4 {
    let mouse = inputs.mouse.truncate().truncate().as_ivec2();
    if mouse == pixel.as_ivec2() {
        return original;
    }
    let point = mouse.as_vec2();
    let mut ui = ContextGather::new(pixel.as_vec2());
    ui.set_cursor(point + Vec2::splat(30.0));
    let backup = ui.line_width;
    ui.line_width = 1.0;
    ui.draw_crosshair(point + Vec2::splat(0.5), 30.0, Vec4::ONE);
    ui.line_width = backup;
    ui.draw_rectangle_aa(
        point + Vec2::splat(15.0),
        point + Vec2::new(280.0, 180.0),
        Vec3::ONE.extend(0.0),
        Vec3::splat(0.125).extend(0.8),
        2.0,
    );
    let integers = (mouse_color * 255.0).as_ivec4().to_array();
    let values = mouse_color.to_array();
    ui.set_scale(3.0);
    ui.print_text(&text!("Pixel"));
    ui.print_lf();
    ui.set_scale(2.0);
    ui.print_lf();
    ui.print_text(&text!("xy="));
    ui.print_int(mouse.x);
    ui.print_text(&text!(","));
    ui.print_int(mouse.y);
    ui.print_lf();
    ui.print_lf();
    for channel in 0..4 {
        ui.text_color = match channel {
            0 => Vec3::new(0.9, 0.1, 0.1).extend(1.0),
            1 => Vec3::new(0.0, 0.8, 0.0).extend(1.0),
            2 => Vec3::new(0.2, 0.2, 1.0).extend(1.0),
            _ => Vec3::splat(0.85).extend(1.0),
        };
        ui.print_character(match channel {
            0 => 'R',
            1 => 'G',
            2 => 'B',
            _ => 'A',
        } as u32);
        ui.print_text(&text!("="));
        ui.print_int(integers[channel]);
        ui.print_text(&text!(" "));
        ui.print_float(values[channel]);
        ui.print_lf();
    }
    if ui.color.w != 0.0 {
        original * (1.0 - ui.color.w) + ui.color.truncate().extend(1.0) * ui.color.w
    } else {
        original
    }
}
