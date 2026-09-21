//! GaussianSplatting at ShaderToHuman d6f98b7d. Electronic Arts 2024-2025.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
#![forbid(unsafe_code)]
pub mod math;
pub mod ply;
mod programs;
pub use programs::{
    base_image, clear_fragment, compute_image, fullscreen_vertex, resolve, splat_fragment,
    splat_vertex,
};
use shader_to_human::{Mat4, UVec4, Vec2, Vec3, Vec4};

pub const PLY_WORDS: usize = 12782;
pub const PLY_OFFSET: usize = 96;
// Match the source-control structured buffer's16-byte struct alignment. These
// two tail words are padding, outside the original PLY payload/parser bounds.
pub const WORDS: usize = PLY_OFFSET + PLY_WORDS + 2;

#[derive(Clone, Copy)]
pub struct Inputs {
    pub camera: math::Camera,
    pub world_from_clip: Mat4,
    pub camera_near: Vec4,
    pub dimensions_time_far: Vec4,
    pub mouse: Vec4,
    pub offset: Vec4,
    pub ray_bounds: Vec4,
    /// Frame index, source frameRandom, two reserved words.
    pub random: UVec4,
    pub tweak: Vec4,
}
impl Inputs {
    pub fn random_frame(self) -> u32 {
        self.random.x.wrapping_mul(self.random.y)
    }
    pub fn read(words: &[u32; WORDS]) -> Self {
        let vector = |offset: usize| {
            Vec4::new(
                f32::from_bits(words[offset]),
                f32::from_bits(words[offset + 1]),
                f32::from_bits(words[offset + 2]),
                f32::from_bits(words[offset + 3]),
            )
        };
        let matrix = |offset| {
            Mat4::from_cols(
                vector(offset),
                vector(offset + 4),
                vector(offset + 8),
                vector(offset + 12),
            )
        };
        let dimensions = vector(72);
        Self {
            camera: math::Camera {
                world_to_clip: matrix(4),
                view_to_clip: matrix(20),
                world_to_view: matrix(36),
                dimensions: Vec2::new(dimensions.x, dimensions.y),
            },
            world_from_clip: matrix(52),
            camera_near: vector(68),
            dimensions_time_far: dimensions,
            mouse: vector(76),
            offset: vector(80),
            ray_bounds: vector(84),
            random: UVec4::new(words[88], words[89], words[90], words[91]),
            tweak: vector(92),
        }
    }
    pub fn write(self, words: &mut [u32; WORDS]) {
        for (offset, matrix) in [
            (4, self.camera.world_to_clip),
            (20, self.camera.view_to_clip),
            (36, self.camera.world_to_view),
            (52, self.world_from_clip),
        ] {
            for (index, value) in matrix.to_cols_array().iter().enumerate() {
                words[offset + index] = value.to_bits();
            }
        }
        for (offset, vector) in [
            (68, self.camera_near),
            (72, self.dimensions_time_far),
            (76, self.mouse),
            (80, self.offset),
            (84, self.ray_bounds),
            (92, self.tweak),
        ] {
            for (index, value) in vector.to_array().iter().enumerate() {
                words[offset + index] = value.to_bits();
            }
        }
        words[88..92].copy_from_slice(&self.random.to_array());
    }
    pub fn ray(self, pixel: Vec2) -> Vec3 {
        let ndc = (pixel / self.camera.dimensions * 2.0 - Vec2::ONE) * Vec2::new(1.0, -1.0);
        let hom = self.world_from_clip * Vec4::new(ndc.x, ndc.y, self.camera_near.w, 1.0);
        (hom.truncate() / hom.w - self.camera_near.truncate()).normalize()
    }
}
