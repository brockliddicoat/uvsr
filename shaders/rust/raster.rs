//! AGFX f91b108a raster, triangle, indexed, depth and blend test shaders.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
//! Ordinary descriptors replace the source resource handle for indexed vertices.
#![no_std]
#![forbid(unsafe_code)]
use spirv_std::{
    glam::{Vec2, Vec3, Vec4},
    spirv,
};

#[repr(C)]
pub struct RasterRoot {
    pub color: Vec4,
    pub discard_x: f32,
    pub use_vertex_color: u32,
    pub padding0: u32,
    pub padding1: u32,
}
#[repr(C)]
pub struct DepthRoot {
    pub depths: Vec4,
    pub tint: Vec4,
}
#[repr(C)]
pub struct BlendRoot {
    pub color: Vec4,
    pub column_scale: Vec4,
    pub column_alpha: Vec4,
}
#[repr(C)]
pub struct IndexedRoot {
    pub vertices: u32,
    pub padding0: u32,
    pub padding1: u32,
    pub padding2: u32,
}
#[repr(C)]
pub struct Vertex {
    pub position: Vec2,
    pub padding: Vec2,
    pub color: Vec4,
}
#[repr(C)]
pub struct PassRoot {
    pub color: Vec4,
    pub fullscreen: u32,
    pub depth: f32,
    pub padding0: u32,
    pub padding1: u32,
}

#[spirv(vertex)]
pub fn raster_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(point_size)] point_size: &mut f32,
    #[spirv(location = 0)] color: &mut Vec3,
    #[spirv(location = 1)] ndc: &mut Vec2,
) {
    let positions = [
        Vec2::new(-0.9, -0.8),
        Vec2::new(-0.1, -0.8),
        Vec2::new(-0.5, 0.8),
        Vec2::new(0.1, -0.8),
        Vec2::new(0.5, 0.8),
        Vec2::new(0.9, -0.8),
    ];
    let colors = [
        Vec3::X,
        Vec3::Y,
        Vec3::Z,
        Vec3::new(1.0, 1.0, 0.0),
        Vec3::new(0.0, 1.0, 1.0),
        Vec3::new(1.0, 0.0, 1.0),
    ];
    let xy = positions[vertex_id as usize];
    *position = Vec4::new(xy.x, xy.y, 0.0, 1.0);
    *color = colors[vertex_id as usize];
    *ndc = xy;
    // Explicit Vulkan point-size output preserves the source's one-pixel points.
    *point_size = 1.0;
}

#[spirv(fragment)]
pub fn raster_fs(
    #[spirv(push_constant)] root: &RasterRoot,
    #[spirv(location = 0)] color: Vec3,
    #[spirv(location = 1)] ndc: Vec2,
    #[spirv(location = 0)] output: &mut Vec4,
) {
    if ndc.x > root.discard_x {
        spirv_std::arch::kill();
    }
    let rgb = if root.use_vertex_color != 0 {
        color
    } else {
        root.color.truncate()
    };
    *output = rgb.extend(1.0);
}

#[spirv(vertex)]
pub fn triangle_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] color: &mut Vec3,
) {
    let positions = [
        Vec2::new(0.0, 0.8),
        Vec2::new(0.8, -0.8),
        Vec2::new(-0.8, -0.8),
    ];
    let xy = positions[vertex_id as usize];
    *position = Vec4::new(xy.x, xy.y, 0.0, 1.0);
    *color = [Vec3::X, Vec3::Y, Vec3::Z][vertex_id as usize];
}

