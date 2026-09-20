//! ShaderToHuman color/math helpers, copyright Electronic Arts Inc.
//! Source: include/s2h.hlsl at d6f98b7d. BSD-3-Clause, see repository notice.
use glam::Vec3;

pub(crate) fn saturate(value: f32) -> f32 {
    // HLSL saturate(NaN) is zero. Rust clamp propagates NaN, which would poison
    // every pixel when the source Features example draws a zero-area arrowhead.
    if value.is_nan() {
        0.0
    } else {
        value.clamp(0.0, 1.0)
    }
}

pub(crate) fn frac(value: f32) -> f32 {
    value - libm::floorf(value)
}

pub fn linear_to_srgb(color: Vec3) -> Vec3 {
    fn convert(value: f32) -> f32 {
        if value <= 0.0031308 {
            value * 12.92
        } else {
            libm::powf(value.abs(), 1.0 / 2.4) * 1.055 - 0.055
        }
    }
    Vec3::new(convert(color.x), convert(color.y), convert(color.z))
}

pub fn srgb_to_linear(color: Vec3) -> Vec3 {
    fn convert(value: f32) -> f32 {
        if value <= 0.04045 {
            value / 12.92
        } else {
            libm::powf((value + 0.055) / 1.055, 2.4)
        }
    }
    Vec3::new(convert(color.x), convert(color.y), convert(color.z))
}

pub fn index_to_color(index: u32) -> Vec3 {
    let channel = |offset: u32| {
        (((index >> offset) & 1) * 4
            + ((index >> (offset + 3)) & 1) * 2
            + ((index >> (offset + 6)) & 1)) as f32
            / 7.0
    };
    Vec3::new(channel(0), channel(1), channel(2))
}

pub fn color_ramp_rgb(value: f32) -> Vec3 {
    Vec3::new(
        saturate(1.0 - value.abs() * 2.0),
        saturate(1.0 - (value - 0.5).abs() * 2.0),
        saturate(1.0 - (value - 1.0).abs() * 2.0),
    )
}
