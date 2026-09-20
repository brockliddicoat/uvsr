//! Owned texture/copy behavior from AGFX f91b108a's Vulkan implementation.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
//! Single-layer, single-mip 2D images with explicit storage/sampling usage.
use super::{vk, Buffer, Completion, Device, Error, Lease, Recording};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TextureFormat {
    Rgba8Unorm,
    /// Native attachment/sample conversion. Transfer bytes remain encoded.
    Rgba8Srgb,
    Rgba32Float,
    D32Float,
}

impl TextureFormat {
    pub(super) fn native(self) -> vk::Format {
        match self {
            Self::Rgba8Unorm => vk::Format::R8G8B8A8_UNORM,
            Self::Rgba8Srgb => vk::Format::R8G8B8A8_SRGB,
            Self::Rgba32Float => vk::Format::R32G32B32A32_SFLOAT,
            Self::D32Float => vk::Format::D32_SFLOAT,
        }
    }

    fn texel_bytes(self) -> u64 {
        match self {
            Self::Rgba8Unorm | Self::Rgba8Srgb | Self::D32Float => 4,
            Self::Rgba32Float => 16,
        }
    }

    pub fn is_depth(self) -> bool {
        self == Self::D32Float
    }

    fn aspect(self) -> vk::ImageAspectFlags {
        if self.is_depth() {
            vk::ImageAspectFlags::DEPTH
        } else {
            vk::ImageAspectFlags::COLOR
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TextureInfo {
    pub width: u32,
    pub height: u32,
    pub format: TextureFormat,
}

/// Shader uses declared before allocation. Transfer upload/readback is always
/// available. Support is queried for this exact combination, not assumed.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TextureUsage {
    pub storage: bool,
    pub sampled: bool,
    /// Color or depth attachment, selected by the texture's format.
    pub attachment: bool,
}

impl TextureInfo {
    pub fn byte_len(self) -> Result<u64, Error> {
        if self.width == 0 || self.height == 0 {
            return Err(Error::Invalid("texture dimensions must be nonzero"));
        }
        u64::from(self.width)
            .checked_mul(u64::from(self.height))
            .and_then(|pixels| pixels.checked_mul(self.format.texel_bytes()))
            .ok_or(Error::Invalid("texture byte size overflow"))
    }

    fn extent(self) -> vk::Extent3D {
        vk::Extent3D {
            width: self.width,
            height: self.height,
            depth: 1,
        }
    }
}

/// One image, allocation and complete storage view borrowing their device.
/// Native handles never escape. Safe operations initialize every texel and
/// complete before returning, including all layout transitions.
pub struct Texture<'d> {
    pub(super) device: &'d Device,
    pub(super) raw: vk::Image,
    pub(super) view: vk::ImageView,
    allocation: vk::DeviceMemory,
    info: TextureInfo,
    pub(super) usage: TextureUsage,
    pub(super) linear_sampling: bool,
    pub(super) initialized: bool,
    _allocation_slot: Lease<'d>,
}

fn range(format: TextureFormat) -> vk::ImageSubresourceRange {
    vk::ImageSubresourceRange::default()
        .aspect_mask(format.aspect())
        .level_count(1)
        .layer_count(1)
}

/// Source buffer offset, row pitch and single-mip 2D texture region.
/// A zero row pitch means tightly packed rows. Padding remains untouched.
#[derive(Clone, Copy, Debug)]
pub struct TextureCopy {
    pub buffer_offset: u64,
    pub row_bytes: u32,
    pub origin: [u32; 2],
    pub extent: [u32; 2],
}

impl TextureCopy {
    pub fn whole(info: TextureInfo) -> Self {
        Self {
            buffer_offset: 0,
            row_bytes: 0,
            origin: [0, 0],
            extent: [info.width, info.height],
        }
    }

