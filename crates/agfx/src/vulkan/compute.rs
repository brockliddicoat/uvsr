//! Native compute boundary U-011, deliberately limited to the first AGFX profile.
use super::{Buffer, Completion, Device, Error, Memory, Recording};
use ash::vk;
use std::ffi::CStr;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ShaderStage {
    Vertex,
    Fragment,
    Compute,
}

pub struct ShaderCode<'a> {
    pub words: &'a [u32],
    pub stage: ShaderStage,
    pub entry_point: &'a CStr,
}

/// The ordinary AGFX storage-buffer root, separate from NGAPI's native profile.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ComputeRoot {
    pub resource: u32,
    pub element_count: u32,
    pub pass_index: u32,
    pub padding: u32,
}

impl ComputeRoot {
    fn bytes(self) -> Result<[u8; 16], Error> {
        if self.resource >= 4
            || self.element_count != 64
            || self.pass_index >= 4
            || self.padding != 0
        {
            return Err(Error::Invalid(
                "compute root requires slot0..3, count64, pass0..3 and padding0",
            ));
        }
        let mut bytes = [0_u8; 16];
        for (word, output) in [
            self.resource,
            self.element_count,
            self.pass_index,
            self.padding,
        ]
        .into_iter()
        .zip(bytes.chunks_exact_mut(4))
        {
            output.copy_from_slice(&word.to_ne_bytes());
        }
        Ok(bytes)
    }
}

/// An ordinary four-storage-buffer pipeline with the explicit AGFX root ABI.
/// Construction is unsafe because arbitrary shader behavior is not verified by
/// Vulkan validation. Once its contract is met, dispatch enforces resource use.
pub struct BufferCompute<'d> {
    device: &'d Device,
    set_layout: vk::DescriptorSetLayout,
    layout: vk::PipelineLayout,
    pool: vk::DescriptorPool,
    set: vk::DescriptorSet,
    pipeline: vk::Pipeline,
}

struct Module<'d> {
    device: &'d Device,
    raw: vk::ShaderModule,
}

impl Drop for Module<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-011. Unique module, retained through pipeline creation and
        // never used by commands directly. Device remains borrowed and live.
        unsafe { self.device.raw.destroy_shader_module(self.raw, None) };
    }
}

