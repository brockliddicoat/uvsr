//! Native ownership boundary U-010. One queue, one thread, completed operations.
use crate::{validate_copy, CopyRegion, Error};
use ash::vk;
use std::{cell::Cell, ffi::CStr, io::Write, marker::PhantomData, rc::Rc};

const API_VERSION: u32 = vk::make_api_version(0, 1, 4, 0);

mod compute;
pub use compute::{
    BufferCompute, ComputeDispatch, ComputeInterface, ComputeRoot, ShaderCode, ShaderStage,
    StorageCompute,
};

#[derive(Clone, Debug)]
pub struct DeviceInfo {
    pub name: String,
    pub loader_api_version: u32,
    pub api_version: u32,
    pub driver_version: u32,
    pub vendor_id: u32,
    pub device_id: u32,
    pub queue_family: u32,
    pub validation: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Memory {
    Upload,
    Device,
    Readback,
}

struct Instance {
    raw: ash::Instance,
    api_version: u32,
    debug: Option<(ash::ext::debug_utils::Instance, vk::DebugUtilsMessengerEXT)>,
    // The dynamically loaded entry must outlive every instance/device call.
    _entry: ash::Entry,
}

/// A single-threaded Vulkan 1.4 device with a graphics/compute queue.
/// All submissions finish before returning. Native handles never escape.
///
/// ```compile_fail
/// fn move_to_worker(device: agfx::Device) {
///     std::thread::spawn(move || drop(device));
/// }
/// ```
/// ```compile_fail
/// fn share_with_worker(device: &agfx::Device) {
///     std::thread::scope(|scope| { scope.spawn(|| { let _ = device.info(); }); });
/// }
/// ```
pub struct Device {
    raw: ash::Device,
    instance: Instance,
    queue: vk::Queue,
    pool: vk::CommandPool,
    timeline: vk::Semaphore,
    value: Cell<u64>,
    memory: vk::PhysicalDeviceMemoryProperties,
    limits: vk::PhysicalDeviceLimits,
    info: DeviceInfo,
    // Marker only. No allocation or shared ownership. Native host calls cannot
    // race through either Device or the owners borrowing it.
    _single_thread: PhantomData<Rc<()>>,
}

/// A unique buffer/allocation borrowing its device. Mapping never escapes.
///
/// ```compile_fail
/// let buffer = {
///     let device = agfx::Device::new(false).unwrap();
///     device.buffer(256, agfx::Memory::Upload).unwrap()
/// };
/// drop(buffer);
/// ```
pub struct Buffer<'d> {
    device: &'d Device,
    raw: vk::Buffer,
    allocation: vk::DeviceMemory,
    bytes: u64,
    memory: Memory,
    memory_flags: vk::MemoryPropertyFlags,
    initialized: bool,
}

/// Proof that one synchronous operation finished on this exact device.
pub struct Completion<'d> {
    device: &'d Device,
    value: u64,
}

impl Completion<'_> {
    pub fn value(&self) -> u64 {
        self.value
    }

    pub fn belongs_to(&self, device: &Device) -> bool {
        std::ptr::eq(self.device, device)
    }
}

// The Vulkan callback can run on driver threads. It uses no device/user data,
// mutable global state, or panicking output path.
#[allow(unsafe_code)]
unsafe extern "system" fn validation_message(
    _severity: vk::DebugUtilsMessageSeverityFlagsEXT,
    _kind: vk::DebugUtilsMessageTypeFlagsEXT,
    data: *const vk::DebugUtilsMessengerCallbackDataEXT<'_>,
    _user: *mut std::ffi::c_void,
) -> vk::Bool32 {
    if !data.is_null() {
        // SAFETY: U-010. Vulkan supplies live callback data for this call only.
        let message = unsafe { (*data).p_message };
        if !message.is_null() {
            // SAFETY: U-010. Vulkan guarantees a terminated message for the
            // duration of the callback. The borrowed bytes do not escape.
            let bytes = unsafe { CStr::from_ptr(message) }.to_bytes();
            let mut stderr = std::io::stderr().lock();
            let _ = stderr.write_all(b"AGFX validation: ");
            let _ = stderr.write_all(bytes);
            let _ = stderr.write_all(b"\n");
        }
    }
    vk::FALSE
}