    fn validate(
        self,
        info: TextureInfo,
        buffer_bytes: u64,
    ) -> Result<(vk::BufferImageCopy, bool, bool), Error> {
        info.byte_len()?;
        let [x, y] = self.origin;
        let [width, height] = self.extent;
        if width == 0
            || height == 0
            || x.checked_add(width).is_none_or(|end| end > info.width)
            || y.checked_add(height).is_none_or(|end| end > info.height)
            || x > i32::MAX as u32
            || y > i32::MAX as u32
        {
            return Err(Error::Invalid("texture copy region or offset is invalid"));
        }
        let texel = info.format.texel_bytes();
        let packed = u64::from(width) * texel;
        let stride = if self.row_bytes == 0 {
            packed
        } else {
            u64::from(self.row_bytes)
        };
        // Vulkan copy VUID07975 requires texel-block offset alignment, not
        // merely four-byte alignment. VUID09108 bounds row pitch to i32::MAX.
        if stride < packed
            || stride % texel != 0
            || stride > i32::MAX as u64
            || self.buffer_offset % texel != 0
        {
            return Err(Error::Invalid("texture copy row pitch is invalid"));
        }
        let end = u64::from(height - 1)
            .checked_mul(stride)
            .and_then(|bytes| bytes.checked_add(packed))
            .and_then(|bytes| bytes.checked_add(self.buffer_offset))
            .ok_or(Error::Invalid("texture copy buffer range overflow"))?;
        if end > buffer_bytes {
            return Err(Error::Invalid("texture copy exceeds buffer range"));
        }
        let full_image = self.origin == [0, 0] && self.extent == [info.width, info.height];
        let full_buffer =
            self.buffer_offset == 0 && end == buffer_bytes && (height == 1 || stride == packed);
        let copy = vk::BufferImageCopy::default()
            .buffer_offset(self.buffer_offset)
            .buffer_row_length(if self.row_bytes == 0 {
                0
            } else {
                (stride / texel) as u32
            })
            .image_subresource(
                vk::ImageSubresourceLayers::default()
                    .aspect_mask(info.format.aspect())
                    .layer_count(1),
            )
            .image_offset(vk::Offset3D {
                x: x as i32,
                y: y as i32,
                z: 0,
            })
            .image_extent(vk::Extent3D {
                width,
                height,
                depth: 1,
            });
        Ok((copy, full_image, full_buffer))
    }
}

impl Device {
    pub fn texture(&self, info: TextureInfo) -> Result<Texture<'_>, Error> {
        self.texture_with_usage(
            info,
            TextureUsage {
                storage: true,
                sampled: false,
                attachment: false,
            },
        )
    }

