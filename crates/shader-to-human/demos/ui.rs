//! Source docs_src/UI_docs.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use super::{Inputs, State};
use shader_to_human::{text, Vec2, Vec3, Vec4};

pub fn draw(inputs: &Inputs, pixel: Vec2, state: &mut State) -> Vec4 {
    let mut ui = super::begin(pixel);
    ui.state = state.capture;
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    let left = inputs.mouse.z != 0.0;
    let clicked = left && inputs.previous_mouse.z == 0.0;
    ui.print_text(&text!("  "));
    match inputs.control.w {
        0 => {
            ui.print_int(state.radio as i32);
            ui.print_text(&text!(" = Clear"));
            if ui.button(5) && left {
                state.radio = 0;
            }
            ui.print_text(&text!(" s2h_button"));
            ui.print_lf();
            ui.print_lf();
        }
        1 => {
            ui.print_int(state.radio as i32);
            ui.print_text(&text!(" = "));
            let backup = ui.button_color;
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
            ui.button_color = backup;
        }
        2 => {
            ui.print_int(state.checkbox as i32);
            ui.print_text(&text!(" = "));
            if ui.check_box(state.checkbox != 0) && clicked {
                state.checkbox = 1_u32.wrapping_sub(state.checkbox);
            }
            ui.print_text(&text!(" s2h_checkBox"));
            ui.print_lf();
            ui.print_lf();
        }
        3 => {
            ui.slider_float(8, &mut state.color.w, 0.0, 1.0);
            ui.print_text(&text!(" s2h_sliderFloat"));
            ui.print_lf();
            ui.print_lf();
        }
        4 => {
            let mut color = state.color.truncate();
            ui.slider_rgb(8, &mut color);
            state.color = color.extend(state.color.w);
            ui.print_text(&text!(" s2h_sliderRGB"));
            ui.print_lf();
            ui.print_lf();
            ui.print_lf();
            ui.print_lf();
        }
        5 => {
            ui.slider_rgba(8, &mut state.color_rgba);
            ui.print_text(&text!(" s2h_sliderRGBA"));
            ui.print_lf();
            ui.print_lf();
            ui.print_lf();
            ui.print_lf();
            ui.print_lf();
        }
        _ => {}
    }
    // The HLSL source calls deinit only inside its GLSL branch. Its capture
    // field is therefore not updated here, including after slider interactions.
    super::composite(ui)
}
