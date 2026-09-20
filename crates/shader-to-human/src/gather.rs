//! Gather drawing translated from include/s2h.hlsl at d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
use crate::{
    math::{frac, saturate},
    Font, IVec4, MiniFont, Vec2, Vec3, Vec4,
};

#[derive(Clone, Copy, Debug)]
pub struct Triangle {
    pub a: Vec2,
    pub b: Vec2,
    pub c: Vec2,
}

/// Per-pixel drawing state. The destination is premultiplied RGBA.
/// A custom Font is the source library's S2H_DISABLE_EMBEDDED_FONT hook.
#[derive(Clone, Copy, Debug)]
pub struct ContextGather<F = MiniFont> {
    pub cursor: Vec2,
    pub scale: f32,
    pub mouse_input: Vec4,
    pub left_x: f32,
    pub pixel: Vec2,
    pub color: Vec4,
    pub text_color: Vec4,
    pub frame_fill_color: Vec4,
    pub frame_border_color: Vec4,
    pub button_color: Vec4,
    pub line_width: f32,
    pub state: IVec4,
    pub font: F,
}

impl ContextGather {
    /// Supply the source's pixel-centered position, usually integer + 0.5.
    pub fn new(pixel: Vec2) -> Self {
        Self::with_font(pixel, MiniFont)
    }
}

impl<F: Font> ContextGather<F> {
    pub fn with_font(pixel: Vec2, font: F) -> Self {
        Self {
            cursor: Vec2::ZERO,
            scale: 1.0,
            mouse_input: Vec4::new(-100.0, -100.0, 0.0, 0.0),
            left_x: 0.0,
            pixel,
            color: Vec4::ZERO,
            text_color: Vec4::ONE,
            frame_fill_color: Vec4::new(0.9, 0.9, 0.9, 1.0),
            frame_border_color: Vec4::new(0.7, 0.7, 0.7, 1.0),
            button_color: Vec4::new(0.5, 0.5, 0.5, 1.0),
            line_width: 2.0,
            state: IVec4::ZERO,
            font,
        }
    }

    pub fn set_cursor(&mut self, cursor: Vec2) {
        self.cursor = cursor;
        self.left_x = cursor.x;
    }

    pub fn set_scale(&mut self, scale: f32) {
        self.scale = scale;
    }

    pub fn deinit(&mut self) -> IVec4 {
        if self.mouse_input.x != -100.0 && self.mouse_input.z == 0.0 {
            self.state = IVec4::ZERO;
        }
        self.state
    }

    pub(crate) fn over(&mut self, color: Vec4, mask: f32) {
        let weight = color.w * mask;
        // Keep the source's explicit lerp, including its premultiplied alpha.
        self.color = self.color * (1.0 - weight) + color.truncate().extend(1.0) * weight;
    }

    pub fn print_character(&mut self, ascii: u32) {
        let local = ((self.pixel - self.cursor) / self.scale).floor().as_ivec2();
        if self.font.lookup(ascii, local) {
            self.over(self.text_color, 1.0);
        }
        self.cursor.x += self.font.size() * self.scale;
    }

    /// Equivalent to the source's one-through-six argument overloads.
    /// Zero and unsupported codes still consume a character cell.
    pub fn print_text<const N: usize>(&mut self, characters: &[u32; N]) {
        let mut index = 0;
        while index < N {
            self.print_character(characters[index]);
            index += 1;
        }
    }

    /// Host convenience. RustGPU callers use `print_text(&text!("hello"))`
    /// because the current backend does not support unsized slice iteration.
    pub fn print_ascii(&mut self, characters: &[u8]) {
        for &character in characters {
            self.print_character(character as u32);
        }
    }

    pub fn print_space(&mut self, characters: f32) {
        self.cursor.x += self.font.size() * characters * self.scale;
    }

    pub fn print_lf(&mut self) {
        self.cursor.x = self.left_x;
        self.cursor.y += self.font.size() * self.scale;
    }

    pub fn print_int(&mut self, value: i32) {
        let magnitude = if value < 0 {
            self.print_character(b'-' as u32);
            value.wrapping_neg() as u32
        } else {
            value as u32
        };
        if magnitude == 0 {
            self.print_character(b'0' as u32);
            return;
        }
        let advance = self.font.size() * self.scale;
        let mut remaining = magnitude;
        while remaining != 0 {
            self.cursor.x += advance;
            remaining /= 10;
        }
        let end = self.cursor.x;
        remaining = magnitude;
        while remaining != 0 {
            let digit = remaining % 10;
            remaining /= 10;
            self.cursor.x -= advance;
            self.print_character(b'0' as u32 + digit);
            self.cursor.x -= advance;
        }
        self.cursor.x = end;
    }

