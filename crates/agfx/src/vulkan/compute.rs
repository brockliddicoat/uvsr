//! Ordinary storage-buffer compute owners. Native boundary U-016, with the
//! source four-pass safe wrapper retained under U-011.
use super::{Buffer, Completion, Device, Error, Lease, Memory, Recording};
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

/// Ordinary set0/binding0 storage-buffer descriptors and inline root bytes.
/// This is separate from NGAPI native heaps. Texture descriptors follow later.
#[derive(Clone, Copy, Debug)]
pub struct ComputeInterface {
    pub buffers: u32,
    pub root_bytes: u32,
    pub local_size: [u32; 3],
}

impl ComputeInterface {
    fn validate_job(
        self,
        job: &ComputeDispatch<'_>,
        limits: &vk::PhysicalDeviceLimits,
    ) -> Result<(), Error> {
        if job.root.len() != self.root_bytes as usize
            || job
                .groups
                .iter()
                .zip(limits.max_compute_work_group_count)
                .any(|(&count, limit)| count > limit)
        {
            return Err(Error::Invalid(
                "compute root size or dispatch count exceeds its contract",
            ));
        }
        Ok(())
    }

    fn validate(self, limits: &vk::PhysicalDeviceLimits) -> Result<(), Error> {
        if self.buffers == 0
            || self.buffers > limits.max_per_stage_descriptor_storage_buffers
            || self.buffers > limits.max_descriptor_set_storage_buffers
            || self.buffers > limits.max_per_stage_resources
            || self.root_bytes & 3 != 0
            || self.root_bytes > limits.max_push_constants_size
        {
            return Err(Error::Invalid(
                "compute descriptor/root interface exceeds device limits",
            ));
        }
        let mut invocations = 1_u32;
        for (axis, value) in self.local_size.into_iter().enumerate() {
            if value == 0 || value > limits.max_compute_work_group_size[axis] {
                return Err(Error::Invalid("compute local size exceeds device limits"));
            }
            invocations = invocations
                .checked_mul(value)
                .ok_or(Error::Invalid("compute local size overflow"))?;
        }
        if invocations > limits.max_compute_work_group_invocations {
            return Err(Error::Invalid(
                "compute invocation count exceeds device limits",
            ));
        }
        Ok(())
    }
}

pub struct ComputeDispatch<'a> {
    pub root: &'a [u8],
    pub groups: [u32; 3],
}

/// Unique ordinary compute pipeline and descriptor owners. All batches finish
/// before returning. Arbitrary shader memory access has an explicit contract.
pub struct StorageCompute<'d> {
    device: &'d Device,
    interface: ComputeInterface,
    set_layout: vk::DescriptorSetLayout,
    layout: vk::PipelineLayout,
    pool: vk::DescriptorPool,
    set: vk::DescriptorSet,
    pipeline: vk::Pipeline,
    _owner_slot: Lease<'d>,
}

/// Source four-pass fixture wrapper with enforced root and buffer bounds.
pub struct BufferCompute<'d> {
    inner: StorageCompute<'d>,
}

struct Module<'d> {
    device: &'d Device,
    raw: vk::ShaderModule,
}

impl Drop for Module<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-016. Unique module, retained through pipeline creation and
        // never used by commands directly. Device remains borrowed and live.
        unsafe { self.device.raw.destroy_shader_module(self.raw, None) };
    }
}