impl Device {
    /// Create the bounded ordinary storage-buffer profile.
    ///
    /// # Safety
    /// U-011: the complete SPIR-V must be valid for this device's enabled
    /// features. Its compute entry must use only set0/binding0 (four ordinary
    /// storage buffers) and the 16-byte ComputeRoot. For every accepted root and
    /// one 64-thread group, access only the selected initialized 64-word buffer,
    /// with disjoint lane writes and no other memory/resource access. All shader
    /// instructions and control flow must satisfy Vulkan/SPIR-V validity rules.
    /// This function's header checks do not validate these semantic obligations.
    #[allow(unsafe_code)]
    pub unsafe fn buffer_compute(
        &self,
        shader: ShaderCode<'_>,
    ) -> Result<BufferCompute<'_>, Error> {
        if shader.stage != ShaderStage::Compute
            || shader.words.len() < 5
            || shader.words[0] != 0x07230203
            || shader.entry_point.to_bytes().is_empty()
        {
            return Err(Error::Invalid(
                "compute stage, SPIR-V header and named entry required",
            ));
        }
        let limits = &self.limits;
        if limits.max_push_constants_size < 16
            || limits.max_per_stage_descriptor_storage_buffers < 4
            || limits.max_descriptor_set_storage_buffers < 4
            || limits.max_storage_buffer_range < 256
            || limits.max_compute_work_group_invocations < 64
            || limits.max_compute_work_group_size[0] < 64
        {
            return Err(Error::Unsupported(
                "ordinary four-buffer compute profile limits".into(),
            ));
        }
        let mut owner = BufferCompute {
            device: self,
            set_layout: vk::DescriptorSetLayout::null(),
            layout: vk::PipelineLayout::null(),
            pool: vk::DescriptorPool::null(),
            set: vk::DescriptorSet::null(),
            pipeline: vk::Pipeline::null(),
        };
        let bindings = [vk::DescriptorSetLayoutBinding::default()
            .binding(0)
            .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
            .descriptor_count(4)
            .stage_flags(vk::ShaderStageFlags::COMPUTE)];
        let create = vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings);
        // SAFETY: U-011. Four supported ordinary storage descriptors, compute
        // stage only, with live binding data and no variable/update-after-bind use.
        owner.set_layout = unsafe { self.raw.create_descriptor_set_layout(&create, None) }?;
        let layouts = [owner.set_layout];
        let ranges = [vk::PushConstantRange::default()
            .stage_flags(vk::ShaderStageFlags::COMPUTE)
            .offset(0)
            .size(16)];
        let create = vk::PipelineLayoutCreateInfo::default()
            .set_layouts(&layouts)
            .push_constant_ranges(&ranges);
        // SAFETY: U-011. Same-device live set layout, supported nonoverlapping
        // aligned root range. Arrays stay live through the creation call.
        owner.layout = unsafe { self.raw.create_pipeline_layout(&create, None) }?;
        let sizes = [vk::DescriptorPoolSize {
            ty: vk::DescriptorType::STORAGE_BUFFER,
            descriptor_count: 4,
        }];
        let create = vk::DescriptorPoolCreateInfo::default()
            .max_sets(1)
            .pool_sizes(&sizes);
        // SAFETY: U-011. Nonzero, exact set/storage capacities. No concurrent use.
        owner.pool = unsafe { self.raw.create_descriptor_pool(&create, None) }?;
        let allocate = vk::DescriptorSetAllocateInfo::default()
            .descriptor_pool(owner.pool)
            .set_layouts(&layouts);
        // SAFETY: U-011. Fresh pool has room for this one matching four-slot set.
        owner.set = unsafe { self.raw.allocate_descriptor_sets(&allocate) }?[0];
        let create = vk::ShaderModuleCreateInfo::default().code(shader.words);
        // SAFETY: U-011. Caller establishes complete module validity and exact
        // enabled-feature/profile agreement. The aligned words stay live here.
        let module = Module {
            device: self,
            raw: unsafe { self.raw.create_shader_module(&create, None) }?,
        };
        let stage = vk::PipelineShaderStageCreateInfo::default()
            .stage(vk::ShaderStageFlags::COMPUTE)
            .module(module.raw)
            .name(shader.entry_point);
        let create = [vk::ComputePipelineCreateInfo::default()
            .stage(stage)
            .layout(owner.layout)];
        // SAFETY: U-011. Caller establishes the entry/interface/64-thread contract.
        // Module/layout/names remain live, limits were checked, no base/cache used.
        match unsafe {
            self.raw
                .create_compute_pipelines(vk::PipelineCache::null(), &create, None)
        } {
            Ok(pipelines) => owner.pipeline = pipelines[0],
            Err((pipelines, error)) => {
                for pipeline in pipelines {
                    // SAFETY: U-011. Vulkan can return partial handles on failure.
                    // None were submitted or put into the owner; null is allowed.
                    unsafe { self.raw.destroy_pipeline(pipeline, None) };
                }
                return Err(error.into());
            }
        }
        Ok(owner)
    }
}

