//! Documentation demos translated from ShaderToHuman/docs_src at d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
//! Shared CPU/RustGPU bodies. Resource ownership belongs to the caller.
#![forbid(unsafe_code)]

mod gather;
mod scatter;
mod two_d;
mod ui;
mod world;

use shader_to_human::{text, ContextGather, IVec4, Mat4, UVec2, UVec4, Vec2, Vec3, Vec4};

/// All selectable source cases, including the default 3D checkerboard and Intro.
/// The source inventory's 35 counts explicit conditional branches, not cases.
pub const BRANCH_COUNTS: [u32; 6] = [7, 6, 11, 6, 6, 1];
pub const CATEGORY_NAMES: [&str; 6] = ["gather", "scatter", "2d", "3d", "ui", "intro"];

/// Explicit 128-byte shader root. Control is width, height, category, branch.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Inputs {
    pub control: UVec4,
    pub origin_and_depth: Vec4,
    pub world_from_clip: Mat4,
    pub mouse: Vec4,
    pub previous_mouse: Vec4,
}

/// Source docs UI buffer. Its initialized host representation is five uvec4s.
/// This does not depend on Rust's layout for this ordinary value type.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct State {
    pub radio: u32,
    pub checkbox: u32,
    pub color: Vec4,
    pub color_rgba: Vec4,
    pub sizes: Vec4,
    pub capture: IVec4,
}

impl State {
    pub fn from_words(words: [UVec4; 5]) -> Self {
        fn floats(word: UVec4) -> Vec4 {
            Vec4::new(
                f32::from_bits(word.x),
                f32::from_bits(word.y),
                f32::from_bits(word.z),
                f32::from_bits(word.w),
            )
        }
        Self {
            radio: words[0].x,
            checkbox: words[0].y,
            color: floats(words[1]),
            color_rgba: floats(words[2]),
            sizes: floats(words[3]),
            capture: words[4].as_ivec4(),
        }
    }

    pub fn to_words(self) -> [UVec4; 5] {
        fn bits(value: Vec4) -> UVec4 {
            UVec4::new(
                value.x.to_bits(),
                value.y.to_bits(),
                value.z.to_bits(),
                value.w.to_bits(),
            )
        }
        [
            UVec4::new(self.radio, self.checkbox, 0, 0),
            bits(self.color),
            bits(self.color_rgba),
            bits(self.sizes),
            self.capture.as_uvec4(),
        ]
    }
}

fn begin(pixel: Vec2) -> ContextGather {
    let mut context = ContextGather::new(pixel);
    context.set_cursor(Vec2::splat(10.0));
    context.set_scale(2.0);
    context
}

fn composite(context: ContextGather) -> Vec4 {
    Vec4::new(0.4, 0.7, 0.4, 1.0) * (1.0 - context.color.w) + context.color
}

/// One source invocation with private UI state. The host validates the case.
pub fn render(inputs: &Inputs, pixel: UVec2, state: &mut State) -> Vec4 {
    let centered = pixel.as_vec2() + Vec2::splat(0.5);
    match inputs.control.z {
        0 => gather::draw(inputs.control.w, centered),
        1 => {
            // Preserve Scatter_docs' round trip through normalized coordinates.
            // Despite its name, this source draws through ContextGather.
            let dimensions = inputs.control.truncate().truncate().as_vec2();
            let frag = Vec2::new(centered.x, dimensions.y - centered.y);
            let mut uv = frag / dimensions;
            uv.y = 1.0 - uv.y;
            let position = uv * dimensions - Vec2::splat(0.5);
            scatter::draw(inputs.control.w, position + Vec2::splat(0.5))
        }
        2 => two_d::draw(inputs, centered),
        3 => world::draw(inputs, centered),
        4 => {
            // Source UI_docs adds (0.5, -0.5) again after mainCS centers pixels.
            ui::draw(inputs, centered + Vec2::new(0.5, -0.5), state)
        }
        5 => intro(centered),
        _ => Vec4::ZERO,
    }
}

/// Commit once after rendering a frame from its immutable state snapshot.
/// Source UI writes overlap. Selecting one source mouse pixel removes that
/// race while retaining widget formulas and the source's omitted deinit call.
pub fn update(inputs: &Inputs, state: &mut State) {
    if inputs.control.z != 4 {
        return;
    }
    let mouse = inputs.mouse.truncate().truncate();
    let dimensions = inputs.control.truncate().truncate().as_vec2();
    let pixel = if mouse.cmpge(Vec2::ZERO).all() && mouse.cmplt(dimensions).all() {
        mouse.as_uvec2()
    } else {
        // No mouse-selected source pixel, but sliders still evaluate global input.
        UVec2::splat(u32::MAX)
    };
    let _ = render(inputs, pixel, state);
}

fn intro(pixel: Vec2) -> Vec4 {
    let mut ui = ContextGather::new(pixel);
    ui.print_lf();
    ui.set_scale(8.0);
    ui.print_space(2.4);
    ui.text_color = Vec3::X.extend(1.0);
    ui.print_text(&text!("S"));
    ui.text_color = Vec3::Y.extend(1.0);
    ui.print_text(&text!("2"));
    ui.text_color = Vec3::Z.extend(1.0);
    ui.print_text(&text!("H"));
    ui.print_lf();
    ui.set_scale(2.0);
    ui.print_space(8.0);
    ui.text_color = Vec3::splat(0.5).extend(1.0);
    ui.print_text(&text!("S2H_VERSION:"));
    ui.print_int(shader_to_human::VERSION as i32);
    ui.color.truncate().extend(1.0)
}