impl Instance {
    #[allow(unsafe_code)]
    fn new(validation: bool) -> Result<Self, Error> {
        // SAFETY: U-010. Load the platform Vulkan loader. Entry stays owned
        // until all calls using its function pointers have ended.
        let entry = unsafe { ash::Entry::load() }.map_err(|e| Error::Loader(e.to_string()))?;
        // SAFETY: U-010. Entry owns the loaded function table; no instance needed.
        let version =
            unsafe { entry.try_enumerate_instance_version() }?.unwrap_or(vk::API_VERSION_1_0);
        if version < API_VERSION {
            return Err(Error::Unsupported("Vulkan 1.4 loader required".into()));
        }
        let mut layers = Vec::new();
        let mut extensions = Vec::new();
        if validation {
            // SAFETY: U-010. Read-only enumeration on the live loader.
            let available = unsafe { entry.enumerate_instance_layer_properties() }?;
            if !available.iter().any(|layer| {
                layer.layer_name_as_c_str().ok() == Some(c"VK_LAYER_KHRONOS_validation")
            }) {
                return Err(Error::Unsupported(
                    "VK_LAYER_KHRONOS_validation required".into(),
                ));
            }
            layers.push(c"VK_LAYER_KHRONOS_validation".as_ptr());
            // The requested extensions must be provided by the loader or layer.
            // SAFETY: U-010. The layer name was found and remains live.
            let mut available = unsafe { entry.enumerate_instance_extension_properties(None) }?;
            // SAFETY: U-010. Same live loader and known, terminated layer name.
            available.extend(unsafe {
                entry.enumerate_instance_extension_properties(Some(c"VK_LAYER_KHRONOS_validation"))
            }?);
            for name in [
                ash::ext::debug_utils::NAME,
                ash::ext::validation_features::NAME,
            ] {
                if !available
                    .iter()
                    .any(|ext| ext.extension_name_as_c_str().ok() == Some(name))
                {
                    return Err(Error::Unsupported(format!(
                        "instance extension {} required",
                        name.to_string_lossy()
                    )));
                }
                extensions.push(name.as_ptr());
            }
        }
        let app = vk::ApplicationInfo::default()
            .application_name(c"theta agfx slice")
            .api_version(API_VERSION);
        let enabled = [vk::ValidationFeatureEnableEXT::SYNCHRONIZATION_VALIDATION];
        let mut validation_features =
            vk::ValidationFeaturesEXT::default().enabled_validation_features(&enabled);
        let mut debug = vk::DebugUtilsMessengerCreateInfoEXT::default()
            .message_severity(
                vk::DebugUtilsMessageSeverityFlagsEXT::WARNING
                    | vk::DebugUtilsMessageSeverityFlagsEXT::ERROR,
            )
            .message_type(
                vk::DebugUtilsMessageTypeFlagsEXT::GENERAL
                    | vk::DebugUtilsMessageTypeFlagsEXT::VALIDATION
                    | vk::DebugUtilsMessageTypeFlagsEXT::PERFORMANCE,
            )
            .pfn_user_callback(Some(validation_message));
        let mut create = vk::InstanceCreateInfo::default()
            .application_info(&app)
            .enabled_layer_names(&layers)
            .enabled_extension_names(&extensions);
        if validation {
            create = create
                .push_next(&mut validation_features)
                .push_next(&mut debug);
        }
        // SAFETY: U-010. All pointed-to names, arrays, application and optional
        // pNext nodes are live through the call and their extensions are enabled.
        let raw = unsafe { entry.create_instance(&create, None) }?;
        let mut owner = Self {
            raw,
            api_version: version,
            debug: None,
            _entry: entry,
        };
        if validation {
            debug.p_next = std::ptr::null();
            let loader = ash::ext::debug_utils::Instance::new(&owner._entry, &owner.raw);
            // SAFETY: U-010. Enabled extension, valid callback, no user pointer.
            let messenger = unsafe { loader.create_debug_utils_messenger(&debug, None) }?;
            owner.debug = Some((loader, messenger));
        }
        Ok(owner)
    }
}

