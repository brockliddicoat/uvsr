//! Source Features/QuadCommon, QuadVSPS and QuadPost. Electronic Arts, BSD-3-Clause.
use super::Inputs;
use shader_to_human::{
    linear_to_srgb, srgb_to_linear, text, Context3D, ContextGather, IVec2, Mat4, UVec2, Vec2, Vec3,
    Vec4,
};

fn positions(vertex: u32, inputs: &Inputs) -> (Vec2, Vec3, Vec4, Vec4) {
    let uv = match vertex % 6 {
        1 => Vec2::new(1.0, 0.0),
        2 | 3 => Vec2::ONE,
        4 => Vec2::new(0.0, 1.0),
        _ => Vec2::ZERO,
    };
    // Preserve the active source +1, including its disagreement with the comment.
    let object = (uv * 2.0 + Vec2::ONE).extend(0.0);
    let view = inputs.world_to_view * object.extend(1.0);
    let clip = inputs.world_to_clip * object.extend(1.0);
    (Vec2::new(uv.x, 1.0 - uv.y), object, view, clip)
}

pub fn quad_vertex(vertex: u32, inputs: &Inputs) -> (Vec4, Vec2) {
    let (uv, _, _, clip) = positions(vertex, inputs);
    (clip, uv)
}

/// Position uses the source's DirectX fragment z and clip w semantics.
pub fn quad_fragment(inputs: &Inputs, uv: Vec2, position: Vec4) -> Vec4 {
    let border = if uv.min(Vec2::ONE - uv).min_element() < 0.01 {
        0.4
    } else {
        0.0
    };
    let background = Vec4::new(border, 0.0, border, 1.0);
    let mut ui = ContextGather::new(uv * 128.0);
    ui.set_cursor(Vec2::splat(10.0));
    ui.print_text(&text!("CameraPos:"));
    ui.print_lf();
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(inputs.camera_near.x);
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(inputs.camera_near.y);
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(inputs.camera_near.z);
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("CameraAngles:"));
    ui.print_lf();
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(inputs.angles.x);
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(inputs.angles.y);
    ui.print_lf();
    ui.draw_rectangle(
        Vec2::new(10.0, 98.0),
        Vec2::new(59.0, 118.0),
        Vec3::splat(position.z).extend(1.0),
    );
    ui.cursor = Vec2::new(13.0, 101.0);
    ui.print_text(&text!("X"));
    ui.draw_rectangle(
        Vec2::new(69.0, 98.0),
        Vec2::new(118.0, 118.0),
        Vec3::splat(position.w - libm::floorf(position.w)).extend(1.0),
    );
    ui.cursor = Vec2::new(72.0, 101.0);
    ui.print_text(&text!("fracW"));
    background * (1.0 - ui.color.w) + ui.color.truncate().extend(1.0) * ui.color.w
}

fn lookup(inputs: &Inputs, column: u32, row: u32) -> Option<f32> {
    if row > 4 {
        return None;
    }
    let (_, object, view, clip) = positions(row, inputs);
    let ndc = clip.truncate() / clip.w;
    // Source pxPos deliberately omits the viewport's scale/offset and y flip.
    let pixel = ndc.truncate() * inputs.dimensions();
    Some(match column {
        0 => object.x,
        1 => object.y,
        2 => object.z,
        3 => view.x,
        4 => view.y,
        5 => view.z,
        6 => clip.x,
        7 => clip.y,
        8 => clip.z,
        9 => clip.w,
        10 => ndc.x,
        11 => ndc.y,
        12 => ndc.z,
        13 => pixel.x,
        14 => pixel.y,
        _ => return None,
    })
}

pub fn quad_post(inputs: &Inputs, pixel: UVec2, stored: Vec4) -> Vec4 {
    let mut ui = ContextGather::new(pixel.as_vec2());
    ui.set_cursor(Vec2::new(466.0, 10.0));
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    ui.set_scale(3.0);
    ui.print_text(&text!("QuadPost"));
    let screen = pixel.as_vec2() / inputs.dimensions() * 2.0 - Vec2::ONE;
    let hom = inputs.world_from_clip * Vec4::new(screen.x, -screen.y, inputs.camera_near.w, 1.0);
    let origin = inputs.camera_near.truncate();
    let mut context = Context3D::new(origin, (hom.truncate() / hom.w - origin).normalize());
    context.draw_basis(Mat4::IDENTITY, 1.0);
    let red = Vec4::new(1.0, 0.0, 0.0, 0.5);
    context.draw_sphere(Vec3::new(1.0, 1.0, 0.0), red, 0.125);
    context.draw_sphere(Vec3::new(3.0, 1.0, 0.0), red, 0.125);
    context.draw_sphere(Vec3::new(1.0, 3.0, 0.0), red, 0.125);
    context.draw_sphere(Vec3::new(3.0, 3.0, 0.0), red, 0.125);
    ui.print_lf();
    ui.set_scale(1.0);
    ui.print_lf();
    ui.print_text(&text!("zNear: "));
    ui.print_float(inputs.camera_near.w);
    ui.print_lf();
    ui.print_text(&text!("zFar: "));
    ui.print_float(inputs.dimensions_time_far.w);
    ui.print_lf();
    ui.print_lf();
    ui.print_int(inputs.dimensions_time_far.x as i32);
    ui.print_text(&text!("x"));
    ui.print_int(inputs.dimensions_time_far.y as i32);
    ui.print_lf();
    ui.print_lf();
    ui.frame_border_color = Vec4::new(0.3, 0.2, 0.1, 0.6);
    ui.frame_fill_color = ui.frame_border_color;
    for group in 0..5 {
        let (first, count, label_length) = match group {
            0 => {
                ui.print_text(&text!("osPos"));
                (0, 3, 5)
            }
            1 => {
                ui.print_text(&text!("vsPos"));
                (3, 3, 5)
            }
            2 => {
                ui.print_text(&text!("csPos hom"));
                (6, 4, 9)
            }
            3 => {
                ui.print_text(&text!("csPos=NDC"));
                (10, 3, 9)
            }
            _ => {
                ui.print_text(&text!("pxPos"));
                (13, 2, 5)
            }
        };
        ui.print_space((count * 9 - label_length) as f32);
        ui.frame(count * 9);
        ui.print_lf();
        for index in 0..count {
            let gray = if index % 2 == 0 { 0.12 } else { 0.3 };
            ui.table_float(
                first + index,
                Vec3::splat(gray).extend(0.6),
                IVec2::new(9, 4),
                index + 1 != count,
                |column, row| lookup(inputs, column, row),
            );
        }
        if group != 4 {
            ui.print_lf();
        }
    }
    let linear = srgb_to_linear(stored.truncate());
    let linear = linear * (1.0 - context.color.w) + context.color.truncate() * context.color.w;
    linear_to_srgb(linear * (1.0 - ui.color.w) + ui.color.truncate() * ui.color.w).extend(1.0)
}
