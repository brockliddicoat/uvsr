//! Zoom2D at ShaderToHuman d6f98b7d, copyright Electronic Arts 2024-2025.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
//! Source Pre, immutable Render, then one persistent Render-state update.
#![forbid(unsafe_code)]

use shader_to_human::{linear_to_srgb, text, ContextGather, UVec2, UVec4, Vec2, Vec3, Vec4};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Inputs {
    pub dimensions: UVec4,
    pub mouse: Vec4,
    pub previous_mouse: Vec4,
}

/// Explicit seven-uvec4 ABI. Words0..4 retain the source UI fields, word5
/// contains PanAndScale.xyz and word6 MouseDragStart.xyzw. Padding is preserved.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct State(pub [UVec4; 7]);

fn floats(word: UVec4) -> Vec4 {
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
    pub fn pan(self) -> Vec3 {
        floats(self.0[5]).truncate()
    }
    pub fn drag_start(self) -> Vec4 {
        floats(self.0[6])
    }

    fn set_pan(&mut self, pan: Vec3) {
        self.0[5] = UVec4::new(
            pan.x.to_bits(),
            pan.y.to_bits(),
            pan.z.to_bits(),
            self.0[5].w,
        );
    }
}

pub fn scale(state: State) -> f32 {
    libm::powf(2.0, state.pan().z * 0.02)
}

/// Original Pre.hlsl, including integer mouse conversion, exact left-button
/// comparisons, nonzero right-button drag, and the original two-step pivot math.
pub fn pre(inputs: &Inputs, state: &mut State) {
    let mouse = inputs.mouse.as_ivec4();
    let previous = inputs.previous_mouse.as_ivec4();
    let mut pan = state.pan();
    if pan == Vec3::ZERO {
        pan = Vec3::ZERO;
    }
    let old_scale = scale(*state);
    if previous.z == 1 && mouse.z == 1 {
        let delta = (mouse - previous).as_vec4();
        pan.x -= delta.x;
        pan.y -= delta.y;
    }
    let mut drag = state.drag_start();
    if previous.w != 0 && mouse.w != 0 {
        pan.z -= (mouse.y - previous.y) as f32;
        let new_scale = libm::powf(2.0, pan.z * 0.02);
        let local = (pan.truncate() + Vec2::new(drag.z, drag.w)) * old_scale;
        pan.x -= local.x / old_scale;
        pan.y -= local.y / old_scale;
        pan.x += local.x / new_scale;
        pan.y += local.y / new_scale;
    }
    state.set_pan(pan);
    if previous.z == 0 && mouse.z == 1 {
        drag.x = mouse.x as f32;
        drag.y = mouse.y as f32;
    }
    if previous.w == 0 && mouse.w == 1 {
        drag.z = mouse.x as f32;
        drag.w = mouse.y as f32;
    }
    state.0[6] = bits(drag);
}

// The source credits https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8.
fn grid_texture(p: Vec2, dx: Vec2, dy: Vec2, n: f32) -> f32 {
    let width = dx.abs().max(dy.abs()) + Vec2::splat(0.01);
    let a = p + width * 0.5;
    let b = p - width * 0.5;
    let i = (a.floor() + ((a - a.floor()) * n).min(Vec2::ONE)
        - b.floor()
        - ((b - b.floor()) * n).min(Vec2::ONE))
        / (n * width);
    (1.0 - i.x) * (1.0 - i.y)
}

fn overlay(inputs: &Inputs, pixel: Vec2, state: State) -> (Vec4, bool) {
    let mut ui = ContextGather::new(pixel);
    ui.set_cursor(Vec2::new(10.0, 480.0));
    ui.state = state.0[4].as_ivec4();
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    ui.text_color = Vec4::new(1.0, 1.0, 0.1, 1.0);
    ui.set_scale(2.0);
    ui.print_text(&text!("XY: "));
    ui.print_float(state.pan().x);
    ui.print_text(&text!(" "));
    ui.print_float(state.pan().y);
    ui.print_lf();
    ui.print_text(&text!(" S: "));
    ui.print_float(scale(state));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("Reset"));
    ui.button_color = Vec4::new(1.0, 0.0, 0.0, 1.0);
    let reset = ui.button(5) && inputs.mouse.z != 0.0;
    ui.print_lf();
    ui.print_lf();
    ui.set_scale(1.0);
    ui.print_text(&text!(" left MouseDrag: Pan"));
    ui.print_lf();
    ui.print_lf();
    ui.print_text(&text!("right MouseDrag: Scale"));
    (ui.color, reset)
}