    pub fn print_hex(&mut self, value: u32) {
        for position in (0..8).rev() {
            let nibble = (value >> (position * 4)) & 15;
            let first = if nibble < 10 {
                b'0' as u32
            } else {
                b'A' as u32 - 10
            };
            self.print_character(first + nibble);
        }
    }

    /// Source formatting truncates to three decimal digits and prints the
    /// integer sign. In particular, -0.5 prints "0.500", as in ShaderToHuman.
    pub fn print_float(&mut self, value: f32) {
        self.print_int(value as i32);
        let mut fractional = frac(value.abs());
        self.print_character(b'.' as u32);
        for _ in 0..3 {
            fractional *= 10.0;
            let digit = fractional as u32;
            fractional = frac(fractional);
            self.print_character(b'0' as u32 + digit);
        }
    }

    pub fn print_box(&mut self, color: Vec4) {
        let local = (self.pixel - self.cursor) / self.scale - Vec2::splat(4.0);
        if saturate(4.0 - local.abs().max_element()) > 0.0 {
            self.over(color, 1.0);
        }
        self.cursor.x += self.font.size() * self.scale;
    }

    pub fn print_disc(&mut self, color: Vec4) {
        let local = (self.pixel - self.cursor) / self.scale - Vec2::splat(4.0);
        if saturate(4.0 - local.length()) > 0.0 {
            self.over(color, 1.0);
        }
        self.cursor.x += self.font.size() * self.scale;
    }

    pub fn draw_disc(&mut self, center: Vec2, radius: f32, color: Vec4) {
        self.over(color, saturate(radius - (self.pixel - center).length()));
    }

    pub fn draw_circle(&mut self, center: Vec2, radius: f32, color: Vec4) {
        let half_width = self.line_width * 0.5;
        let distance = radius - (self.pixel - center).length();
        self.over(
            color,
            saturate(distance + half_width) * (1.0 - saturate(distance - half_width)),
        );
    }

    pub fn draw_half_space(
        &mut self,
        plane: Vec3,
        point: Vec2,
        color: Vec4,
        circle_radius: f32,
        line_radius: f32,
    ) {
        let plane = plane / plane.truncate().length();
        let projected = point - plane.truncate() * plane.dot(point.extend(1.0));
        let distance = plane.dot(self.pixel.extend(1.0));
        let radial = (projected - self.pixel).length();
        let side = saturate(distance);
        let line = saturate(self.line_width - (distance - self.line_width).abs())
            * saturate(line_radius - radial);
        self.over(color, line.max(saturate(circle_radius - radial) * side));
    }

    pub fn draw_rectangle(&mut self, min: Vec2, max: Vec2, color: Vec4) {
        if self.pixel.cmpge(min).all() && self.pixel.cmplt(max).all() {
            self.over(color, 1.0);
        }
    }

    pub fn draw_rectangle_aa(
        &mut self,
        a: Vec2,
        b: Vec2,
        border: Vec4,
        inner: Vec4,
        thickness: f32,
    ) {
        let radius = thickness * 0.5;
        let center = (a + b) * 0.5;
        let half_size = (b - a).abs() * 0.5;
        let offset = (self.pixel - center).abs() - half_size;
        let outer_mask = saturate(1.0 + radius - offset.max(Vec2::ZERO).length());
        let inner_mask = saturate((offset + Vec2::splat(radius)).max(Vec2::ZERO).length() - 0.5);
        let weight = border.w * inner_mask;
        let color = inner * (1.0 - weight) + border.truncate().extend(1.0) * weight;
        self.over(color, outer_mask);
    }

    pub fn draw_crosshair(&mut self, center: Vec2, radius: f32, color: Vec4) {
        let h = Vec2::new(radius, 0.0);
        let v = Vec2::new(0.0, radius);
        self.draw_line(center - h, center + h, color);
        self.draw_line(center - v, center + v, color);
    }

    pub fn draw_line(&mut self, begin: Vec2, end: Vec2, color: Vec4) {
        let radius = (self.line_width + 1.0) * 0.5;
        let delta = end - begin;
        let length = delta.length();
        if length > 0.01 {
            let tangent = delta / length;
            let normal = Vec2::new(tangent.y, -tangent.x);
            let local = self.pixel - begin;
            let uv = Vec2::new(local.dot(tangent), local.dot(normal));
            let mask = saturate(radius - uv.y.abs())
                * saturate(radius - uv.x + length)
                * saturate(radius + uv.x);
            self.over(color, mask);
        }
    }