fn column_corner(column: u32, corner: u32) -> Vec2 {
    let x0 = -0.85 + 0.6 * column as f32;
    let x1 = x0 + 0.5;
    [
        Vec2::new(x0, -0.5),
        Vec2::new(x1, -0.5),
        Vec2::new(x1, 0.5),
        Vec2::new(x0, 0.5),
    ][corner.min(3) as usize]
}
fn column_color(column: u32) -> Vec3 {
    [Vec3::X, Vec3::Y, Vec3::Z][column.min(2) as usize]
}
fn quad_corner(vertex: u32) -> u32 {
    [0, 1, 2, 0, 2, 3][(vertex % 6) as usize]
}
fn fullscreen_corner(vertex: u32) -> Vec2 {
    [
        Vec2::new(-1.0, -1.0),
        Vec2::new(1.0, -1.0),
        Vec2::new(1.0, 1.0),
        Vec2::new(-1.0, 1.0),
    ][quad_corner(vertex) as usize]
}

#[spirv(vertex)]
pub fn depth_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(push_constant)] root: &DepthRoot,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] color: &mut Vec3,
) {
    let column = (vertex_id / 6).min(2);
    let xy = column_corner(column, quad_corner(vertex_id));
    *position = Vec4::new(xy.x, xy.y, root.depths.to_array()[column as usize], 1.0);
    *color = column_color(column) * root.tint.truncate();
}

#[spirv(vertex)]
pub fn depth_fullscreen_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(push_constant)] root: &DepthRoot,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] color: &mut Vec3,
) {
    let xy = fullscreen_corner(vertex_id);
    *position = Vec4::new(xy.x, xy.y, root.depths.x, 1.0);
    *color = root.tint.truncate();
}

#[spirv(fragment)]
pub fn color3_fs(#[spirv(location = 0)] color: Vec3, #[spirv(location = 0)] output: &mut Vec4) {
    *output = color.extend(1.0);
}

#[spirv(vertex)]
pub fn blend_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(push_constant)] root: &BlendRoot,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] color: &mut Vec4,
) {
    let column = (vertex_id / 6).min(2);
    let xy = column_corner(column, quad_corner(vertex_id));
    *position = Vec4::new(xy.x, xy.y, 0.0, 1.0);
    *color = (column_color(column) * root.column_scale.to_array()[column as usize])
        .extend(root.column_alpha.to_array()[column as usize]);
}

#[spirv(vertex)]
pub fn blend_fullscreen_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(push_constant)] root: &BlendRoot,
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] color: &mut Vec4,
) {
    let xy = fullscreen_corner(vertex_id);
    *position = Vec4::new(xy.x, xy.y, 0.0, 1.0);
    *color = root.color;
}

#[spirv(vertex)]
pub fn indexed_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(push_constant)] _root: &IndexedRoot,
    #[spirv(storage_buffer, descriptor_set = 0, binding = 0)] vertices: &[Vertex],
    #[spirv(position)] position: &mut Vec4,
    #[spirv(location = 0)] color: &mut Vec4,
) {
    let vertex = &vertices[vertex_id as usize];
    *position = Vec4::new(vertex.position.x, vertex.position.y, 0.0, 1.0);
    *color = vertex.color;
}

#[spirv(fragment)]
pub fn color4_fs(#[spirv(location = 0)] color: Vec4, #[spirv(location = 0)] output: &mut Vec4) {
    *output = color;
}

#[spirv(vertex)]
pub fn pass_vs(
    #[spirv(vertex_index)] vertex_id: u32,
    #[spirv(push_constant)] root: &PassRoot,
    #[spirv(position)] position: &mut Vec4,
) {
    let fullscreen = [
        Vec2::new(-1.0, -1.0),
        Vec2::new(3.0, -1.0),
        Vec2::new(-1.0, 3.0),
    ];
    let centered = [
        Vec2::new(-0.6, -0.6),
        Vec2::new(0.6, -0.6),
        Vec2::new(0.0, 0.7),
    ];
    let xy = if root.fullscreen != 0 {
        fullscreen[vertex_id as usize]
    } else {
        centered[vertex_id as usize]
    };
    *position = Vec4::new(xy.x, xy.y, root.depth, 1.0);
}

#[spirv(fragment)]
pub fn pass_fs(#[spirv(push_constant)] root: &PassRoot, #[spirv(location = 0)] output: &mut Vec4) {
    *output = root.color;
}