impl Drop for Instance {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        if let Some((loader, messenger)) = &self.debug {
            // SAFETY: U-010. Device destruction has ended. The unique messenger
            // is still live and no callback retains borrowed data.
            unsafe { loader.destroy_debug_utils_messenger(*messenger, None) };
        }
        // SAFETY: U-010. The device and messenger are destroyed before instance.
        // Entry remains live as a field until this method finishes.
        unsafe { self.raw.destroy_instance(None) };
    }
}

impl Device {
    #[allow(unsafe_code)]
    pub fn new(validation: bool) -> Result<Self, Error> {
        let instance = Instance::new(validation)?;
        // SAFETY: U-010. The instance owner is live for all physical queries.
        let physicals = unsafe { instance.raw.enumerate_physical_devices() }?;
        let mut selected = None;
        // AGFX f91b108a device selection, A-012: preserve preference order and
        // enumeration order within each kind, checking this slice's requirements.
        'devices: for kind in [
            vk::PhysicalDeviceType::DISCRETE_GPU,
            vk::PhysicalDeviceType::INTEGRATED_GPU,
            vk::PhysicalDeviceType::VIRTUAL_GPU,
            vk::PhysicalDeviceType::CPU,
        ] {
            for &physical in &physicals {
                // SAFETY: U-010. Physical handle came from this live instance.
                let properties = unsafe { instance.raw.get_physical_device_properties(physical) };
                if properties.device_type != kind || properties.api_version < API_VERSION {
                    continue;
                }
                // SAFETY: U-010. Same physical device; returns owned property data.
                let families = unsafe {
                    instance
                        .raw
                        .get_physical_device_queue_family_properties(physical)
                };
                let Some(family) = families.iter().position(|q| {
                    q.queue_count > 0
                        && q.queue_flags
                            .contains(vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE)
                }) else {
                    continue;
                };
                let mut features12 = vk::PhysicalDeviceVulkan12Features::default();
                let mut features =
                    vk::PhysicalDeviceFeatures2::default().push_next(&mut features12);
                // SAFETY: U-010. Supported core structure with a live mutable pNext.
                unsafe {
                    instance
                        .raw
                        .get_physical_device_features2(physical, &mut features)
                };
                if features
                    .features
                    .shader_storage_buffer_array_dynamic_indexing
                    == 0
                    || features12.timeline_semaphore == 0
                    || features12.vulkan_memory_model == 0
                    || features12.runtime_descriptor_array == 0
                {
                    continue;
                }
                selected = Some((physical, properties, family as u32));
                break 'devices;
            }
        }
        let (physical, properties, family) = selected.ok_or_else(|| Error::Unsupported("Vulkan 1.4 graphics/compute queue, timelineSemaphore, vulkanMemoryModel, runtimeDescriptorArray and shaderStorageBufferArrayDynamicIndexing required".into()))?;
        let name = properties
            .device_name_as_c_str()
            .map_err(|_| Error::Invalid("unterminated adapter name"))?
            .to_string_lossy()
            .into_owned();
        let priorities = [1.0];
        let queues = [vk::DeviceQueueCreateInfo::default()
            .queue_family_index(family)
            .queue_priorities(&priorities)];
        let core = vk::PhysicalDeviceFeatures::default()
            .shader_storage_buffer_array_dynamic_indexing(true);
        let mut features12 = vk::PhysicalDeviceVulkan12Features::default()
            .timeline_semaphore(true)
            .vulkan_memory_model(true)
            .runtime_descriptor_array(true);
        let create = vk::DeviceCreateInfo::default()
            .queue_create_infos(&queues)
            .enabled_features(&core)
            .push_next(&mut features12);
        // SAFETY: U-010. Only queried features and the valid selected queue are
        // enabled. The borrowed create-info graph lives through the call.
        let raw = unsafe { instance.raw.create_device(physical, &create, None) }?;
        // SAFETY: U-010. Queue zero was created in this exact family/device.
        let queue = unsafe { raw.get_device_queue(family, 0) };
        // SAFETY: U-010. Read-only physical query while its instance remains live.
        let memory = unsafe { instance.raw.get_physical_device_memory_properties(physical) };
        let loader_api_version = instance.api_version;
        let mut owner = Self {
            raw,
            instance,
            queue,
            pool: vk::CommandPool::null(),
            timeline: vk::Semaphore::null(),
            value: Cell::new(0),
            memory,
            limits: properties.limits,
            info: DeviceInfo {
                name,
                loader_api_version,
                api_version: properties.api_version,
                driver_version: properties.driver_version,
                vendor_id: properties.vendor_id,
                device_id: properties.device_id,
                queue_family: family,
                validation,
            },
            _single_thread: PhantomData,
        };
        let create = vk::CommandPoolCreateInfo::default()
            .queue_family_index(family)
            .flags(vk::CommandPoolCreateFlags::TRANSIENT);
        // SAFETY: U-010. Pool family matches the owned queue. Partial errors drop
        // the already-owned device; no pending work exists during construction.
        owner.pool = unsafe { owner.raw.create_command_pool(&create, None) }?;
        let mut kind = vk::SemaphoreTypeCreateInfo::default()
            .semaphore_type(vk::SemaphoreType::TIMELINE)
            .initial_value(0);
        let create = vk::SemaphoreCreateInfo::default().push_next(&mut kind);
        // SAFETY: U-010. timelineSemaphore was queried/enabled; pNext is live.
        owner.timeline = unsafe { owner.raw.create_semaphore(&create, None) }?;
        Ok(owner)
    }

    pub fn info(&self) -> &DeviceInfo {
        &self.info
    }

    #[allow(unsafe_code)]
    pub fn buffer(&self, bytes: u64, memory: Memory) -> Result<Buffer<'_>, Error> {
        if bytes == 0 || bytes & 3 != 0 || bytes > isize::MAX as u64 {
            return Err(Error::Invalid(
                "buffer size must be nonzero, four-byte aligned and host-representable",
            ));
        }
        let mut owner = Buffer {
            device: self,
            raw: vk::Buffer::null(),
            allocation: vk::DeviceMemory::null(),
            bytes,
            memory,
            memory_flags: vk::MemoryPropertyFlags::empty(),
            initialized: false,
        };
        let create = vk::BufferCreateInfo::default()
            .size(bytes)
            .usage(
                vk::BufferUsageFlags::TRANSFER_SRC
                    | vk::BufferUsageFlags::TRANSFER_DST
                    | vk::BufferUsageFlags::STORAGE_BUFFER,
            )
            .sharing_mode(vk::SharingMode::EXCLUSIVE);
        // SAFETY: U-010. Valid nonzero size/usage; one family owns every operation.
        owner.raw = unsafe { self.raw.create_buffer(&create, None) }?;
        // SAFETY: U-010. This live buffer belongs to the queried device.
        let requirements = unsafe { self.raw.get_buffer_memory_requirements(owner.raw) };
        let required = match memory {
            Memory::Device => vk::MemoryPropertyFlags::DEVICE_LOCAL,
            _ => vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
        };
        let preferred = if memory == Memory::Readback {
            required | vk::MemoryPropertyFlags::HOST_CACHED
        } else {
            required
        };
        let compatible = |flags| {
            (0..self.memory.memory_type_count).find(|&i| {
                requirements.memory_type_bits & (1 << i) != 0
                    && self.memory.memory_types[i as usize]
                        .property_flags
                        .contains(flags)
            })
        };
        let index = compatible(preferred)
            .or_else(|| compatible(required))
            .ok_or_else(|| Error::Unsupported(format!("no compatible {memory:?} memory type")))?;
        owner.memory_flags = self.memory.memory_types[index as usize].property_flags;
        let allocate = vk::MemoryAllocateInfo::default()
            .allocation_size(requirements.size)
            .memory_type_index(index);
        // SAFETY: U-010. Queried type bits, full required size and separate
        // allocation. Offset zero satisfies the required binding alignment.
        owner.allocation = unsafe { self.raw.allocate_memory(&allocate, None) }?;
        // SAFETY: U-010. Unique unbound buffer, compatible complete allocation.
        unsafe { self.raw.bind_buffer_memory(owner.raw, owner.allocation, 0) }?;
        Ok(owner)
    }

    #[allow(unsafe_code)]
    pub fn copy<'d>(
        &'d self,
        source: &Buffer<'d>,
        destination: &mut Buffer<'d>,
        regions: &[CopyRegion],
    ) -> Result<Completion<'d>, Error> {
        if !std::ptr::eq(source.device, self) || !std::ptr::eq(destination.device, self) {
            return Err(Error::Invalid("copy buffers belong to another device"));
        }
        if source.raw == destination.raw || !source.initialized {
            return Err(Error::Invalid(
                "copy needs distinct buffers and an initialized source",
            ));
        }
        if regions.len() > u32::MAX as usize {
            return Err(Error::Invalid("too many copy regions"));
        }
        let covers_all = validate_copy(source.bytes, destination.bytes, regions)?;
        if !destination.initialized && !covers_all {
            return Err(Error::Invalid(
                "copy must initialize the complete destination",
            ));
        }
        let copies: Vec<_> = regions
            .iter()
            .map(|r| vk::BufferCopy {
                src_offset: r.source,
                dst_offset: r.destination,
                size: r.bytes,
            })
            .collect();
        let recording = Recording::new(self)?;
        let before = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::MEMORY_WRITE | vk::AccessFlags::HOST_WRITE)
            .dst_access_mask(vk::AccessFlags::TRANSFER_READ | vk::AccessFlags::TRANSFER_WRITE)];
        // SAFETY: U-010. Recording outside a render pass on the owning family.
        // Earlier host/device writes become visible to this copy; execution
        // dependency also orders prior reads before destination overwrite.
        unsafe {
            self.raw.cmd_pipeline_barrier(
                recording.raw,
                vk::PipelineStageFlags::ALL_COMMANDS | vk::PipelineStageFlags::HOST,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &before,
                &[],
                &[],
            )
        };
        // SAFETY: U-010. Same-device, distinct unique allocations, transfer usage,
        // initialized source, checked bounds and disjoint destination regions.
        // Both owners remain borrowed until this command has completed.
        unsafe {
            self.raw
                .cmd_copy_buffer(recording.raw, source.raw, destination.raw, &copies)
        };
        let after = [vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
            .dst_access_mask(
                vk::AccessFlags::MEMORY_READ
                    | vk::AccessFlags::MEMORY_WRITE
                    | vk::AccessFlags::HOST_READ,
            )];
        // SAFETY: U-010. Transfer writes become visible to later commands and
        // coherent host reads. The synchronous wait separately proves completion.
        unsafe {
            self.raw.cmd_pipeline_barrier(
                recording.raw,
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::ALL_COMMANDS | vk::PipelineStageFlags::HOST,
                vk::DependencyFlags::empty(),
                &after,
                &[],
                &[],
            )
        };
        let completion = recording.finish()?;
        destination.initialized = true;
        Ok(completion)
    }
}

