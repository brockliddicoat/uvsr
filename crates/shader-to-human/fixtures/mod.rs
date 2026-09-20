//! Translated from ShaderToHuman/unittests at d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
//! Shared CPU/RustGPU fixture bodies. Native resources belong to their hosts.
#![forbid(unsafe_code)]

mod gather;
mod scatter;
mod table;
mod two_d;
mod world;

pub use gather::gather;
pub use scatter::scatter;
pub use table::table;
pub use two_d::two_d;
pub use world::{world, Camera};

use shader_to_human::{IVec4, Vec4};

pub const WIDTH: u32 = 800;
pub const HEIGHT: u32 = 600;

/// Persistent buffer from s2h_unittests.gg. Gigi's non-imported buffer path
/// allocates zeroed storage without applying the structure's field defaults.
#[derive(Clone, Copy)]
pub struct UiState {
    pub radio: u32,
    pub checkbox: u32,
    pub color: Vec4,
    pub capture: IVec4,
}

impl Default for UiState {
    fn default() -> Self {
        Self {
            radio: 0,
            checkbox: 0,
            color: Vec4::ZERO,
            capture: IVec4::ZERO,
        }
    }
}