    #[allow(unsafe_code)]
    pub fn texture_with_usage(
        &self,
        info: TextureInfo,
        usage: TextureUsage,
    ) -> Result<Texture<'_>, Error> {
        if !usage.storage && !usage.sampled && !usage.attachment {
            return Err(Error::Invalid(
                "texture view requires storage, sampled or attachment usage",
            ));
        }
        if usage.storage && info.format.is_depth() {
            return Err(Error::Invalid("depth format cannot be a storage image"));
        }
        let bytes = info.byte_len()?;
        if info.width > self.limits.max_image_dimension2_d
            || info.height > self.limits.max_image_dimension2_d
        {
            return Err(Error::Invalid("texture dimensions exceed device limits"));
        }
        let mut native_usage =
            vk::ImageUsageFlags::TRANSFER_SRC | vk::ImageUsageFlags::TRANSFER_DST;
        if usage.storage {
            native_usage |= vk::ImageUsageFlags::STORAGE;
        }
        if usage.sampled {
            native_usage |= vk::ImageUsageFlags::SAMPLED;
        }
        if usage.attachment {
            native_usage |= if info.format.is_depth() {
                vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT
            } else {
                vk::ImageUsageFlags::COLOR_ATTACHMENT
            };
        }
        // SAFETY: U-017. Live retained physical device and a supported enum.
        // Optimal-tiling features describe the actual image's filter support.
        let format_features = unsafe {
            self.instance
                .raw
                .get_physical_device_format_properties(self.physical, info.format.native())
        }
        .optimal_tiling_features;
        // SAFETY: U-017. The retained physical device belongs to this live
        // instance. Only the exact format/type/tiling/usage being created is queried.
        let support = unsafe {
            self.instance
                .raw
                .get_physical_device_image_format_properties(
                    self.physical,
                    info.format.native(),
                    vk::ImageType::TYPE_2D,
                    vk::ImageTiling::OPTIMAL,
                    native_usage,
                    vk::ImageCreateFlags::empty(),
                )
        };
        let support = match support {
            Ok(value) => value,
            Err(vk::Result::ERROR_FORMAT_NOT_SUPPORTED) => {
                return Err(Error::Unsupported(format!(
                    "{:?} image with usage {:?}",
                    info.format, usage
                )))
            }
            Err(error) => return Err(error.into()),
        };
        if info.width > support.max_extent.width
            || info.height > support.max_extent.height
            || bytes > support.max_resource_size
            || support.max_mip_levels == 0
            || support.max_array_layers == 0
            || !support.sample_counts.contains(vk::SampleCountFlags::TYPE_1)
        {
            return Err(Error::Unsupported(
                "texture extent or allocation exceeds format capabilities".into(),
            ));
        }
        let mut owner = Texture {
            device: self,
            raw: vk::Image::null(),
            view: vk::ImageView::null(),
            allocation: vk::DeviceMemory::null(),
            info,
            usage,
            linear_sampling: usage.sampled
                && format_features.contains(vk::FormatFeatureFlags::SAMPLED_IMAGE_FILTER_LINEAR),
            initialized: false,
            _allocation_slot: self.allocations.acquire()?,
        };
        let create = vk::ImageCreateInfo::default()
            .image_type(vk::ImageType::TYPE_2D)
            .format(info.format.native())
            .extent(info.extent())
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL)
            .usage(native_usage)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .initial_layout(vk::ImageLayout::UNDEFINED);
        // SAFETY: U-017. Nonzero extent, exact queried support, one mip/layer,
        // no sparse/external/alias flags. One queue family owns all operations.
        owner.raw = unsafe { self.raw.create_image(&create, None) }?;
        // SAFETY: U-017. Live same-device image, not yet bound or submitted.
        let requirements = unsafe { self.raw.get_image_memory_requirements(owner.raw) };
        let memory_type = (0..self.memory.memory_type_count)
            .find(|&i| {
                requirements.memory_type_bits & (1 << i) != 0
                    && !self.memory.memory_types[i as usize]
                        .property_flags
                        .contains(vk::MemoryPropertyFlags::PROTECTED)
                    && self.memory.memory_types[i as usize]
                        .property_flags
                        .contains(vk::MemoryPropertyFlags::DEVICE_LOCAL)
            })
            .ok_or_else(|| {
                Error::Unsupported("no compatible device-local texture memory".into())
            })?;
        self.validate_allocation(requirements.size, memory_type)?;
        // This owner already has one allocation per image. Mark it dedicated
        // explicitly, satisfying implementations that require a dedicated bind.
        let mut dedicated = vk::MemoryDedicatedAllocateInfo::default().image(owner.raw);
        let allocate = vk::MemoryAllocateInfo::default()
            .allocation_size(requirements.size)
            .memory_type_index(memory_type)
            .push_next(&mut dedicated);
        // SAFETY: U-017. Compatible non-protected type/full size, checked heap
        // and allocation limits, reserved device allocation slot. Dedicated
        // image is live and unbound; offset zero meets binding alignment.
        owner.allocation = unsafe { self.raw.allocate_memory(&allocate, None) }?;
        // SAFETY: U-017. Unique complete dedicated allocation and matching image.
        unsafe { self.raw.bind_image_memory(owner.raw, owner.allocation, 0) }?;
        let view = vk::ImageViewCreateInfo::default()
            .image(owner.raw)
            .view_type(vk::ImageViewType::TYPE_2D)
            .format(info.format.native())
            .subresource_range(range(info.format));
        // SAFETY: U-017. Identical supported format, bound image, identity swizzle,
        // complete existing color/depth mip/layer. No format reinterpretation.
        owner.view = unsafe { self.raw.create_image_view(&view, None) }?;
        Ok(owner)
    }

    #[allow(unsafe_code)]
    pub fn copy_buffer_to_texture<'d>(
        &'d self,
        source: &Buffer<'d>,
        destination: &mut Texture<'d>,
        region: TextureCopy,
    ) -> Result<Completion<'d>, Error> {
        validate_owners(self, source, destination)?;
        let (copy, full_image, _) = region.validate(destination.info, source.bytes)?;
        if !source.initialized {
            return Err(Error::Invalid("texture upload source is uninitialized"));
        }
        if !destination.initialized && !full_image {
            return Err(Error::Invalid(
                "first texture upload must initialize every texel",
            ));
        }
        let recording = Recording::new(self)?;
        let old = destination.layout();
        barrier(
            &recording,
            destination,
            old,
            vk::ImageLayout::TRANSFER_DST_OPTIMAL,
        );
        let regions = [copy];
        // SAFETY: U-017. Checked same device, initialized source and full buffer
        // footprint including row pitch. Offset/pitch and the single-mip/layer
        // region satisfy both supported formats. Transfer usages/layout
        // are valid, and distinct unique allocations cannot overlap.
        unsafe {
            self.raw.cmd_copy_buffer_to_image(
                recording.raw,
                source.raw,
                destination.raw,
                vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                &regions,
            )
        };
        barrier(
            &recording,
            destination,
            vk::ImageLayout::TRANSFER_DST_OPTIMAL,
            vk::ImageLayout::GENERAL,
        );
        let complete = recording.finish()?;
        destination.initialized = true;
        Ok(complete)
    }

    #[allow(unsafe_code)]
    pub fn copy_texture_to_buffer<'d>(
        &'d self,
        source: &mut Texture<'d>,
        destination: &mut Buffer<'d>,
        region: TextureCopy,
    ) -> Result<Completion<'d>, Error> {
        validate_owners(self, destination, source)?;
        let (copy, _, full_buffer) = region.validate(source.info, destination.bytes)?;
        if !source.initialized {
            return Err(Error::Invalid("texture readback source is uninitialized"));
        }
        if !destination.initialized && !full_buffer {
            return Err(Error::Invalid(
                "first texture readback must initialize every buffer byte",
            ));
        }
        let recording = Recording::new(self)?;
        barrier(
            &recording,
            source,
            vk::ImageLayout::GENERAL,
            vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
        );
        let regions = [copy];
        // SAFETY: U-017. Same-device initialized image, checked color region,
        // offset, row pitch and destination footprint. Untouched destination bytes
        // were initialized already. Transfer usages and source layout are valid.
        unsafe {
            self.raw.cmd_copy_image_to_buffer(
                recording.raw,
                source.raw,
                vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
                destination.raw,
                &regions,
            )
        };
        barrier(
            &recording,
            source,
            vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
            vk::ImageLayout::GENERAL,
        );
        let complete = recording.finish()?;
        destination.initialized = true;
        Ok(complete)
    }
}

