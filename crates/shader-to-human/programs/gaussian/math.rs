//! SplatCommon.hlsl and the raster helpers at ShaderToHuman d6f98b7d.
use glam::{Mat2, Mat3};
use shader_to_human::{Mat4, Vec2, Vec3, Vec4};

pub const ALPHA_CUTOFF: f32 = 1.0 / 256.0;
pub const FIXUP_MUL: f32 = 0.25;

pub fn cutoff_scale() -> f32 {
    libm::sqrtf(2.0 * libm::logf(1.0 / ALPHA_CUTOFF))
}
fn log2_e() -> f32 {
    libm::log2f(core::f32::consts::E)
}
pub fn saturate(value: f32) -> f32 {
    if value.is_nan() {
        0.0
    } else {
        value.clamp(0.0, 1.0)
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Splat {
    pub color_alpha: Vec4,
    pub position: Vec3,
    /// Source ordering is real,x,y,z, unlike glam::Quat's x,y,z,real.
    pub rotation: Vec4,
    pub scale: Vec3,
}

#[derive(Clone, Copy, Debug)]
pub struct Camera {
    pub world_to_clip: Mat4,
    pub view_to_clip: Mat4,
    pub world_to_view: Mat4,
    pub dimensions: Vec2,
}

pub fn construct_srt(scale: Vec3, rotation: Mat3, translation: Vec3) -> Mat4 {
    Mat4::from_translation(translation) * (Mat4::from_mat3(rotation) * Mat4::from_scale(scale))
}

pub fn conic_pixels(
    object_to_world: Mat4,
    camera: &Camera,
    pixel_size: f32,
    coc: f32,
    alpha: &mut f32,
) -> Vec3 {
    let world = Mat3::from_mat4(object_to_world);
    let world_sigma = world * world.transpose();
    let view = Mat3::from_mat4(camera.world_to_view);
    let view_sigma = view * (world_sigma * view.transpose());
    let mut center =
        (camera.world_to_view * object_to_world.w_axis.truncate().extend(1.0)).truncate();
    let projection = camera.view_to_clip;
    let limits = Vec2::new(1.3 / projection.x_axis.x, 1.3 / projection.y_axis.y);
    center.x = (center.x / center.z).clamp(-limits.x, limits.x) * center.z;
    center.y = (center.y / center.z).clamp(-limits.y, limits.y) * center.z;
    // The source intentionally uses projection[1][1] for both focal lengths.
    let focal = projection.y_axis.y;
    let jacobian = Mat3::from_cols(
        Vec3::new(focal / center.z, 0.0, 0.0),
        Vec3::new(0.0, focal / center.z, 0.0),
        Vec3::new(
            -focal * center.x / (center.z * center.z),
            -focal * center.y / (center.z * center.z),
            0.0,
        ),
    );
    let clip_sigma = jacobian * (view_sigma * jacobian.transpose());
    let mut covariance =
        Mat2::from_cols(clip_sigma.x_axis.truncate(), clip_sigma.y_axis.truncate());
    let inverse_y = 1.0 / camera.dimensions.y;
    let inverse_y_squared = inverse_y * inverse_y;
    covariance.x_axis.x += pixel_size * pixel_size * inverse_y_squared;
    covariance.y_axis.y += pixel_size * pixel_size * inverse_y_squared;
    let before = covariance.determinant();
    covariance.x_axis.x += coc * coc * inverse_y_squared;
    covariance.y_axis.y += coc * coc * inverse_y_squared;
    *alpha *= before / covariance.determinant();
    let inverse = covariance.inverse();
    let conic = Vec3::new(inverse.x_axis.x, 2.0 * inverse.y_axis.x, inverse.y_axis.y);
    let inv_aspect = projection.y_axis.y / projection.x_axis.x;
    let adjusted = conic * Vec3::new(inv_aspect * inv_aspect, inv_aspect, 1.0);
    let pixels = Vec2::new(2.0, -2.0) / camera.dimensions;
    adjusted
        * Vec3::new(
            pixels.x * pixels.x,
            pixels.x * pixels.y,
            pixels.y * pixels.y,
        )
}

pub fn view_depth(device_depth: f32, projection: Mat4) -> f32 {
    projection.w_axis.z / (device_depth * projection.z_axis.w + projection.z_axis.z)
}
pub fn device_depth(view_depth: f32, projection: Mat4) -> f32 {
    (projection.w_axis.z / view_depth - projection.z_axis.z) / projection.z_axis.w
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Raster {
    pub color_alpha: Vec4,
    pub center: Vec2,
    pub splat_z: Vec2,
    pub conic_mul: Vec3,
}
impl Raster {
    pub fn setup(
        rotation_scale: Mat3,
        position: Vec3,
        width_z: f32,
        mut color_alpha: Vec4,
        camera: &Camera,
        coc: f32,
    ) -> Self {
        let object = construct_srt(Vec3::ONE, rotation_scale, position);
        let homogeneous = camera.world_to_clip * position.extend(1.0);
        let clip = homogeneous.truncate() / homogeneous.w;
        color_alpha.w = color_alpha.w.max(0.0);
        let conic = conic_pixels(object, camera, 1.0, coc, &mut color_alpha.w);
        Self {
            color_alpha,
            center: (clip.truncate() * Vec2::new(0.5, -0.5) + Vec2::splat(0.5)) * camera.dimensions,
            splat_z: Vec2::new(-view_depth(clip.z, camera.view_to_clip), width_z),
            conic_mul: (-0.5 * conic) * log2_e(),
        }
    }

    pub fn evaluate(self, pixel: Vec2) -> Vec4 {
        let d = pixel - self.center;
        let power = self
            .conic_mul
            .dot(Vec3::new(d.x * d.x, d.x * d.y, d.y * d.y));
        let alpha = saturate(libm::exp2f(power));
        self.color_alpha
            .truncate()
            .extend(self.color_alpha.w * if alpha < ALPHA_CUTOFF { 0.0 } else { alpha })
    }

    pub fn original_conic(self) -> Vec3 {
        (-(self.conic_mul / log2_e()) / 0.5) * Vec3::new(1.0, 0.5, 1.0)
    }

    pub fn aabb(self) -> Vec4 {
        let conic = self.original_conic();
        let (a, b, c) = (conic.x, conic.y, conic.z);
        let k = cutoff_scale();
        let half = Vec2::new(
            k / libm::sqrtf(a - b * b / c),
            k / libm::sqrtf(c - b * b / a),
        );
        let min = self.center - half;
        let max = self.center + half;
        Vec4::new(min.x, min.y, max.x, max.y)
    }

    pub fn to_interpolators(self) -> (Vec4, Vec4, Vec3) {
        (
            self.color_alpha,
            Vec4::new(self.center.x, self.center.y, self.splat_z.x, self.splat_z.y),
            self.conic_mul,
        )
    }
    pub fn from_interpolators(a: Vec4, b: Vec4, c: Vec3) -> Self {
        Self {
            color_alpha: a,
            center: b.truncate().truncate(),
            splat_z: Vec2::new(b.z, b.w),
            conic_mul: c,
        }
    }
}

pub fn quaternion_matrix(quaternion: Vec4) -> Mat3 {
    let q = quaternion.normalize();
    let (r, x, y, z) = (q.x, q.y, q.z, q.w);
    Mat3::from_cols(
        Vec3::new(
            1.0 - 2.0 * (y * y + z * z),
            2.0 * (x * y - r * z),
            2.0 * (x * z + r * y),
        ),
        Vec3::new(
            2.0 * (x * y + r * z),
            1.0 - 2.0 * (x * x + z * z),
            2.0 * (y * z - r * x),
        ),
        Vec3::new(
            2.0 * (x * z - r * y),
            2.0 * (y * z + r * x),
            1.0 - 2.0 * (x * x + y * y),
        ),
    )
    .transpose()
}

pub fn procedural_splat(id: u32, offset: Vec3) -> Splat {
    let angle = id as f32 / 6.0 * core::f32::consts::PI * 2.0;
    let sin = libm::sinf(angle);
    let cos = libm::cosf(angle);
    let color = if id == 0 {
        Vec3::ONE
    } else {
        Vec3::new(sin, 0.5, cos) * 0.3 + Vec3::splat(0.3)
    };
    Splat {
        color_alpha: color.extend(1.0),
        position: Vec3::new(4.0, -1.0, 4.0) + offset + Vec3::new(sin, 0.0, cos) * 2.0,
        rotation: Vec4::new(1.0, 2.0, 3.0, 1.0).normalize(),
        scale: Vec3::new(1.0, 2.0, 3.0) * 0.4,
    }
}

pub fn splat_base(splat: Splat) -> Mat4 {
    let matrix = quaternion_matrix(splat.rotation) * Mat3::from_diagonal(splat.scale);
    Mat4::from_cols(
        matrix.x_axis.extend(0.0),
        matrix.y_axis.extend(0.0),
        matrix.z_axis.extend(0.0),
        splat.position.extend(1.0),
    )
}

pub fn inverse_srt(scale: Vec3, rotation: Mat3, translation: Vec3) -> Mat4 {
    let matrix = Mat3::from_diagonal(Vec3::ONE / scale) * rotation.transpose();
    Mat4::from_mat3(matrix) * Mat4::from_translation(-translation)
}

pub fn rasterize(splat: Splat, camera: &Camera) -> (Raster, bool) {
    let rotation_scale = quaternion_matrix(splat.rotation) * Mat3::from_diagonal(splat.scale);
    let raster = Raster::setup(
        rotation_scale,
        splat.position,
        splat.scale.x.abs(),
        splat.color_alpha,
        camera,
        0.0,
    );
    (raster, raster.splat_z.x < 0.0)
}

pub fn hit_sphere(center: Vec3, radius: f32, origin: Vec3, direction: Vec3) -> Vec2 {
    let oc = origin - center;
    let b = oc.dot(direction);
    let qc = oc - b * direction;
    let h = radius * radius - qc.dot(qc);
    if h < 0.0 {
        Vec2::splat(-1.0)
    } else {
        let h = libm::sqrtf(h);
        Vec2::new(-b - h, -b + h)
    }
}

pub fn ray_cast(
    origin: Vec3,
    direction: Vec3,
    splat: Splat,
    random_depth: &mut f32,
    max_t: f32,
) -> Vec4 {
    let mut scale = splat.scale * cutoff_scale();
    // Retain source ordering: AA enlargement changes opacity, not this matrix.
    let world_to_object = inverse_srt(scale, quaternion_matrix(splat.rotation), splat.position);
    let distance = (splat.position - origin).length();
    let before = scale.dot(scale);
    scale = scale.max(Vec3::splat(distance * 0.0001));
    let alpha_adjustment = libm::powf(before / scale.dot(scale), 1.5);
    let pos = (world_to_object * origin.extend(1.0)).truncate();
    let dir = Mat3::from_mat4(world_to_object) * direction;
    let mut hit = hit_sphere(Vec3::ZERO, 1.0, pos, dir.normalize());
    if hit.y > 0.0 {
        hit /= dir.length();
        let ray = dir.normalize();
        let closest = (pos + ray * ray.dot(-pos)).length() * cutoff_scale();
        let alpha = libm::expf(-0.5 * closest * closest);
        let mut output = splat
            .color_alpha
            .truncate()
            .extend(splat.color_alpha.w * alpha * alpha_adjustment);
        let start = saturate(-hit.x / (hit.y - hit.x));
        let end = saturate((max_t - hit.x) / (hit.y - hit.x));
        let smooth = |v: f32| v * v * (3.0 - 2.0 * v);
        output.w *= smooth(end) - smooth(start);
        if *random_depth != f32::MAX {
            *random_depth = hit.x + (hit.y - hit.x) * *random_depth;
        }
        output
    } else {
        if *random_depth != f32::MAX {
            *random_depth = f32::MAX;
        }
        Vec4::ZERO
    }
}

pub fn init_random(val0: u32, val1: u32, backoff: u32) -> u32 {
    let (mut v0, mut v1, mut s0) = (val0, val1, 0_u32);
    for _ in 0..backoff {
        s0 = s0.wrapping_add(0x9e3779b9);
        v0 = v0.wrapping_add(
            (v1.wrapping_shl(4).wrapping_add(0xa341316c))
                ^ v1.wrapping_add(s0)
                ^ (v1 >> 5).wrapping_add(0xc8013ea4),
        );
        v1 = v1.wrapping_add(
            (v0.wrapping_shl(4).wrapping_add(0xad90777d))
                ^ v0.wrapping_add(s0)
                ^ (v0 >> 5).wrapping_add(0x7e95761e),
        );
    }
    v0
}
pub fn next_random(state: &mut u32) -> f32 {
    *state = 1664525_u32.wrapping_mul(*state).wrapping_add(1013904223);
    (*state & 0x00ffffff) as f32 / 0x01000000_u32 as f32
}

pub fn corner_pixels(xy: Vec2, conic: Vec3) -> Vec2 {
    let (a, b, c) = (conic.x, 0.5 * conic.y, conic.z);
    let base_half = (a + c) * 0.5;
    let root_half = 0.5 * libm::sqrtf((a - c) * (a - c) + 4.0 * b * b);
    let rx = 1.0 / libm::sqrtf(base_half + root_half);
    let ry = 1.0 / libm::sqrtf(base_half - root_half);
    let k = Vec2::new(a - c, 2.0 * b);
    let axis0 = (k + Vec2::new(k.length(), 0.0)).normalize();
    let axis1 = Vec2::new(axis0.y, -axis0.x);
    axis0 * (rx * xy.x) + axis1 * (ry * xy.y)
}

pub fn coverage(alpha: f32, pixel: [u32; 2], splat_id: u32, random_frame: u32) -> u32 {
    let seed = pixel[0]
        .wrapping_mul(82927)
        .wrapping_add(pixel[1].wrapping_mul(21313));
    let seed2 = splat_id
        .wrapping_mul(12345)
        .wrapping_add(0x12345678)
        .wrapping_add(random_frame);
    let mut state = init_random(seed, seed2, 16);
    let random = next_random(&mut state);
    // Active source code uses eight bits even for the MSAA=1 variant.
    let step = libm::floorf(saturate(alpha) * 7.0 + random) as u32;
    let mut mask = 255_u32 >> (8 - step);
    for _ in 0..20 {
        let bit = libm::floorf(next_random(&mut state) * 8.0) as u32;
        let old0 = mask & 1 != 0;
        let old_n = mask & (1 << bit) != 0;
        mask &= !1;
        mask &= !(1 << bit);
        if old0 {
            mask |= 1 << bit;
        }
        if old_n {
            mask |= 1;
        }
        mask |= mask << 8;
        mask = (mask >> 1) & 255;
    }
    mask |= mask << 8;
    mask >> ((8.0 - 0.001) * random) as u32
}
