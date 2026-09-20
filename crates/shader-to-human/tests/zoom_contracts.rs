#![forbid(unsafe_code)]
#[path = "../programs/zoom.rs"]
mod zoom;
use shader_to_human::{UVec2, UVec4, Vec2, Vec3, Vec4};
use zoom::{Inputs, State};

fn input(mouse: Vec4, previous_mouse: Vec4) -> Inputs {
    Inputs {
        dimensions: UVec4::new(800, 600, 0, 0),
        mouse,
        previous_mouse,
    }
}

#[test]
fn zoom_001_drag_pivot_and_integer_mouse_contract() {
    let mut state = State::default();
    let start = Vec4::new(100.9, 100.9, 1.0, 0.0);
    zoom::pre(&input(start, Vec4::ZERO), &mut state);
    let moved = Vec4::new(124.1, 116.1, 1.0, 0.0);
    zoom::pre(&input(moved, start), &mut state);
    assert_eq!(state.pan(), Vec3::new(-24.0, -16.0, 0.0));
    assert_eq!(state.drag_start().truncate().truncate(), Vec2::splat(100.0));
    let pivot = Vec4::new(200.0, 150.0, 0.0, 1.0);
    zoom::pre(&input(pivot, moved), &mut state);
    let before = (state.pan().truncate() + Vec2::new(200.0, 150.0)) * zoom::scale(state);
    zoom::pre(&input(Vec4::new(200.0, 200.0, 0.0, 1.0), pivot), &mut state);
    assert_eq!(state.pan(), Vec3::new(152.0, 118.0, -50.0));
    assert_eq!(zoom::scale(state), 0.5);
    assert_eq!(
        (state.pan().truncate() + Vec2::new(200.0, 150.0)) * zoom::scale(state),
        before
    );
}

#[test]
fn zoom_002_preserved_fields_and_source_release_sentinel() {
    let mut state = State::default();
    for index in 0..5 {
        state.0[index] = UVec4::splat(0x100 + index as u32);
    }
    state.0[5].w = 0x12345678;
    let before = state;
    let sentinel = input(Vec4::new(-100.9, 0.0, 0.0, 0.0), Vec4::ZERO);
    zoom::pre(&sentinel, &mut state);
    zoom::post(&sentinel, &mut state);
    assert_eq!(state, before);
    zoom::post(&input(Vec4::ZERO, Vec4::ZERO), &mut state);
    assert_eq!(&state.0[..4], &before.0[..4]);
    assert_eq!(state.0[4], UVec4::ZERO);
    assert_eq!(&state.0[5..], &before.0[5..]);
}

#[test]
fn zoom_003_reset_occurs_after_immutable_frame() {
    let mut state = State::default();
    state.0[5] = UVec4::new(
        (-1996.0_f32).to_bits(),
        (-1414.0_f32).to_bits(),
        (-100.0_f32).to_bits(),
        0,
    );
    let reset = input(Vec4::new(40.0, 538.0, 1.0, 0.0), Vec4::ZERO);
    zoom::pre(&reset, &mut state);
    let snapshot = state;
    let before = zoom::render(&reset, UVec2::new(50, 130), state);
    assert_eq!(state, snapshot);
    zoom::post(&reset, &mut state);
    assert_eq!(state.pan(), Vec3::ZERO);
    assert_eq!(state.drag_start(), snapshot.drag_start());
    let after = zoom::render(&reset, UVec2::new(50, 130), state);
    assert!(before.is_finite() && after.is_finite());
    assert_eq!(before.w, 1.0);
    assert_eq!(after.w, 1.0);
    assert_ne!(before, after);
    // Hover alone does not reset, and out-of-frame pixels cannot activate it.
    state = snapshot;
    zoom::post(
        &input(Vec4::new(40.0, 538.0, 0.0, 0.0), Vec4::ZERO),
        &mut state,
    );
    assert_eq!(state.pan(), snapshot.pan());
}
