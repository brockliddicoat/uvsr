//! Source docs_src/Gather_docs.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use shader_to_human::{text, Vec2, Vec3, Vec4};

pub fn draw(branch: u32, pixel: Vec2) -> Vec4 {
    let mut ui = super::begin(pixel);
    match branch {
        0 => ui.print_text(&text!("s2h_init")),
        1 => {
            ui.print_text(&text!("s2h_printTxt"));
            ui.print_lf();
            ui.text_color = Vec3::ZERO.extend(1.0);
            ui.print_lf();
            ui.set_scale(6.0);
            ui.text_color = Vec3::X.extend(1.0);
            ui.print_text(&text!("R"));
            ui.text_color = Vec3::Y.extend(1.0);
            ui.print_text(&text!("G"));
            ui.text_color = Vec3::Z.extend(1.0);
            ui.print_text(&text!("B "));
        }
        2 => {
            ui.print_text(&text!("s2h_printInt"));
            ui.print_lf();
            ui.print_lf();
            ui.text_color = Vec3::ZERO.extend(1.0);
            ui.print_int(12345);
            ui.print_lf();
            ui.print_int(-12345);
            ui.print_lf();
            ui.print_lf();
        }
        3 => {
            ui.print_text(&text!("s2h_printHex"));
            ui.print_lf();
            ui.print_lf();
            ui.text_color = Vec3::ZERO.extend(1.0);
            ui.print_hex(0x1297ab);
            ui.print_lf();
            ui.print_lf();
        }
        4 => {
            ui.print_text(&text!("s2h_printFloat"));
            ui.print_lf();
            ui.print_lf();
            ui.text_color = Vec3::ZERO.extend(1.0);
            ui.print_float(-12.34);
            ui.print_text(&text!(","));
            ui.print_float(0.34);
        }
        5 => {
            ui.print_text(&text!("s2h_printBox/Disc"));
            ui.print_lf();
            ui.print_lf();
            ui.set_scale(3.0);
            ui.print_box(Vec4::new(1.0, 0.7, 0.3, 1.0));
            ui.print_box(Vec3::X.extend(1.0));
            ui.print_disc(Vec3::Y.extend(1.0));
            ui.print_disc(Vec4::new(1.0, 1.0, 0.0, 1.0));
            ui.print_lf();
            ui.set_scale(1.0);
            ui.print_lf();
            ui.print_box(Vec4::new(1.0, 0.7, 0.3, 1.0));
            ui.print_box(Vec3::X.extend(1.0));
            ui.print_disc(Vec3::Y.extend(1.0));
            ui.print_disc(Vec4::new(1.0, 1.0, 0.0, 1.0));
        }
        6 => {
            ui.print_text(&text!("s2h_progress"));
            ui.print_lf();
            ui.print_lf();
            ui.text_color = Vec3::ZERO.extend(1.0);
            ui.progress(10, 0.25);
            ui.print_lf();
            ui.progress(5, 0.25);
            ui.print_lf();
            ui.set_scale(3.0);
            ui.text_color = Vec3::X.extend(1.0);
            ui.button_color = Vec3::Y.extend(1.0);
            ui.progress(15, 0.25);
            ui.print_text(&text!(" "));
        }
        _ => {}
    }
    super::composite(ui)
}
