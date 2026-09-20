//! Scatter drawing translated from include/s2h_scatter.hlsl at d6f98b7d.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! BSD-3-Clause, see legal/licenses/ShaderToHuman-BSD-3-Clause.txt.
use crate::{
    math::{frac, saturate},
    Font, IVec2, MiniFont, Vec2, Vec4,
};

/// The output callback is onGfxForAllScatter from the source library. The
/// caller owns clipping, output storage and any synchronization it requires.
#[derive(Clone, Copy, Debug)]
pub struct ContextScatter<F = MiniFont> {
    pub text_color: Vec4,
    pub cursor: IVec2,
    pub left_x: i32,
    pub scale: i32,
    pub font: F,
}

impl Default for ContextScatter {
    fn default() -> Self {
        Self::with_font(MiniFont)
    }
}

impl<F: Font> ContextScatter<F> {
    pub fn with_font(font: F) -> Self {
        Self {
            text_color: Vec4::ONE,
            cursor: IVec2::ZERO,
            left_x: 0,
            scale: 1,
            font,
        }
    }

    pub fn set_cursor(&mut self, position: Vec2) {
        self.cursor = position.as_ivec2();
        self.left_x = position.x as i32;
    }

    pub fn set_scale(&mut self, scale: u32) {
        self.scale = scale as i32;
    }

    fn advance(&mut self, cells: i32) {
        self.cursor.x = self
            .cursor
            .x
            .wrapping_add(8_i32.wrapping_mul(self.scale).wrapping_mul(cells));
    }

    fn position(&self, x: i32, y: i32) -> IVec2 {
        IVec2::new(self.cursor.x.wrapping_add(x), self.cursor.y.wrapping_add(y))
    }

    pub fn print_character(&mut self, ascii: u32, output: &mut impl FnMut(IVec2, Vec4)) {
        // Scatter deliberately uses the source's fixed 8-pixel cell, even for
        // a custom font. A zero/negative span does not enter the source loops.
        let span = 8_i32.wrapping_mul(self.scale);
        for y in 0..span {
            for x in 0..span {
                if self.font.lookup(ascii, IVec2::new(x, y) / self.scale) {
                    output(self.position(x, y), self.text_color);
                }
            }
        }
        self.advance(1);
    }

    pub fn print_text<const N: usize>(
        &mut self,
        characters: &[u32; N],
        output: &mut impl FnMut(IVec2, Vec4),
    ) {
        let mut index = 0;
        while index < N {
            self.print_character(characters[index], output);
            index += 1;
        }
    }

    /// Host convenience. Use fixed `print_text` character arrays in RustGPU.
    pub fn print_ascii(&mut self, characters: &[u8], output: &mut impl FnMut(IVec2, Vec4)) {
        for &character in characters {
            self.print_character(character as u32, output);
        }
    }

    pub fn print_lf(&mut self) {
        self.cursor.x = self.left_x;
        self.cursor.y = self.cursor.y.wrapping_add(8_i32.wrapping_mul(self.scale));
    }

    pub fn print_int(&mut self, value: i32, output: &mut impl FnMut(IVec2, Vec4)) {
        let magnitude = if value < 0 {
            self.print_character(b'-' as u32, output);
            value.wrapping_neg() as u32
        } else {
            value as u32
        };
        if magnitude == 0 {
            self.print_character(b'0' as u32, output);
            return;
        }
        let mut remaining = magnitude;
        while remaining != 0 {
            self.advance(1);
            remaining /= 10;
        }
        // Preserve the source's float backup, including its large-cursor
        // precision limit. It is a distinct source behavior from Gather.
        let end = self.cursor.x as f32;
        remaining = magnitude;
        while remaining != 0 {
            let digit = remaining % 10;
            remaining /= 10;
            self.advance(-1);
            self.print_character(b'0' as u32 + digit, output);
            self.advance(-1);
        }
        self.cursor.x = end as i32;
    }

    pub fn print_hex(&mut self, value: u32, output: &mut impl FnMut(IVec2, Vec4)) {
        for position in (0..8).rev() {
            let nibble = (value >> (position * 4)) & 15;
            let first = if nibble < 10 {
                b'0' as u32
            } else {
                b'A' as u32 - 10
            };
            self.print_character(first + nibble, output);
        }
    }

    pub fn print_float(&mut self, value: f32, output: &mut impl FnMut(IVec2, Vec4)) {
        self.print_int(value as i32, output);
        let mut fractional = frac(value.abs());
        self.print_character(b'.' as u32, output);
        for _ in 0..3 {
            fractional *= 10.0;
            let digit = fractional as u32;
            fractional = frac(fractional);
            self.print_character(b'0' as u32 + digit, output);
        }
    }

    pub fn print_block(&mut self, color: Vec4, output: &mut impl FnMut(IVec2, Vec4)) {
        self.print_shape(color, true, output);
    }

    pub fn print_disc(&mut self, color: Vec4, output: &mut impl FnMut(IVec2, Vec4)) {
        self.print_shape(color, false, output);
    }

    fn print_shape(&mut self, color: Vec4, square: bool, output: &mut impl FnMut(IVec2, Vec4)) {
        let span = 8_i32.wrapping_mul(self.scale);
        for y in 0..span {
            for x in 0..span {
                let local = Vec2::new(x as f32, y as f32) / self.scale as f32 - Vec2::splat(3.5);
                let distance = if square {
                    local.abs().max_element()
                } else {
                    local.length()
                };
                if saturate(4.0 - distance) > 0.0 {
                    output(self.position(x, y), color);
                }
            }
        }
        self.advance(1);
    }

    pub fn draw_crosshair(
        &self,
        center: Vec2,
        radius: f32,
        color: Vec4,
        output: &mut impl FnMut(IVec2, Vec4),
    ) {
        // The center uses text_color, while the arms use the argument color.
        output(center.as_ivec2(), self.text_color);
        let mut i = 1.0;
        while i < radius {
            output((center + Vec2::new(i, 0.0)).as_ivec2(), color);
            output((center + Vec2::new(-i, 0.0)).as_ivec2(), color);
            output((center + Vec2::new(0.0, i)).as_ivec2(), color);
            output((center + Vec2::new(0.0, -i)).as_ivec2(), color);
            i += 1.0;
        }
    }
}
