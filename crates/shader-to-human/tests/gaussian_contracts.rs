//! Independent numeric, source data and bounded parsing contracts.
#![forbid(unsafe_code)]
#[allow(dead_code)]
#[path = "../programs/gaussian/mod.rs"]
mod gaussian;
use gaussian::{math::*, ply};
use glam::{Mat3, Mat4, Vec2, Vec3, Vec4};

fn words(bytes: &[u8]) -> Vec<u32> {
    bytes
        .chunks(4)
        .map(|chunk| {
            let mut word = [0; 4];
            word[..chunk.len()].copy_from_slice(chunk);
            u32::from_le_bytes(word)
        })
        .collect()
}
fn close(actual: f32, expected: f32) {
    assert!(
        (actual - expected).abs() < 2e-6,
        "actual={actual:?}, expected={expected:?}"
    );
}
fn basic() -> Splat {
    Splat {
        color_alpha: Vec4::new(0.2, 0.4, 0.6, 0.8),
        position: Vec3::ZERO,
        rotation: Vec4::new(1.0, 0.0, 0.0, 0.0),
        scale: Vec3::ONE,
    }
}

#[test]
fn gaussian_001_original_ply_header_and_all_records() {
    let bytes =
        include_bytes!("../../../tests/parity/fixtures/shader-to-human/gaussian-debug.ply.bin");
    let values = words(bytes);
    assert_eq!(bytes.len(), 51128);
    let header = ply::parse_header(&values).unwrap();
    assert_eq!(
        header,
        ply::Header {
            header_words: 382,
            stride_words: 62,
            format: 0,
            vertices: 200
        }
    );
    assert_eq!(
        (header.header_words + header.vertices * header.stride_words) as usize,
        values.len()
    );
    for id in 0..200 {
        let a = ply::splat(&values, header, id, Vec3::ZERO).unwrap();
        let b = ply::splat(&values, header, id, Vec3::splat(100.0)).unwrap();
        assert_eq!(a.position, b.position, "source PLY offset is ignored");
        assert!(
            a.position.is_finite()
                && a.color_alpha.is_finite()
                && a.scale.is_finite()
                && a.rotation.is_finite()
        );
        assert!(a.scale.cmpgt(Vec3::ZERO).all());
        assert!((0.0..=1.0).contains(&a.color_alpha.w));
    }
    assert!(ply::splat(&values, header, 200, Vec3::ZERO).is_none());
    assert!(ply::splat(&values[..values.len() - 1], header, 199, Vec3::ZERO).is_none());
    assert!(ply::splat(
        &values,
        ply::Header {
            header_words: u32::MAX,
            ..header
        },
        0,
        Vec3::ZERO
    )
    .is_none());
    assert!(ply::splat(
        &values,
        ply::Header {
            stride_words: u32::MAX,
            ..header
        },
        199,
        Vec3::ZERO
    )
    .is_none());
}

#[test]
fn gaussian_002_ply_field_offsets_and_narrowing() {
    let mut values = [0_u32; 62];
    for (offset, value) in [
        (0, 1.0_f32),
        (1, 2.0),
        (2, 3.0),
        (3, 99.0),
        (6, 1.0),
        (7, 2.0),
        (8, 3.0),
        (54, 0.0),
        (55, 0.0),
        (56, 0.0),
        (57, 0.0),
        (58, 1.0),
        (59, 2.0),
        (60, 3.0),
        (61, 4.0),
    ] {
        values[offset] = value.to_bits();
    }
    let splat = ply::splat(
        &values,
        ply::Header {
            header_words: 0,
            stride_words: 62,
            format: 0,
            vertices: 1,
        },
        0,
        Vec3::ONE,
    )
    .unwrap();
    assert_eq!(splat.position, Vec3::new(1.0, 2.0, 3.0));
    assert_eq!(splat.rotation, Vec4::new(1.0, 2.0, 3.0, 4.0));
    assert_eq!(splat.scale, Vec3::ONE);
    assert_eq!(splat.color_alpha.w, 0.5);
    for (a, b) in splat
        .color_alpha
        .truncate()
        .to_array()
        .into_iter()
        .zip([0.7820948, 1.0641896, 1.3462844])
    {
        close(a, b);
    }
    for (text, wanted) in [("4294967297", 1_u32), ("-1", u32::MAX)] {
        let input =
            words(format!("ply\nelement vertex {text}\nproperty float x\nend_header\n").as_bytes());
        assert_eq!(ply::parse_header(&input).unwrap().vertices, wanted);
    }
}

