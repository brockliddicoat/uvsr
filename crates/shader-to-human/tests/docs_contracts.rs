//! Deterministic source cases for documentation branches and frame state.
#![forbid(unsafe_code)]
#[path = "../demos/mod.rs"]
mod demos;

use demos::{Inputs, State, BRANCH_COUNTS, CATEGORY_NAMES};
use shader_to_human::{linear_to_srgb, IVec4, Mat4, UVec2, UVec4, Vec3, Vec4};

fn inputs(category: u32, branch: u32) -> Inputs {
    let values: Vec<f32> =
        include_str!("../../../tests/parity/fixtures/shader-to-human/camera.txt")
            .split_whitespace()
            .map(|word| word.parse().unwrap())
            .collect();
    Inputs {
        control: UVec4::new(800, 600, category, branch),
        origin_and_depth: Vec4::from_slice(&values[..4]),
        world_from_clip: Mat4::from_cols_array(values[4..].try_into().unwrap()),
        mouse: Vec4::ZERO,
        previous_mouse: Vec4::ZERO,
    }
}

#[test]
fn s2h_docs_root_and_persistent_words() {
    assert_eq!(std::mem::size_of::<Inputs>(), 128);
    assert_eq!(std::mem::offset_of!(Inputs, origin_and_depth), 16);
    assert_eq!(std::mem::offset_of!(Inputs, world_from_clip), 32);
    assert_eq!(std::mem::offset_of!(Inputs, mouse), 96);
    assert_eq!(std::mem::offset_of!(Inputs, previous_mouse), 112);
    let state = State {
        radio: 3,
        checkbox: 1,
        color: Vec4::new(-0.0, 0.25, 0.5, 1.0),
        color_rgba: Vec4::new(-1.0, 2.0, 3.0, 4.0),
        sizes: Vec4::splat(7.0),
        capture: IVec4::new(-1, 17, -999, i32::MAX),
    };
    let words = state.to_words();
    assert_eq!(words[0], UVec4::new(3, 1, 0, 0));
    assert_eq!(
        words[1],
        UVec4::new(0x80000000, 0x3e800000, 0x3f000000, 0x3f800000)
    );
    assert_eq!(State::from_words(words).to_words(), words);
    assert_eq!(State::default().to_words(), [UVec4::ZERO; 5]);
}

#[test]
fn s2h_docs_all_source_cases_are_finite() {
    assert_eq!(BRANCH_COUNTS.iter().sum::<u32>(), 37);
    for (category, &count) in BRANCH_COUNTS.iter().enumerate() {
        for branch in 0..count {
            for pixel in [
                UVec2::new(11, 12),
                UVec2::new(113, 17),
                UVec2::new(287, 251),
                UVec2::new(799, 599),
            ] {
                let mut state = State::default();
                let output = demos::render(&inputs(category as u32, branch), pixel, &mut state);
                assert!(
                    output.is_finite(),
                    "{}.{branch} {pixel:?}: {output:?}",
                    CATEGORY_NAMES[category]
                );
            }
        }
    }
}

#[test]
fn s2h_docs_source_placeholders_and_shape_oracles() {
    let background = linear_to_srgb(Vec3::new(0.7, 0.4, 0.4)).extend(1.0);
    for branch in [9, 10] {
        for pixel in [UVec2::ZERO, UVec2::new(150, 50), UVec2::new(799, 599)] {
            assert_eq!(
                demos::render(&inputs(2, branch), pixel, &mut State::default()),
                background
            );
        }
    }
    let rectangle = demos::render(&inputs(2, 4), UVec2::new(110, 20), &mut State::default());
    assert!((rectangle - Vec3::X.extend(1.0)).abs().max_element() < 1.0e-6);
    let overlap = demos::render(&inputs(2, 4), UVec2::new(220, 60), &mut State::default());
    assert_eq!(
        overlap,
        linear_to_srgb(Vec3::new(0.0, 0.5, 0.0)).extend(1.0)
    );
    let unused_crosshair =
        demos::render(&inputs(1, 5), UVec2::new(200, 200), &mut State::default());
    assert_eq!(unused_crosshair, Vec4::new(0.4, 0.7, 0.4, 1.0));
}

#[test]
fn s2h_docs_checkbox_press_hold_release_and_single_commit() {
    let mut input = inputs(4, 2);
    input.mouse = Vec4::new(113.0, 17.0, 1.0, 0.0);
    let mut state = State::default();
    // Four source pixels satisfy ties-to-even selection. Rendering them uses
    // private copies, so their overlapping clicks cannot toggle shared state.
    for pixel in [
        UVec2::new(112, 17),
        UVec2::new(113, 17),
        UVec2::new(112, 18),
        UVec2::new(113, 18),
    ] {
        let mut private = state;
        demos::render(&input, pixel, &mut private);
        assert_eq!(private.checkbox, 1);
        assert_eq!(state.checkbox, 0);
    }
    demos::update(&input, &mut state);
    assert_eq!(state.checkbox, 1);
    input.previous_mouse = input.mouse;
    demos::update(&input, &mut state);
    assert_eq!(state.checkbox, 1);
    input.mouse.z = 0.0;
    demos::update(&input, &mut state);
    assert_eq!(state.checkbox, 1);
    input.previous_mouse = input.mouse;
    input.mouse.z = 1.0;
    demos::update(&input, &mut state);
    assert_eq!(state.checkbox, 0);
}

#[test]
fn s2h_docs_radio_clear_and_color_channels() {
    let mut state = State::default();
    let mut input = inputs(4, 1);
    for (x, value) in [(113.0, 1), (129.0, 2), (145.0, 3)] {
        input.mouse = Vec4::new(x, 17.0, 1.0, 0.0);
        demos::update(&input, &mut state);
        assert_eq!(state.radio, value);
    }
    input.control.w = 0;
    input.mouse = Vec4::new(146.0, 18.0, 1.0, 0.0);
    demos::update(&input, &mut state);
    assert_eq!(state.radio, 0);
    input.control.w = 4;
    input.mouse = Vec4::new(169.0, 47.0, 1.0, 0.0);
    demos::update(&input, &mut state);
    assert_eq!(state.color, Vec4::new(0.0, 0.0, 1.0, 0.0));
    input.control.w = 5;
    input.mouse.y = 64.0;
    demos::update(&input, &mut state);
    assert_eq!(state.color_rgba, Vec4::W);
}

#[test]
fn s2h_docs_slider_keeps_source_capture_omission() {
    let mut input = inputs(4, 3);
    let mut state = State::default();
    input.mouse = Vec4::new(107.0, 15.0, 1.0, 0.0);
    demos::update(&input, &mut state);
    assert_eq!(state.color.w, 62.5 / 124.0);
    assert_eq!(state.capture, IVec4::ZERO);
    input.previous_mouse = input.mouse;
    input.mouse.x = 300.0;
    demos::update(&input, &mut state);
    assert_eq!(state.color.w, 62.5 / 124.0);
    // A supplied capture is read and preserved, even on release. UI_docs'
    // HLSL branch omits deinit, so fixing capture is a separate behavior change.
    state.capture = IVec4::new(107, 15, 0, 0);
    demos::update(&input, &mut state);
    assert_eq!(state.color.w, 1.0);
    input.mouse.z = 0.0;
    demos::update(&input, &mut state);
    assert_eq!(state.capture, IVec4::new(107, 15, 0, 0));
}
