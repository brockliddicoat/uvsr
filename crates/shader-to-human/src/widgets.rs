//! Stateful UI and table callbacks translated from include/s2h.hlsl.
//! Copyright (c) 2024-2025 Electronic Arts Inc. All rights reserved.
//! Revision d6f98b7d, BSD-3-Clause, see the repository license notice.
use crate::{distance_to_box, math::saturate, ContextGather, Font, IVec2, Vec2, Vec3, Vec4};

/// Source `s2h_computeDistToBox(p, aabb)` with bounds ordered min.xy, max.xy.
pub fn distance_to_aabb(position: Vec2, bounds: Vec4) -> f32 {
    let min = Vec2::new(bounds.x, bounds.y);
    let max = Vec2::new(bounds.z, bounds.w);
    distance_to_box(position, (min + max) * 0.5, (max - min) * 0.5)
}

use distance_to_aabb as box_distance;

fn bounds(min: Vec2, max: Vec2) -> Vec4 {
    Vec4::new(min.x, min.y, max.x, max.y)
}

impl<F: Font> ContextGather<F> {
    fn under(&mut self, color: Vec4) {
        self.over(color, 1.0 - self.color.w);
    }

    fn text_bounds(&self, width: u32) -> Vec4 {
        bounds(
            self.cursor - Vec2::new(width as f32 * self.font.size() * self.scale, 0.0),
            self.cursor,
        ) + Vec4::new(4.0, 4.0, -4.0, 4.0) * self.scale
    }

    fn slider_bounds(&self, width: u32) -> Vec4 {
        bounds(
            self.cursor,
            self.cursor
                + Vec2::new(width as f32 * self.font.size(), self.font.size() - 2.0) * self.scale,
        ) + Vec4::splat(0.5)
    }

    fn mouse_pixel(&self) -> bool {
        let delta =
            Vec2::new(self.mouse_input.x, self.mouse_input.y) + Vec2::splat(0.5) - self.pixel;
        // HLSL round uses ties-to-even. A value rounds to zero exactly in
        // [-0.5, 0.5], including both endpoints. Rust round uses different ties.
        delta.abs().cmple(Vec2::splat(0.5)).all()
    }

    pub fn frame(&mut self, width: u32) {
        let distance = box_distance(self.pixel, self.text_bounds(width)) / self.scale;
        let mut color = Vec4::ZERO;
        if saturate(4.0 - distance) > 0.0 {
            color = self.frame_border_color;
        }
        if saturate(3.0 - distance) > 0.0 {
            color = self.frame_fill_color;
        }
        self.under(color);
    }

    /// Only the source's mouse-selected pixel returns true. Press/release
    /// policy remains with the caller, just as in the original API.
    pub fn button(&mut self, width: u32) -> bool {
        let bounds = self.text_bounds(width);
        let distance = box_distance(self.pixel, bounds) / self.scale;
        let mouse_over = box_distance(Vec2::new(self.mouse_input.x, self.mouse_input.y), bounds)
            / self.scale
            < 5.0;
        let mut color = Vec4::ZERO;
        if mouse_over && saturate(5.0 - distance) > 0.0 {
            color = Vec4::ONE;
        }
        if saturate(4.0 - distance) > 0.0 {
            color = self.button_color;
        }
        self.under(color);
        mouse_over && self.mouse_pixel()
    }

    pub fn radio_button(&mut self, checked: bool) -> bool {
        self.selection_button(checked, false)
    }

    pub fn check_box(&mut self, checked: bool) -> bool {
        self.selection_button(checked, true)
    }

    fn selection_button(&mut self, checked: bool, square: bool) -> bool {
        let local = (self.pixel - self.cursor - Vec2::splat(0.5)) / self.scale - Vec2::splat(3.5);
        let distance = if square {
            local.abs().max_element()
        } else {
            local.length()
        };
        let mouse = (Vec2::new(self.mouse_input.x, self.mouse_input.y) - self.cursor) / self.scale
            - Vec2::splat(3.5);
        // The source checkbox deliberately shares the circular hover test.
        let mouse_over = mouse.length() < 4.0;
        if mouse_over && saturate(5.0 - distance) > 0.0 {
            self.color = Vec4::ONE;
        }
        if saturate(4.0 - distance) > 0.0 {
            self.over(self.button_color, 1.0);
        }
        if checked && saturate(2.5 - distance) > 0.0 {
            self.over(self.text_color, 1.0);
        }
        self.cursor.x += self.font.size() * self.scale;
        mouse_over && self.mouse_pixel()
    }

