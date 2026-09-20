//! Source Features/Gather_example.hlsl, Electronic Arts 2024-2025, BSD-3-Clause.
use super::{Inputs, State};
use shader_to_human::{text, UVec2, Vec2, Vec3, Vec4};

pub fn draw(inputs: &Inputs, pixel: UVec2, state: &mut State) -> Vec4 {
    let mut ui = inputs.ui(pixel.as_vec2() + Vec2::splat(0.5), *state);
    let left = inputs.mouse.z != 0.0;
    let clicked = left && inputs.previous_mouse.z == 0.0;
    ui.set_scale(3.0);
    ui.print_text(&text!("GatherTest"));
    ui.print_lf();
    ui.print_lf();
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.set_scale(2.0);
    ui.print_text(&text!("Pixel=Thread"));
    ui.set_scale(3.0);
    ui.print_lf();
    ui.set_scale(1.0);
    ui.text_color = Vec3::X.extend(1.0);
    ui.print_text(&text!("R"));
    ui.text_color = Vec3::Y.extend(1.0);
    ui.print_text(&text!("G"));
    ui.text_color = Vec3::Z.extend(1.0);
    ui.print_text(&text!("B "));
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.print_text(&text!("XYZ:"));
    ui.print_lf();
    ui.print_int(12345);
    ui.print_lf();
    ui.print_int(-12345);
    ui.print_lf();
    ui.print_hex(0x1297ab);
    ui.print_lf();
    ui.print_lf();
    ui.set_scale(2.0);
    ui.print_float(-12.34);
    ui.print_text(&text!(","));
    ui.print_float(0.34);
    ui.print_lf();
    ui.print_box(Vec4::new(1.0, 0.7, 0.3, 1.0));
    ui.print_box(Vec3::X.extend(1.0));
    ui.print_disc(Vec3::Y.extend(1.0));
    ui.print_disc(Vec4::new(1.0, 1.0, 0.0, 1.0));
    for _ in 0..3 {
        ui.print_lf();
    }
    ui.print_text(&text!("UIState: <-- Touch Me"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("  "));
    ui.print_int(state.radio as i32);
    ui.print_text(&text!(" = "));
    let backup = ui.button_color;
    for choice in 1..=3 {
        ui.button_color = match choice {
            1 => Vec3::X,
            2 => Vec3::Y,
            _ => Vec3::Z,
        }
        .extend(1.0);
        if ui.radio_button(state.radio == choice) && left {
            state.radio = choice;
        }
    }
    ui.button_color = backup;
    ui.print_text(&text!(" s2h_radioButton"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("  "));
    ui.print_int(state.radio as i32);
    ui.print_text(&text!(" = Clear"));
    if ui.button(5) && left {
        state.radio = 0;
    }
    ui.print_text(&text!(" s2h_button"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("  "));
    ui.print_int(state.checkbox as i32);
    ui.print_text(&text!(" = "));
    if ui.check_box(state.checkbox != 0) && clicked {
        state.checkbox = 1_u32.wrapping_sub(state.checkbox);
    }
    ui.print_text(&text!(" s2h_checkBox"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("  "));
    ui.progress(5, inputs.time() - libm::floorf(inputs.time()));
    ui.print_text(&text!(" s2h_progress"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("  "));
    ui.slider_float(8, &mut state.color0.w, 0.0, 1.0);
    ui.print_text(&text!(" s2h_sliderFloat"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("  "));
    let mut rgb = state.color0.truncate();
    ui.slider_rgb(8, &mut rgb);
    state.color0 = rgb.extend(state.color0.w);
    ui.print_text(&text!(" s2h_sliderRGB"));
    for _ in 0..4 {
        ui.print_lf();
    }
    // Preserve the source's lerp of its premultiplied gather RGB.
    let color = Vec4::new(0.4, 0.7, 0.4, 1.0) * (1.0 - ui.color.w)
        + ui.color.truncate().extend(1.0) * ui.color.w;
    state.capture = ui.deinit();
    color
}