/// Original Render.hlsl math and ordering, with an immutable frame snapshot.
pub fn render(inputs: &Inputs, pixel: UVec2, state: State) -> Vec4 {
    let centered = pixel.as_vec2() + Vec2::splat(0.5);
    let pan_scale = scale(state);
    let position = (centered + state.pan().truncate()) * pan_scale;
    let mut ui = ContextGather::new(position.floor() + Vec2::splat(0.5));
    ui.set_cursor(Vec2::splat(10.0));
    ui.state = state.0[4].as_ivec4();
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    ui.coordinate_system(
        Vec2::new(50.0, 130.0),
        Vec4::new(-30.0, -30.0, 250.0, 250.0),
        1.0,
        20.0,
        Vec4::new(1.0, 1.0, 1.0, 0.25),
        0,
    );
    ui.line_width = 1.0;
    ui.coordinate_system(
        Vec2::new(340.0, 120.0),
        Vec4::new(-10.0, -100.0, 150.0, 10.0),
        1.0,
        20.0,
        Vec4::new(1.0, 1.0, 1.0, 0.25),
        3,
    );
    ui.print_text(&text!("coordinateSystem"));
    // Preserve the source's truncation toward zero, including (-1, 0).
    let integer = position.as_ivec2();
    let outside = !(integer.cmpge(shader_to_human::IVec2::ZERO).all()
        && integer
            .cmplt(inputs.dimensions.truncate().truncate().as_ivec2())
            .all());
    let alpha = if outside {
        0.0
    } else {
        1.0 - grid_texture(
            position + Vec2::splat(0.5 / 20.0),
            Vec2::new(pan_scale, 0.0),
            Vec2::new(0.0, pan_scale),
            20.0,
        )
    };
    ui.color = ui.color * (1.0 - alpha * 0.05) + Vec4::ONE * (alpha * 0.05);
    let mut linear = Vec4::new(0.01, 0.01, 0.1, 1.0) * (1.0 - ui.color.w) + ui.color;

    let mut pixel_ui =
        ContextGather::new((position - position.floor()) * 8.0 * 5.0 + Vec2::splat(0.5));
    pixel_ui.text_color = Vec4::new(1.0, 0.6, 0.6, 0.1);
    pixel_ui.text_color.w *= (1.0 / pan_scale - 8.0).clamp(0.0, 1.0);
    if outside {
        pixel_ui.text_color.w = 0.0;
    }
    pixel_ui.set_cursor(Vec2::splat(3.0));
    pixel_ui.print_int(position.x as i32);
    pixel_ui.print_lf();
    pixel_ui.print_int(position.y as i32);
    linear = linear * (1.0 - pixel_ui.color.w) + pixel_ui.color;
    let (overlay, _) = overlay(inputs, centered, state);
    linear = linear * (1.0 - overlay.w) + overlay;
    linear_to_srgb(linear.truncate()).extend(linear.w)
}

/// Commit Render's deinit and reset once after every pixel finishes. Source
/// shared writes raced with reads. No pixel is allowed to observe half a reset.
pub fn post(inputs: &Inputs, state: &mut State) {
    let mut ui = ContextGather::new(Vec2::ZERO);
    ui.state = state.0[4].as_ivec4();
    ui.mouse_input = inputs.mouse.as_ivec4().as_vec4();
    state.0[4] = ui.deinit().as_uvec4();
    let mouse = inputs.mouse.as_ivec4().truncate().truncate();
    if mouse.cmpge(shader_to_human::IVec2::ZERO).all()
        && mouse
            .cmplt(inputs.dimensions.truncate().truncate().as_ivec2())
            .all()
        && overlay(inputs, mouse.as_vec2() + Vec2::splat(0.5), *state).1
    {
        state.set_pan(Vec3::ZERO);
    }
}
