//! Stable CPU cases for source behavior, independent of any GPU claim.
#![forbid(unsafe_code)]
use shader_to_human::*;
use std::collections::BTreeMap;

fn close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() <= 1.0e-6,
        "expected {expected}, got {actual}"
    );
}

#[test]
fn s2h_font_a_bitmap_and_invalid_domain() {
    // Independently read rows from the pinned source font's 'A' glyph.
    let rows = [0x30_u8, 0x78, 0xcc, 0xcc, 0xfc, 0xcc, 0xcc, 0];
    for (y, row) in rows.into_iter().enumerate() {
        for x in 0..8 {
            assert_eq!(
                MiniFont.lookup(65, IVec2::new(x, y as i32)),
                row & (1 << (7 - x)) != 0
            );
        }
    }
    for ascii in [0, 31, 32, 128, u32::MAX] {
        for y in 0..8 {
            for x in 0..8 {
                assert!(!MiniFont.lookup(ascii, IVec2::new(x, y)));
            }
        }
    }
    for point in [
        IVec2::new(-1, 0),
        IVec2::new(0, -1),
        IVec2::new(8, 0),
        IVec2::new(0, 8),
    ] {
        assert!(!MiniFont.lookup(65, point));
    }
}

#[test]
fn s2h_gather_rectangle_edges_and_premultiplied_alpha() {
    let mut ui = ContextGather::new(Vec2::new(4.5, 5.5));
    ui.draw_rectangle(Vec2::ZERO, Vec2::splat(8.0), Vec4::new(1.0, 0.0, 0.0, 0.5));
    ui.draw_rectangle(Vec2::ZERO, Vec2::splat(8.0), Vec4::new(0.0, 0.0, 1.0, 0.25));
    assert_eq!(ui.color, Vec4::new(0.375, 0.0, 0.25, 0.625));
    for point in [
        Vec2::new(8.0, 4.0),
        Vec2::new(4.0, 8.0),
        Vec2::new(-0.1, 4.0),
    ] {
        let mut edge = ContextGather::new(point);
        edge.draw_rectangle(Vec2::ZERO, Vec2::splat(8.0), Vec4::ONE);
        assert_eq!(edge.color, Vec4::ZERO);
    }
}

#[test]
fn s2h_frame_retains_source_background_blending() {
    let mut ui = ContextGather::new(Vec2::new(4.0, 4.0));
    ui.cursor.x = 8.0;
    ui.color = Vec4::new(0.0, 0.0, 0.5, 0.5);
    ui.frame_fill_color = Vec4::new(1.0, 0.0, 0.0, 1.0);
    ui.frame(1);
    assert_eq!(ui.color, Vec4::new(0.5, 0.0, 0.25, 0.75));
}

#[test]
fn s2h_gather_cursor_and_empty_glyph_cells() {
    let mut ui = ContextGather::new(Vec2::ZERO);
    ui.set_cursor(Vec2::new(12.0, 20.0));
    ui.set_scale(2.0);
    ui.print_text(&[65, 0, 128]);
    assert_eq!(ui.cursor, Vec2::new(60.0, 20.0));
    ui.print_lf();
    assert_eq!(ui.cursor, Vec2::new(12.0, 36.0));
    ui.print_space(1.5);
    assert_eq!(ui.cursor.x, 36.0);
}

fn scatter_pixels(
    draw: impl FnOnce(&mut ContextScatter, &mut dyn FnMut(IVec2, Vec4)),
) -> BTreeMap<(i32, i32), [u32; 4]> {
    let mut pixels = BTreeMap::new();
    let mut ui = ContextScatter::default();
    draw(&mut ui, &mut |p, color| {
        pixels.insert((p.x, p.y), color.to_array().map(f32::to_bits));
    });
    pixels
}

#[test]
fn s2h_scatter_formatting_matches_source_strings() {
    for (value, expected) in [
        (0, "0"),
        (-1234, "-1234"),
        (i32::MIN, "-2147483648"),
        (i32::MAX, "2147483647"),
    ] {
        let actual = scatter_pixels(|ui, output| ui.print_int(value, &mut |p, c| output(p, c)));
        let reference = scatter_pixels(|ui, output| {
            ui.print_ascii(expected.as_bytes(), &mut |p, c| output(p, c))
        });
        assert_eq!(actual, reference, "integer {value}");
    }
    for (value, expected) in [(-0.5, "0.500"), (1.25, "1.250"), (-3.75, "-3.750")] {
        let actual = scatter_pixels(|ui, output| ui.print_float(value, &mut |p, c| output(p, c)));
        let reference = scatter_pixels(|ui, output| {
            ui.print_ascii(expected.as_bytes(), &mut |p, c| output(p, c))
        });
        assert_eq!(actual, reference, "float {value}");
    }
    let actual = scatter_pixels(|ui, output| ui.print_hex(0x1234_abcd, &mut |p, c| output(p, c)));
    let reference =
        scatter_pixels(|ui, output| ui.print_ascii(b"1234ABCD", &mut |p, c| output(p, c)));
    assert_eq!(actual, reference);
}

#[test]
fn s2h_scatter_scale_clipping_hook_and_crosshair_center() {
    let mut ui = ContextScatter::default();
    ui.set_cursor(Vec2::new(10.5, 20.9));
    ui.set_scale(4);
    let mut pixels = BTreeMap::new();
    ui.print_character(65, &mut |p, _| {
        assert!(p.x >= 10 && p.x < 42 && p.y >= 20 && p.y < 52);
        pixels.insert((p.x, p.y), ());
    });
    assert_eq!(pixels.len(), 28 * 16);
    assert_eq!(ui.cursor, IVec2::new(42, 20));
    ui.text_color = Vec4::new(0.0, 1.0, 0.0, 0.5);
    let arm = Vec4::new(1.0, 0.0, 0.0, 1.0);
    let mut calls = Vec::new();
    ui.draw_crosshair(Vec2::new(5.0, 6.0), 3.0, arm, &mut |p, c| {
        calls.push((p, c))
    });
    assert_eq!(calls.len(), 9);
    assert_eq!(calls[0], (IVec2::new(5, 6), ui.text_color));
    assert!(calls[1..].iter().all(|(_, c)| *c == arm));
}

