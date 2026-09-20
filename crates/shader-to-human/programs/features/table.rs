//! Source Features/Table_example.hlsl, Electronic Arts 2024-2025, BSD-3-Clause.
use super::{Inputs, State};
use shader_to_human::{text, IVec2, UVec2, Vec2, Vec3, Vec4};

fn integer(column: u32, row: u32) -> Option<i32> {
    if column == 1 {
        if row > 10 {
            None
        } else {
            Some((2 + row * row) as i32)
        }
    } else if row > 12 {
        None
    } else {
        Some(row as i32)
    }
}

pub fn draw(inputs: &Inputs, pixel: UVec2, state: &mut State) -> Vec4 {
    let mut ui = inputs.ui(pixel.as_vec2(), *state);
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    ui.set_scale(3.0);
    ui.print_text(&text!("TableTest"));
    ui.print_lf();
    ui.print_lf();
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.set_scale(2.0);
    ui.print_text(&text!("Pixel=Thread"));
    ui.set_scale(3.0);
    ui.print_lf();
    ui.print_lf();
    ui.set_scale(2.0);
    ui.print_text(&text!("s2h_table"));
    ui.print_lf();
    ui.print_lf();
    ui.print_space(0.5);
    ui.print_text(&text!("Id"));
    ui.print_space(0.5);
    ui.frame(3);
    ui.print_space(1.0);
    ui.print_text(&text!("Cnt"));
    ui.print_space(1.0);
    ui.frame(5);
    ui.print_space(3.0);
    ui.print_text(&text!("x"));
    ui.print_space(3.0);
    ui.frame(7);
    ui.print_space(3.0);
    ui.print_text(&text!("y"));
    ui.print_space(3.0);
    ui.frame(7);
    ui.print_lf();
    ui.table_int(0, Vec3::ONE.extend(0.35), IVec2::new(3, 15), true, integer);
    ui.table_int(
        1,
        Vec3::splat(0.4).extend(0.75),
        IVec2::new(5, 15),
        true,
        integer,
    );
    let floating = |column, row| {
        if row > 10 {
            None
        } else if column == 2 {
            Some(libm::sinf(inputs.time() + row as f32 * 0.5))
        } else {
            Some(libm::cosf(inputs.time() + row as f32 * 0.5))
        }
    };
    ui.table_float(2, Vec3::X.extend(0.35), IVec2::new(7, 15), true, floating);
    ui.table_float(3, Vec3::Y.extend(0.25), IVec2::new(7, 15), false, floating);
    ui.print_lf();
    ui.print_text(&text!("s2h_function"));
    ui.print_lf();
    ui.set_scale(2.0);
    ui.text_color = Vec4::ONE;
    let range_x = Vec2::new(0.0, core::f32::consts::PI * 2.0);
    let range_y = Vec2::new(-1.3, 1.3);
    ui.function(
        0,
        Vec3::ZERO.extend(0.45),
        IVec2::new(22, 8),
        range_x,
        range_y,
        |_, x| libm::sinf(x) + libm::cosf(inputs.time() * 3.0 + x * 15.0) * 0.1,
    );
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.print_text(&text!("x: "));
    ui.print_float(range_x.x);
    ui.print_text(&text!(" .. "));
    ui.print_float(range_x.y);
    ui.print_lf();
    ui.print_text(&text!("y: "));
    ui.print_float(range_y.x);
    ui.print_text(&text!(" .. "));
    ui.print_float(range_y.y);
    ui.print_lf();
    let color = (Vec3::new(0.4, 0.7, 0.4) * (1.0 - ui.color.w) + ui.color.truncate()).extend(1.0);
    state.capture = ui.deinit();
    color
}