#[test]
fn gaussian_003_parser_rejects_truncated_headers_and_preserves_cursor_contracts() {
    for bytes in [
        b"".as_slice(),
        b"ply\r\n",
        b"ply\nelement vertex x\nend_header\n",
        b"ply\nproperty float x\n",
        b"ply\nend_header\r\n",
    ] {
        assert!(ply::parse_header(&words(bytes)).is_none(), "{bytes:?}");
    }
    let values = words(b" \t-9223372036854775808\r\nabc\n");
    let mut reader = ply::Reader::new(values.len() as u32);
    let word = |index| values[index as usize];
    reader.whitespace_no_lf(&word);
    assert_eq!(reader.position, 2);
    assert_eq!(reader.int64(&word), Some(i64::MIN));
    assert!(reader.parse_to_end_of_line(&word));
    let position = reader.position;
    assert!(!reader.starts_with(&[97, 98, 100], &word));
    assert_eq!(reader.position, position);
    assert_eq!(reader.int64(&word), None);
    assert_eq!(reader.position, position);
    assert!(reader.starts_with(&[97, 98, 99], &word));
    assert!(reader.parse_to_end_of_line(&word));
    assert!(!reader.parse_to_end_of_line(&word));
    assert_eq!(reader.byte(u32::MAX, &word), 0);
}

#[test]
fn gaussian_004_quaternion_order_and_affine_inverse() {
    assert_eq!(
        quaternion_matrix(Vec4::new(2.0, 0.0, 0.0, 0.0)),
        Mat3::IDENTITY
    );
    let q = Vec4::new(1.0, 0.0, 0.0, 1.0);
    let rotation = quaternion_matrix(q);
    assert!((rotation * Vec3::X - Vec3::Y).length() < 2e-6);
    let scale = Vec3::new(2.0, 3.0, 4.0);
    let translation = Vec3::new(5.0, -6.0, 7.0);
    let forward = construct_srt(scale, rotation, translation);
    let inverse = inverse_srt(scale, rotation, translation);
    let point = Vec4::new(0.25, 0.5, -0.75, 1.0);
    assert!((inverse * (forward * point) - point).length() < 2e-6);
    let splat = Splat {
        rotation: q,
        scale,
        position: translation,
        ..basic()
    };
    assert!((splat_base(splat) * point - forward * point).length() < 2e-6);
}

#[test]
fn gaussian_005_covariance_projection_and_depth_formula() {
    let projection = Mat4::from_cols(
        Vec4::new(0.5, 0.0, 0.0, 0.0),
        Vec4::Y,
        Vec4::new(0.0, 0.0, 2.0, 3.0),
        Vec4::new(0.0, 0.0, 4.0, 0.0),
    );
    let camera = Camera {
        world_to_view: Mat4::IDENTITY,
        view_to_clip: projection,
        world_to_clip: projection,
        dimensions: Vec2::new(200.0, 100.0),
    };
    let object = construct_srt(
        Vec3::new(1.0, 2.0, 3.0),
        Mat3::IDENTITY,
        Vec3::new(0.0, 0.0, 4.0),
    );
    let mut alpha = 0.75;
    let conic = conic_pixels(object, &camera, 1.0, 0.0, &mut alpha);
    close(conic.x, (0.0004_f64 / 0.0626) as f32);
    close(conic.y, 0.0);
    close(conic.z, (0.0004_f64 / 0.2501) as f32);
    close(alpha, 0.75);
    let _ = conic_pixels(object, &camera, 1.0, 3.0, &mut alpha);
    close(
        alpha,
        (0.75_f64 * (0.0626 * 0.2501) / (0.0635 * 0.2510)) as f32,
    );
    close(view_depth(0.5, projection), 8.0 / 7.0);
    close(device_depth(8.0 / 7.0, projection), 0.5);
}