fn validate_owners(
    device: &Device,
    buffer: &Buffer<'_>,
    texture: &Texture<'_>,
) -> Result<(), Error> {
    if !std::ptr::eq(device, buffer.device) || !std::ptr::eq(device, texture.device) {
        return Err(Error::Invalid("texture copy uses another device"));
    }
    Ok(())
}

impl<'d> Texture<'d> {
    pub fn info(&self) -> TextureInfo {
        self.info
    }

    pub(super) fn layout(&self) -> vk::ImageLayout {
        if self.initialized {
            vk::ImageLayout::GENERAL
        } else {
            vk::ImageLayout::UNDEFINED
        }
    }

    #[allow(unsafe_code)]
    pub fn clear(&mut self, color: [f32; 4]) -> Result<Completion<'d>, Error> {
        if self.info.format.is_depth() {
            return Err(Error::Invalid("color clear requires a color format"));
        }
        let recording = Recording::new(self.device)?;
        barrier(
            &recording,
            self,
            self.layout(),
            vk::ImageLayout::TRANSFER_DST_OPTIMAL,
        );
        // SAFETY: U-017. Both formats are color float/UNORM, matching float32
        // clear values. Complete existing range, transfer usage/layout and unique
        // mutable ownership hold. Values/range stay live through command recording.
        unsafe {
            self.device.raw.cmd_clear_color_image(
                recording.raw,
                self.raw,
                vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                &vk::ClearColorValue { float32: color },
                &[range(self.info.format)],
            )
        };
        barrier(
            &recording,
            self,
            vk::ImageLayout::TRANSFER_DST_OPTIMAL,
            vk::ImageLayout::GENERAL,
        );
        let complete = recording.finish()?;
        self.initialized = true;
        Ok(complete)
    }

    #[allow(unsafe_code)]
    pub fn clear_depth(&mut self, depth: f32) -> Result<Completion<'d>, Error> {
        if !self.info.format.is_depth() || !depth.is_finite() || !(0.0..=1.0).contains(&depth) {
            return Err(Error::Invalid(
                "depth clear requires D32Float and a finite value in 0..1",
            ));
        }
        let recording = Recording::new(self.device)?;
        barrier(
            &recording,
            self,
            self.layout(),
            vk::ImageLayout::TRANSFER_DST_OPTIMAL,
        );
        // SAFETY: U-021. A complete live D32 depth range with transfer usage,
        // valid depth value and exclusive owner. Layout is established above.
        unsafe {
            self.device.raw.cmd_clear_depth_stencil_image(
                recording.raw,
                self.raw,
                vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                &vk::ClearDepthStencilValue { depth, stencil: 0 },
                &[range(self.info.format)],
            )
        };
        barrier(
            &recording,
            self,
            vk::ImageLayout::TRANSFER_DST_OPTIMAL,
            vk::ImageLayout::GENERAL,
        );
        let complete = recording.finish()?;
        self.initialized = true;
        Ok(complete)
    }
}