#[test]
fn s2h_color_helpers_preserve_thresholds_and_bit_order() {
    assert_eq!(index_to_color(0), Vec3::ZERO);
    assert_eq!(index_to_color(1), Vec3::new(4.0 / 7.0, 0.0, 0.0));
    assert_eq!(index_to_color(8), Vec3::new(2.0 / 7.0, 0.0, 0.0));
    assert_eq!(index_to_color(64), Vec3::new(1.0 / 7.0, 0.0, 0.0));
    assert_eq!(index_to_color(511), Vec3::ONE);
    assert_eq!(color_ramp_rgb(0.0), Vec3::X);
    assert_eq!(color_ramp_rgb(0.5), Vec3::Y);
    assert_eq!(color_ramp_rgb(1.0), Vec3::Z);
    close(srgb_to_linear(Vec3::splat(0.04045)).x, 0.04045 / 12.92);
    close(linear_to_srgb(Vec3::splat(0.0031308)).x, 0.0031308 * 12.92);
    for value in [0.0, 0.01, 0.2, 0.5, 0.9, 1.0] {
        close(srgb_to_linear(linear_to_srgb(Vec3::splat(value))).x, value);
    }
}

#[test]
fn s2h_mouse_round_ties_and_slider_capture_release() {
    let mut ui = ContextGather::new(Vec2::new(4.0, 4.5));
    ui.cursor.x = 8.0;
    ui.mouse_input = Vec4::new(4.0, 4.0, 1.0, 0.0);
    assert!(ui.button(1), "HLSL round(0.5) is zero");
    ui.pixel.x = 3.99;
    assert!(!ui.button(1));
    let mut ui = ContextGather::new(Vec2::new(15.5, 12.5));
    ui.set_cursor(Vec2::new(10.0, 10.0));
    ui.mouse_input = Vec4::new(25.0, 12.0, 1.0, 0.0);
    let mut value = 0.0;
    ui.slider_float(10, &mut value, 0.0, 1.0);
    close(value, 13.5 / 78.0);
    assert_eq!(ui.state, IVec4::new(25, 12, 0, 0));
    ui.set_cursor(Vec2::new(10.0, 10.0));
    ui.mouse_input = Vec4::new(200.0, 12.0, 1.0, 0.0);
    ui.slider_float(10, &mut value, 0.0, 1.0);
    assert_eq!(value, 1.0);
    ui.mouse_input.z = 0.0;
    assert_eq!(ui.deinit(), IVec4::ZERO);
}

#[test]
fn s2h_table_callback_row_and_cursor_contract() {
    let mut ui = ContextGather::new(Vec2::new(3.5, 19.5));
    ui.table_int(7, Vec4::ZERO, IVec2::new(4, 4), true, |column, row| {
        assert_eq!((column, row), (7, 2));
        Some(123)
    });
    assert_eq!(ui.cursor, Vec2::new(32.0, 16.0));
    ui.set_cursor(Vec2::ZERO);
    ui.table_float(0, Vec4::ZERO, IVec2::new(4, 4), false, |_, row| {
        assert_eq!(row, 2);
        None
    });
    assert_eq!(ui.cursor, Vec2::new(0.0, 32.0));
}

#[test]
fn s2h_world_intersections_depth_and_alpha() {
    assert_eq!(
        sphere_intersection(Vec3::new(0.0, 0.0, -3.0), Vec3::Z, Vec3::ZERO, 1.0),
        Vec2::new(2.0, 4.0)
    );
    let direction = Vec3::new(0.1, 0.2, 1.0).normalize();
    let (hit, normal) = box_intersection(Vec3::new(0.0, 0.0, -3.0), direction, Vec3::ONE);
    close(hit.x, 2.0 / direction.z);
    close(hit.y, 4.0 / direction.z);
    assert_eq!(normal, -Vec3::Z);
    let mut context = Context3D::new(Vec3::new(0.0, 0.0, -3.0), Vec3::Z);
    context.draw_sphere(Vec3::ZERO, Vec4::new(1.0, 1.0, 1.0, 0.2), 1.0);
    assert_eq!(context.depth, 2.0);
    assert_eq!(context.color.w, 0.2);
    let color = context.color;
    context.draw_sphere(Vec3::new(0.0, 0.0, 8.0), Vec4::ZERO, 1.0);
    context.draw_skybox();
    assert_eq!(context.color, color);
}

#[test]
fn s2h_world_shadow_callback_light_bias_and_alpha() {
    let origin = Vec3::new(1.0, 2.0, 3.0);
    let mut context = Context3D::new(origin, Vec3::Z);
    let mut calls = 0;
    context.scene_with_shadows(|ray| {
        calls += 1;
        if calls == 1 {
            ray.depth = 3.0;
            ray.color = Vec4::new(0.8, 0.6, 0.4, 0.25);
        } else {
            let light = Vec3::new(1.0, 3.0, 2.0).normalize();
            assert!((ray.origin - (origin + Vec3::Z * 3.0 + light * 0.001)).length() < 1.0e-6);
            assert_eq!(ray.direction, light);
            ray.depth = 2.0;
        }
    });
    assert_eq!(calls, 2);
    assert_eq!(context.color, Vec4::new(0.4, 0.3, 0.2, 0.25));
    assert_eq!(context.depth, 3.0);
}