impl Device {
    /// Create an ordinary storage-buffer pipeline.
    ///
    /// # Safety
    /// U-016: the complete SPIR-V must be valid for the device's enabled
    /// features. Its named compute entry uses exactly the declared local size,
    /// only set0/binding0 storage buffers and at most the declared root bytes.
    /// All module instructions, entry interfaces and layouts must be valid.
    /// No physical addresses or undeclared resources are permitted. Header and
    /// device-limit checks do not establish these shader validity obligations.
    #[allow(unsafe_code)]
    pub unsafe fn storage_compute(
        &self,
        shader: ShaderCode<'_>,
        interface: ComputeInterface,
    ) -> Result<StorageCompute<'_>, Error> {
        if shader.stage != ShaderStage::Compute
            || shader.words.len() < 5
            || shader.words[0] != 0x07230203
            || shader.entry_point.to_bytes().is_empty()
        {
            return Err(Error::Invalid(
                "compute stage, SPIR-V header and named entry required",
            ));
        }
        interface.validate(&self.limits)?;
        let mut owner = StorageCompute {
            device: self,
            interface,
            set_layout: vk::DescriptorSetLayout::null(),
            layout: vk::PipelineLayout::null(),
            pool: vk::DescriptorPool::null(),
            set: vk::DescriptorSet::null(),
            pipeline: vk::Pipeline::null(),
            _owner_slot: self.pipelines.acquire()?,
        };
        let bindings = [vk::DescriptorSetLayoutBinding::default()
            .binding(0)
            .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
            .descriptor_count(interface.buffers)
            .stage_flags(vk::ShaderStageFlags::COMPUTE)];
        let create = vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings);
        // SAFETY: U-016. Queried supported ordinary storage descriptors, compute
        // stage only, with live binding data and no variable/update-after-bind use.
        owner.set_layout = unsafe { self.raw.create_descriptor_set_layout(&create, None) }?;
        let layouts = [owner.set_layout];
        let ranges = [vk::PushConstantRange::default()
            .stage_flags(vk::ShaderStageFlags::COMPUTE)
            .offset(0)
            .size(interface.root_bytes)];
        let create = vk::PipelineLayoutCreateInfo::default()
            .set_layouts(&layouts)
            .push_constant_ranges(if interface.root_bytes == 0 {
                &[]
            } else {
                &ranges
            });
        // SAFETY: U-016. Same-device live set layout, supported nonoverlapping
        // aligned root range. Arrays stay live through the creation call.
        owner.layout = unsafe { self.raw.create_pipeline_layout(&create, None) }?;
        let sizes = [vk::DescriptorPoolSize {
            ty: vk::DescriptorType::STORAGE_BUFFER,
            descriptor_count: interface.buffers,
        }];
        let create = vk::DescriptorPoolCreateInfo::default()
            .max_sets(1)
            .pool_sizes(&sizes);
        // SAFETY: U-016. Nonzero, exact set/storage capacities. No concurrent use.
        owner.pool = unsafe { self.raw.create_descriptor_pool(&create, None) }?;
        let allocate = vk::DescriptorSetAllocateInfo::default()
            .descriptor_pool(owner.pool)
            .set_layouts(&layouts);
        // SAFETY: U-016. Fresh pool has room for this one matching set.
        owner.set = unsafe { self.raw.allocate_descriptor_sets(&allocate) }?[0];
        let create = vk::ShaderModuleCreateInfo::default().code(shader.words);
        // SAFETY: U-016. Caller establishes complete module validity and exact
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
        // SAFETY: U-016. Caller establishes the entry/interface/local-size contract.
        // Module/layout/names remain live, limits were checked, no base/cache used.
        match unsafe {
            self.raw
                .create_compute_pipelines(vk::PipelineCache::null(), &create, None)
        } {
            Ok(pipelines) => owner.pipeline = pipelines[0],
            Err((pipelines, error)) => {
                for pipeline in pipelines {
                    // SAFETY: U-016. Vulkan can return partial handles on failure.
                    // None were submitted or put into the owner; null is allowed.
                    unsafe { self.raw.destroy_pipeline(pipeline, None) };
                }
                return Err(error.into());
            }
        }
        Ok(owner)
    }
}

impl Device {
    /// Create the bounded ordinary AGFX four-buffer profile.
    ///
    /// # Safety
    /// U-011: shader satisfies U-016 module validity for four storage buffers,
    /// a 16-byte ComputeRoot and local size[64,1,1]. For every accepted root and
    /// one group, it accesses only its selected initialized 64-word buffer with
    /// disjoint lane writes. It uses no other resources or physical addresses.
    #[allow(unsafe_code)]
    pub unsafe fn buffer_compute(
        &self,
        shader: ShaderCode<'_>,
    ) -> Result<BufferCompute<'_>, Error> {
        // SAFETY: U-011. The caller's stronger bounded contract establishes
        // U-016 validity and this exact ordinary descriptor/root/local interface.
        let inner = unsafe {
            self.storage_compute(
                shader,
                ComputeInterface {
                    buffers: 4,
                    root_bytes: 16,
                    local_size: [64, 1, 1],
                },
            )
        }?;
        Ok(BufferCompute { inner })
    }
}

