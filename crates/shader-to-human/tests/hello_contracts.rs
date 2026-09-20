//! Source coordinates, color math and explicit root ABI for the Hello families.
#![forbid(unsafe_code)]
#[path = "../programs/hello.rs"]
mod hello;
use hello::Inputs;
use shader_to_human::{Mat4, Vec2, Vec3, Vec4};

#[test]
fn s2h_hello_root_layout_and_frozen_cameras() {
    assert_eq!(std::mem::size_of::<Inputs>(), 96);
    assert_eq!(std::mem::offset_of!(Inputs, camera), 64);
    assert_eq!(std::mem::offset_of!(Inputs, dimensions), 80);
    let rows: Vec<_> =
        include_str!("../../../tests/parity/fixtures/shader-to-human/hello-cameras.txt")
            .lines()
            .map(|line| {
                line.split_whitespace()
                    .map(|n| n.parse::<f32>().unwrap())
                    .collect::<Vec<_>>()
            })
            .collect();
    assert_eq!(rows.len(), 3);
    for row in rows {
        assert_eq!(row.len(), 24);
        assert!(row.iter().all(|n| n.is_finite()));
        assert_eq!(&row[20..], &[800.0, 600.0, 0.0, 0.0]);
        let input = Inputs {
            world_to_clip: Mat4::from_cols_array(row[..16].try_into().unwrap()),
            camera: Vec4::from_slice(&row[16..20]),
            dimensions: Vec4::new(800.0, 600.0, 0.0, 0.0),
        };
        for vertex in 0..6 {
            let (clip, _) = hello::quad_vertex(vertex, &input);
            assert!(clip.is_finite() && clip.w > 0.0 && clip.z > 0.0 && clip.z < clip.w);
        }
    }
}

#[test]
fn s2h_hello_source_quad_offset_and_uv_orientation() {
    let input = Inputs {
        world_to_clip: Mat4::IDENTITY,
        camera: Vec4::ZERO,
        dimensions: Vec4::ONE,
    };
    assert_eq!(
        hello::quad_vertex(0, &input),
        (Vec4::new(1.0, 1.0, 0.0, 1.0), Vec2::new(0.0, 1.0))
    );
    assert_eq!(
        hello::quad_vertex(2, &input),
        (Vec4::new(3.0, 3.0, 0.0, 1.0), Vec2::new(1.0, 0.0))
    );
    assert_eq!(hello::screen_vertex(0), Vec4::new(-1.0, -1.0, 0.5, 1.0));
    assert_eq!(hello::screen_vertex(6), hello::screen_vertex(0));
}

#[test]
fn s2h_hello_source_text_depth_and_clip_w() {
    let dimensions = Vec2::new(800.0, 600.0);
    for compute in [false, true] {
        assert_eq!(
            hello::screen(Vec2::splat(10.5), dimensions, compute),
            Vec4::ONE
        );
        let c = hello::screen(Vec2::new(400.0, 300.0), dimensions, compute);
        assert_eq!(c, Vec4::new(0.5, 0.5, 0.0, 1.0));
    }
    let position = Vec4::new(0.0, 0.0, 0.025, 4.375);
    assert_eq!(
        hello::quad(Vec2::new(0.25, 0.81), position, Vec3::ZERO),
        Vec3::splat(0.025).extend(1.0)
    );
    assert_eq!(
        hello::quad(Vec2::new(0.75, 0.81), position, Vec3::ZERO),
        Vec3::splat(0.375).extend(1.0)
    );
}
