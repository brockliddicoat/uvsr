//! Source Features/3D_example and Clear_example, Electronic Arts, BSD-3-Clause.
use super::Inputs;
use shader_to_human::{
    linear_to_srgb, text, Context3D, ContextGather, Mat4, UVec2, Vec2, Vec3, Vec4,
};

fn direction(inputs: &Inputs, uv: Vec2) -> Vec3 {
    let screen = uv * 2.0 - Vec2::ONE;
    let hom = inputs.world_from_clip * Vec4::new(screen.x, -screen.y, inputs.camera_near.w, 1.0);
    // Source 3D_example reads context.ro before its first initialization.
    // Use the declared camera origin explicitly instead of reproducing undef.
    (hom.truncate() / hom.w - inputs.camera_near.truncate()).normalize()
}

pub fn clear(inputs: &Inputs, pixel: UVec2) -> Vec4 {
    let ray = direction(inputs, pixel.as_vec2() / inputs.dimensions());
    let mut context = Context3D::new(inputs.camera_near.truncate(), ray);
    context.draw_skybox();
    context.color.truncate().extend(1.0)
}

fn scene(context: &mut Context3D) {
    let offset = Vec3::new(0.0, -1.0, 0.0);
    context.draw_checker_board(offset);
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
}

pub fn draw(inputs: &Inputs, pixel: UVec2) -> Vec4 {
    let origin = inputs.camera_near.truncate();
    let mut ui = ContextGather::new(pixel.as_vec2());
    ui.set_cursor(Vec2::splat(10.0));
    ui.set_scale(2.0);
    ui.print_text(&text!("Pos:"));
    ui.print_float(origin.x);
    ui.print_text(&text!(","));
    ui.print_float(origin.y);
    ui.print_text(&text!(","));
    ui.print_float(origin.z);
    ui.print_lf();
    let mut total = Vec4::ZERO;
    for m in 0..3 {
        for n in 0..3 {
            let sub = Vec2::new(m as f32, n as f32) / 3.0 - Vec2::splat(0.5);
            let uv = (pixel.as_vec2() + Vec2::splat(0.5) + sub) / inputs.dimensions();
            let ray = direction(inputs, uv);
            let mut context = Context3D::new(origin, ray);
            let sky = (ray * 0.5 + Vec3::splat(0.5)).normalize() * 0.5;
            context.color = (sky * sky).extend(1.0);
            context.scene_with_shadows(scene);
            let eye = Vec3::new(
                libm::sinf(inputs.time()) * 3.0,
                1.0,
                libm::cosf(inputs.time()) * 3.0,
            );
            let z = (Vec3::Y - eye).normalize();
            let x = Vec3::Y.cross(z).normalize();
            let y = z.cross(x);
            let matrix =
                Mat4::from_cols(x.extend(0.0), y.extend(0.0), z.extend(0.0), eye.extend(1.0));
            context.draw_basis(matrix, 1.0);
            total += context.color;
        }
    }
    total /= 9.0;
    let color = total.truncate() * total.w;
    linear_to_srgb(color * (1.0 - ui.color.w) + ui.color.truncate()).extend(1.0)
}
