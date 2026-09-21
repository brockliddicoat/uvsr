//! InitPlyCS.hlsl and SplatVS_example.hlsl's binary readers at d6f98b7d.
use super::math::Splat;
use shader_to_human::{Vec3, Vec4};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Header {
    /// Source fields are all u32, including the narrowed int64 parser result.
    pub header_words: u32,
    pub stride_words: u32,
    pub format: u32,
    pub vertices: u32,
}
impl Header {
    pub fn read<const N: usize>(words: &[u32; N]) -> Self {
        Self {
            header_words: words[0],
            stride_words: words[1],
            format: words[2],
            vertices: words[3],
        }
    }
    pub fn write<const N: usize>(self, words: &mut [u32; N]) {
        words[0] = self.header_words;
        words[1] = self.stride_words;
        words[2] = self.format;
        words[3] = self.vertices;
    }
}

pub struct Reader {
    bytes: u32,
    pub position: u32,
}
impl Reader {
    pub fn new(word_count: u32) -> Self {
        assert!(word_count <= u32::MAX / 4);
        Self {
            bytes: word_count * 4,
            position: 0,
        }
    }
    pub fn byte(&self, position: u32, word: &impl Fn(u32) -> u32) -> u32 {
        if position >= self.bytes {
            return 0;
        }
        (word(position / 4) >> ((position % 4) * 8)) & 255
    }
    pub fn whitespace_no_lf(&mut self, word: &impl Fn(u32) -> u32) {
        while matches!(self.byte(self.position, word), 32 | 9) {
            self.position += 1;
        }
    }
    pub fn starts_with<const N: usize>(
        &mut self,
        text: &[u32; N],
        word: &impl Fn(u32) -> u32,
    ) -> bool {
        let backup = self.position;
        let mut index = 0;
        while index < N {
            if self.position >= self.bytes || self.byte(self.position, word) != text[index] {
                self.position = backup;
                return false;
            }
            self.position += 1;
            index += 1;
        }
        true
    }
    pub fn parse_to_end_of_line(&mut self, word: &impl Fn(u32) -> u32) -> bool {
        loop {
            match self.byte(self.position, word) {
                0 => return false,
                13 => {
                    self.position += 1;
                    if self.byte(self.position, word) == 10 {
                        self.position += 1;
                    }
                    return true;
                }
                10 => {
                    self.position += 1;
                    return true;
                }
                _ => self.position += 1,
            }
        }
    }
    pub fn int64(&mut self, word: &impl Fn(u32) -> u32) -> Option<i64> {
        let backup = self.position;
        let negate = self.byte(self.position, word) == 45;
        if negate {
            self.position += 1;
        }
        let first = self.byte(self.position, word);
        // Scalar bounds avoid RustGPU's unsupported RangeInclusive pointer IR.
        if first.wrapping_sub(48) > 9 {
            self.position = backup;
            return None;
        }
        let mut result = 0_i64;
        loop {
            let c = self.byte(self.position, word);
            if c.wrapping_sub(48) > 9 {
                break;
            }
            result = result.wrapping_mul(10).wrapping_add((c - 48) as i64);
            self.position += 1;
        }
        Some(if negate {
            result.wrapping_neg()
        } else {
            result
        })
    }
}

pub fn parse_header(words: &[u32]) -> Option<Header> {
    parse_header_with(u32::try_from(words.len()).ok()?, |index| {
        words[index as usize]
    })
}

/// A bounded word callback avoids unsupported array-to-slice shader casts.
pub fn parse_header_with(word_count: u32, word: impl Fn(u32) -> u32) -> Option<Header> {
    if word_count == 0 || word_count > u32::MAX / 4 || word(0) != 0x0a796c70 {
        return None;
    } // source requires ply + LF
    let mut reader = Reader::new(word_count);
    let mut stride = 0_u32;
    let mut vertices = 0;
    let mut valid = true;
    loop {
        if reader.starts_with(&shader_to_human::text!("elem"), &word)
            && reader.starts_with(&shader_to_human::text!("ent "), &word)
            && reader.starts_with(&shader_to_human::text!("vert"), &word)
            && reader.starts_with(&shader_to_human::text!("ex "), &word)
        {
            if let Some(count) = reader.int64(&word) {
                vertices = count as u32;
            } else {
                valid = false;
            }
            continue;
        }
        if reader.starts_with(&shader_to_human::text!("prop"), &word)
            && reader.starts_with(&shader_to_human::text!("erty"), &word)
            && reader.starts_with(&shader_to_human::text!(" "), &word)
        {
            stride = stride.wrapping_add(1);
        }
        if reader.starts_with(&shader_to_human::text!("end_"), &word)
            && reader.starts_with(&shader_to_human::text!("head"), &word)
            && reader.starts_with(&shader_to_human::text!("er\n"), &word)
        {
            break;
        }
        // The source loops past EOF. Defined failure replaces that invalid read.
        if !reader.parse_to_end_of_line(&word) {
            return None;
        }
    }
    if valid {
        Some(Header {
            header_words: reader.position / 4,
            stride_words: stride,
            format: 0,
            vertices,
        })
    } else {
        None
    }
}

pub fn unpack_scale(value: Vec3) -> Vec3 {
    Vec3::new(
        libm::expf(value.x),
        libm::expf(value.y),
        libm::expf(value.z),
    )
}
pub fn pack_scale(value: Vec3) -> Vec3 {
    Vec3::new(
        libm::logf(value.x),
        libm::logf(value.y),
        libm::logf(value.z),
    )
}
pub fn sigmoid(value: f32) -> f32 {
    1.0 / (1.0 + libm::expf(-value))
}
pub fn unsigmoid(value: f32) -> f32 {
    -libm::logf(1.0 / value - 1.0)
}

pub fn splat(words: &[u32], header: Header, id: u32, _source_offset: Vec3) -> Option<Splat> {
    splat_with(
        u32::try_from(words.len()).ok()?,
        |index| words[index as usize],
        header,
        id,
    )
}

pub fn splat_with(
    word_count: u32,
    word: impl Fn(u32) -> u32,
    header: Header,
    id: u32,
) -> Option<Splat> {
    if id >= header.vertices || header.stride_words < 62 || header.header_words > word_count {
        return None;
    }
    let remaining = word_count - header.header_words;
    // Bound the product before multiplication. The shader backend does not yet
    // support checked_mul, and this proves both the product and final span fit.
    if remaining < 62 || id > (remaining - 62) / header.stride_words {
        return None;
    }
    let start = header.header_words + id * header.stride_words;
    let f = |offset: u32| f32::from_bits(word(start + offset));
    let v3 = |offset| Vec3::new(f(offset), f(offset + 1), f(offset + 2));
    // Retain the source's ignored offset, SH0-only color and skipped normal.
    Some(Splat {
        position: v3(0),
        color_alpha: (v3(6) * 0.2820948 + Vec3::splat(0.5)).extend(sigmoid(f(54))),
        scale: unpack_scale(v3(55)),
        rotation: Vec4::new(f(58), f(59), f(60), f(61)),
    })
}