impl<'d> StorageCompute<'d> {
    /// Submit ordered dispatches using complete initialized buffer ranges.
    /// A full compute dependency separates each job. Completion precedes return.
    ///
    /// # Safety
    /// U-016: for every job, the caller must establish that the shader's actual
    /// root values and invocation IDs select only the bound initialized ranges,
    /// with valid types, alignment, synchronization and race-free accesses.
    /// Every shader-dependent index, offset and cross-invocation dependency
    /// must be valid. The method checks resource identity, native limits, root
    /// size and host retirement, but cannot prove arbitrary shader semantics.
    #[allow(unsafe_code)]
    pub unsafe fn dispatch(
        &mut self,
        buffers: &mut [&mut Buffer<'_>],
        jobs: &[ComputeDispatch<'_>],
    ) -> Result<Completion<'d>, Error> {
        if buffers.len() != self.interface.buffers as usize || jobs.is_empty() {
            return Err(Error::Invalid(
                "compute requires its declared buffers and at least one job",
            ));
        }
        for job in jobs {
            self.interface.validate_job(job, &self.device.limits)?;
        }
        for (index, buffer) in buffers.iter().enumerate() {
            if !std::ptr::eq(buffer.device, self.device)
                || !buffer.initialized
                || buffer.bytes > self.device.limits.max_storage_buffer_range as u64
            {
                return Err(Error::Invalid(
                    "compute needs initialized same-device buffers within storage range limits",
                ));
            }
            if buffers[..index].iter().any(|other| other.raw == buffer.raw) {
                return Err(Error::Invalid(
                    "compute buffers must have distinct allocations",
                ));
            }
        }
        let infos: Vec<_> = buffers
            .iter()
            .map(|buffer| {
                vk::DescriptorBufferInfo::default()
                    .buffer(buffer.raw)
                    .offset(0)
                    .range(buffer.bytes)
            })
            .collect();
        let writes = [vk::WriteDescriptorSet::default()
            .dst_set(self.set)
            .dst_binding(0)
            .descriptor_type(vk::DescriptorType::STORAGE_BUFFER)
            .buffer_info(&infos)];
        // SAFETY: U-016. Every same-device descriptor uses its live complete
        // initialized storage range at aligned offset0. Exclusive mutable
        // borrows and prior synchronous completion exclude updates in flight.
        unsafe { self.device.raw.update_descriptor_sets(&writes, &[]) };
        let recording = Recording::new(self.device)?;
        let before = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::MEMORY_WRITE | vk::AccessFlags::HOST_WRITE)
            .dst_access_mask(vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE)];
        // SAFETY: U-016. Recording outside a render pass. Host and prior device
        // writes are ordered before compute reads/writes on the same queue.
        unsafe {
            self.device.raw.cmd_pipeline_barrier(
                recording.raw,
                vk::PipelineStageFlags::ALL_COMMANDS | vk::PipelineStageFlags::HOST,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::DependencyFlags::empty(),
                &before,
                &[],
                &[],
            )
        };
        // SAFETY: U-016. Live compatible pipeline/layout/set. All descriptor
        // owners remain borrowed through the synchronous completion below.
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
        for (index, job) in jobs.iter().enumerate() {
            if !job.root.is_empty() {
                // SAFETY: U-016. Nonempty four-byte multiple, exact checked
                // layout size. Caller establishes the shader meaning of bytes.
                unsafe {
                    self.device.raw.cmd_push_constants(
                        recording.raw,
                        self.layout,
                        vk::ShaderStageFlags::COMPUTE,
                        0,
                        job.root,
                    )
                };
            }
            // SAFETY: U-016. Queried group/local limits and all owner lifetimes
            // hold. The unsafe caller establishes bounds, valid shader values,
            // race freedom and other execution-dependent shader obligations.
            unsafe {
                self.device.raw.cmd_dispatch(
                    recording.raw,
                    job.groups[0],
                    job.groups[1],
                    job.groups[2],
                )
            };
            if index + 1 < jobs.len() {
                let barrier = [vk::MemoryBarrier::default()
                    .src_access_mask(vk::AccessFlags::SHADER_WRITE)
                    .dst_access_mask(vk::AccessFlags::SHADER_READ | vk::AccessFlags::SHADER_WRITE)];
                // SAFETY: U-016. Same-queue compute dependency covers every
                // bound allocation between jobs, including prior read hazards.
                unsafe {
                    self.device.raw.cmd_pipeline_barrier(
                        recording.raw,
                        vk::PipelineStageFlags::COMPUTE_SHADER,
                        vk::PipelineStageFlags::COMPUTE_SHADER,
                        vk::DependencyFlags::empty(),
                        &barrier,
                        &[],
                        &[],
                    )
                };
            }
        }
        let after = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::SHADER_WRITE)
            .dst_access_mask(
                vk::AccessFlags::MEMORY_READ
                    | vk::AccessFlags::MEMORY_WRITE
                    | vk::AccessFlags::HOST_READ,
            )];
        // SAFETY: U-016. Shader writes become visible to later commands and
        // coherent mapped reads. The wait establishes completed resource use.
        unsafe {
            self.device.raw.cmd_pipeline_barrier(
                recording.raw,
                vk::PipelineStageFlags::COMPUTE_SHADER,
                vk::PipelineStageFlags::ALL_COMMANDS | vk::PipelineStageFlags::HOST,
                vk::DependencyFlags::empty(),
                &after,
                &[],
                &[],
            )
        };
        recording.finish()
    }
}