    pub fn progress(&mut self, width: u32, fraction: f32) {
        let outer = self.slider_bounds(width);
        let mut inner = outer + Vec4::new(1.0, 1.0, -1.0, -1.0) * self.scale;
        let fraction = saturate(fraction);
        inner.z = inner.x * (1.0 - fraction) + inner.z * fraction;
        let mut color = Vec4::ZERO;
        if box_distance(self.pixel, outer) <= 0.0 {
            color = self.button_color;
        }
        if box_distance(self.pixel, inner) <= 0.0 {
            color = color * (1.0 - self.text_color.w)
                + self.text_color.truncate().extend(1.0) * self.text_color.w;
        }
        self.cursor.x += width as f32 * self.font.size() * self.scale;
        self.under(color);
    }

    pub fn slider_float(&mut self, width: u32, value: &mut f32, min_value: f32, max_value: f32) {
        let outer = self.slider_bounds(width);
        let inner = outer + Vec4::new(1.0, 1.0, -1.0, -1.0) * self.scale;
        let slider_distance = box_distance(self.pixel, outer);
        let inactive = self.state.x == 0 && self.state.y == 0;
        let mouse = Vec2::new(self.mouse_input.x, self.mouse_input.y);
        let current_mouse = if inactive {
            mouse
        } else {
            Vec2::new(self.state.x as f32, self.state.y as f32)
        };
        let mouse_over = box_distance(current_mouse, outer) <= 0.0;
        let mut knob_color = self.text_color.truncate();
        if mouse_over && self.mouse_input.z != 0.0 {
            let fraction = saturate((mouse.x - inner.x) / (inner.z - inner.x));
            *value = min_value * (1.0 - fraction) + max_value * fraction;
            knob_color = Vec3::ONE;
            if inactive {
                self.state.x = mouse.x as i32;
                self.state.y = mouse.y as i32;
            }
        }
        let fraction = saturate((*value - min_value) / (max_value - min_value));
        let range = (width as f32 - 1.0) * self.font.size() * self.scale;
        let position = self.cursor
            + Vec2::new(self.font.size() * 0.5 * self.scale, 0.0)
            + Vec2::new(fraction * range, 3.0 * self.scale);
        let size = Vec2::splat((self.font.size() - 4.0) * 0.5 * self.scale);
        let knob = bounds(position - size, position + size) + Vec4::splat(0.5);
        let mut color = Vec4::ZERO;
        if mouse_over && slider_distance <= 2.0 {
            color = Vec4::ONE;
        }
        if slider_distance <= 0.0 {
            color = self.button_color;
        }
        if box_distance(self.pixel, knob) <= 0.0 {
            color = color * (1.0 - self.text_color.w) + knob_color.extend(1.0) * self.text_color.w;
        }
        self.cursor.x += width as f32 * self.font.size() * self.scale;
        self.under(color);
    }

    pub fn slider_rgb(&mut self, width: u32, value: &mut Vec3) {
        let radius = 3.0 * self.font.size() * 0.5 * self.scale - 1.0;
        let backup = self.button_color;
        let initial = self.cursor;
        let x = initial.x + 3.0 * self.font.size() * self.scale;
        self.cursor.x = x;
        self.button_color = Vec4::new(1.0, 0.1, 0.1, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.x, 0.0, 1.0);
        self.print_lf();
        self.cursor.x = x;
        self.button_color = Vec4::new(0.0, 1.0, 0.0, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.y, 0.0, 1.0);
        self.print_lf();
        self.cursor.x = x;
        self.button_color = Vec4::new(0.2, 0.2, 1.0, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.z, 0.0, 1.0);
        self.print_lf();
        self.draw_disc(initial + Vec2::splat(radius), radius, value.extend(1.0));
        self.cursor = initial + Vec2::new(width as f32 * self.font.size() * self.scale, 0.0);
        self.button_color = backup;
    }