    pub fn draw_triangle(&mut self, triangle: Triangle, color: Vec4) {
        let position = self.pixel.extend(1.0);
        let ab = saturate(half_space_plane(triangle.a, triangle.b).dot(position) - 0.5);
        let bc = saturate(half_space_plane(triangle.b, triangle.c).dot(position) - 0.5);
        let ca = saturate(half_space_plane(triangle.c, triangle.a).dot(position) - 0.5);
        self.over(color, ab * bc * ca);
    }

    pub fn draw_arrow(
        &mut self,
        start: Vec2,
        end: Vec2,
        color: Vec4,
        head_length: f32,
        head_width: f32,
    ) {
        let direction = (end - start).normalize();
        let line_end = end - direction * head_length;
        let perpendicular = Vec2::new(direction.y, -direction.x).normalize();
        self.draw_line(start, line_end, color);
        self.draw_triangle(
            Triangle {
                a: line_end - perpendicular * head_width,
                b: line_end + direction * head_length,
                c: line_end + perpendicular * head_width,
            },
            color,
        );
    }

    pub fn draw_srgb_ramp(&mut self, position: Vec2) {
        let position = position.floor() + Vec2::splat(0.5);
        let local = self.pixel - position;
        let mut u = local.x / 256.0;
        if local.y > 16.0 {
            u = libm::floorf(u * 16.0) / 16.0;
        }
        let color = crate::srgb_to_linear(Vec3::splat(u));
        self.draw_rectangle(
            position - Vec2::splat(2.0),
            position + Vec2::new(258.0, 34.0),
            crate::color_ramp_rgb(u).extend(1.0),
        );
        self.draw_rectangle(
            position,
            position + Vec2::new(256.0, 32.0),
            color.extend(1.0),
        );
        let backup = (self.cursor, self.scale, self.text_color, self.left_x);
        self.set_scale(1.0);
        self.text_color = Vec4::ONE;
        self.set_cursor(position + Vec2::new(2.0, 22.0));
        self.print_text(&[48]);
        self.set_cursor(position + Vec2::new(128.0 - 1.5 * 8.0, 22.0));
        self.print_text(&[49, 50, 55]);
        self.text_color = Vec4::new(0.0, 0.0, 0.0, 1.0);
        self.set_cursor(position + Vec2::new(256.0 - 3.2 * 8.0, 22.0));
        self.print_text(&[50, 53, 53]);
        (self.cursor, self.scale, self.text_color, self.left_x) = backup;
    }

    pub fn coordinate_system(
        &mut self,
        origin: Vec2,
        domain: Vec4,
        scale: f32,
        grid_size: f32,
        mut grid_color: Vec4,
        flags: u32,
    ) {
        let origin = origin.floor();
        let y_up = flags & 1 != 0;
        let grid_lines = flags & 2 != 0;
        let color = self.text_color;
        let dots = (self.pixel - origin + Vec2::splat(self.line_width * 0.5)).floor() / grid_size;
        let dots = (dots - dots.floor()) * grid_size;
        let off_x = dots.x > libm::floorf(self.line_width);
        let off_y = dots.y > libm::floorf(self.line_width);
        if (grid_lines && off_x && off_y) || (!grid_lines && (off_x || off_y)) {
            grid_color = Vec4::ZERO;
        }
        let extent = domain * scale + Vec4::new(origin.x, origin.y, origin.x, origin.y);
        self.draw_rectangle(
            Vec2::new(extent.x, extent.y),
            Vec2::new(extent.z, extent.w),
            grid_color,
        );
        let width = self.line_width;
        if dots.x < width {
            self.line_width *= 2.0;
        }
        self.draw_arrow(
            Vec2::new(extent.x, origin.y),
            Vec2::new(extent.z, origin.y),
            color,
            16.0,
            8.0,
        );
        self.line_width = width;
        if dots.y < width {
            self.line_width *= 2.0;
        }
        let (begin, end) = if y_up {
            (extent.w, extent.y)
        } else {
            (extent.y, extent.w)
        };
        self.draw_arrow(
            Vec2::new(origin.x, begin),
            Vec2::new(origin.x, end),
            color,
            16.0,
            8.0,
        );
        self.line_width = width;
    }
}

pub fn half_space_plane(a: Vec2, b: Vec2) -> Vec3 {
    let direction = (a - b).normalize();
    let normal = Vec2::new(-direction.y, direction.x);
    normal.extend(normal.dot(-a))
}

pub fn distance_to_box(position: Vec2, center: Vec2, half_size: Vec2) -> f32 {
    ((position - center).abs() - half_size)
        .max(Vec2::ZERO)
        .max_element()
}
