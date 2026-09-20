//! Source layout, interaction and color contracts independent of GPU images.
#![forbid(unsafe_code)]
#[path = "../programs/features/mod.rs"]
mod features;
use features::{Inputs, State};
use shader_to_human::{IVec4, Mat4, UVec2, UVec4, Vec2, Vec3, Vec4};

#[test]
fn features_007_hlsl_nan_saturation_keeps_degenerate_arrows_finite() {
    let mut ui = shader_to_human::ContextGather::new(Vec2::new(40.0, 50.0));
    ui.draw_arrow(
        Vec2::new(20.0, 50.0),
        Vec2::new(60.0, 50.0),
        Vec4::ONE,
        0.0,
        0.0,
    );
    assert_eq!(ui.color, Vec4::ONE, "zero-area head must preserve the line");
    let mut outside = shader_to_human::ContextGather::new(Vec2::new(40.0, 100.0));
    outside.draw_arrow(
        Vec2::new(20.0, 50.0),
        Vec2::new(60.0, 50.0),
        Vec4::ONE,
        0.0,
        0.0,
    );
    assert_eq!(outside.color, Vec4::ZERO);
    assert_eq!(shader_to_human::color_ramp_rgb(f32::NAN), Vec3::ZERO);
    assert_eq!(shader_to_human::color_ramp_rgb(f32::INFINITY), Vec3::ZERO);
    let input = inputs();
    let line = features::image(3, &input, UVec2::new(40, 70), &mut State::default());
    assert_eq!(line, Vec3::ZERO.extend(1.0));
    let mut centered = input;
    centered.mouse = Vec4::new(400.0, 300.0, 0.0, 0.0);
    let background = features::image(3, &centered, UVec2::new(799, 599), &mut State::default());
    assert!(background.is_finite());
    assert!(
        (background.truncate() - Vec3::splat(0.735357))
            .abs()
            .max_element()
            < 1e-6
    );
}

fn inputs() -> Inputs {
    Inputs {
        world_from_clip: Mat4::IDENTITY,
        world_to_clip: Mat4::IDENTITY,
        world_to_view: Mat4::IDENTITY,
        camera_near: Vec4::new(0.0, 2.0, -4.0, 0.1),
        mouse: Vec4::new(100.0, 100.0, 0.0, 0.0),
        previous_mouse: Vec4::ZERO,
        dimensions_time_far: Vec4::new(800.0, 600.0, 0.25, 1000.0),
        angles: Vec4::ZERO,
    }
}

#[test]
fn features_001_state_abi_preserves_padding_pan_and_inputs() {
    assert_eq!(std::mem::size_of::<[UVec4; features::WORDS]>(), 384);
    let mut words = core::array::from_fn(|index| UVec4::splat(0x3f000000 + index as u32));
    let original = words;
    let state = State {
        radio: 3,
        checkbox: 1,
        color0: Vec4::new(0.25, 0.5, 0.75, 1.0),
        color1: Vec4::ONE,
        sizes: Vec4::new(3.0, 4.0, 5.0, 6.0),
        capture: IVec4::new(-100, 200, 1, 2),
    };
    state.write(&mut words);
    assert_eq!(State::read(&words), state);
    assert_eq!(words[0].z, original[0].z);
    assert_eq!(words[0].w, original[0].w);
    assert_eq!(&words[5..], &original[5..]);
    let read = Inputs::read(&words);
    assert_eq!(read.world_from_clip.x_axis, features::floats(words[7]));
    assert_eq!(read.world_to_clip.w_axis, features::floats(words[14]));
    assert_eq!(read.world_to_view.w_axis, features::floats(words[18]));
    assert_eq!(read.camera_near, features::floats(words[19]));
    assert_eq!(read.mouse, features::floats(words[20]));
    assert_eq!(read.previous_mouse, features::floats(words[21]));
    assert_eq!(read.dimensions_time_far, features::floats(words[22]));
    assert_eq!(read.angles, features::floats(words[23]));
}

#[test]
fn features_002_source_radio_checkbox_and_clear_interactions() {
    let mut input = inputs();
    let mut state = State::default();
    // Source cursor arithmetic places the three circles at x113,129,145 y225.
    input.mouse = Vec4::new(129.9, 225.9, 1.0, 0.0);
    features::commit(0, &input, &mut state);
    assert_eq!(state.radio, 2);
    input.mouse = Vec4::new(145.0, 225.0, 0.0, 0.0);
    features::commit(0, &input, &mut state);
    assert_eq!(state.radio, 2, "hover must not select");
    input.mouse = Vec4::new(125.0, 258.0, 1.0, 0.0);
    features::commit(0, &input, &mut state);
    assert_eq!(state.radio, 0);
    input.mouse = Vec4::new(112.0, 289.0, 1.0, 0.0);
    features::commit(0, &input, &mut state);
    assert_eq!(state.checkbox, 1);
    input.previous_mouse = input.mouse;
    features::commit(0, &input, &mut state);
    assert_eq!(state.checkbox, 1, "held mouse must not toggle repeatedly");
    input.previous_mouse = Vec4::ZERO;
    state.checkbox = 3;
    features::commit(0, &input, &mut state);
    assert_eq!(
        state.checkbox,
        u32::MAX - 1,
        "source uint subtraction wraps"
    );
}