impl<'d> BufferCompute<'d> {
    /// Execute four dependent passes with one 64-thread group per pass.
    /// Every descriptor is initialized before use. Completion precedes return.
    #[allow(unsafe_code)]
    pub fn four_passes(
        &mut self,
        buffers: [&mut Buffer<'_>; 4],
        resource: u32,
    ) -> Result<Completion<'d>, Error> {
        // Pack and validate all roots before recording or descriptor writes.
        let mut roots = [[0_u8; 16]; 4];
        for (pass, root) in roots.iter_mut().enumerate() {
            *root = ComputeRoot {
                resource,
                element_count: 64,
                pass_index: pass as u32,
                padding: 0,
            }
            .bytes()?;
        }
        for (index, buffer) in buffers.iter().enumerate() {
            if !std::ptr::eq(buffer.device, self.device)
                || buffer.memory != Memory::Device
                || buffer.bytes != 256
                || !buffer.initialized
            {
                return Err(Error::Invalid(
                    "compute needs four initialized same-device 256-byte device buffers",
                ));
            }
            if buffers[..index].iter().any(|other| other.raw == buffer.raw) {
                return Err(Error::Invalid(
                    "compute descriptors must select distinct allocations",
                ));
            }
        }
        let infos = buffers.each_ref().map(|buffer| {
            vk::DescriptorBufferInfo::default()
                .buffer(buffer.raw)
                .offset(0)
                .range(256)
        });
        let writes = [vk::WriteDescriptorSet::default()
            .dst_set(self.set)
            .dst_binding(0)
            .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
            .buffer_info(&infos)];
        // SAFETY: U-011. Every same-device storage slot is initialized with a
        // complete, aligned live range. No earlier submission is pending. The
        // mutable owner and buffer borrows exclude concurrent update/access.
        unsafe { self.device.raw.update_descriptor_sets(&writes, &[]) };
        let recording = Recording::new(self.device)?;
        let before = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::MEMORY_WRITE)
            .dst_access_mask(vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE)];
        // SAFETY: U-011. All previous transfers/computes precede this recording;
        // make their complete initialized contents visible to compute access.
        unsafe {
            self.device.raw.cmd_pipeline_barrier(
                recording.raw,
                vk::PipelineStageFlags::ALL_COMMANDS,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::DependencyFlags::empty(),
                &before,
                &[],
                &[],
            )
        };
        // SAFETY: U-011. Live compute pipeline, compatible layout/set and four
        // retained descriptors. No dynamic offsets or render pass are involved.
        unsafe {
            self.device.raw.cmd_bind_pipeline(
                recording.raw,
                vk::PipelineBindPoint::COMPUTE,
                self.pipeline,
            );
            self.device.raw.cmd_bind_descriptor_sets(
                recording.raw,
                vk::PipelineBindPoint::COMPUTE,
                self.layout,
                0,
                &[self.set],
                &[],
            );
        }
        for (pass, root) in roots.iter().enumerate() {
            // SAFETY: U-011. The complete 16-byte root matches the layout and the
            // constructor's shader contract. Checked resource and count64 select
            // only distinct initialized words. One group has exactly64 lanes.
            unsafe {
                self.device.raw.cmd_push_constants(
                    recording.raw,
                    self.layout,
                    vk::ShaderStageFlags::COMPUTE,
                    0,
                    root,
                );
                self.device.raw.cmd_dispatch(recording.raw, 1, 1, 1);
            }
            if pass < 3 {
                let barrier = [vk::BufferMemoryBarrier::default()
                    .src_access_mask(vk::AccessFlags::SHADER_WRITE)
                    .dst_access_mask(vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE)
                    .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                    .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
                    .buffer(infos[resource as usize].buffer)
                    .offset(0)
                    .size(256)];
                // SAFETY: U-011. Source AGFX whole-buffer UAV dependency, same
                // family and selected initialized allocation, between each pass.
                unsafe {
                    self.device.raw.cmd_pipeline_barrier(
                        recording.raw,
                        vk::PipelineStageFlags::COMPUTE_SHADER,
                        vk::PipelineStageFlags::COMPUTE_SHADER,
                        vk::DependencyFlags::empty(),
                        &[],
                        &barrier,
                        &[],
                    )
                };
            }
        }
        let after = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::SHADER_WRITE)
            .dst_access_mask(vk::AccessFlags::MEMORY_READ | vk::AccessFlags::MEMORY_WRITE)];
        // SAFETY: U-011. Shader writes become visible to following transfers or
        // computes. U-010's copy then supplies host visibility and completion.
        unsafe {
            self.device.raw.cmd_pipeline_barrier(
                recording.raw,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::PipelineStageFlags::ALL_COMMANDS,
                vk::DependencyFlags::empty(),
                &after,
                &[],
                &[],
            )
        };
        recording.finish()
    }
}

impl Drop for BufferCompute<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-011. All dispatches completed synchronously; uncertain waits
        // abort. Unique live children (or null during partial construction), with
        // the borrowed Device retained. Free sets with their pool before layouts.
        unsafe {
            self.device.raw.destroy_pipeline(self.pipeline, None);
            self.device.raw.destroy_descriptor_pool(self.pool, None);
            self.device.raw.destroy_pipeline_layout(self.layout, None);
            self.device
                .raw
                .destroy_descriptor_set_layout(self.set_layout, None);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn root_layout_and_words_match_the_shader_abi() {
        assert_eq!(std::mem::size_of::<ComputeRoot>(), 16);
        assert_eq!(std::mem::align_of::<ComputeRoot>(), 4);
        assert_eq!(std::mem::offset_of!(ComputeRoot, resource), 0);
        assert_eq!(std::mem::offset_of!(ComputeRoot, element_count), 4);
        assert_eq!(std::mem::offset_of!(ComputeRoot, pass_index), 8);
        assert_eq!(std::mem::offset_of!(ComputeRoot, padding), 12);
        let root = ComputeRoot {
            resource: 3,
            element_count: 64,
            pass_index: 2,
            padding: 0,
        };
        assert_eq!(
            root.bytes().unwrap().as_slice(),
            [3_u32, 64, 2, 0]
                .into_iter()
                .flat_map(u32::to_ne_bytes)
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn invalid_roots_are_rejected_before_commands() {
        let valid = ComputeRoot {
            resource: 1,
            element_count: 64,
            pass_index: 0,
            padding: 0,
        };
        for root in [
            ComputeRoot {
                resource: 4,
                ..valid
            },
            ComputeRoot {
                element_count: 65,
                ..valid
            },
            ComputeRoot {
                pass_index: 4,
                ..valid
            },
            ComputeRoot {
                padding: 1,
                ..valid
            },
        ] {
            assert!(root.bytes().is_err());
        }
    }
}
