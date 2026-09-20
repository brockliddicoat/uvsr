//! Source: unittests/gather_test.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use super::UiState;
use shader_to_human::{text, ContextGather, UVec2, Vec2, Vec3, Vec4};

fn separator(ui: &mut ContextGather) {
    ui.text_color = Vec4::ONE;
    ui.set_scale(1.0);
    ui.print_lf();
    ui.print_space(30.0);
    ui.frame(30);
    ui.print_lf();
    ui.print_lf();
}

/// One source invocation. State is private to the invocation. The host commits
/// mouse-selected updates separately instead of racing writes to one buffer.
pub fn gather(pixel: UVec2, state: &mut UiState, mouse: Vec4, previous_mouse: Vec4) -> Vec4 {
    let mut ui = ContextGather::new(pixel.as_vec2() + Vec2::splat(0.5));
    ui.set_cursor(Vec2::new(10.0, 10.0));
    ui.mouse_input = mouse.as_ivec4().as_vec4();
    let left = mouse.z != 0.0;
    let clicked = left && previous_mouse.z == 0.0;
    ui.set_scale(2.0);
    ui.print_text(&text!("GatherTest"));
    ui.print_lf();
    separator(&mut ui);
    ui.text_color = Vec4::new(1.0, 1.0, 0.0, 1.0);
    for i in 1..=3 {
        ui.set_scale(i as f32);
        ui.print_text(&text!("ABCabc"));
        ui.print_space(2.0);
        ui.print_text(&text!("x"));
        ui.print_lf();
    }
    separator(&mut ui);
    for i in 1..=2 {
        ui.set_scale(i as f32);
        let values = [0, -12345, 8, 0x7ffffff, -0x7ffffff];
        let mut value_index = 0;
        while value_index < values.len() {
            let value = values[value_index];
            ui.print_int(value);
            ui.print_lf();

            value_index += 1;
        }
    }
    separator(&mut ui);
    for i in 1..=2 {
        ui.set_scale(i as f32);
        let values = [0, 0xfffffff, 0x87654321, 0x09abcdef];
        let mut value_index = 0;
        while value_index < values.len() {
            let value = values[value_index];
            ui.print_hex(value);
            ui.print_lf();

            value_index += 1;
        }
    }
    separator(&mut ui);
    for i in 1..=2 {
        ui.set_scale(i as f32);
        let values = [
            0.0,
            -0.1,
            -12.34,
            12.34,
            -12345.0,
            100.0,
            0x7ffffff as f32,
            -0x7ffffff as f32,
        ];
        let mut value_index = 0;
        while value_index < values.len() {
            let value = values[value_index];
            ui.print_float(value);
            ui.print_lf();

            value_index += 1;
        }
    }
    separator(&mut ui);
    ui.set_cursor(Vec2::new(269.0, 5.0));
    ui.set_scale(2.0);
    let values = [0.5, 1.0, 10.0];
    let mut value_index = 0;
    while value_index < values.len() {
        let intensity = values[value_index];
        ui.text_color = (Vec3::X * intensity).extend(1.0);
        ui.print_text(&text!("R"));
        ui.text_color = (Vec3::Y * intensity).extend(1.0);
        ui.print_text(&text!("G"));
        ui.text_color = (Vec3::Z * intensity).extend(1.0);
        ui.print_text(&text!("B"));
        if intensity != 10.0 {
            ui.print_text(&text!(" "));
        }

        value_index += 1;
    }
    ui.print_lf();
    separator(&mut ui);
    for i in 1..=3 {
        ui.set_scale(i as f32);
        ui.print_box(Vec4::new(1.0, 0.7, 0.3, 1.0));
        ui.print_box(Vec3::X.extend(1.0));
        ui.print_disc(Vec3::Y.extend(1.0));
        ui.print_disc(Vec4::new(1.0, 1.0, 0.0, 1.0));
        ui.print_lf();
    }
    separator(&mut ui);
    ui.set_scale(1.0);
    ui.print_text(&text!("Radio = "));
    ui.print_int(state.radio as i32);
    ui.print_lf();
    ui.print_lf();
    for i in 1..=2 {
        ui.set_scale(i as f32);
        ui.print_text(&text!("X"));
        ui.button_color = Vec3::X.extend(1.0);
        if ui.radio_button(state.radio == 1) && left {
            state.radio = 1;
        }
        ui.button_color = Vec3::Y.extend(1.0);
        if ui.radio_button(state.radio == 2) && left {
            state.radio = 2;
        }
        ui.button_color = Vec3::Z.extend(1.0);
        if ui.radio_button(state.radio == 3) && left {
            state.radio = 3;
        }
        ui.button_color = Vec3::splat(0.5).extend(1.0);
        ui.print_text(&text!(" Clear"));
        if ui.button(5) && left {
            state.radio = 0;
        }
        ui.print_text(&text!("X"));
        ui.print_lf();
    }
    separator(&mut ui);
    for i in 1..=3 {
        ui.set_scale(i as f32);
        ui.print_int(state.checkbox as i32);
        ui.print_text(&text!("=X"));
        ui.button_color = Vec3::splat(0.5).extend(1.0);
        if ui.check_box(state.checkbox != 0) && clicked {
            state.checkbox = (state.checkbox == 0) as u32;
        }
        ui.print_text(&text!("Check"));
        ui.print_lf();
    }
    separator(&mut ui);
    ui.print_text(&text!("Progress"));
    ui.print_lf();
    for i in 1..=3 {
        ui.set_scale(i as f32);
        let values = [(-10.0, 0.0), (0.2, 0.7), (1.0, 2.0)];
        let mut value_index = 0;
        while value_index < values.len() {
            let (a, b) = values[value_index];
            ui.progress(3, a);
            ui.print_text(&text!("X"));
            ui.progress(5, b);
            ui.print_lf();

            value_index += 1;
        }
    }
    separator(&mut ui);
    ui.print_text(&text!("Float"));
    ui.slider_float(8, &mut state.color.w, 0.0, 1.0);
    ui.print_lf();
    separator(&mut ui);
    for i in 1..=2 {
        ui.set_scale(i as f32);
        let mut rgb = state.color.truncate();
        ui.slider_rgb(10, &mut rgb);
        state.color = rgb.extend(state.color.w);
        ui.print_text(&text!("RGB"));
        ui.print_lf();
        ui.print_lf();
        ui.print_lf();
    }
    separator(&mut ui);
    ui.print_text(&text!("s2h_State "));
    ui.print_int(state.capture.x);
    ui.print_text(&text!(","));
    ui.print_int(state.capture.y);
    ui.print_text(&text!(","));
    ui.print_int(state.capture.z);
    ui.print_text(&text!(","));
    ui.print_int(state.capture.w);
    ui.print_lf();
    ui.print_lf();
    ui.set_cursor(Vec2::new(533.0, 5.0));
    ui.line_width = 0.0;
    ui.draw_circle(ui.cursor + Vec2::splat(10.0), 10.0, Vec3::X.extend(1.0));
    ui.line_width = 4.0;
    ui.draw_circle(ui.cursor + Vec2::new(30.0, 10.0), 10.0, Vec3::Y.extend(1.0));
    ui.line_width = 10.0;
    ui.draw_circle(ui.cursor + Vec2::new(50.0, 10.0), 10.0, Vec3::Z.extend(1.0));
    ui.print_lf();
    ui.print_lf();
    ui.print_lf();
    separator(&mut ui);
    ui.scale = 3.0;
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.print_space(1.0);
    ui.print_text(&text!("Frame"));
    ui.frame(5);
    ui.print_lf();
    ui.print_space(1.0);
    ui.text_color = Vec4::new(1.0, 1.0, 0.0, 1.0);
    ui.frame_fill_color = Vec3::X.extend(1.0);
    ui.frame_border_color = Vec4::new(0.5, 0.0, 0.0, 1.0);
    ui.print_text(&text!("Frame"));
    ui.frame(5);
    let background = Vec4::new(0.2, 0.5, 0.2, 1.0);
    let result = background * (1.0 - ui.color.w) + ui.color.truncate().extend(1.0) * ui.color.w;
    state.capture = ui.deinit();
    result
}
