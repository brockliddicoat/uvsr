//! Classic AGFX f91b108a rendering, with shader-pulled vertices and one sample.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
//! Native boundary U-021. Dynamic rendering uses the existing synchronous queue.
use super::{
    bindings::{Descriptors, Interface, Module},
    texture, vk, Buffer, ComparisonFunction, Completion, Device, Error, GraphicsCapabilities,
    Recording, Sampler, ShaderCode, ShaderStage, Texture, TextureFormat,
};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FillMode {
    #[default]
    Solid,
    Wireframe,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum CullMode {
    #[default]
    None,
    Front,
    Back,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FrontFace {
    #[default]
    Clockwise,
    CounterClockwise,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Topology {
    #[default]
    Triangles,
    Lines,
    Points,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum BlendFactor {
    #[default]
    Zero,
    One,
    SourceColor,
    OneMinusSourceColor,
    DestinationColor,
    OneMinusDestinationColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationAlpha,
    OneMinusDestinationAlpha,
}

impl BlendFactor {
    fn native(self) -> vk::BlendFactor {
        match self {
            Self::Zero => vk::BlendFactor::ZERO,
            Self::One => vk::BlendFactor::ONE,
            Self::SourceColor => vk::BlendFactor::SRC_COLOR,
            Self::OneMinusSourceColor => vk::BlendFactor::ONE_MINUS_SRC_COLOR,
            Self::DestinationColor => vk::BlendFactor::DST_COLOR,
            Self::OneMinusDestinationColor => vk::BlendFactor::ONE_MINUS_DST_COLOR,
            Self::SourceAlpha => vk::BlendFactor::SRC_ALPHA,
            Self::OneMinusSourceAlpha => vk::BlendFactor::ONE_MINUS_SRC_ALPHA,
            Self::DestinationAlpha => vk::BlendFactor::DST_ALPHA,
            Self::OneMinusDestinationAlpha => vk::BlendFactor::ONE_MINUS_DST_ALPHA,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum BlendOperation {
    #[default]
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
}
impl BlendOperation {
    fn native(self) -> vk::BlendOp {
        match self {
            Self::Add => vk::BlendOp::ADD,
            Self::Subtract => vk::BlendOp::SUBTRACT,
            Self::ReverseSubtract => vk::BlendOp::REVERSE_SUBTRACT,
            Self::Min => vk::BlendOp::MIN,
            Self::Max => vk::BlendOp::MAX,
        }
    }
}

/// Defaults retain the source's zero-initialized, disabled blend fields.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct BlendState {
    pub enabled: bool,
    pub source_color: BlendFactor,
    pub destination_color: BlendFactor,
    pub color_operation: BlendOperation,
    pub source_alpha: BlendFactor,
    pub destination_alpha: BlendFactor,
    pub alpha_operation: BlendOperation,
}
impl BlendState {
    fn native(self) -> vk::PipelineColorBlendAttachmentState {
        vk::PipelineColorBlendAttachmentState::default()
            .blend_enable(self.enabled)
            .src_color_blend_factor(self.source_color.native())
            .dst_color_blend_factor(self.destination_color.native())
            .color_blend_op(self.color_operation.native())
            .src_alpha_blend_factor(self.source_alpha.native())
            .dst_alpha_blend_factor(self.destination_alpha.native())
            .alpha_blend_op(self.alpha_operation.native())
            .color_write_mask(vk::ColorComponentFlags::RGBA)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct ColorTarget {
    pub format: TextureFormat,
    pub blend: BlendState,
}

#[derive(Clone, Copy, Debug)]
pub struct DepthState {
    pub format: TextureFormat,
    pub test: bool,
    pub write: bool,
    pub comparison: ComparisonFunction,
}

/// Read-only storage buffers at binding0, sampled images at binding2 and samplers
/// at binding3. The same ordinary layout is used by compute. No native heap ABI.
#[derive(Clone, Copy, Debug, Default)]
pub struct RenderInterface {
    pub buffers: u32,
    pub sampled_images: u32,
    pub samplers: u32,
    pub root_bytes: u32,
}
impl RenderInterface {
    fn resources(self) -> Interface {
        Interface {
            buffers: self.buffers,
            images: 0,
            sampled_images: self.sampled_images,
            samplers: self.samplers,
            root_bytes: self.root_bytes,
        }
    }
}

/// Native source state, with no vertex layout. Index data selects shader vertex
/// IDs. The source fixes rasterization to one sample and line width to one.
#[derive(Clone, Debug, Default)]
pub struct RenderPipelineInfo {
    pub interface: RenderInterface,
    pub fill: FillMode,
    pub cull: CullMode,
    pub front_face: FrontFace,
    pub topology: Topology,
    pub depth_clamp: bool,
    pub depth: Option<DepthState>,
    pub colors: Vec<ColorTarget>,
}
impl RenderPipelineInfo {
    fn validate(
        &self,
        limits: &vk::PhysicalDeviceLimits,
        caps: GraphicsCapabilities,
    ) -> Result<(), Error> {
        if !caps.dynamic_rendering
            || (self.fill == FillMode::Wireframe && !caps.wireframe)
            || (self.depth_clamp && !caps.depth_clamp)
        {
            return Err(Error::Unsupported(
                "requested graphics feature is unavailable".into(),
            ));
        }
        if self.colors.len() > 8
            || self.colors.len() > limits.max_color_attachments as usize
            || self.colors.iter().any(|c| c.format.is_depth())
            || self.depth.is_some_and(|d| !d.format.is_depth())
        {
            return Err(Error::Invalid(
                "graphics attachment formats or counts are invalid",
            ));
        }
        if !caps.independent_blend && self.colors.windows(2).any(|c| c[0].blend != c[1].blend) {
            return Err(Error::Unsupported(
                "independentBlend is required for distinct blend states".into(),
            ));
        }
        self.interface
            .resources()
            .validate(limits, self.colors.len() as u32)
    }
}

pub struct RenderPipeline<'d> {
    device: &'d Device,
    info: RenderPipelineInfo,
    bindings: Descriptors<'d>,
    raw: vk::Pipeline,
    cache: vk::PipelineCache,
}

impl Device {
    /// Create the source's classic vertex/optional-fragment pipeline.
    ///
    /// # Safety
    /// U-021: both modules must be valid for enabled features and their named
    /// stages. Stage interfaces, attachment outputs and the declared descriptor/
    /// root layout must match. Shaders only read declared buffer/image resources
    /// and have no physical, external, atomic or storage-image accesses. A point
    /// vertex shader must write valid PointSize. A nonempty cache must be valid
    /// data previously retrieved from a compatible Vulkan pipeline cache.
    /// Header checks alone do not establish any of these obligations.
    #[allow(unsafe_code)]
    pub unsafe fn render_pipeline(
        &self,
        vertex: ShaderCode<'_>,
        fragment: Option<ShaderCode<'_>>,
        info: RenderPipelineInfo,
        cache: &[u8],
    ) -> Result<RenderPipeline<'_>, Error> {
        info.validate(&self.limits, self.info.graphics)?;
        if vertex.stage != ShaderStage::Vertex
            || fragment
                .as_ref()
                .is_some_and(|s| s.stage != ShaderStage::Fragment)
        {
            return Err(Error::Invalid(
                "classic pipeline requires vertex and optional fragment stages",
            ));
        }
        for (format, blend) in info
            .colors
            .iter()
            .map(|c| (c.format, c.blend.enabled))
            .chain(info.depth.iter().map(|d| (d.format, false)))
        {
            // SAFETY: U-021. Read-only format query on the retained physical
            // device. All format enum values have a concrete native mapping.
            let features = unsafe {
                self.instance
                    .raw
                    .get_physical_device_format_properties(self.physical, format.native())
            }
            .optimal_tiling_features;
            let required = if format.is_depth() {
                vk::FormatFeatureFlags::DEPTH_STENCIL_ATTACHMENT
            } else {
                vk::FormatFeatureFlags::COLOR_ATTACHMENT
            };
            if !features.contains(required)
                || (blend && !features.contains(vk::FormatFeatureFlags::COLOR_ATTACHMENT_BLEND))
            {
                return Err(Error::Unsupported(
                    "attachment format or blending is unsupported".into(),
                ));
            }
        }
        let mut owner = RenderPipeline {
            device: self,
            bindings: Descriptors::new(self, info.interface.resources(), GRAPHICS_STAGES)?,
            info,
            raw: vk::Pipeline::null(),
            cache: vk::PipelineCache::null(),
        };
        let create = vk::PipelineCacheCreateInfo::default().initial_data(cache);
        // SAFETY: U-021. Empty data creates a fresh cache. Nonempty data has the
        // caller's explicit origin/compatibility contract and stays live here.
        owner.cache = unsafe { self.raw.create_pipeline_cache(&create, None) }?;
        // SAFETY: U-021. This constructor requires complete validity for both
        // modules. Lexical owners retain modules through pipeline creation.
        let vs = unsafe { Module::new(self, &vertex) }?;
        let fs = if let Some(shader) = &fragment {
            // SAFETY: U-021. The same checked stage and complete caller contract.
            Some(unsafe { Module::new(self, shader) }?)
        } else {
            None
        };
        let mut stages = vec![vk::PipelineShaderStageCreateInfo::default()
            .stage(vk::ShaderStageFlags::VERTEX)
            .module(vs.raw)
            .name(vertex.entry_point)];
        if let (Some(module), Some(shader)) = (&fs, &fragment) {
            stages.push(
                vk::PipelineShaderStageCreateInfo::default()
                    .stage(vk::ShaderStageFlags::FRAGMENT)
                    .module(module.raw)
                    .name(shader.entry_point),
            );
        }
        let vertex_input = vk::PipelineVertexInputStateCreateInfo::default();
        let assembly = vk::PipelineInputAssemblyStateCreateInfo::default().topology(
            match owner.info.topology {
                Topology::Triangles => vk::PrimitiveTopology::TRIANGLE_LIST,
                Topology::Lines => vk::PrimitiveTopology::LINE_LIST,
                Topology::Points => vk::PrimitiveTopology::POINT_LIST,
            },
        );
        let viewport = vk::PipelineViewportStateCreateInfo::default()
            .viewport_count(1)
            .scissor_count(1);
        let raster = vk::PipelineRasterizationStateCreateInfo::default()
            .depth_clamp_enable(owner.info.depth_clamp)
            .line_width(1.0)
            .polygon_mode(match owner.info.fill {
                FillMode::Solid => vk::PolygonMode::FILL,
                FillMode::Wireframe => vk::PolygonMode::LINE,
            })
            .cull_mode(match owner.info.cull {
                CullMode::None => vk::CullModeFlags::NONE,
                CullMode::Front => vk::CullModeFlags::FRONT,
                CullMode::Back => vk::CullModeFlags::BACK,
            })
            .front_face(match owner.info.front_face {
                FrontFace::Clockwise => vk::FrontFace::CLOCKWISE,
                FrontFace::CounterClockwise => vk::FrontFace::COUNTER_CLOCKWISE,
            });
        let samples = vk::PipelineMultisampleStateCreateInfo::default()
            .rasterization_samples(vk::SampleCountFlags::TYPE_1);
        let mut depth = vk::PipelineDepthStencilStateCreateInfo::default();
        if let Some(state) = owner.info.depth {
            depth = depth
                .depth_test_enable(state.test)
                .depth_write_enable(state.write)
                .depth_compare_op(state.comparison.native());
        }
        let blends: Vec<_> = owner.info.colors.iter().map(|c| c.blend.native()).collect();
        let blend = vk::PipelineColorBlendStateCreateInfo::default().attachments(&blends);
        let dynamic_states = [vk::DynamicState::VIEWPORT, vk::DynamicState::SCISSOR];
        let dynamic = vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dynamic_states);
        let formats: Vec<_> = owner
            .info
            .colors
            .iter()
            .map(|c| c.format.native())
            .collect();
        let mut rendering = vk::PipelineRenderingCreateInfo::default()
            .color_attachment_formats(&formats)
            .depth_attachment_format(
                owner
                    .info
                    .depth
                    .map_or(vk::Format::UNDEFINED, |d| d.format.native()),
            );
        let create = [vk::GraphicsPipelineCreateInfo::default()
            .stages(&stages)
            .vertex_input_state(&vertex_input)
            .input_assembly_state(&assembly)
            .viewport_state(&viewport)
            .rasterization_state(&raster)
            .multisample_state(&samples)
            .depth_stencil_state(&depth)
            .color_blend_state(&blend)
            .dynamic_state(&dynamic)
            .layout(owner.bindings.layout)
            .push_next(&mut rendering)];
        // SAFETY: U-021. Checked enabled features, formats and limits. All native
        // arrays/modules/layout/cache/pNext structs remain live. The caller
        // establishes complete shader linkage and declared interface agreement.
        match unsafe {
            self.raw
                .create_graphics_pipelines(owner.cache, &create, None)
        } {
            Ok(pipelines) => owner.raw = pipelines[0],
            Err((pipelines, error)) => {
                for pipeline in pipelines {
                    // SAFETY: U-021. Partial native outputs were never submitted
                    // or assigned to the owner. Vulkan permits null destruction.
                    unsafe { self.raw.destroy_pipeline(pipeline, None) };
                }
                return Err(error.into());
            }
        }
        Ok(owner)
    }
}

impl RenderPipeline<'_> {
    /// Source GetCache returns Vulkan cache data, not an ISA disassembly.
    #[allow(unsafe_code)]
    pub fn cache_data(&self) -> Result<Vec<u8>, Error> {
        // SAFETY: U-021. Unique live same-device cache, no concurrent access.
        // ash owns the output vector and handles the queried native byte size.
        Ok(unsafe { self.device.raw.get_pipeline_cache_data(self.cache) }?)
    }
}
impl Drop for RenderPipeline<'_> {
    #[allow(unsafe_code)]
    fn drop(&mut self) {
        // SAFETY: U-021. Synchronous completion or pending-work abort precedes
        // destruction. Partial null handles are permitted. Descriptors and their
        // device-retaining lease are dropped after these native children.
        unsafe {
            self.device.raw.destroy_pipeline(self.raw, None);
            self.device.raw.destroy_pipeline_cache(self.cache, None);
        }
    }
}

const GRAPHICS_STAGES: vk::ShaderStageFlags = vk::ShaderStageFlags::from_raw(
    vk::ShaderStageFlags::VERTEX.as_raw() | vk::ShaderStageFlags::FRAGMENT.as_raw(),
);

#[derive(Clone, Copy, Debug)]
pub struct Viewport {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
    pub min_depth: f32,
    pub max_depth: f32,
}
impl Viewport {
    pub fn whole(width: u32, height: u32) -> Self {
        Self {
            x: 0.0,
            y: 0.0,
            width: width as f32,
            height: height as f32,
            min_depth: 0.0,
            max_depth: 1.0,
        }
    }
    fn native(self, limits: &vk::PhysicalDeviceLimits) -> Result<vk::Viewport, Error> {
        let end_x = self.x + self.width;
        let end_y = self.y + self.height;
        let [low, high] = limits.viewport_bounds_range;
        if ![
            self.x,
            self.y,
            self.width,
            self.height,
            self.min_depth,
            self.max_depth,
            end_x,
            end_y,
        ]
        .into_iter()
        .all(f32::is_finite)
            || self.width <= 0.0
            || self.width > limits.max_viewport_dimensions[0] as f32
            || self.height.abs() > limits.max_viewport_dimensions[1] as f32
            || self.x < low
            || end_x > high
            || self.y < low
            || self.y > high
            || end_y < low
            || end_y > high
            || !(0.0..=1.0).contains(&self.min_depth)
            || !(0.0..=1.0).contains(&self.max_depth)
        {
            return Err(Error::Invalid(
                "viewport exceeds native dimensions, bounds or depth range",
            ));
        }
        // Source A-011: flip height and move y to the lower edge. Reverse depth
        // ranges remain legal, as in Vulkan and the source API.
        Ok(vk::Viewport {
            x: self.x,
            y: end_y,
            width: self.width,
            height: -self.height,
            min_depth: self.min_depth,
            max_depth: self.max_depth,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn limits() -> vk::PhysicalDeviceLimits {
        vk::PhysicalDeviceLimits {
            max_color_attachments: 8,
            max_per_stage_resources: 4,
            max_push_constants_size: 128,
            max_viewport_dimensions: [4096; 2],
            viewport_bounds_range: [-8192.0, 8191.0],
            ..Default::default()
        }
    }
    fn caps() -> GraphicsCapabilities {
        GraphicsCapabilities {
            dynamic_rendering: true,
            wireframe: true,
            depth_clamp: true,
            independent_blend: true,
        }
    }

    #[test]
    fn graphics_viewport_preserves_source_flip_and_reverse_depth() {
        let source = Viewport {
            x: 10.0,
            y: 20.0,
            width: 100.0,
            height: 50.0,
            min_depth: 1.0,
            max_depth: 0.0,
        };
        let native = source.native(&limits()).unwrap();
        assert_eq!(
            [
                native.x,
                native.y,
                native.width,
                native.height,
                native.min_depth,
                native.max_depth
            ],
            [10.0, 70.0, 100.0, -50.0, 1.0, 0.0]
        );
        let negative = Viewport {
            height: -50.0,
            ..source
        }
        .native(&limits())
        .unwrap();
        assert_eq!([negative.y, negative.height], [-30.0, 50.0]);
        for invalid in [
            Viewport {
                width: 0.0,
                ..source
            },
            Viewport {
                x: f32::NAN,
                ..source
            },
            Viewport {
                height: 4097.0,
                ..source
            },
            Viewport {
                x: 8150.0,
                ..source
            },
            Viewport {
                y: -8200.0,
                ..source
            },
            Viewport {
                min_depth: -0.01,
                ..source
            },
            Viewport {
                max_depth: f32::INFINITY,
                ..source
            },
        ] {
            assert!(invalid.native(&limits()).is_err());
        }
    }

    #[test]
    fn graphics_scissor_rejects_negative_and_signed_overflow() {
        assert!(Scissor::whole(128, 128).native().is_ok());
        assert!(Scissor::whole(0, 0).native().is_ok());
        for invalid in [
            Scissor {
                x: -1,
                y: 0,
                width: 1,
                height: 1,
            },
            Scissor {
                x: i32::MAX,
                y: 0,
                width: 1,
                height: 1,
            },
            Scissor::whole(1, u32::MAX),
        ] {
            assert!(invalid.native().is_err());
        }
    }

    #[test]
    fn graphics_optional_features_and_combined_resource_limits_are_enforced() {
        let color = ColorTarget {
            format: TextureFormat::Rgba8Unorm,
            blend: BlendState::default(),
        };
        let mut info = RenderPipelineInfo {
            colors: vec![color],
            ..Default::default()
        };
        assert!(info.validate(&limits(), caps()).is_ok());
        info.fill = FillMode::Wireframe;
        assert!(info
            .validate(
                &limits(),
                GraphicsCapabilities {
                    wireframe: false,
                    ..caps()
                }
            )
            .is_err());
        info.depth_clamp = true;
        assert!(info
            .validate(
                &limits(),
                GraphicsCapabilities {
                    depth_clamp: false,
                    ..caps()
                }
            )
            .is_err());
        assert!(info
            .validate(
                &limits(),
                GraphicsCapabilities {
                    dynamic_rendering: false,
                    ..caps()
                }
            )
            .is_err());
        info.colors = vec![color; 5];
        assert!(info.validate(&limits(), caps()).is_err());
        info.colors = vec![color; 2];
        info.colors[1].blend.enabled = true;
        assert!(info
            .validate(
                &limits(),
                GraphicsCapabilities {
                    independent_blend: false,
                    ..caps()
                }
            )
            .is_err());
        info.colors = vec![color];
        info.colors[0].format = TextureFormat::D32Float;
        assert!(info.validate(&limits(), caps()).is_err());
        info.colors = vec![color];
        info.depth = Some(DepthState {
            format: TextureFormat::Rgba8Unorm,
            test: true,
            write: true,
            comparison: ComparisonFunction::Always,
        });
        assert!(info.validate(&limits(), caps()).is_err());
        info.depth = None;
        info.interface.root_bytes = 129;
        assert!(info.validate(&limits(), caps()).is_err());
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Scissor {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}
impl Scissor {
    pub fn whole(width: u32, height: u32) -> Self {
        Self {
            x: 0,
            y: 0,
            width,
            height,
        }
    }
    fn native(self) -> Result<vk::Rect2D, Error> {
        if self.x < 0
            || self.y < 0
            || i64::from(self.x) + i64::from(self.width) > i64::from(i32::MAX)
            || i64::from(self.y) + i64::from(self.height) > i64::from(i32::MAX)
        {
            return Err(Error::Invalid("scissor offset or signed extent is invalid"));
        }
        Ok(vk::Rect2D {
            offset: vk::Offset2D {
                x: self.x,
                y: self.y,
            },
            extent: vk::Extent2D {
                width: self.width,
                height: self.height,
            },
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadOperation {
    Load,
    Clear,
    DontCare,
}
impl LoadOperation {
    fn native(self) -> vk::AttachmentLoadOp {
        match self {
            Self::Load => vk::AttachmentLoadOp::LOAD,
            Self::Clear => vk::AttachmentLoadOp::CLEAR,
            Self::DontCare => vk::AttachmentLoadOp::DONT_CARE,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StoreOperation {
    Store,
    DontCare,
}
impl StoreOperation {
    fn native(self) -> vk::AttachmentStoreOp {
        match self {
            Self::Store => vk::AttachmentStoreOp::STORE,
            Self::DontCare => vk::AttachmentStoreOp::DONT_CARE,
        }
    }
}
pub struct ColorAttachment<'a, 'd> {
    pub texture: &'a mut Texture<'d>,
    pub load: LoadOperation,
    pub store: StoreOperation,
    pub clear: [f32; 4],
}
pub struct DepthAttachment<'a, 'd> {
    pub texture: &'a mut Texture<'d>,
    pub load: LoadOperation,
    pub store: StoreOperation,
    pub clear: f32,
}
#[derive(Default)]
pub struct RenderResources<'a, 'd> {
    pub buffers: &'a [&'a Buffer<'d>],
    pub sampled_images: &'a [&'a Texture<'d>],
    pub samplers: &'a [&'a Sampler<'d>],
}
#[derive(Clone, Copy, Debug)]
pub enum IndexType {
    U16,
    U32,
}
pub enum DrawVertices<'a, 'd> {
    Direct {
        count: u32,
        first: u32,
    },
    Indexed {
        buffer: &'a Buffer<'d>,
        index_type: IndexType,
        count: u32,
        first: u32,
        vertex_offset: i32,
    },
}
pub struct RenderDraw<'a, 'd> {
    pub pipeline: &'a RenderPipeline<'d>,
    pub root: &'a [u8],
    pub viewport: Viewport,
    pub scissor: Scissor,
    pub vertices: DrawVertices<'a, 'd>,
    pub instances: u32,
    pub first_instance: u32,
}

impl Device {
    /// Execute a complete source render pass with ordered draws and shared
    /// read-only shader resources. All pipelines must declare those resources.
    ///
    /// # Safety
    /// U-021: actual vertex/instance/index values, roots and shader accesses must
    /// be in bounds, initialized, correctly typed/aligned and race-free. Shaders
    /// may only read bound resources and write their declared raster outputs.
    /// For a DontCare load, do not blend/read/test undefined prior contents.
    /// A stored DontCare attachment must be completely initialized by the draws,
    /// covering all texels and channels, or safe later readback would be unsound.
    /// These shader/coverage properties cannot be inferred from draw counts.
    #[allow(unsafe_code)]
    pub unsafe fn render<'d>(
        &'d self,
        colors: &mut [ColorAttachment<'_, 'd>],
        mut depth: Option<DepthAttachment<'_, 'd>>,
        resources: &RenderResources<'_, 'd>,
        draws: &[RenderDraw<'_, 'd>],
    ) -> Result<Completion<'d>, Error> {
        if !self.info.graphics.dynamic_rendering {
            return Err(Error::Unsupported("dynamicRendering is unavailable".into()));
        }
        if colors.len() > 8 || colors.len() > self.limits.max_color_attachments as usize {
            return Err(Error::Invalid("too many color attachments"));
        }
        let first = colors
            .first()
            .map(|c| c.texture.info())
            .or_else(|| depth.as_ref().map(|d| d.texture.info()))
            .ok_or(Error::Invalid("render pass needs at least one attachment"))?;
        if first.width > self.limits.max_framebuffer_width
            || first.height > self.limits.max_framebuffer_height
        {
            return Err(Error::Invalid(
                "attachment extent exceeds framebuffer limits",
            ));
        }
        let mut handles = Vec::with_capacity(colors.len() + 1);
        for (texture, load, is_depth) in colors
            .iter()
            .map(|c| (&*c.texture, c.load, false))
            .chain(depth.iter().map(|d| (&*d.texture, d.load, true)))
        {
            let info = texture.info();
            if !std::ptr::eq(texture.device, self)
                || !texture.usage.attachment
                || info.format.is_depth() != is_depth
                || info.width != first.width
                || info.height != first.height
                || (load == LoadOperation::Load && !texture.initialized)
                || handles.contains(&texture.raw)
                || resources
                    .sampled_images
                    .iter()
                    .any(|i| i.raw == texture.raw)
            {
                return Err(Error::Invalid(
                    "attachment owner, usage, extent, initialization or alias is invalid",
                ));
            }
            handles.push(texture.raw);
        }
        if depth.as_ref().is_some_and(|d| {
            d.load == LoadOperation::Clear
                && (!d.clear.is_finite() || !(0.0..=1.0).contains(&d.clear))
        }) {
            return Err(Error::Invalid("depth clear must be finite and in 0..1"));
        }
        let mut dynamics = Vec::with_capacity(draws.len());
        for draw in draws {
            if !std::ptr::eq(draw.pipeline.device, self)
                || draw.root.len() != draw.pipeline.info.interface.root_bytes as usize
                || draw.pipeline.info.colors.len() != colors.len()
                || draw
                    .pipeline
                    .info
                    .colors
                    .iter()
                    .zip(colors.iter())
                    .any(|(p, a)| p.format != a.texture.view_formats().attachment)
                || draw.pipeline.info.depth.map(|d| d.format)
                    != depth.as_ref().map(|d| d.texture.view_formats().attachment)
            {
                return Err(Error::Invalid(
                    "draw pipeline, root or attachment interface differs from the pass",
                ));
            }
            if let DrawVertices::Indexed {
                buffer,
                index_type,
                count,
                first,
                ..
            } = draw.vertices
            {
                let bytes = match index_type {
                    IndexType::U16 => 2,
                    IndexType::U32 => 4,
                };
                if !std::ptr::eq(buffer.device, self)
                    || !buffer.initialized
                    || (u64::from(first) + u64::from(count)) * bytes > buffer.bytes
                {
                    return Err(Error::Invalid(
                        "indexed draw exceeds its initialized same-device index buffer",
                    ));
                }
            }
            dynamics.push((draw.viewport.native(&self.limits)?, draw.scissor.native()?));
        }
        // All native descriptor updates occur before any commands/submission.
        // Repeated pipeline references use the same resources for this pass.
        for draw in draws {
            draw.pipeline.bindings.update(
                resources.buffers,
                &[],
                resources.sampled_images,
                resources.samplers,
            )?;
        }
        let recording = Recording::new(self)?;
        for color in colors.iter() {
            texture::barrier(
                &recording,
                color.texture,
                color.texture.layout(),
                vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            );
        }
        if let Some(d) = &depth {
            texture::barrier(
                &recording,
                d.texture,
                d.texture.layout(),
                vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            );
        }
        let native_colors: Vec<_> = colors
            .iter()
            .map(|c| {
                vk::RenderingAttachmentInfo::default()
                    .image_view(c.texture.attachment_view())
                    .image_layout(vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL)
                    .load_op(c.load.native())
                    .store_op(c.store.native())
                    .clear_value(vk::ClearValue {
                        color: vk::ClearColorValue { float32: c.clear },
                    })
            })
            .collect();
        let native_depth = depth.as_ref().map(|d| {
            vk::RenderingAttachmentInfo::default()
                .image_view(d.texture.attachment_view())
                .image_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                .load_op(d.load.native())
                .store_op(d.store.native())
                .clear_value(vk::ClearValue {
                    depth_stencil: vk::ClearDepthStencilValue {
                        depth: d.clear,
                        stencil: 0,
                    },
                })
        });
        let mut rendering = vk::RenderingInfo::default()
            .render_area(vk::Rect2D {
                offset: vk::Offset2D::default(),
                extent: vk::Extent2D {
                    width: first.width,
                    height: first.height,
                },
            })
            .layer_count(1)
            .color_attachments(&native_colors);
        if let Some(d) = &native_depth {
            rendering = rendering.depth_attachment(d);
        }
        // SAFETY: U-021. Feature enabled, exact nonaliasing same-device views,
        // usage/format/extent/load validity and transitioned layouts. Full area,
        // one layer/sample, no resolves or stencil. Local arrays remain live.
        unsafe { self.raw.cmd_begin_rendering(recording.raw, &rendering) };
        for (draw, (viewport, scissor)) in draws.iter().zip(dynamics) {
            // SAFETY: U-021. Compatible live pipeline/set and validated dynamic
            // state. Descriptor owners and read-only resources outlive completion.
            unsafe {
                self.raw.cmd_bind_pipeline(
                    recording.raw,
                    vk::PipelineBindPoint::GRAPHICS,
                    draw.pipeline.raw,
                );
                self.raw.cmd_bind_descriptor_sets(
                    recording.raw,
                    vk::PipelineBindPoint::GRAPHICS,
                    draw.pipeline.bindings.layout,
                    0,
                    &[draw.pipeline.bindings.set],
                    &[],
                );
                self.raw.cmd_set_viewport(recording.raw, 0, &[viewport]);
                self.raw.cmd_set_scissor(recording.raw, 0, &[scissor]);
            }
            if !draw.root.is_empty() {
                // SAFETY: U-021. Exact checked nonzero aligned layout range for
                // both stages. Caller establishes the shader meaning of bytes.
                unsafe {
                    self.raw.cmd_push_constants(
                        recording.raw,
                        draw.pipeline.bindings.layout,
                        GRAPHICS_STAGES,
                        0,
                        draw.root,
                    )
                };
            }
            match draw.vertices {
                DrawVertices::Direct { count, first } => {
                    // SAFETY: U-021. Live rendering and compatible pipeline.
                    // Caller proves shader bounds for all actual vertex/instance
                    // IDs and roots. Outputs obey the attachment contract.
                    unsafe {
                        self.raw.cmd_draw(
                            recording.raw,
                            count,
                            draw.instances,
                            first,
                            draw.first_instance,
                        )
                    };
                }
                DrawVertices::Indexed {
                    buffer,
                    index_type,
                    count,
                    first,
                    vertex_offset,
                } => {
                    // SAFETY: U-021. Buffer has INDEX_BUFFER usage, aligned zero
                    // offset and checked complete initialized range. Caller proves
                    // the shader bounds of loaded index + signed vertex offset.
                    unsafe {
                        self.raw.cmd_bind_index_buffer(
                            recording.raw,
                            buffer.raw,
                            0,
                            match index_type {
                                IndexType::U16 => vk::IndexType::UINT16,
                                IndexType::U32 => vk::IndexType::UINT32,
                            },
                        );
                        self.raw.cmd_draw_indexed(
                            recording.raw,
                            count,
                            draw.instances,
                            first,
                            vertex_offset,
                            draw.first_instance,
                        );
                    }
                }
            }
        }
        // SAFETY: U-021. Exactly one rendering instance was begun above, with
        // no intervening fallible native calls or nested rendering operations.
        unsafe { self.raw.cmd_end_rendering(recording.raw) };
        for c in colors.iter() {
            texture::barrier(
                &recording,
                c.texture,
                vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
                vk::ImageLayout::GENERAL,
            );
        }
        if let Some(d) = &depth {
            texture::barrier(
                &recording,
                d.texture,
                vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                vk::ImageLayout::GENERAL,
            );
        }
        let complete = recording.finish()?;
        for c in colors.iter_mut() {
            c.texture.initialized = c.store == StoreOperation::Store;
        }
        if let Some(d) = &mut depth {
            d.texture.initialized = d.store == StoreOperation::Store;
        }
        Ok(complete)
    }
}