impl<'d> BufferCompute<'d> {
    /// Preserve the source four dependent passes with one 64-thread group each.
    #[allow(unsafe_code)]
    pub fn four_passes(
        &mut self,
        mut buffers: [&mut Buffer<'_>; 4],
        resource: u32,
    ) -> Result<Completion<'d>, Error> {
        let mut roots = [[0; 16]; 4];
        for (pass, root) in roots.iter_mut().enumerate() {
            *root = ComputeRoot {
                resource,
                element_count: 64,
                pass_index: pass as u32,
                padding: 0,
            }
            .bytes()?;
        }
        if buffers
            .iter()
            .any(|buffer| buffer.memory != Memory::Device || buffer.bytes != 256)
        {
            return Err(Error::Invalid(
                "bounded compute requires four 256-byte device buffers",
            ));
        }
        let jobs = roots.each_ref().map(|root| ComputeDispatch {
            root,
            groups: [1, 1, 1],
        });
        // SAFETY: U-011. The constructor requires disjoint 64-word shader
        // access for every valid root. Packed roots enforce slot0..3/count64/
        // pass0..3/padding0. Exact256-byte owners and one64-thread group satisfy
        // those conditions. U-016 also checks initialization/device identity.
        unsafe { self.inner.dispatch(&mut buffers, &jobs) }
    }
}

impl Drop for StorageCompute<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-016. All dispatches completed synchronously; uncertain waits
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
    fn compute_limits() -> vk::PhysicalDeviceLimits {
        vk::PhysicalDeviceLimits {
            max_per_stage_descriptor_storage_buffers: 4,
            max_descriptor_set_storage_buffers: 4,
            max_per_stage_resources: 4,
            max_push_constants_size: 128,
            max_compute_work_group_size: [64, 64, 64],
            max_compute_work_group_invocations: 64,
            max_compute_work_group_count: [65535; 3],
            ..Default::default()
        }
    }

    #[test]
    fn agfx_compute_interface_rejects_limits_and_overflow() {
        let valid = ComputeInterface {
            buffers: 1,
            root_bytes: 80,
            local_size: [8, 8, 1],
        };
        let limits = compute_limits();
        assert!(valid.validate(&limits).is_ok());
        for invalid in [
            ComputeInterface {
                buffers: 0,
                ..valid
            },
            ComputeInterface {
                buffers: 5,
                ..valid
            },
            ComputeInterface {
                root_bytes: 81,
                ..valid
            },
            ComputeInterface {
                root_bytes: 132,
                ..valid
            },
            ComputeInterface {
                local_size: [0, 8, 1],
                ..valid
            },
            ComputeInterface {
                local_size: [65, 1, 1],
                ..valid
            },
            ComputeInterface {
                local_size: [8, 8, 2],
                ..valid
            },
        ] {
            assert!(invalid.validate(&limits).is_err(), "{invalid:?}");
        }
        let excessive = vk::PhysicalDeviceLimits {
            max_compute_work_group_size: [u32::MAX; 3],
            max_compute_work_group_invocations: u32::MAX,
            ..limits
        };
        assert!(ComputeInterface {
            local_size: [u32::MAX, 2, 1],
            ..valid
        }
        .validate(&excessive)
        .is_err());
    }

    #[test]
    fn agfx_compute_jobs_reject_bad_roots_and_group_counts() {
        let interface = ComputeInterface {
            buffers: 1,
            root_bytes: 80,
            local_size: [8, 8, 1],
        };
        let limits = compute_limits();
        let valid = ComputeDispatch {
            root: &[0; 80],
            groups: [100, 75, 1],
        };
        assert!(interface.validate_job(&valid, &limits).is_ok());
        assert!(interface
            .validate_job(
                &ComputeDispatch {
                    root: &[0; 76],
                    ..valid
                },
                &limits
            )
            .is_err());
        assert!(interface
            .validate_job(
                &ComputeDispatch {
                    groups: [100, 65536, 1],
                    ..valid
                },
                &limits
            )
            .is_err());
        // Vulkan permits zero groups. The test runner must never count a
        // required rendering case as passed merely because such a job finished.
        assert!(interface
            .validate_job(
                &ComputeDispatch {
                    groups: [0, 75, 1],
                    ..valid
                },
                &limits
            )
            .is_ok());
    }
}