#[test]
fn gaussian_006_raster_gaussian_bounds_and_interpolators() {
    let raster = Raster {
        color_alpha: Vec4::new(0.2, 0.4, 0.6, 0.75),
        center: Vec2::new(10.0, 20.0),
        splat_z: Vec2::new(-4.0, 0.5),
        conic_mul: Vec3::new(-0.5, 0.0, -2.0) * core::f32::consts::LOG2_E,
    };
    assert_eq!(raster.evaluate(raster.center), raster.color_alpha);
    close(
        raster.evaluate(raster.center + Vec2::X).w,
        0.75 * libm::expf(-0.5),
    );
    assert_eq!(raster.evaluate(raster.center + Vec2::splat(100.0)).w, 0.0);
    let bounds = raster.aabb();
    close(bounds.x, 10.0 - cutoff_scale());
    close(bounds.w, 20.0 + cutoff_scale() / 2.0);
    let (a, b, c) = raster.to_interpolators();
    let restored = Raster::from_interpolators(a, b, c);
    assert_eq!(
        restored.evaluate(Vec2::new(9.0, 21.0)),
        raster.evaluate(Vec2::new(9.0, 21.0))
    );
    let corner = corner_pixels(Vec2::ONE, Vec3::new(4.0, 0.0, 1.0));
    close(corner.x, 0.5);
    close(corner.y, -1.0);
}

#[test]
fn gaussian_007_ray_alpha_clipping_and_random_depth() {
    let splat = basic();
    let origin = Vec3::new(0.0, 0.0, -10.0);
    let mut depth = f32::MAX;
    let value = ray_cast(origin, Vec3::Z, splat, &mut depth, f32::MAX);
    close(value.w, 0.8);
    assert_eq!(value.truncate(), splat.color_alpha.truncate());
    assert_eq!(depth, f32::MAX);
    depth = 0.25;
    let _ = ray_cast(origin, Vec3::Z, splat, &mut depth, f32::MAX);
    close(depth, 10.0 - cutoff_scale() * 0.5);
    close(
        ray_cast(Vec3::ZERO, Vec3::Z, splat, &mut depth, f32::MAX).w,
        0.4,
    );
    assert_eq!(ray_cast(Vec3::ZERO, Vec3::Z, splat, &mut depth, 0.0).w, 0.0);
    depth = 0.25;
    assert_eq!(
        ray_cast(
            Vec3::new(100.0, 0.0, -10.0),
            Vec3::Z,
            splat,
            &mut depth,
            f32::MAX
        ),
        Vec4::ZERO
    );
    assert_eq!(depth, f32::MAX);
    assert_eq!(
        hit_sphere(Vec3::ZERO, 1.0, Vec3::new(0.0, 0.0, -2.0), Vec3::Z),
        Vec2::new(1.0, 3.0)
    );
}

