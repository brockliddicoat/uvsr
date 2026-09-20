//! Ordinary storage buffer/image compute owners. Native boundary U-016, with the
//! source four-pass safe wrapper retained under U-011.
use super::bindings::{Descriptors, Interface, Module};
use super::{Buffer, Completion, Device, Error, Memory, Recording, Sampler, Texture};
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

/// Ordinary set0 storage buffers at binding0, storage images at binding1,
/// sampled images at binding2, samplers at binding3, and inline root bytes.
/// This is separate from NGAPI native heaps.
#[derive(Clone, Copy, Debug)]
pub struct ComputeInterface {
    pub buffers: u32,
    pub images: u32,
    pub sampled_images: u32,
    pub samplers: u32,
    pub root_bytes: u32,
    pub local_size: [u32; 3],
}

impl ComputeInterface {
    fn resources(self) -> Interface {
        Interface {
            buffers: self.buffers,
            images: self.images,
            sampled_images: self.sampled_images,
            samplers: self.samplers,
            root_bytes: self.root_bytes,
        }
    }

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
        self.resources().validate(limits, 0)?;
        if self.buffers == 0 && self.images == 0 && self.sampled_images == 0 {
            return Err(Error::Invalid(
                "compute requires at least one shader resource",
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
    bindings: Descriptors<'d>,
    pipeline: vk::Pipeline,
}

/// Source four-pass fixture wrapper with enforced root and buffer bounds.
pub struct BufferCompute<'d> {
    inner: StorageCompute<'d>,
}

impl Device {
    /// Create an ordinary storage buffer/image pipeline.
    ///
    /// # Safety
    /// U-016: the complete SPIR-V must be valid for the device's enabled
    /// features. Its named compute entry uses exactly the declared local size,
    /// only set0/binding0 storage buffers, binding1 storage images, binding2
    /// sampled images, binding3 samplers, and at
    /// most the declared root bytes. Descriptor counts match this interface.
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
            bindings: Descriptors::new(self, interface.resources(), vk::ShaderStageFlags::COMPUTE)?,
            pipeline: vk::Pipeline::null(),
        };
        // SAFETY: U-016. Constructor's caller proves complete shader validity
        // for the declared interface and enabled features.
        let module = unsafe { Module::new(self, &shader) }?;
        let stage = vk::PipelineShaderStageCreateInfo::default()
            .stage(vk::ShaderStageFlags::COMPUTE)
            .module(module.raw)
            .name(shader.entry_point);
        let create = [vk::ComputePipelineCreateInfo::default()
            .stage(stage)
            .layout(owner.bindings.layout)];
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
                    images: 0,
                    sampled_images: 0,
                    samplers: 0,
                    root_bytes: 16,
                    local_size: [64, 1, 1],
                },
            )
        }?;
        Ok(BufferCompute { inner })
    }
}

impl<'d> StorageCompute<'d> {
    /// Submit ordered dispatches using complete initialized buffers/images.
    /// A full compute dependency separates each job. Completion precedes return.
    ///
    /// # Safety
    /// U-016: for every job, the caller must establish that the shader's actual
    /// root values and invocation IDs select only the bound initialized ranges,
    /// with valid types, alignment, synchronization and race-free accesses.
    /// Every image access must match its bound format, shape and subresources.
    /// Sampling instructions must use compatible floating-point, normalized
    /// color images and non-comparison samplers. Sampled images are read-only.
    /// No invocation may access outside a bound image or race another access.
    /// Every shader-dependent index, offset and cross-invocation dependency
    /// must be valid. The method checks resource identity, native limits, root
    /// size and host retirement, but cannot prove arbitrary shader semantics.
    #[allow(unsafe_code)]
    pub unsafe fn dispatch(
        &mut self,
        buffers: &mut [&mut Buffer<'_>],
        images: &mut [&mut Texture<'_>],
        sampled_images: &[&Texture<'_>],
        samplers: &[&Sampler<'_>],
        jobs: &[ComputeDispatch<'_>],
    ) -> Result<Completion<'d>, Error> {
        if jobs.is_empty() {
            return Err(Error::Invalid("compute requires at least one job"));
        }
        for job in jobs {
            self.interface.validate_job(job, &self.device.limits)?;
        }
        self.bindings.update(
            &buffers.iter().map(|b| &**b).collect::<Vec<_>>(),
            &images.iter().map(|i| &**i).collect::<Vec<_>>(),
            sampled_images,
            samplers,
        )?;
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
                self.bindings.layout,
                0,
                &[self.bindings.set],
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
                        self.bindings.layout,
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
        unsafe { self.inner.dispatch(&mut buffers, &mut [], &[], &[], &jobs) }
    }
}

impl Drop for StorageCompute<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-016. All dispatches completed synchronously; uncertain
        // waits abort. The pipeline is destroyed before its descriptor fields,
        // including the lease retaining the borrowed Device.
        unsafe { self.device.raw.destroy_pipeline(self.pipeline, None) };
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
            max_per_stage_descriptor_storage_images: 4,
            max_descriptor_set_storage_images: 4,
            max_per_stage_descriptor_sampled_images: 4,
            max_descriptor_set_sampled_images: 4,
            max_per_stage_descriptor_samplers: 4,
            max_descriptor_set_samplers: 4,
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
            images: 0,
            sampled_images: 0,
            samplers: 0,
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
    fn agfx_compute_images_and_buffers_share_stage_limits() {
        let image_only = ComputeInterface {
            buffers: 0,
            images: 4,
            sampled_images: 0,
            samplers: 0,
            root_bytes: 0,
            local_size: [1; 3],
        };
        let limits = compute_limits();
        assert!(image_only.validate(&limits).is_ok());
        assert!(ComputeInterface {
            images: 5,
            ..image_only
        }
        .validate(&limits)
        .is_err());
        assert!(ComputeInterface {
            buffers: 1,
            ..image_only
        }
        .validate(&limits)
        .is_err());
        assert!(ComputeInterface {
            buffers: 2,
            images: 2,
            ..image_only
        }
        .validate(&limits)
        .is_ok());
        let lower_set_limit = vk::PhysicalDeviceLimits {
            max_descriptor_set_storage_images: 3,
            ..limits
        };
        assert!(image_only.validate(&lower_set_limit).is_err());
    }

    #[test]
    fn agfx_compute_jobs_reject_bad_roots_and_group_counts() {
        let interface = ComputeInterface {
            buffers: 1,
            images: 0,
            sampled_images: 0,
            samplers: 0,
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

    #[test]
    fn agfx_sampled_counts_and_separate_sampler_limits() {
        let limits = compute_limits();
        let valid = ComputeInterface {
            buffers: 0,
            images: 1,
            sampled_images: 3,
            samplers: 4,
            root_bytes: 48,
            local_size: [8, 8, 1],
        };
        // Separate samplers do not consume maxPerStageResources.
        assert!(valid.validate(&limits).is_ok());
        for invalid in [
            ComputeInterface {
                sampled_images: 4,
                ..valid
            },
            ComputeInterface {
                samplers: 5,
                ..valid
            },
        ] {
            assert!(invalid.validate(&limits).is_err());
        }
        assert!(valid
            .validate(&vk::PhysicalDeviceLimits {
                max_descriptor_set_sampled_images: 2,
                ..limits
            })
            .is_err());
        assert!(valid
            .validate(&vk::PhysicalDeviceLimits {
                max_descriptor_set_samplers: 3,
                ..limits
            })
            .is_err());
    }
}
