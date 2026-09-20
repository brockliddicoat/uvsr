//! 3D drawing translated from include/s2h_3d.hlsl at d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
//! The source attributes intersection formulas to Inigo Quilez.
use crate::{
    math::{frac, saturate},
    ContextGather, Font, Mat4, MiniFont, Vec2, Vec3, Vec4,
};

#[derive(Clone, Copy, Debug)]
pub struct Context3D {
    pub origin: Vec3,
    pub direction: Vec3,
    pub depth: f32,
    pub color: Vec4,
}

fn sign(value: f32) -> f32 {
    if value > 0.0 {
        1.0
    } else if value < 0.0 {
        -1.0
    } else {
        0.0
    }
}

fn with_normal(distance: f32, normal: Vec3) -> Vec4 {
    Vec4::new(distance, normal.x, normal.y, normal.z)
}

fn shaded(color: Vec4, normal: Vec3) -> Vec4 {
    (color.truncate() * 0.7 + (normal * 0.5 + Vec3::splat(0.5)) * 0.3).extend(color.w)
}

pub fn sphere_intersection(origin: Vec3, direction: Vec3, center: Vec3, radius: f32) -> Vec2 {
    let offset = origin - center;
    let b = offset.dot(direction);
    let c = offset.dot(offset) - radius * radius;
    let h = b * b - c;
    if h < 0.0 {
        return Vec2::splat(-1.0);
    }
    let h = libm::sqrtf(h);
    Vec2::new(-b - h, -b + h)
}

pub fn box_intersection(origin: Vec3, direction: Vec3, half_size: Vec3) -> (Vec2, Vec3) {
    let reciprocal = Vec3::ONE / direction;
    let n = reciprocal * origin;
    let k = reciprocal.abs() * half_size;
    let t1 = -n - k;
    let t2 = -n + k;
    let near = t1.x.max(t1.y).max(t1.z);
    let far = t2.x.min(t2.y).min(t2.z);
    if near > far || far < 0.0 {
        return (Vec2::splat(-1.0), Vec3::ZERO);
    }
    let normal = if near > 0.0 {
        Vec3::new(
            (t1.x >= near) as u32 as f32,
            (t1.y >= near) as u32 as f32,
            (t1.z >= near) as u32 as f32,
        )
    } else {
        Vec3::new(
            (far >= t2.x) as u32 as f32,
            (far >= t2.y) as u32 as f32,
            (far >= t2.z) as u32 as f32,
        )
    } * -Vec3::new(sign(direction.x), sign(direction.y), sign(direction.z));
    (Vec2::new(near, far), normal)
}

pub fn cylinder_intersection(origin: Vec3, direction: Vec3, a: Vec3, b: Vec3, radius: f32) -> Vec4 {
    let ba = b - a;
    let oc = origin - a;
    let baba = ba.dot(ba);
    let bard = ba.dot(direction);
    let baoc = ba.dot(oc);
    let k2 = baba - bard * bard;
    let k1 = baba * oc.dot(direction) - baoc * bard;
    let k0 = baba * oc.dot(oc) - baoc * baoc - radius * radius * baba;
    let h = k1 * k1 - k2 * k0;
    if h < 0.0 {
        return Vec4::splat(-1.0);
    }
    let h = libm::sqrtf(h);
    let t = (-k1 - h) / k2;
    let y = baoc + t * bard;
    if y > 0.0 && y < baba {
        return with_normal(t, (oc + t * direction - ba * y / baba) / radius);
    }
    let t = ((if y < 0.0 { 0.0 } else { baba }) - baoc) / bard;
    if (k1 + k2 * t).abs() < h {
        return with_normal(t, ba * sign(y) / libm::sqrtf(baba));
    }
    Vec4::splat(-1.0)
}

pub fn cylinder_normal(position: Vec3, a: Vec3, b: Vec3, radius: f32) -> Vec3 {
    let pa = position - a;
    let ba = b - a;
    let h = pa.dot(ba) / ba.dot(ba);
    (pa - ba * h) / radius
}