// All callers are the complete synchronous operations above. Outside an active
// operation an initialized image is GENERAL, otherwise it is UNDEFINED.
#[allow(unsafe_code)]
pub(super) fn barrier(
    recording: &Recording<'_>,
    texture: &Texture<'_>,
    old: vk::ImageLayout,
    new: vk::ImageLayout,
) {
    assert!(std::ptr::eq(recording.device, texture.device));
    let to_transfer = matches!(
        new,
        vk::ImageLayout::TRANSFER_SRC_OPTIMAL | vk::ImageLayout::TRANSFER_DST_OPTIMAL
    );
    let destination = if to_transfer {
        vk::AccessFlags::TRANSFER_READ | vk::AccessFlags::TRANSFER_WRITE
    } else {
        vk::AccessFlags::MEMORY_READ | vk::AccessFlags::MEMORY_WRITE | vk::AccessFlags::HOST_READ
    };
    let memories = [vk::MemoryBarrier::default()
        .src_access_mask(vk::AccessFlags::MEMORY_WRITE | vk::AccessFlags::HOST_WRITE)
        .dst_access_mask(destination)];
    let images = [vk::ImageMemoryBarrier::default()
        .image(texture.raw)
        .subresource_range(range(texture.info.format))
        .old_layout(old)
        .new_layout(new)
        .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .src_access_mask(if old == vk::ImageLayout::UNDEFINED {
            vk::AccessFlags::empty()
        } else {
            vk::AccessFlags::MEMORY_WRITE
        })
        .dst_access_mask(destination & !vk::AccessFlags::HOST_READ)];
    // SAFETY: U-017. Same-device recording outside a render pass. The complete
    // owned image has the old layout established by the preceding command or
    // completed operation. No queue ownership transfer or partial subresources.
    // Global dependency also covers upload/readback buffer writes and host writes.
    // Conservative all-command ordering includes prior reads before overwrite.
    unsafe {
        recording.device.raw.cmd_pipeline_barrier(
            recording.raw,
            vk::PipelineStageFlags::ALL_COMMANDS | vk::PipelineStageFlags::HOST,
            if to_transfer {
                vk::PipelineStageFlags::TRANSFER
            } else {
                vk::PipelineStageFlags::ALL_COMMANDS | vk::PipelineStageFlags::HOST
            },
            vk::DependencyFlags::empty(),
            &memories,
            &[],
            &images,
        )
    };
}

