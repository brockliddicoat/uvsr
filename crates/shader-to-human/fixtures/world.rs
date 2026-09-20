//! Source: unittests/3D_test.hlsl, BSD-3-Clause, Electronic Arts 2024-2025.
use super::{HEIGHT, WIDTH};
use shader_to_human::{Context3D, Mat4, UVec2, Vec2, Vec3, Vec4};

/// Explicit Gigi host inputs. A guessed matrix is not a source golden setup.
#[derive(Clone, Copy)]
pub struct Camera {
    pub origin: Vec3,
    pub inverse_view_projection: Mat4,
    pub depth_near: f32,
}

fn scene(context: &mut Context3D) {
    let offset = Vec3::new(0.0, -1.0, 0.0);
    context.draw_checker_board(offset);
    context.draw_sphere(Vec3::X + offset, Vec3::X.extend(1.0), 0.1);
    context.draw_sphere(Vec3::Y + offset, Vec3::Y.extend(1.0), 0.1);
    context.draw_sphere(Vec3::Z + offset, Vec3::Z.extend(1.0), 0.1);
    context.draw_arrow(offset, Vec3::X + offset, Vec3::X.extend(1.0), 0.09);
    context.draw_arrow(offset, Vec3::Y + offset, Vec3::Y.extend(1.0), 0.09);
    context.draw_arrow(offset, Vec3::Z + offset, Vec3::Z.extend(1.0), 0.09);
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

fn look_at(eye: Vec3, target: Vec3, up: Vec3) -> Mat4 {
    let z = (target - eye).normalize();
    let x = up.cross(z).normalize();
    let y = z.cross(x);
    Mat4::from_cols(x.extend(0.0), y.extend(0.0), z.extend(0.0), eye.extend(1.0))
}

pub fn world(pixel: UVec2, camera: Camera, previous: Vec4) -> Vec4 {
    let dimensions = Vec2::new(WIDTH as f32, HEIGHT as f32);
    let mut total = Vec4::ZERO;
    for m in 0..3 {
        for n in 0..3 {
            let subpixel = Vec2::new(m as f32, n as f32) / 3.0 - Vec2::splat(0.5);
            let uv = (pixel.as_vec2() + Vec2::splat(0.5) + subpixel) / dimensions;
            let mut screen = uv * 2.0 - Vec2::ONE;
            screen.y = -screen.y;
            let homogeneous = camera.inverse_view_projection
                * Vec4::new(screen.x, screen.y, camera.depth_near, 1.0);
            let position = homogeneous.truncate() / homogeneous.w;
            let mut context = Context3D::new(camera.origin, (position - camera.origin).normalize());
            context.color =
                ((context.direction * 0.5 + Vec3::splat(0.5)).normalize() * 0.5).extend(1.0);
            context.scene_with_shadows(scene);
            let eye = Vec3::new(libm::sinf(124.4) * 2.0, 1.0, libm::cosf(124.4) * 2.0);
            context.draw_basis(look_at(eye, Vec3::Y, Vec3::Y), 1.0);
            total += context.color;
        }
    }
    total /= 9.0;
    previous * (1.0 - total.w) + total.truncate().extend(1.0) * total.w
}
