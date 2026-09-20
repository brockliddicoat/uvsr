//! Shared private descriptor ownership for ordinary compute and graphics.
//! Native boundaries U-016 and U-021. This is not NGAPI's native heap profile.
use super::{
    vk, Buffer, ComparisonFunction, Device, Error, Lease, Sampler, SamplerFilter, ShaderCode,
    Texture,
};

#[derive(Clone, Copy, Debug)]
pub(super) struct Interface {
    pub buffers: u32,
    pub images: u32,
    pub sampled_images: u32,
    pub samplers: u32,
    pub root_bytes: u32,
}

impl Interface {
    pub fn validate(
        self,
        limits: &vk::PhysicalDeviceLimits,
        color_attachments: u32,
    ) -> Result<(), Error> {
        if self.buffers > limits.max_per_stage_descriptor_storage_buffers
            || self.buffers > limits.max_descriptor_set_storage_buffers
            || self.images > limits.max_per_stage_descriptor_storage_images
            || self.images > limits.max_descriptor_set_storage_images
            || self.sampled_images > limits.max_per_stage_descriptor_sampled_images
            || self.sampled_images > limits.max_descriptor_set_sampled_images
            || self.samplers > limits.max_per_stage_descriptor_samplers
            || self.samplers > limits.max_descriptor_set_samplers
            || self
                .buffers
                .checked_add(self.images)
                .and_then(|n| n.checked_add(self.sampled_images))
                .and_then(|n| n.checked_add(color_attachments))
                .is_none_or(|n| n > limits.max_per_stage_resources)
            || self.root_bytes & 3 != 0
            || self.root_bytes > limits.max_push_constants_size
        {
            return Err(Error::Invalid(
                "descriptor/root interface exceeds device limits",
            ));
        }
        Ok(())
    }
}

pub(super) struct Descriptors<'d> {
    device: &'d Device,
    interface: Interface,
    set_layout: vk::DescriptorSetLayout,
    pub layout: vk::PipelineLayout,
    pool: vk::DescriptorPool,
    pub set: vk::DescriptorSet,
    // Acquired before native creation, retained through every child's cleanup.
    // A forgotten public pipeline therefore also retains its Device/loader.
    _slot: Lease<'d>,
}