#[test]
fn gaussian_008_coverage_preserves_source_seven_of_eight_at_full_alpha() {
    let mut changed = false;
    for x in 0..64 {
        assert_eq!(coverage(0.0, [x, 3], 7, 0) & 255, 0);
        assert_eq!((coverage(1.0, [x, 3], 7, 0) & 255).count_ones(), 7);
        assert_eq!(coverage(f32::NAN, [x, 3], 7, 0) & 255, 0);
        changed |= coverage(0.5, [x, 3], 7, 0) != coverage(0.5, [x, 3], 7, 1);
    }
    assert!(changed, "frame randomization must change the mask sequence");
    let mut random = u32::MAX;
    for _ in 0..100 {
        let value = next_random(&mut random);
        assert!((0.0..1.0).contains(&value));
    }
    assert_eq!(init_random(123, 456, 0), 123);
    close(ply::sigmoid(0.0), 0.5);
    close(ply::unsigmoid(0.5), 0.0);
    assert_eq!(ply::unpack_scale(Vec3::ZERO), Vec3::ONE);
    assert_eq!(ply::pack_scale(Vec3::ONE), Vec3::ZERO);
    let a = procedural_splat(0, Vec3::ZERO);
    assert_eq!(a.position, Vec3::new(4.0, -1.0, 6.0));
    assert_eq!(a.color_alpha, Vec4::ONE);
    let projection = Mat4::from_cols(
        Vec4::X,
        Vec4::Y,
        Vec4::new(0.0, 0.0, -0.001, 1.0),
        Vec4::new(0.0, 0.0, 0.1, 0.0),
    );
    let camera = Camera {
        world_to_clip: projection,
        world_to_view: Mat4::IDENTITY,
        view_to_clip: projection,
        dimensions: Vec2::splat(100.0),
    };
    assert!(rasterize(a, &camera).1);
    assert!(
        !rasterize(
            Splat {
                position: -a.position,
                ..a
            },
            &camera
        )
        .1
    );
}

#[test]
fn gaussian_009_input_abi_and_active_program_outputs() {
    let projection = Mat4::from_cols(
        Vec4::new(0.75, 0.0, 0.0, 0.0),
        Vec4::Y,
        Vec4::new(0.0, 0.0, -0.001, 1.0),
        Vec4::new(0.0, 0.0, 0.1, 0.0),
    );
    let view = Mat4::from_translation(Vec3::new(0.0, 0.0, 10.0));
    let camera = Camera {
        world_to_clip: projection * view,
        view_to_clip: projection,
        world_to_view: view,
        dimensions: Vec2::new(800.0, 600.0),
    };
    let inputs = gaussian::Inputs {
        camera,
        world_from_clip: camera.world_to_clip.inverse(),
        camera_near: Vec4::new(0.0, 0.0, -10.0, 0.1),
        dimensions_time_far: Vec4::new(800.0, 600.0, 0.0, 1000.0),
        mouse: Vec4::ZERO,
        offset: Vec4::ZERO,
        ray_bounds: Vec4::new(0.0, 10000.0, 0.0, 0.0),
        random: glam::UVec4::new(17, 1, 0, 0),
        tweak: Vec4::new(0.0, 1.0, 1.0, 0.0),
    };
    let mut words = [0xa5a5a5a5; gaussian::WORDS];
    inputs.write(&mut words);
    assert_eq!(&words[..4], &[0xa5a5a5a5; 4]);
    assert!(words[gaussian::PLY_OFFSET..]
        .iter()
        .all(|v| *v == 0xa5a5a5a5));
    let restored = gaussian::Inputs::read(&words);
    assert_eq!(restored.random_frame(), 17);
    assert_eq!(restored.camera.world_to_clip, camera.world_to_clip);
    assert_eq!(restored.camera.dimensions, camera.dimensions);
    assert_eq!(restored.tweak, inputs.tweak);
    let mut copy = words;
    restored.write(&mut copy);
    assert_eq!(words, copy);
    // The source quad's two triangles cover the same clip-space square.
    assert_eq!(
        gaussian::fullscreen_vertex(0),
        Vec4::new(-1.0, -1.0, 0.5, 1.0)
    );
    assert_eq!(
        gaussian::fullscreen_vertex(1),
        Vec4::new(1.0, -1.0, 0.5, 1.0)
    );
    assert_eq!(
        gaussian::fullscreen_vertex(2),
        Vec4::new(1.0, 1.0, 0.5, 1.0)
    );
    assert_eq!(
        gaussian::fullscreen_vertex(3),
        gaussian::fullscreen_vertex(2)
    );
    assert_eq!(
        gaussian::fullscreen_vertex(5),
        gaussian::fullscreen_vertex(0)
    );
    let splat = procedural_splat(0, Vec3::ZERO);
    let mut center = Vec4::ZERO;
    for vertex in [0, 1, 2, 4] {
        let (clip, params) = gaussian::splat_vertex(vertex, splat, &camera);
        assert!(clip.is_finite());
        assert_eq!(clip.w, 1.0);
        let pixel = (clip.truncate().truncate() * Vec2::new(0.5, -0.5) + Vec2::splat(0.5))
            * camera.dimensions;
        let delta = pixel - params.center;
        let conic = params.original_conic();
        assert!(conic.y.abs() > 1e-7, "this must exercise a rotated ellipse");
        // Each OBB corner is one cutoff radius along both eigenvectors, hence
        // the quadratic form is2*cutoff^2, independently of its rotation.
        let quadratic = conic.x * delta.x * delta.x
            + 2.0 * conic.y * delta.x * delta.y
            + conic.z * delta.y * delta.y;
        assert!((quadratic - 2.0 * cutoff_scale() * cutoff_scale()).abs() < 0.001);
        center += clip * 0.25;
        let (color, depth, mask) = gaussian::splat_fragment(params.center, params, 0, &inputs);
        assert_eq!(color, Vec4::ONE);
        close(depth, clip.z);
        assert!((mask & 255).count_ones() <= 7);
    }
    let projected = camera.world_to_clip * splat.position.extend(1.0);
    close(center.x, projected.x / projected.w);
    close(center.y, projected.y / projected.w);
    for pixel in [
        glam::UVec2::ZERO,
        glam::UVec2::new(40, 120),
        glam::UVec2::new(399, 299),
        glam::UVec2::new(799, 599),
    ] {
        let clear = gaussian::clear_fragment(pixel, &restored);
        assert!(clear.is_finite());
        assert_eq!(clear.w, 0.5);
        for kind in [0, 1, 3] {
            let base = gaussian::base_image(kind, pixel, &restored);
            assert!(base.is_finite());
            assert_eq!(base.w, 1.0);
            let output = gaussian::compute_image(kind, pixel, base, &restored);
            assert!(output.is_finite());
            assert_eq!(output.w, 1.0);
        }
    }
}

