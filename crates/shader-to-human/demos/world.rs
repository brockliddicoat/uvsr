//! Source docs_src/3D_docs.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use super::Inputs;
use shader_to_human::{text, Context3D, ContextGather, Mat4, Vec2, Vec3, Vec4};

fn scene(context: &mut Context3D, branch: u32) {
    let offset = Vec3::new(0.0, -1.0, 0.0);
    context.draw_checker_board(offset);
    match branch {
        1 => {
            context.draw_sphere(Vec3::new(1.0, 2.0, 0.0) + offset, Vec4::ONE, 2.0);
            context.draw_sphere(
                Vec3::new(-2.0, 1.0, 0.0) + offset,
                Vec4::new(1.0, 0.1, 0.1, 1.0),
                1.0,
            );
        }
        2 => {
            let yellow = Vec4::new(1.0, 1.0, 0.0, 1.0);
            context.draw_line(
                Vec3::new(-1.0, 2.0, 1.0) + offset,
                Vec3::new(1.0, 2.0, 1.0) + offset,
                yellow,
                0.09,
            );
            context.draw_line(
                Vec3::new(-1.0, 2.0, -1.0) + offset,
                Vec3::new(1.0, 2.0, -1.0) + offset,
                yellow,
                0.09,
            );
            context.draw_line(
                Vec3::new(-1.0, 2.0, -1.0) + offset,
                Vec3::new(-1.0, 2.0, 1.0) + offset,
                yellow,
                0.09,
            );
            context.draw_line(
                Vec3::new(1.0, 2.0, -1.0) + offset,
                Vec3::new(1.0, 2.0, 1.0) + offset,
                yellow,
                0.09,
            );
            context.draw_line(
                Vec3::new(1.0, 0.0, -1.0) + offset,
                Vec3::new(1.0, 4.0, -1.0) + offset,
                Vec4::ONE,
                0.5,
            );
        }
        3 => {
            context.draw_arrow(offset, Vec3::new(0.0, 5.0, 0.0) + offset, Vec4::ONE, 1.0);
            context.draw_arrow(
                Vec3::new(2.0, 0.0, 0.0) + offset,
                Vec3::new(2.0, 3.0, 0.0) + offset,
                Vec4::new(1.0, 1.0, 0.0, 1.0),
                1.0,
            );
            context.draw_arrow(
                Vec3::new(0.0, 0.0, 2.0) + offset,
                Vec3::new(0.0, 3.0, 2.0) + offset,
                Vec4::new(0.0, 1.0, 1.0, 1.0),
                1.0,
            );
        }
        5 => {
            context.draw_aabb(
                Vec3::new(1.0, 2.0, 0.0) + offset,
                Vec3::splat(2.0),
                Vec4::ONE,
            );
            context.draw_aabb(
                Vec3::new(-2.0, 1.0, 2.0) + offset,
                Vec3::new(0.5, 1.0, 0.25),
                Vec4::new(1.0, 0.1, 0.1, 1.0),
            );
        }
        _ => {}
    }
}

fn look_at(eye: Vec3, target: Vec3, up: Vec3) -> Mat4 {
    let z = (target - eye).normalize();
    let x = up.cross(z).normalize();
    let y = z.cross(x);
    Mat4::from_cols(x.extend(0.0), y.extend(0.0), z.extend(0.0), eye.extend(1.0))
}

pub fn draw(inputs: &Inputs, pixel: Vec2) -> Vec4 {
    let origin = inputs.origin_and_depth.truncate();
    let mut ui = ContextGather::new(pixel);
    ui.set_cursor(Vec2::splat(10.0));
    ui.print_text(&text!("Pos:"));
    ui.print_float(origin.x);
    ui.print_text(&text!(","));
    ui.print_float(origin.y);
    ui.print_text(&text!(","));
    ui.print_float(origin.z);
    ui.print_lf();
    ui.print_lf();
    ui.set_scale(3.0);
    ui.print_text(&text!(" W"));
    ui.print_lf();
    ui.print_text(&text!("ASD"));
    let dimensions = inputs.control.truncate().truncate().as_vec2();
    let mut total = Vec4::ZERO;
    for m in 0..3 {
        for n in 0..3 {
            let sub_pixel = Vec2::new(m as f32, n as f32) / 3.0 - Vec2::splat(0.5);
            let uv = (pixel + sub_pixel) / dimensions;
            let mut screen = uv * 2.0 - Vec2::ONE;
            screen.y = -screen.y;
            let hom = inputs.world_from_clip
                * Vec4::new(screen.x, screen.y, inputs.origin_and_depth.w, 1.0);
            let direction = (hom.truncate() / hom.w - origin).normalize();
            let mut context = Context3D::new(origin, direction);
            context.color = ((direction * 0.5 + Vec3::splat(0.5)).normalize() * 0.5).extend(1.0);
            context.scene_with_shadows(|context| scene(context, inputs.control.w));
            if inputs.control.w == 4 {
                for index in 0..5 {
                    let angle = index as f32 / 5.0 * core::f32::consts::PI * 2.0;
                    let eye = Vec3::new(libm::sinf(angle) * 3.0, 3.0, libm::cosf(angle) * 3.0);
                    context.draw_basis(look_at(eye, Vec3::Y, Vec3::Y), 1.0);
                }
                context.draw_sphere(Vec3::Y, Vec4::new(1.0, 1.0, 0.0, 1.0), 0.25);
            }
            total += context.color;
        }
    }
    total /= 9.0;
    let color = total.truncate().extend(1.0) * total.w;
    color * (1.0 - ui.color.w) + ui.color
}
