//! Features at ShaderToHuman d6f98b7d. Electronic Arts 2024-2025, BSD-3-Clause.
//! Source bodies share only safe math. Native images belong to the caller.
#![forbid(unsafe_code)]
mod gather;
mod images;
mod quad;
#[path = "../../fixtures/scatter.rs"]
mod scatter;
mod table;
mod two_d;
mod world;

pub use images::{debug_zoom, font_atlas, use_font};
pub use quad::{quad_fragment, quad_post, quad_vertex};
pub use scatter::scatter;
use shader_to_human::{ContextGather, IVec4, Mat4, UVec2, UVec4, Vec2, Vec4};

/// Seven source state words followed by seventeen immutable input words.
/// State padding, PanAndScale and MouseDragStart remain untouched by Features.
pub const WORDS: usize = 24;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct State {
    pub radio: u32,
    pub checkbox: u32,
    pub color0: Vec4,
    pub color1: Vec4,
    pub sizes: Vec4,
    pub capture: IVec4,
}

#[derive(Clone, Copy)]
pub struct Inputs {
    pub world_from_clip: Mat4,
    pub world_to_clip: Mat4,
    pub world_to_view: Mat4,
    pub camera_near: Vec4,
    pub mouse: Vec4,
    pub previous_mouse: Vec4,
    /// Framebuffer width/height, time and far distance.
    pub dimensions_time_far: Vec4,
    pub angles: Vec4,
}

pub fn floats(word: UVec4) -> Vec4 {
    Vec4::new(
        f32::from_bits(word.x),
        f32::from_bits(word.y),
        f32::from_bits(word.z),
        f32::from_bits(word.w),
    )
}

fn bits(value: Vec4) -> UVec4 {
    UVec4::new(
        value.x.to_bits(),
        value.y.to_bits(),
        value.z.to_bits(),
        value.w.to_bits(),
    )
}

impl State {
    pub fn read(words: &[UVec4; WORDS]) -> Self {
        Self {
            radio: words[0].x,
            checkbox: words[0].y,
            color0: floats(words[1]),
            color1: floats(words[2]),
            sizes: floats(words[3]),
            capture: words[4].as_ivec4(),
        }
    }
    pub fn write(self, words: &mut [UVec4; WORDS]) {
        words[0].x = self.radio;
        words[0].y = self.checkbox;
        words[1] = bits(self.color0);
        words[2] = bits(self.color1);
        words[3] = bits(self.sizes);
        words[4] = self.capture.as_uvec4();
    }
}

impl Inputs {
    pub fn read(words: &[UVec4; WORDS]) -> Self {
        Self {
            world_from_clip: Mat4::from_cols(
                floats(words[7]),
                floats(words[8]),
                floats(words[9]),
                floats(words[10]),
            ),
            world_to_clip: Mat4::from_cols(
                floats(words[11]),
                floats(words[12]),
                floats(words[13]),
                floats(words[14]),
            ),
            world_to_view: Mat4::from_cols(
                floats(words[15]),
                floats(words[16]),
                floats(words[17]),
                floats(words[18]),
            ),
            camera_near: floats(words[19]),
            mouse: floats(words[20]),
            previous_mouse: floats(words[21]),
            dimensions_time_far: floats(words[22]),
            angles: floats(words[23]),
        }
    }
    pub fn dimensions(self) -> Vec2 {
        self.dimensions_time_far.truncate().truncate()
    }
    pub fn time(self) -> f32 {
        self.dimensions_time_far.z
    }

    fn ui(self, pixel: Vec2, state: State) -> ContextGather {
        let mut ui = ContextGather::new(pixel);
        ui.set_cursor(Vec2::splat(10.0));
        ui.state = state.capture;
        // Gather and 2D assign float4 directly. Table and Arrow explicitly cast
        // to int4 at their call sites. ContextGather itself stores float4.
        ui.mouse_input = self.mouse;
        ui
    }
}

/// Per-invocation private state. A separate single invocation commits the same
/// source update after the image, without the source's shared-write races.
pub fn image(kind: u32, inputs: &Inputs, pixel: UVec2, state: &mut State) -> Vec4 {
    match kind {
        0 => gather::draw(inputs, pixel, state),
        1 => table::draw(inputs, pixel, state),
        2 => two_d::draw(inputs, pixel, state),
        3 => images::arrows(inputs, pixel, state),
        4 => world::draw(inputs, pixel),
        5 => world::clear(inputs, pixel),
        6 => images::coordinates(pixel),
        7 => font_atlas(pixel, inputs.time()),
        _ => Vec4::ZERO,
    }
}

/// Commit after image completion using the invocation selected by source mouse
/// hit testing. Offscreen mouse coordinates cannot activate a button. Sliders
/// and release handling still run, matching their invocation-independent rules.
pub fn commit(kind: u32, inputs: &Inputs, state: &mut State) {
    if kind > 3 {
        return;
    }
    // Gather tests round(mouse - integer pixel) == 0. Choose the nearest
    // invocation, including fractional mouse coordinates. At an exact half,
    // either adjacent in-bounds invocation computes the same button update.
    let mouse = (inputs.mouse.truncate().truncate() + Vec2::splat(0.5))
        .floor()
        .as_ivec2();
    let pixel = if mouse.cmpge(shader_to_human::IVec2::ZERO).all()
        && mouse.cmplt(inputs.dimensions().as_ivec2()).all()
    {
        mouse.as_uvec2()
    } else {
        UVec2::ZERO
    };
    image(kind, inputs, pixel, state);
}
