//! Source docs_src/2D_docs.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use super::Inputs;
use shader_to_human::{index_to_color, linear_to_srgb, text, ContextGather, Vec2, Vec3, Vec4};

pub fn draw(inputs: &Inputs, pixel: Vec2) -> Vec4 {
    let mut ui = ContextGather::new(pixel);
    ui.mouse_input = inputs.mouse;
    match inputs.control.w {
        0 => {
            ui.set_cursor(Vec2::splat(10.0));
            ui.set_scale(2.0);
            ui.print_text(&text!("init"));
        }
        1 | 2 => {
            ui.line_width = 1.0;
            let centers = [
                Vec2::new(100.0, 50.0),
                Vec2::new(200.0, 50.0),
                Vec2::new(150.0, 50.0),
            ];
            let radii = [40.0, 20.0, 30.0];
            let widths = [1.0, 5.0, 8.0];
            let colors = [
                Vec3::X.extend(1.0),
                Vec3::Y.extend(1.0),
                Vec3::ZERO.extend(0.5),
            ];
            for index in 0..3 {
                if inputs.control.w == 1 {
                    ui.draw_disc(centers[index], radii[index], colors[index]);
                } else {
                    ui.line_width = widths[index];
                    ui.draw_circle(centers[index], radii[index], colors[index]);
                }
            }
        }
        3 => {
            ui.line_width = 2.0;
            let mouse = ui.mouse_input.truncate().truncate() + Vec2::splat(0.5);
            ui.draw_crosshair(mouse, 10.0, Vec4::ONE);
            let mut inside = true;
            let mut inside_aa = 1.0;
            for index in 0..3 {
                let angle = index as f32 * core::f32::consts::PI * 2.0 / 3.0 + 0.2;
                let mut half_space = Vec3::new(libm::sinf(angle), libm::cosf(angle), -20.0);
                half_space.z -= half_space.dot(Vec3::new(150.0, 50.0, 0.0));
                ui.draw_half_space(
                    half_space,
                    mouse,
                    index_to_color(index + 1).extend(1.0),
                    10.0,
                    20.0,
                );
                if half_space.dot(pixel.extend(1.0)) > 0.0 {
                    inside = false;
                }
                inside_aa *= (0.5 - half_space.dot((pixel - Vec2::new(200.0, 0.0)).extend(1.0)))
                    .clamp(0.0, 1.0);
            }
            if inside {
                ui.color = Vec4::ONE;
            }
            ui.color = ui.color * (1.0 - inside_aa) + Vec4::ONE * inside_aa;
            ui.set_scale(2.0);
            ui.set_cursor(Vec2::new(166.0, 10.0));
            ui.print_text(&text!("noAA"));
            ui.set_cursor(Vec2::new(366.0, 10.0));
            ui.print_text(&text!("AA"));
        }
        4 => {
            ui.draw_rectangle(
                Vec2::new(100.0, 10.0),
                Vec2::new(300.0, 90.0),
                Vec3::X.extend(1.0),
            );
            ui.draw_rectangle(
                Vec2::new(200.0, 50.0),
                Vec2::new(400.0, 65.0),
                Vec3::Y.extend(1.0),
            );
            ui.draw_rectangle(
                Vec2::new(150.0, 25.0),
                Vec2::new(350.0, 75.0),
                Vec3::ZERO.extend(0.5),
            );
        }
        5 => {
            ui.draw_rectangle_aa(
                Vec2::new(100.0, 10.0),
                Vec2::new(300.0, 90.0),
                Vec3::X.extend(1.0),
                Vec4::new(1.0, 1.0, 0.0, 1.0),
                5.0,
            );
            ui.draw_rectangle_aa(
                Vec2::new(200.0, 50.0),
                Vec2::new(400.0, 65.0),
                Vec3::Z.extend(1.0),
                Vec4::new(0.0, 1.0, 1.0, 1.0),
                3.0,
            );
            ui.draw_rectangle_aa(
                Vec2::new(150.0, 25.0),
                Vec2::new(350.0, 75.0),
                Vec4::ONE,
                Vec3::ZERO.extend(0.5),
                2.0,
            );
        }
        6 => {
            ui.line_width = 1.0;
            ui.draw_crosshair(Vec2::new(50.5, 50.5), 10.0, Vec3::Z.extend(1.0));
            ui.line_width = 3.0;
            ui.draw_crosshair(Vec2::new(200.5, 50.5), 20.0, Vec3::ZERO.extend(1.0));
            ui.line_width = 1.0;
            ui.draw_crosshair(Vec2::new(200.5, 50.5), 20.0, Vec4::ONE);
            ui.line_width = 4.0;
            ui.draw_crosshair(Vec2::new(360.0, 50.0), 30.0, Vec3::ZERO.extend(1.0));
            ui.line_width = 2.0;
            ui.draw_crosshair(Vec2::new(360.0, 50.0), 30.0, Vec4::ONE);
        }
        7 => {
            for index in 0..5 {
                let angle = index as f32 * 1.1 + 1.0;
                let center = Vec2::splat(50.0) + Vec2::new((90 * index) as f32, 0.0);
                let direction = Vec2::new(libm::sinf(angle), libm::cosf(angle)) * 20.0;
                ui.line_width = 1.0 + index as f32 * 4.0;
                ui.draw_line(
                    center + direction,
                    center - direction,
                    index_to_color(index).extend(1.0),
                );
            }
        }
        8 => {
            let middle = inputs.control.x as f32 * 0.5;
            let value = (pixel.x - middle) / 256.0 + 0.5;
            ui.color = Vec3::splat(value).extend(1.0);
            ui.draw_srgb_ramp(Vec2::new(middle - 128.0, 59.0));
        }
        // Source branches 9 (arrow) and 10 (triangle) are empty TODOs.
        _ => {}
    }
    let linear = Vec3::new(0.7, 0.4, 0.4) * (1.0 - ui.color.w) + ui.color.truncate();
    linear_to_srgb(linear).extend(1.0)
}
