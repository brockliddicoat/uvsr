//! Source: unittests/scatter_test.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use shader_to_human::{text, ContextScatter, IVec2, Vec2, Vec3, Vec4};

fn disc_values(ui: &mut ContextScatter, color: Vec4, output: &mut impl FnMut(IVec2, Vec4)) {
    ui.print_disc(color, output);
    ui.print_text(&text!(" "), output);
    // The source prints the red channel three times, including for green.
    let value = (color.x * 255.9) as i32;
    ui.print_int(value, output);
    ui.print_text(&text!(","), output);
    ui.print_int(value, output);
    ui.print_text(&text!(","), output);
    ui.print_int(value, output);
}

pub fn scatter(output: &mut impl FnMut(IVec2, Vec4)) {
    let mut ui = ContextScatter::default();
    ui.set_cursor(Vec2::new(522.0, 10.0));
    ui.set_scale(3);
    ui.print_text(&text!("S2H_Scatter"), output);
    ui.print_lf();
    ui.print_lf();
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.set_scale(2);
    ui.print_text(&text!("Single Thread"), output);
    ui.set_scale(3);
    ui.print_lf();
    ui.set_scale(2);
    ui.text_color = Vec3::X.extend(1.0);
    ui.print_text(&text!("R"), output);
    ui.text_color = Vec3::Y.extend(1.0);
    ui.print_text(&text!("G"), output);
    ui.text_color = Vec3::Z.extend(1.0);
    ui.print_text(&text!("B "), output);
    ui.text_color = Vec3::ZERO.extend(1.0);
    ui.print_text(&text!("XYZ:"), output);
    ui.print_lf();
    ui.print_int(12345, output);
    ui.print_lf();
    ui.print_int(-12345, output);
    ui.print_lf();
    ui.print_hex(0x1297ab, output);
    ui.print_lf();
    ui.print_lf();
    ui.print_float(-12.34, output);
    ui.print_text(&text!(","), output);
    ui.print_float(0.34, output);
    ui.print_lf();
    ui.print_block(Vec4::new(1.0, 0.7, 0.3, 1.0), output);
    ui.print_block(Vec3::X.extend(1.0), output);
    ui.print_disc(Vec3::Y.extend(1.0), output);
    ui.print_disc(Vec4::new(1.0, 1.0, 0.0, 1.0), output);
    ui.left_x += 4;
    ui.set_scale(2);
    for _ in 0..4 {
        ui.print_lf();
    }
    let a = Vec3::X.extend(1.0);
    let b = Vec3::Y.extend(1.0);
    disc_values(&mut ui, a, output);
    ui.print_text(&text!("=A"), output);
    ui.print_lf();
    disc_values(&mut ui, b, output);
    ui.print_text(&text!("=B"), output);
    ui.print_lf();
    disc_values(&mut ui, a + b, output);
    ui.print_text(&text!("=A+B"), output);
    ui.print_lf();
    disc_values(&mut ui, a * b, output);
    ui.print_text(&text!("=A*B"), output);
    ui.print_lf();
}