impl Drop for Device {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-010. All operations completed synchronously; uncertain
        // submissions abort. Borrows prevent live Buffer/Recording owners here.
        // Null children are permitted for partial initialization cleanup.
        unsafe {
            self.raw.destroy_semaphore(self.timeline, None);
            self.raw.destroy_command_pool(self.pool, None);
            self.raw.destroy_device(None);
        }
        // Keep the instance visibly owned until after the device calls above.
        let _ = &self.instance;
    }
}

impl Buffer<'_> {
    pub fn len(&self) -> u64 {
        self.bytes
    }
    pub fn is_empty(&self) -> bool {
        false
    }
    pub fn memory_flags(&self) -> vk::MemoryPropertyFlags {
        self.memory_flags
    }

    #[allow(unsafe_code)]
    pub fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
        if self.memory != Memory::Upload || bytes.len() as u64 != self.bytes {
            return Err(Error::Invalid("write needs an exact full upload buffer"));
        }
        // SAFETY: U-010. Unique coherent host-visible allocation, never currently
        // mapped or in flight, with a live complete range starting at offset zero.
        let pointer = unsafe {
            self.device
                .raw
                .map_memory(self.allocation, 0, self.bytes, vk::MemoryMapFlags::empty())
        }?;
        // SAFETY: U-010. Both byte ranges are complete and disjoint. Native maps
        // never escape, so the caller cannot provide a slice into this allocation.
        unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), pointer.cast::<u8>(), bytes.len()) };
        // SAFETY: U-010. This exact allocation was mapped above; no access escapes.
        unsafe { self.device.raw.unmap_memory(self.allocation) };
        self.initialized = true;
        Ok(())
    }

    #[allow(unsafe_code)]
    pub fn read(&mut self) -> Result<Vec<u8>, Error> {
        if self.memory != Memory::Readback || !self.initialized {
            return Err(Error::Invalid(
                "read needs a completely initialized readback buffer",
            ));
        }
        let mut result = vec![0_u8; self.bytes as usize];
        // SAFETY: U-010. All bytes were initialized by completed writes with a
        // host visibility dependency. No concurrent mapping or GPU access exists.
        let pointer = unsafe {
            self.device
                .raw
                .map_memory(self.allocation, 0, self.bytes, vk::MemoryMapFlags::empty())
        }?;
        // SAFETY: U-010. Complete initialized native bytes and a disjoint owned
        // vector of the checked size; no typed representation is fabricated.
        unsafe {
            std::ptr::copy_nonoverlapping(pointer.cast::<u8>(), result.as_mut_ptr(), result.len())
        };
        // SAFETY: U-010. Matching live mapping; the returned vector owns its copy.
        unsafe { self.device.raw.unmap_memory(self.allocation) };
        Ok(result)
    }
}