#[test]
fn features_003_capture_release_and_private_image_state() {
    let mut input = inputs();
    let initial = State {
        capture: IVec4::new(52, 350, 0, 0),
        ..State::default()
    };
    let mut state = initial;
    input.mouse = Vec4::new(-100.9, 0.0, 0.0, 0.0);
    features::commit(1, &input, &mut state);
    assert_eq!(state.capture, initial.capture);
    input.mouse = Vec4::ZERO;
    features::commit(1, &input, &mut state);
    assert_eq!(state.capture, IVec4::ZERO);
    let snapshot = state;
    input.mouse = Vec4::new(129.0, 225.0, 1.0, 0.0);
    let mut private = state;
    features::image(0, &input, UVec2::new(129, 225), &mut private);
    assert_eq!(state, snapshot);
    assert_eq!(private.radio, 2);
    features::commit(0, &input, &mut state);
    assert_eq!(state.radio, 2);
}

#[test]
fn features_008_gather_fractional_mouse_changes_slider_before_capture_truncation() {
    let mut input = inputs();
    input.mouse = Vec4::new(52.75, 350.25, 1.0, 0.0);
    let mut state = State::default();
    features::commit(0, &input, &mut state);
    assert_eq!(state.capture, IVec4::new(52, 350, 0, 0));
    assert_eq!(state.color0.w, (52.75 - 44.5) / 124.0);
}

#[test]
fn features_009_mouse_release_sentinel_follows_each_source_cast() {
    let mut input = inputs();
    let captured = IVec4::new(52, 350, 0, 0);
    for kind in 0..4 {
        for (mouse, keep) in [(-100.0, true), (-100.9, kind == 1 || kind == 3)] {
            let mut state = State {
                capture: captured,
                ..State::default()
            };
            input.mouse = Vec4::new(mouse, 0.0, 0.0, 0.0);
            features::commit(kind, &input, &mut state);
            assert_eq!(
                state.capture,
                if keep { captured } else { IVec4::ZERO },
                "kind {kind}, mouse {mouse}"
            );
        }
    }
}

#[test]
fn features_004_custom_colored_font_and_immutable_mouse_texel() {
    let mut loads = Vec::new();
    let color = features::use_font(UVec2::splat(10), |coordinate| {
        loads.push(coordinate);
        Vec4::new(0.2, 0.4, 0.6, 0.5)
    });
    assert_eq!(loads, [UVec2::new((b'U' as u32 - 32) * 8, 0)]);
    assert_eq!(color, Vec4::new(0.1, 0.2, 0.3, 1.0));
    assert_eq!(
        features::use_font(UVec2::ZERO, |_| panic!("outside glyph")),
        Vec3::ZERO.extend(1.0)
    );
    let input = inputs();
    let stored = Vec4::new(0.25, 0.5, 0.75, 1.0);
    assert_eq!(
        features::debug_zoom(&input, UVec2::splat(100), stored, Vec4::ZERO),
        stored
    );
    assert_eq!(
        features::debug_zoom(&input, UVec2::new(799, 599), stored, Vec4::ONE),
        stored
    );
    assert_eq!(
        features::font_atlas(UVec2::ZERO, 0.0),
        Vec4::ZERO,
        "space glyph is empty"
    );
    let glyph = features::font_atlas(UVec2::new((b'A' as u32 - 32) * 8 + 3, 2), 0.25);
    assert!(glyph.is_finite() && glyph.cmpge(Vec4::ZERO).all() && glyph.cmple(Vec4::ONE).all());
}

#[test]
fn features_005_quad_source_offset_depth_and_clip_w() {
    let input = inputs();
    assert_eq!(
        features::quad_vertex(0, &input),
        (Vec4::new(1.0, 1.0, 0.0, 1.0), Vec2::new(0.0, 1.0))
    );
    assert_eq!(
        features::quad_vertex(2, &input),
        (Vec4::new(3.0, 3.0, 0.0, 1.0), Vec2::new(1.0, 0.0))
    );
    let position = Vec4::new(0.0, 0.0, 0.025, 4.375);
    assert_eq!(
        features::quad_fragment(&input, Vec2::new(0.25, 0.9), position),
        Vec3::splat(0.025).extend(1.0)
    );
    assert_eq!(
        features::quad_fragment(&input, Vec2::new(0.75, 0.9), position),
        Vec3::splat(0.375).extend(1.0)
    );
    // Far outside the quad and overlay, the post pass preserves sRGB RGB.
    let stored = Vec4::new(0.25, 0.5, 0.75, 0.2);
    let result = features::quad_post(&input, UVec2::new(790, 590), stored);
    assert!((result.truncate() - stored.truncate()).abs().max_element() < 1e-6);
    assert_eq!(result.w, 1.0);
}

#[test]
fn features_006_source_programs_cover_all_active_image_kinds() {
    let input = inputs();
    for kind in 0..8 {
        let pixel = if kind == 7 {
            UVec2::new(267, 3)
        } else {
            UVec2::new(750, 550)
        };
        let result = features::image(kind, &input, pixel, &mut State::default());
        assert!(result.is_finite(), "kind {kind}: {result:?}");
        assert_eq!(
            result.w,
            if kind == 7 {
                features::font_atlas(pixel, input.time()).w
            } else {
                1.0
            }
        );
    }
    let mut coordinates = Vec::new();
    features::scatter(&mut |pixel, _| coordinates.push(pixel));
    assert!(coordinates
        .iter()
        .all(|pixel| pixel.x >= 522 && pixel.y >= 10));
    assert!(coordinates.iter().any(|pixel| pixel.x > 750));
    assert!(
        coordinates
            .iter()
            .all(|pixel| pixel.x < 800 && pixel.y < 600),
        "the unchanged HLSL Scatter control must never issue an out-of-range store"
    );
}