pub fn cone_intersection(
    origin: Vec3,
    direction: Vec3,
    a: Vec3,
    b: Vec3,
    radius_a: f32,
    radius_b: f32,
) -> Vec4 {
    let ba = b - a;
    let oa = origin - a;
    let ob = origin - b;
    let m0 = ba.dot(ba);
    let m1 = oa.dot(ba);
    let m2 = direction.dot(ba);
    let m3 = direction.dot(oa);
    let m5 = oa.dot(oa);
    let m9 = ob.dot(ba);
    if m1 < 0.0 {
        if (oa * m2 - direction * m1).length_squared() < radius_a * radius_a * m2 * m2 {
            return with_normal(-m1 / m2, -ba * (1.0 / libm::sqrtf(m0)));
        }
    } else if m9 > 0.0 {
        let t = -m9 / m2;
        if (ob + direction * t).length_squared() < radius_b * radius_b {
            return with_normal(t, ba * (1.0 / libm::sqrtf(m0)));
        }
    }
    let rr = radius_a - radius_b;
    let hy = m0 + rr * rr;
    let k2 = m0 * m0 - m2 * m2 * hy;
    let k1 = m0 * m0 * m3 - m1 * m2 * hy + m0 * radius_a * (rr * m2 * 1.0);
    let k0 = m0 * m0 * m5 - m1 * m1 * hy + m0 * radius_a * (rr * m1 * 2.0 - m0 * radius_a);
    let h = k1 * k1 - k2 * k0;
    if h < 0.0 {
        return Vec4::splat(-1.0);
    }
    let t = (-k1 - libm::sqrtf(h)) / k2;
    let y = m1 + t * m2;
    if y < 0.0 || y > m0 {
        return Vec4::splat(-1.0);
    }
    with_normal(
        t,
        (m0 * (m0 * (oa + t * direction) + rr * ba * radius_a) - ba * hy * y).normalize(),
    )
}

impl Context3D {
    /// Direction is the source ray direction, normalized by its caller.
    pub fn new(origin: Vec3, direction: Vec3) -> Self {
        Self {
            origin,
            direction,
            depth: f32::MAX,
            color: Vec4::ZERO,
        }
    }

    pub fn draw_aabb(&mut self, center: Vec3, half_size: Vec3, color: Vec4) {
        let (hit, normal) = box_intersection(self.origin - center, self.direction, half_size);
        if hit.y > 0.0 && hit.x < self.depth {
            self.depth = hit.x;
            self.color = color * 0.7 + (normal * 0.5 + Vec3::splat(0.5)).extend(1.0) * 0.3;
        }
    }

    pub fn draw_line(&mut self, from: Vec3, to: Vec3, color: Vec4, thickness: f32) {
        let hit = cylinder_intersection(self.origin, self.direction, from, to, thickness);
        if hit.x > 0.0 && hit.x < self.depth {
            self.depth = hit.x;
            let position = self.origin + self.depth * self.direction;
            self.color = shaded(color, cylinder_normal(position, from, to, thickness));
        }
    }

    pub fn draw_arrow(&mut self, from: Vec3, to: Vec3, color: Vec4, thickness: f32) {
        let hit = cone_intersection(self.origin, self.direction, from, to, thickness, 0.0);
        if hit.x > 0.0 && hit.x < self.depth {
            self.depth = hit.x;
            self.color = shaded(color, Vec3::new(hit.y, hit.z, hit.w));
        }
    }

    pub fn draw_basis(&mut self, world_from_object: Mat4, radius: f32) {
        let origin = world_from_object * Vec4::W;
        let x = world_from_object * Vec4::new(radius, 0.0, 0.0, 1.0);
        let y = world_from_object * Vec4::new(0.0, radius, 0.0, 1.0);
        let z = world_from_object * Vec4::new(0.0, 0.0, radius, 1.0);
        let origin = origin.truncate() / origin.w;
        self.draw_arrow(
            origin,
            x.truncate() / x.w,
            Vec4::new(1.0, 0.0, 0.0, 1.0),
            0.09,
        );
        self.draw_arrow(
            origin,
            y.truncate() / y.w,
            Vec4::new(0.0, 1.0, 0.0, 1.0),
            0.09,
        );
        self.draw_arrow(
            origin,
            z.truncate() / z.w,
            Vec4::new(0.0, 0.0, 1.0, 1.0),
            0.09,
        );
    }