impl<'d> Descriptors<'d> {
    #[allow(unsafe_code)]
    pub fn new(
        device: &'d Device,
        interface: Interface,
        stages: vk::ShaderStageFlags,
    ) -> Result<Self, Error> {
        interface.validate(&device.limits, 0)?;
        assert!(
            stages == vk::ShaderStageFlags::COMPUTE
                || stages == (vk::ShaderStageFlags::VERTEX | vk::ShaderStageFlags::FRAGMENT)
        );
        let mut owner = Self {
            device,
            interface,
            set_layout: vk::DescriptorSetLayout::null(),
            layout: vk::PipelineLayout::null(),
            pool: vk::DescriptorPool::null(),
            set: vk::DescriptorSet::null(),
            _slot: device.pipelines.acquire()?,
        };
        let bindings: Vec<_> = [
            (0, vk::DescriptorType::STORAGE_BUFFER, interface.buffers),
            (1, vk::DescriptorType::STORAGE_IMAGE, interface.images),
            (
                2,
                vk::DescriptorType::SAMPLED_IMAGE,
                interface.sampled_images,
            ),
            (3, vk::DescriptorType::SAMPLER, interface.samplers),
        ]
        .into_iter()
        .filter(|&(_, _, count)| count != 0)
        .map(|(binding, kind, count)| {
            vk::DescriptorSetLayoutBinding::default()
                .binding(binding)
                .descriptor_type(kind)
                .descriptor_count(count)
                .stage_flags(stages)
        })
        .collect();
        let create = vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings);
        // SAFETY: U-016/U-021. Checked per-stage/set limits, distinct bindings,
        // supported core stages, and live arrays. No update-after-bind flags.
        owner.set_layout = unsafe { device.raw.create_descriptor_set_layout(&create, None) }?;
        let layouts = [owner.set_layout];
        let ranges = [vk::PushConstantRange::default()
            .stage_flags(stages)
            .offset(0)
            .size(interface.root_bytes)];
        let create = vk::PipelineLayoutCreateInfo::default()
            .set_layouts(&layouts)
            .push_constant_ranges(if interface.root_bytes == 0 {
                &[]
            } else {
                &ranges
            });
        // SAFETY: U-016/U-021. Live same-device set layout and checked aligned
        // nonoverlapping push range. Borrowed arrays remain live for this call.
        owner.layout = unsafe { device.raw.create_pipeline_layout(&create, None) }?;
        let sizes: Vec<_> = bindings
            .iter()
            .map(|b| vk::DescriptorPoolSize {
                ty: b.descriptor_type,
                descriptor_count: b.descriptor_count,
            })
            .collect();
        let create = vk::DescriptorPoolCreateInfo::default()
            .max_sets(1)
            .pool_sizes(&sizes);
        // SAFETY: U-016/U-021. Exact capacities for one set. Empty layouts are
        // valid for shaders using only builtins/push constants.
        owner.pool = unsafe { device.raw.create_descriptor_pool(&create, None) }?;
        let allocate = vk::DescriptorSetAllocateInfo::default()
            .descriptor_pool(owner.pool)
            .set_layouts(&layouts);
        // SAFETY: U-016/U-021. Fresh pool, one compatible live layout.
        owner.set = unsafe { device.raw.allocate_descriptor_sets(&allocate) }?[0];
        Ok(owner)
    }

    // Private synchronous callers retain all resource borrows through completion.
    // Graphics permits only shader reads. Compute additionally retains exclusive
    // borrows of buffers/storage images for the entire submitted batch.
    #[allow(unsafe_code)]
    pub fn update(
        &self,
        buffers: &[&Buffer<'_>],
        images: &[&Texture<'_>],
        sampled_images: &[&Texture<'_>],
        samplers: &[&Sampler<'_>],
    ) -> Result<(), Error> {
        if buffers.len() != self.interface.buffers as usize
            || images.len() != self.interface.images as usize
            || sampled_images.len() != self.interface.sampled_images as usize
            || samplers.len() != self.interface.samplers as usize
        {
            return Err(Error::Invalid(
                "shader resources do not match the declared interface",
            ));
        }
        for (index, buffer) in buffers.iter().enumerate() {
            if !std::ptr::eq(buffer.device, self.device)
                || !buffer.initialized
                || buffer.bytes > self.device.limits.max_storage_buffer_range as u64
                || buffers[..index].iter().any(|other| other.raw == buffer.raw)
            {
                return Err(Error::Invalid(
                    "shader buffers must be initialized, distinct, same-device storage ranges",
                ));
            }
        }
        for (index, image) in images.iter().enumerate() {
            if !std::ptr::eq(image.device, self.device)
                || !image.initialized
                || !image.usage.storage
                || images[..index].iter().any(|other| other.raw == image.raw)
            {
                return Err(Error::Invalid(
                    "storage images must be initialized, distinct and from this device",
                ));
            }
        }
        for sampler in samplers {
            if !std::ptr::eq(sampler.device, self.device)
                || sampler.info().comparison != ComparisonFunction::Always
            {
                return Err(Error::Invalid(
                    "ordinary sampling requires same-device non-comparison samplers",
                ));
            }
        }
        for image in sampled_images {
            if !std::ptr::eq(image.device, self.device)
                || !image.initialized
                || !image.usage.sampled
                || images.iter().any(|other| other.raw == image.raw)
            {
                return Err(Error::Invalid("sampled images must be initialized, same-device and distinct from storage images"));
            }
            if !image.linear_sampling
                && samplers
                    .iter()
                    .any(|s| s.info().filter == SamplerFilter::Linear)
            {
                return Err(Error::Unsupported(
                    "sampled format does not support linear filtering".into(),
                ));
            }
        }
        let buffers: Vec<_> = buffers
            .iter()
            .map(|b| {
                vk::DescriptorBufferInfo::default()
                    .buffer(b.raw)
                    .offset(0)
                    .range(b.bytes)
            })
            .collect();
        let storage: Vec<_> = images
            .iter()
            .map(|i| {
                vk::DescriptorImageInfo::default()
                    .image_view(i.view)
                    .image_layout(vk::ImageLayout::GENERAL)
            })
            .collect();
        let sampled: Vec<_> = sampled_images
            .iter()
            .map(|i| {
                vk::DescriptorImageInfo::default()
                    .image_view(i.view)
                    .image_layout(vk::ImageLayout::GENERAL)
            })
            .collect();
        let samplers: Vec<_> = samplers
            .iter()
            .map(|s| vk::DescriptorImageInfo::default().sampler(s.raw))
            .collect();
        let mut writes = Vec::with_capacity(4);
        if !buffers.is_empty() {
            writes.push(
                vk::WriteDescriptorSet::default()
                    .dst_set(self.set)
                    .dst_binding(0)
                    .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
                    .buffer_info(&buffers),
            );
        }
        for (binding, kind, infos) in [
            (1, vk::DescriptorType::STORAGE_IMAGE, &storage),
            (2, vk::DescriptorType::SAMPLED_IMAGE, &sampled),
            (3, vk::DescriptorType::SAMPLER, &samplers),
        ] {
            if !infos.is_empty() {
                writes.push(
                    vk::WriteDescriptorSet::default()
                        .dst_set(self.set)
                        .dst_binding(binding)
                        .descriptor_type(kind)
                        .image_info(infos),
                );
            }
        }
        // SAFETY: U-016/U-021. Exact live set capacities and fully initialized
        // same-device views/ranges, offset zero, GENERAL layouts, checked uses
        // and filter support. No update is concurrent with a submitted use.
        unsafe { self.device.raw.update_descriptor_sets(&writes, &[]) };
        Ok(())
    }
}

impl Drop for Descriptors<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-016/U-021. Public pipeline cleanup precedes these fields.
        // Synchronous work is complete, device is borrowed, null partial handles
        // are allowed. Destroying the pool implicitly releases its only set.
        unsafe {
            self.device.raw.destroy_descriptor_pool(self.pool, None);
            self.device.raw.destroy_pipeline_layout(self.layout, None);
            self.device
                .raw
                .destroy_descriptor_set_layout(self.set_layout, None);
        }
    }
}

pub(super) struct Module<'d> {
    device: &'d Device,
    pub raw: vk::ShaderModule,
}

impl<'d> Module<'d> {
    /// # Safety
    /// Complete module validity and enabled-feature agreement are required.
    #[allow(unsafe_code)]
    pub unsafe fn new(device: &'d Device, shader: &ShaderCode<'_>) -> Result<Self, Error> {
        if shader.words.len() < 5
            || shader.words[0] != 0x07230203
            || shader.entry_point.to_bytes().is_empty()
        {
            return Err(Error::Invalid("SPIR-V header and named entry required"));
        }
        let create = vk::ShaderModuleCreateInfo::default().code(shader.words);
        // SAFETY: U-016/U-021. Caller proves module validity. Aligned word slice
        // remains live through creation on this borrowed device.
        Ok(Self {
            device,
            raw: unsafe { device.raw.create_shader_module(&create, None) }?,
        })
    }
}

impl Drop for Module<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-016/U-021. Private lexical owner retained through pipeline
        // creation, never used directly by commands. Its device remains live.
        unsafe { self.device.raw.destroy_shader_module(self.raw, None) };
    }
}