#[test]
fn gaussian_010_eight_sample_resolve_keeps_epsilon_bias_and_ignores_alpha() {
    let mut visited = 0_u32;
    let mixed = gaussian::resolve(|sample| {
        visited |= 1 << sample;
        if sample < 4 {
            Vec4::new(1.0, 0.0, 0.0, 0.0)
        } else {
            Vec4::new(0.0, 1.0, 0.0, 1.0)
        }
    });
    assert_eq!(visited, 255);
    let expected = 1.055 * libm::powf(4.0 / 8.0001, 1.0 / 2.4) - 0.055;
    close(mixed.x, expected);
    close(mixed.y, expected);
    assert_eq!(mixed.z, 0.0);
    assert_eq!(mixed.w, 1.0);
    let white = gaussian::resolve(|_| Vec4::ONE);
    assert!(
        white.x < 1.0 && white.x > 0.9999,
        "source epsilon must remain observable"
    );
    assert_eq!(
        gaussian::resolve(|_| Vec4::ZERO),
        Vec4::new(0.0, 0.0, 0.0, 1.0)
    );
}

#[test]
fn gaussian_011_source_corner_basis_is_degenerate_for_some_axis_aligned_ellipses() {
    // Retain this inherited failure as a future change candidate. The source
    // normalizes k+(length(k),0), which is zero for equal axes and for a<c,b=0.
    assert!(!corner_pixels(Vec2::ONE, Vec3::new(1.0, 0.0, 1.0)).is_finite());
    assert!(!corner_pixels(Vec2::ONE, Vec3::new(1.0, 0.0, 4.0)).is_finite());
    assert!(corner_pixels(Vec2::ONE, Vec3::new(4.0, 0.0, 1.0)).is_finite());
}