impl Drop for Buffer<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-010. No pending uses or escaping map. Device borrow remains
        // live. Destroy the unique buffer before freeing its allocation, including
        // null handles during partial construction failure.
        unsafe {
            self.device.raw.destroy_buffer(self.raw, None);
            self.device.raw.free_memory(self.allocation, None);
        }
    }
}

struct Recording<'d> {
    device: &'d Device,
    raw: vk::CommandBuffer,
    pending: bool,
}

impl<'d> Recording<'d> {
    #[allow(unsafe_code)]
    fn new(device: &'d Device) -> Result<Self, Error> {
        let allocate = vk::CommandBufferAllocateInfo::default()
            .command_pool(device.pool)
            .level(vk::CommandBufferLevel::PRIMARY)
            .command_buffer_count(1);
        // SAFETY: U-010. Pool is live, same-thread and same-family; count is one.
        let raw = unsafe { device.raw.allocate_command_buffers(&allocate) }?[0];
        let owner = Self {
            device,
            raw,
            pending: false,
        };
        let begin = vk::CommandBufferBeginInfo::default()
            .flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT);
        // SAFETY: U-010. Fresh primary command buffer, not submitted or recording.
        unsafe { device.raw.begin_command_buffer(raw, &begin) }?;
        Ok(owner)
    }

    #[allow(unsafe_code)]
    fn finish(mut self) -> Result<Completion<'d>, Error> {
        let value = self
            .device
            .value
            .get()
            .checked_add(1)
            .ok_or(Error::Invalid("timeline exhausted"))?;
        // SAFETY: U-010. The private recorder is valid and currently recording.
        unsafe { self.device.raw.end_command_buffer(self.raw) }?;
        let commands = [self.raw];
        let semaphores = [self.device.timeline];
        let values = [value];
        let mut timeline =
            vk::TimelineSemaphoreSubmitInfo::default().signal_semaphore_values(&values);
        let submit = [vk::SubmitInfo::default()
            .command_buffers(&commands)
            .signal_semaphores(&semaphores)
            .push_next(&mut timeline)];
        self.pending = true;
        // SAFETY: U-010. Same-device executable command, no pending reuse. Signal
        // value strictly increases by one after every completed submission.
        // Stack submit arrays live through the call; all resources through wait.
        let submitted = unsafe {
            self.device
                .raw
                .queue_submit(self.device.queue, &submit, vk::Fence::null())
        };
        if submitted.is_err() {
            std::process::abort();
        }
        let wait = vk::SemaphoreWaitInfo::default()
            .semaphores(&semaphores)
            .values(&values);
        // SAFETY: U-010. Live timeline and its just-submitted signal value. No
        // resources are retired or host-accessed before successful completion.
        let completed = unsafe { self.device.raw.wait_semaphores(&wait, u64::MAX) };
        if completed.is_err() {
            std::process::abort();
        }
        self.pending = false;
        self.device.value.set(value);
        Ok(Completion {
            device: self.device,
            value,
        })
    }
}

impl Drop for Recording<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // Also prevents unwinding from ever freeing potentially pending commands.
        if self.pending {
            std::process::abort();
        }
        // SAFETY: U-010. Never submitted, or its timeline wait completed. Same
        // live pool/device, one uniquely owned command buffer, no host races.
        unsafe {
            self.device
                .raw
                .free_command_buffers(self.device.pool, &[self.raw])
        };
    }
}
