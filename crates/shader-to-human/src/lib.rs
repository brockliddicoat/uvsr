//! Rust translation of ShaderToHuman version 14, revision d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! Distributed under legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
//! Source mappings and evidence are in tests/parity/shader-to-human.md.
//!
//! All library operations are safe, allocation-free math. Native image access
//! belongs to the caller, which supplies the scatter output and custom fonts.
#![no_std]
#![forbid(unsafe_code)]

mod font;
mod gather;
mod math;
mod scatter;
mod widgets;
mod world;

pub use font::{Font, MiniFont};
pub use gather::{distance_to_box, half_space_plane, ContextGather, Triangle};
pub use glam::{IVec2, IVec4, Mat4, UVec2, UVec3, UVec4, Vec2, Vec3, Vec4};
pub use math::{color_ramp_rgb, index_to_color, linear_to_srgb, srgb_to_linear};
pub use scatter::ContextScatter;
pub use widgets::distance_to_aabb;
pub use world::{
    box_intersection, cone_intersection, cylinder_intersection, cylinder_normal,
    sphere_intersection, Context3D,
};

pub const VERSION: u32 = 14;

/// Expand a string literal into fixed u32 character codes at compile time.
/// This avoids byte capabilities and unsized slice operations in RustGPU.
/// Non-ASCII bytes remain unsupported font codes, as in the source font.
#[macro_export]
macro_rules! text {
    ($literal:literal) => {{
        const CODES: [u32; $literal.len()] = {
            let bytes = $literal.as_bytes();
            let mut codes = [0; $literal.len()];
            let mut index = 0;
            while index < codes.len() {
                codes[index] = bytes[index] as u32;
                index += 1;
            }
            codes
        };
        CODES
    }};
}