    pub fn draw_sphere(&mut self, position: Vec3, color: Vec4, radius: f32) {
        let hit = sphere_intersection(self.origin, self.direction, position, radius);
        if hit.x > 0.0 && hit.x < self.depth {
            let hit_position = self.origin + hit.x * self.direction;
            self.depth = hit.x;
            self.color = shaded(color, (hit_position - position).normalize());
        }
    }

    pub fn draw_checker_board(&mut self, offset: Vec3) {
        let position = Vec3::new(0.0, -0.2, 0.0) + offset;
        let size = Vec3::new(4.4, 0.2, 4.4);
        let (hit, normal) = box_intersection(self.origin - position, self.direction, size);
        if hit.y > 0.0 && hit.x < self.depth {
            self.depth = hit.x;
            let hit_position = self.origin + hit.x * self.direction;
            let uv = Vec2::new(hit_position.z, hit_position.x);
            let mut value = 1.0;
            if uv.x.abs() < 4.0 && uv.y.abs() < 4.0 {
                value = if frac(libm::floorf(uv.x) * 0.5 + libm::floorf(uv.y) * 0.5) > 0.25 {
                    0.4
                } else {
                    0.6
                };
            }
            let mut color = Vec3::splat(value);
            if uv.x.abs() < (4.0 - uv.y) * 0.1 && uv.y > 0.0 {
                color = Vec3::X;
            }
            if uv.y.abs() < (4.0 - uv.x) * 0.1 && uv.x > 0.0 {
                color = Vec3::Z;
            }
            if uv.dot(uv) < 0.25 {
                color = Vec3::Y;
            }
            self.color = shaded(color.extend(1.0), normal);
        }
    }

    pub fn draw_skybox(&mut self) {
        self.draw_skybox_with_font(MiniFont);
    }

    pub fn draw_skybox_with_font(&mut self, font: impl Font) {
        if self.depth != f32::MAX {
            return;
        }
        let d = self.direction;
        let pi = core::f32::consts::PI;
        let uv = Vec2::new(-libm::atan2f(d.z, d.x) / pi + 1.0, libm::acosf(d.y) / pi);
        let px = uv * Vec2::new(font.size() * 8.0, font.size() * 4.0);
        let tile_x = font.size() * 4.0;
        let mut ui =
            ContextGather::with_font(Vec2::new(frac(px.x / tile_x + 0.5) * tile_x, px.y), font);
        ui.color = Vec3::splat(saturate(1.0 - libm::powf(d.y.abs(), 0.2))).extend(1.0);
        let grid = Vec2::new(frac(ui.pixel.x), frac(ui.pixel.y));
        let grid = grid.min(Vec2::ONE - grid).min_element();
        let weight = 0.07 * saturate(1.0 - grid * 30.0);
        ui.color = (ui.color.truncate() * (1.0 - weight) + Vec3::ONE * weight).extend(ui.color.w);
        let x_axis = d.x.abs() > d.z.abs();
        let positive = if x_axis { d.x > 0.0 } else { d.z > 0.0 };
        ui.set_cursor(Vec2::new(0.0, 12.0));
        ui.text_color = (if x_axis { Vec3::X } else { Vec3::Z }).extend(0.4);
        ui.print_text(&[
            b' ' as u32,
            if positive { b'+' as u32 } else { b'-' as u32 },
            if x_axis { b'X' as u32 } else { b'Z' as u32 },
        ]);
        self.color = ui.color;
    }

    /// Preserves the source sceneWithShadows callback, fixed light, bias and
    /// grey half-intensity shadow. The same concrete scene is evaluated twice.
    pub fn scene_with_shadows(&mut self, mut scene: impl FnMut(&mut Self)) {
        scene(self);
        let lit = self.color;
        if self.depth < f32::MAX {
            let mut shadow = Self::new(
                self.origin + self.depth * self.direction,
                Vec3::new(1.0, 3.0, 2.0).normalize(),
            );
            shadow.origin += 0.001 * shadow.direction;
            scene(&mut shadow);
            let visible = if shadow.depth == f32::MAX { 1.0 } else { 0.0 };
            let shadow_factor = 0.5 - visible * 0.5;
            self.color = (lit.truncate() * (1.0 - shadow_factor)).extend(self.color.w);
        }
    }
}
