//! AGFX f91b108a sampler behavior. Copyright (c) 2026 Amélie Heinrich.
//! See legal/licenses/AGFX-MIT.txt and the A-009/A-010/A-015 port notes.
use super::{vk, Device, Error, Lease};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SamplerFilter {
    Nearest,
    Linear,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
}

impl AddressMode {
    fn native(self) -> vk::SamplerAddressMode {
        match self {
            Self::Repeat => vk::SamplerAddressMode::REPEAT,
            Self::MirroredRepeat => vk::SamplerAddressMode::MIRRORED_REPEAT,
            Self::ClampToEdge => vk::SamplerAddressMode::CLAMP_TO_EDGE,
            Self::ClampToBorder => vk::SamplerAddressMode::CLAMP_TO_BORDER,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ComparisonFunction {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    /// AGFX samplers disable comparison for this value. Depth tests use ALWAYS.
    Always,
}

impl ComparisonFunction {
    pub(super) fn native(self) -> vk::CompareOp {
        match self {
            Self::Never => vk::CompareOp::NEVER,
            Self::Less => vk::CompareOp::LESS,
            Self::Equal => vk::CompareOp::EQUAL,
            Self::LessEqual => vk::CompareOp::LESS_OR_EQUAL,
            Self::Greater => vk::CompareOp::GREATER,
            Self::NotEqual => vk::CompareOp::NOT_EQUAL,
            Self::GreaterEqual => vk::CompareOp::GREATER_OR_EQUAL,
            Self::Always => vk::CompareOp::ALWAYS,
        }
    }
}

/// Source sampler fields. `Default` matches AGFX's Cpp create-info, including
/// its zero-initialized Never comparison. Ordinary sampling selects Always.
#[derive(Clone, Copy, Debug)]
pub struct SamplerInfo {
    pub filter: SamplerFilter,
    pub address: [AddressMode; 3],
    pub mip_lod_bias: f32,
    /// Retained source field. The source Vulkan path disables anisotropy.
    pub max_anisotropy: f32,
    pub comparison: ComparisonFunction,
    pub min_lod: f32,
    pub max_lod: f32,
    /// Retained source field, ignored by AGFX Vulkan. Use mip_lod_bias.
    pub lod_bias: f32,
}

impl Default for SamplerInfo {
    fn default() -> Self {
        Self {
            filter: SamplerFilter::Nearest,
            address: [AddressMode::Repeat; 3],
            mip_lod_bias: 0.0,
            max_anisotropy: 1.0,
            comparison: ComparisonFunction::Never,
            min_lod: 0.0,
            max_lod: f32::MAX,
            lod_bias: 0.0,
        }
    }
}

impl SamplerInfo {
    fn native(self, max_bias: f32) -> Result<vk::SamplerCreateInfo<'static>, Error> {
        if !self.mip_lod_bias.is_finite()
            || self.mip_lod_bias.abs() > max_bias
            || !self.min_lod.is_finite()
            || !self.max_lod.is_finite()
            || self.max_lod < self.min_lod
        {
            return Err(Error::Invalid("sampler LOD range or bias is invalid"));
        }
        let (filter, mipmap) = match self.filter {
            SamplerFilter::Nearest => (vk::Filter::NEAREST, vk::SamplerMipmapMode::NEAREST),
            SamplerFilter::Linear => (vk::Filter::LINEAR, vk::SamplerMipmapMode::LINEAR),
        };
        Ok(vk::SamplerCreateInfo::default()
            .mag_filter(filter)
            .min_filter(filter)
            .mipmap_mode(mipmap)
            .address_mode_u(self.address[0].native())
            .address_mode_v(self.address[1].native())
            .address_mode_w(self.address[2].native())
            .mip_lod_bias(self.mip_lod_bias)
            .anisotropy_enable(false)
            .compare_enable(self.comparison != ComparisonFunction::Always)
            .compare_op(self.comparison.native())
            .min_lod(self.min_lod)
            .max_lod(self.max_lod)
            .border_color(vk::BorderColor::FLOAT_TRANSPARENT_BLACK))
    }
}

/// Unique, immutable sampler borrowing its device. Native handles stay private.
pub struct Sampler<'d> {
    pub(super) device: &'d Device,
    pub(super) raw: vk::Sampler,
    info: SamplerInfo,
    _owner_slot: Lease<'d>,
}

impl Sampler<'_> {
    pub fn info(&self) -> SamplerInfo {
        self.info
    }
}

impl Device {
    #[allow(unsafe_code)]
    pub fn sampler(&self, info: SamplerInfo) -> Result<Sampler<'_>, Error> {
        let create = info.native(self.limits.max_sampler_lod_bias)?;
        let slot = self.samplers.acquire()?;
        // SAFETY: U-020. Live device, valid enum values, finite ordered LODs and
        // queried bias/count bounds. Normalized coordinates, no extension chain
        // or optional feature. Anisotropy is disabled as in the source Vulkan path.
        let raw = unsafe { self.raw.create_sampler(&create, None) }?;
        Ok(Sampler {
            device: self,
            raw,
            info,
            _owner_slot: slot,
        })
    }
}

impl Drop for Sampler<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-020. Owned successful creation, live borrowed device. Every
        // use borrows this owner until synchronous completion. No handle escapes.
        unsafe { self.device.raw.destroy_sampler(self.raw, None) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agfx_sampler_lod_limits_and_nonfinite_rejection() {
        let info = SamplerInfo::default();
        assert!(info.native(2.0).is_ok());
        for value in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 2.01, -2.01] {
            assert!(SamplerInfo {
                mip_lod_bias: value,
                ..info
            }
            .native(2.0)
            .is_err());
        }
        for value in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
            assert!(SamplerInfo {
                min_lod: value,
                ..info
            }
            .native(2.0)
            .is_err());
            assert!(SamplerInfo {
                max_lod: value,
                ..info
            }
            .native(2.0)
            .is_err());
        }
        assert!(SamplerInfo {
            min_lod: 2.0,
            max_lod: 1.0,
            ..info
        }
        .native(2.0)
        .is_err());
        assert!(SamplerInfo {
            min_lod: -2.0,
            max_lod: -1.0,
            mip_lod_bias: -2.0,
            ..info
        }
        .native(2.0)
        .is_ok());
    }

    #[test]
    fn agfx_sampler_source_comparison_and_ignored_fields() {
        let info = SamplerInfo::default();
        let native = info.native(2.0).unwrap();
        assert_eq!(native.compare_enable, vk::TRUE);
        assert_eq!(native.compare_op, vk::CompareOp::NEVER);
        let native = SamplerInfo {
            comparison: ComparisonFunction::Always,
            max_anisotropy: 16.0,
            lod_bias: 100.0,
            ..info
        }
        .native(2.0)
        .unwrap();
        assert_eq!(native.compare_enable, vk::FALSE);
        assert_eq!(native.anisotropy_enable, vk::FALSE);
        assert_eq!(native.mip_lod_bias, 0.0);
        assert_eq!(
            native.border_color,
            vk::BorderColor::FLOAT_TRANSPARENT_BLACK
        );
    }
}
