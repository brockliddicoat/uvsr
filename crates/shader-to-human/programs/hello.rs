//! Four Hello example families from ShaderToHuman d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
//! The Slang quad source differs only in equivalent syntax and shares this body.
#![forbid(unsafe_code)]

use shader_to_human::{text, ContextGather, Mat4, Vec2, Vec3, Vec4};

/// Column-major world-to-clip matrix, camera position, framebuffer dimensions.
/// The source transposes Gigi's row-major matrix before matrix * vector.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Inputs {
    pub world_to_clip: Mat4,
    pub camera: Vec4,
    pub dimensions: Vec4,
}

pub fn vertex_uv(vertex: u32) -> Vec2 {
    match vertex % 6 {
        1 => Vec2::new(1.0, 0.0),
        2 | 3 => Vec2::ONE,
        4 => Vec2::new(0.0, 1.0),
        _ => Vec2::ZERO,
    }
}

pub fn screen_vertex(vertex: u32) -> Vec4 {
    let xy = vertex_uv(vertex) * 2.0 - Vec2::ONE;
    Vec4::new(xy.x, xy.y, 0.5, 1.0)
}

pub fn quad_vertex(vertex: u32, inputs: &Inputs) -> (Vec4, Vec2) {
    let uv = vertex_uv(vertex);
    // The source's active +1 expression deliberately differs from its comment.
    let xy = uv * 2.0 + Vec2::ONE;
    let clip = inputs.world_to_clip * Vec4::new(xy.x, xy.y, 0.0, 1.0);
    (clip, Vec2::new(uv.x, 1.0 - uv.y))
}

pub fn screen(pixel: Vec2, dimensions: Vec2, compute: bool) -> Vec4 {
    let mut ui = ContextGather::new(pixel);
    ui.set_cursor(Vec2::splat(10.0));
    ui.set_scale(3.0);
    ui.print_text(&text!("Hello"));
    ui.print_lf();
    if compute {
        ui.print_text(&text!("Compute"));
    } else {
        ui.print_text(&text!("Screen"));
        ui.draw_srgb_ramp(Vec2::new(10.0, 100.0));
    }
    let uv = pixel / dimensions;
    let background = Vec4::new(uv.x, uv.y, 0.0, 1.0);
    background * (1.0 - ui.color.w) + ui.color.truncate().extend(1.0) * ui.color.w
}

/// `position` is DirectX fragment SV_Position: window z and clip w.
pub fn quad(uv: Vec2, position: Vec4, camera: Vec3) -> Vec4 {
    let edge = uv.min(Vec2::ONE - uv);
    let border = if edge.min_element() < 0.01 { 0.4 } else { 0.0 };
    let background = Vec4::new(border, 0.0, border, 1.0);
    let mut ui = ContextGather::new(uv * 256.0);
    ui.set_cursor(Vec2::splat(10.0));
    ui.set_scale(2.0);
    ui.print_text(&text!("HelloQuad"));
    ui.print_lf();
    ui.print_lf();
    ui.text_color = Vec4::new(0.2, 0.2, 0.5, 1.0);
    ui.print_text(&text!("CameraPos:"));
    ui.print_lf();
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(camera.x);
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(camera.y);
    ui.print_lf();
    ui.print_space(2.0);
    ui.print_float(camera.z);
    ui.print_lf();
    ui.print_lf();
    ui.draw_srgb_ramp(Vec2::new(0.0, 222.0));
    ui.draw_rectangle(
        Vec2::new(10.0, 88.0) * 2.0,
        Vec2::new(59.0, 108.0) * 2.0,
        Vec3::splat(position.z).extend(1.0),
    );
    ui.cursor = Vec2::new(10.0, 88.0) * 2.0 + Vec2::splat(6.0);
    ui.print_text(&text!("X"));
    ui.draw_rectangle(
        Vec2::new(69.0, 88.0) * 2.0,
        Vec2::new(118.0, 108.0) * 2.0,
        Vec3::splat(position.w - libm::floorf(position.w)).extend(1.0),
    );
    ui.cursor = Vec2::new(69.0, 88.0) * 2.0 + Vec2::splat(6.0);
    ui.print_text(&text!("fracW"));
    background * (1.0 - ui.color.w) + ui.color.truncate().extend(1.0) * ui.color.w
}