    pub fn slider_rgba(&mut self, width: u32, value: &mut Vec4) {
        let radius = 3.0 * self.font.size() * 0.5 * self.scale - 1.0;
        let backup = self.button_color;
        let initial = self.cursor;
        let x = initial.x + 3.0 * self.font.size() * self.scale;
        self.cursor.x = x;
        self.button_color = Vec4::new(1.0, 0.1, 0.1, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.x, 0.0, 1.0);
        self.print_lf();
        self.cursor.x = x;
        self.button_color = Vec4::new(0.0, 1.0, 0.0, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.y, 0.0, 1.0);
        self.print_lf();
        self.cursor.x = x;
        self.button_color = Vec4::new(0.2, 0.2, 1.0, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.z, 0.0, 1.0);
        self.print_lf();
        self.cursor.x = x;
        self.button_color = Vec4::new(0.5, 0.5, 0.5, 1.0);
        self.slider_float(width.wrapping_sub(3), &mut value.w, 0.0, 1.0);
        self.print_lf();
        self.draw_disc(initial + Vec2::splat(radius), radius, *value);
        self.cursor = initial + Vec2::new(width as f32 * self.font.size() * self.scale, 0.0);
        self.button_color = backup;
    }

    fn table_row(&mut self, background: Vec4, size: IVec2) -> Option<u32> {
        let character_size = self.font.size() * self.scale;
        let local = self.pixel - self.cursor;
        let extent = size.as_vec2() * character_size;
        if local.cmpge(Vec2::ZERO).all() && local.cmplt(extent).all() {
            self.under(background);
            self.cursor.y += libm::floorf(local.y / character_size) * character_size;
            Some((local.y / character_size) as u32)
        } else {
            None
        }
    }

    fn finish_table(&mut self, initial: Vec2, size: IVec2, go_right: bool) {
        let extent = size.as_vec2() * (self.font.size() * self.scale);
        if go_right {
            self.cursor.x = initial.x + extent.x;
        } else {
            self.print_lf();
            self.cursor.y = initial.y + extent.y;
        }
    }

    /// The callback is the source s2h_tableLookupInt hook, with None meaning
    /// the row is absent. It is statically dispatched, without a trait object.
    pub fn table_int(
        &mut self,
        column: u32,
        background: Vec4,
        size: IVec2,
        go_right: bool,
        mut lookup: impl FnMut(u32, u32) -> Option<i32>,
    ) {
        let initial = self.cursor;
        if let Some(row) = self.table_row(background, size) {
            if let Some(value) = lookup(column, row) {
                self.print_int(value);
            }
        }
        self.finish_table(initial, size, go_right);
    }

    pub fn table_float(
        &mut self,
        column: u32,
        background: Vec4,
        size: IVec2,
        go_right: bool,
        mut lookup: impl FnMut(u32, u32) -> Option<f32>,
    ) {
        let initial = self.cursor;
        if let Some(row) = self.table_row(background, size) {
            if let Some(value) = lookup(column, row) {
                self.print_float(value);
            }
        }
        self.finish_table(initial, size, go_right);
    }

    /// The callback preserves s2h_floatLookupFloat's function ID and domain.
    pub fn function(
        &mut self,
        id: u32,
        background: Vec4,
        size: IVec2,
        range_x: Vec2,
        range_y: Vec2,
        mut lookup: impl FnMut(u32, f32) -> f32,
    ) {
        let character_size = self.font.size() * self.scale;
        let initial = self.cursor;
        let local = self.pixel - initial;
        let extent = size.as_vec2() * character_size;
        if local.cmpge(Vec2::ZERO).all() && local.cmplt(extent).all() {
            let fraction = local.x / extent.x;
            let x = range_x.x * (1.0 - fraction) + range_x.y * fraction;
            self.under(background);
            self.cursor.y = initial.y + libm::floorf(local.y / character_size) * character_size;
            let y = lookup(id, x);
            let pixel_y = (1.0 - (y - range_y.x) / (range_y.y - range_y.x)) * extent.y;
            if pixel_y < local.y {
                self.over(self.text_color, 1.0);
            }
        }
        self.print_lf();
        self.cursor.y = initial.y + extent.y;
    }
}