impl Drop for Texture<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-017. Unique children, possibly null during partial creation.
        // The borrowed device is live. All operations completed, uncertain waits
        // abort under U-010. Destroy view, then image, then its dedicated memory.
        unsafe {
            self.device.raw.destroy_image_view(self.view, None);
            self.device.raw.destroy_image(self.raw, None);
            self.device.raw.free_memory(self.allocation, None);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn agfx_texture_size_rejects_zero_and_overflow() {
        let info = TextureInfo {
            width: 800,
            height: 600,
            format: TextureFormat::Rgba8Unorm,
        };
        assert_eq!(info.byte_len().unwrap(), 1_920_000);
        assert_eq!(
            TextureInfo {
                format: TextureFormat::Rgba32Float,
                ..info
            }
            .byte_len()
            .unwrap(),
            7_680_000
        );
        for dimensions in [[0, 600], [800, 0], [u32::MAX, u32::MAX]] {
            assert!(TextureInfo {
                width: dimensions[0],
                height: dimensions[1],
                ..info
            }
            .byte_len()
            .is_err());
        }
    }

    #[test]
    fn agfx_texture_copy_bounds_include_pitch_and_last_row() {
        let info = TextureInfo {
            width: 8,
            height: 4,
            format: TextureFormat::Rgba8Unorm,
        };
        let whole = TextureCopy::whole(info);
        let (_, image, buffer) = whole.validate(info, 128).unwrap();
        assert!(image && buffer);
        let region = TextureCopy {
            origin: [2, 1],
            extent: [3, 2],
            buffer_offset: 4,
            row_bytes: 16,
        };
        let (copy, image, buffer) = region.validate(info, 32).unwrap();
        assert!(!image && !buffer);
        assert_eq!((copy.buffer_offset, copy.buffer_row_length), (4, 4));
        assert_eq!((copy.image_offset.x, copy.image_offset.y), (2, 1));
        assert!(region.validate(info, 31).is_err());
        assert!(whole.validate(info, 127).is_err());
        for bad in [
            TextureCopy {
                extent: [0, 2],
                ..region
            },
            TextureCopy {
                origin: [u32::MAX, 0],
                ..region
            },
            TextureCopy {
                origin: [6, 1],
                ..region
            },
            TextureCopy {
                row_bytes: 8,
                ..region
            },
            TextureCopy {
                row_bytes: 14,
                ..region
            },
            TextureCopy {
                row_bytes: 1 << 31,
                ..region
            },
            TextureCopy {
                buffer_offset: 2,
                ..region
            },
            TextureCopy {
                buffer_offset: u64::MAX - 3,
                ..region
            },
        ] {
            assert!(bad.validate(info, u64::MAX).is_err(), "{bad:?}");
        }
        let float = TextureInfo {
            format: TextureFormat::Rgba32Float,
            ..info
        };
        assert!(TextureCopy {
            buffer_offset: 4,
            ..whole
        }
        .validate(float, 1024)
        .is_err());
    }

    #[test]
    fn agfx_texture_copy_padding_is_not_initialization() {
        let info = TextureInfo {
            width: 3,
            height: 2,
            format: TextureFormat::Rgba8Unorm,
        };
        let (_, image, buffer) = TextureCopy {
            row_bytes: 16,
            ..TextureCopy::whole(info)
        }
        .validate(info, 28)
        .unwrap();
        assert!(image && !buffer);
        let one_row = TextureInfo { height: 1, ..info };
        let (_, image, buffer) = TextureCopy {
            row_bytes: 16,
            ..TextureCopy::whole(one_row)
        }
        .validate(one_row, 12)
        .unwrap();
        assert!(image && buffer); // unused stride creates no trailing-row padding
        let (_, _, buffer) = TextureCopy::whole(info).validate(info, 28).unwrap();
        assert!(!buffer); // a trailing allocation suffix must already be initialized
    }
}
