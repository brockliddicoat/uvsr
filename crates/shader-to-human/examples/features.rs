//! Native Vulkan Features, source Electronic Arts 2024-2025, BSD-3-Clause.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::{
    BlendState, Buffer, ColorAttachment, ColorTarget, ComputeDispatch, ComputeInterface,
    CopyRegion, Device, DrawVertices, FrontFace, LoadOperation, Memory, RenderDraw,
    RenderInterface, RenderPipeline, RenderPipelineInfo, RenderResources, Scissor, ShaderCode,
    ShaderStage, StorageCompute, StoreOperation, Texture, TextureCopy, TextureFormat, TextureInfo,
    TextureUsage, TextureViewFormats, Viewport,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, path::Path};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "02ce1a5a7fc64a9703c6fec69f136a7b6d98c1872bb26d50c31cbd68065e5ab8",
    ),
    (
        3,
        "89fe1f0caf74e5d264ab36b544b2423e8a4249c4501d53ebe0bd5c194b8dd665",
    ),
];
fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
fn identity() -> Value {
    macro_rules! sources {
        ($($path:literal),+ $(,)?) => { json!({$($path: hash(include_bytes!(concat!("../../../", $path)))),+}) };
    }
    sources!(
        "Cargo.toml",
        "Cargo.lock",
        "crates/agfx/Cargo.toml",
        "crates/agfx/src/lib.rs",
        "crates/agfx/src/vulkan.rs",
        "crates/agfx/src/vulkan/compute.rs",
        "crates/agfx/src/vulkan/bindings.rs",
        "crates/agfx/src/vulkan/graphics.rs",
        "crates/agfx/src/vulkan/ownership.rs",
        "crates/agfx/src/vulkan/sampler.rs",
        "crates/agfx/src/vulkan/texture.rs",
        "crates/shader-to-human/Cargo.toml",
        "crates/shader-to-human/examples/features.rs",
        "crates/shader-to-human/src/lib.rs",
        "crates/shader-to-human/src/font.rs",
        "crates/shader-to-human/src/math.rs",
        "crates/shader-to-human/src/gather.rs",
        "crates/shader-to-human/src/widgets.rs",
        "crates/shader-to-human/src/scatter.rs",
        "crates/shader-to-human/src/world.rs",
        "crates/shader-to-human/programs/features/mod.rs",
        "crates/shader-to-human/programs/features/gather.rs",
        "crates/shader-to-human/programs/features/images.rs",
        "crates/shader-to-human/programs/features/quad.rs",
        "crates/shader-to-human/programs/features/table.rs",
        "crates/shader-to-human/programs/features/two_d.rs",
        "crates/shader-to-human/programs/features/world.rs",
        "crates/shader-to-human/fixtures/scatter.rs",
        "tests/parity/fixtures/shader-to-human/features-cameras.txt",
        "shaders/rust/shader_to_human_features.rs",
        "tests/parity/fixtures/shader-to-human/features-inputs.json",
    )
}

struct ReviewedShader {
    words: Vec<u32>,
    metadata: Value,
}
impl ReviewedShader {
    fn load(directory: &Path, level: u32) -> Result<Self> {
        let expected = REVIEWED
            .iter()
            .find(|r| r.0 == level)
            .ok_or("unreviewed level")?
            .1;
        let bytes = std::fs::read(directory.join(format!("features_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("features_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.features.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "vertex+fragment+compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "ordinary-raster-storage-sampled-image"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["entry_points"]
                != json!([
                    "features_commit_cs",
                    "features_cs",
                    "features_debug_cs",
                    "features_font_cs",
                    "features_quad_fs",
                    "features_quad_post_cs",
                    "features_quad_vs",
                    "features_scatter_cs"
                ])
            || metadata["capabilities"]
                != json!(["Shader", "SignedZeroInfNanPreserve", "VulkanMemoryModel"])
            || metadata["entry_sha256"] != sources["shaders/rust/shader_to_human_features.rs"]
        {
            return Err("unreviewed shader or metadata rejected before Vulkan".into());
        }
        for (field, directory, count) in [
            ("library_sources", "src", 7),
            ("program_sources", "programs", 7),
            ("fixture_sources", "fixtures", 1),
        ] {
            let entries = metadata[field]
                .as_object()
                .ok_or("missing source identities")?;
            if entries.len() != count {
                return Err("incomplete source identities".into());
            }
            for (name, digest) in entries {
                if !digest.is_string()
                    || sources[format!("crates/shader-to-human/{directory}/{name}")] != *digest
                {
                    return Err(format!("stale {directory}/{name}").into());
                }
            }
        }
        if bytes.len() % 4 != 0 {
            return Err("malformed SPIR-V".into());
        }
        Ok(Self {
            words: bytes
                .chunks_exact(4)
                .map(|w| u32::from_le_bytes(w.try_into().unwrap()))
                .collect(),
            metadata,
        })
    }
    fn stage(&self, stage: ShaderStage, entry_point: &'static CStr) -> ShaderCode<'_> {
        ShaderCode {
            words: &self.words,
            stage,
            entry_point,
        }
    }
}

const BYTES: u64 = 384;
const STATE_COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: BYTES,
}];
const IMAGE: TextureInfo = TextureInfo {
    width: 800,
    height: 600,
    format: TextureFormat::Rgba8Srgb,
};
const ATLAS: TextureInfo = TextureInfo {
    width: 768,
    height: 8,
    format: TextureFormat::Rgba8Srgb,
};

fn root(width: u32, height: u32, kind: u32) -> Vec<u8> {
    [width, height, kind, 0]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect()
}
fn cameras() -> Result<Vec<Vec<f32>>> {
    let rows: Vec<Vec<f32>> =
        include_str!("../../../tests/parity/fixtures/shader-to-human/features-cameras.txt")
            .lines()
            .map(|line| line.split_whitespace().map(str::parse).collect())
            .collect::<std::result::Result<_, _>>()?;
    if rows.len() != 3
        || rows
            .iter()
            .any(|r| r.len() != 56 || r.iter().any(|v| !v.is_finite()))
    {
        return Err("invalid fixed camera data".into());
    }
    Ok(rows)
}
fn seeded_state(seed: bool) -> Vec<u8> {
    let mut words = [0_u32; 28];
    if seed {
        words[0] = 2;
        words[2] = 0x12345678;
        words[3] = 0x87654321;
        for (index, value) in [
            0.2_f32, 0.4, 0.8, 0.75, 0.8, 0.3, 0.1, 0.6, 3.0, 4.0, 5.0, 6.0,
        ]
        .into_iter()
        .enumerate()
        {
            words[4 + index] = value.to_bits();
        }
        for (index, value) in [1.0_f32, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]
            .into_iter()
            .enumerate()
        {
            words[20 + index] = value.to_bits();
        }
    }
    words.into_iter().flat_map(u32::to_le_bytes).collect()
}
fn source_inputs(camera: &[f32], time: f32, mouse: [f32; 4], previous: [f32; 4]) -> Vec<u8> {
    camera[..52]
        .iter()
        .copied()
        .chain(mouse)
        .chain(previous)
        .chain([800.0, 600.0, time, 1000.0])
        .chain(camera[52..].iter().copied())
        .flat_map(f32::to_le_bytes)
        .collect()
}
fn texture(device: &Device, info: TextureInfo, sampled: bool) -> Result<Texture<'_>> {
    let mut image = device.texture_with_views(
        info,
        TextureUsage {
            storage: true,
            sampled,
            attachment: !sampled,
        },
        TextureViewFormats {
            storage: TextureFormat::Rgba8Unorm,
            sampled: TextureFormat::Rgba8Srgb,
            attachment: TextureFormat::Rgba8Srgb,
        },
    )?;
    image.clear([0.0; 4])?;
    Ok(image)
}

struct Runner<'d> {
    device: &'d Device,
    image_pipeline: StorageCompute<'d>,
    commit_pipeline: StorageCompute<'d>,
    debug: StorageCompute<'d>,
    font: StorageCompute<'d>,
    scatter: StorageCompute<'d>,
    quad_post: StorageCompute<'d>,
    quad: RenderPipeline<'d>,
    image: Texture<'d>,
    atlas: Texture<'d>,
    state: Buffer<'d>,
    upload: Buffer<'d>,
    state_readback: Buffer<'d>,
    image_readback: Buffer<'d>,
    atlas_readback: Buffer<'d>,
}
impl<'d> Runner<'d> {
    #[allow(unsafe_code)]
    fn new(device: &'d Device, shader: &ReviewedShader) -> Result<Self> {
        let compute = |entry: &'static CStr, buffers, images, sampled_images, local_size| {
            // SAFETY: U-025. Private allowlisted validated module. All interfaces
            // use root16, at most one384-byte buffer, one Rgba8 storage view,
            // one sampled font, no sampler/pointer. Entry local sizes reviewed.
            unsafe {
                device.storage_compute(
                    shader.stage(ShaderStage::Compute, entry),
                    ComputeInterface {
                        buffers,
                        images,
                        sampled_images,
                        samplers: 0,
                        root_bytes: 16,
                        local_size,
                    },
                )
            }
        };
        // SAFETY: U-025. Linked reviewed vertex/fragment entries, immutable
        //384-byte buffer, no shader writes. One matching SRGB attachment.
        let quad = unsafe {
            device.render_pipeline(
                shader.stage(ShaderStage::Vertex, c"features_quad_vs"),
                Some(shader.stage(ShaderStage::Fragment, c"features_quad_fs")),
                RenderPipelineInfo {
                    interface: RenderInterface {
                        buffers: 1,
                        root_bytes: 16,
                        ..Default::default()
                    },
                    front_face: FrontFace::CounterClockwise,
                    colors: vec![ColorTarget {
                        format: TextureFormat::Rgba8Srgb,
                        blend: BlendState::default(),
                    }],
                    ..Default::default()
                },
                &[],
            )
        }?;
        Ok(Self {
            device,
            image_pipeline: compute(c"features_cs", 1, 1, 0, [8, 8, 1])?,
            commit_pipeline: compute(c"features_commit_cs", 1, 0, 0, [1, 1, 1])?,
            debug: compute(c"features_debug_cs", 1, 1, 0, [8, 8, 1])?,
            font: compute(c"features_font_cs", 0, 1, 1, [8, 8, 1])?,
            scatter: compute(c"features_scatter_cs", 0, 1, 0, [8, 8, 1])?,
            quad_post: compute(c"features_quad_post_cs", 1, 1, 0, [8, 8, 1])?,
            quad,
            image: texture(device, IMAGE, false)?,
            atlas: texture(device, ATLAS, true)?,
            state: device.buffer(BYTES, Memory::Device)?,
            upload: device.buffer(BYTES, Memory::Upload)?,
            state_readback: device.buffer(BYTES, Memory::Readback)?,
            image_readback: device.buffer(IMAGE.byte_len()?, Memory::Readback)?,
            atlas_readback: device.buffer(ATLAS.byte_len()?, Memory::Readback)?,
        })
    }
    fn upload(&mut self, bytes: &[u8], done: &mut Vec<u64>) -> Result<()> {
        self.upload.write(bytes)?;
        done.push(
            self.device
                .copy(&self.upload, &mut self.state, &STATE_COPY)?
                .value(),
        );
        Ok(())
    }
    #[allow(unsafe_code)]
    fn image(&mut self, kind: u32, done: &mut Vec<u64>) -> Result<()> {
        let (image, width, height) = if kind == 7 {
            (&mut self.atlas, 768, 8)
        } else {
            (&mut self.image, 800, 600)
        };
        let root = root(width, height, kind);
        // SAFETY: U-025. Complete immutable input buffer, one exclusive initialized
        //view with exact extent, one z plane, distinct guarded writes. kind0..7.
        // Font generation uses768x8 while the source framebuffer input stays800x600.
        done.push(
            unsafe {
                self.image_pipeline.dispatch(
                    &mut [&mut self.state],
                    &mut [image],
                    &[],
                    &[],
                    &[ComputeDispatch {
                        root: &root,
                        groups: [width / 8, height / 8, 1],
                    }],
                )
            }?
            .value(),
        );
        Ok(())
    }
    #[allow(unsafe_code)]
    fn commit(&mut self, kind: u32, done: &mut Vec<u64>) -> Result<Vec<u8>> {
        // SAFETY: U-025. Exactly one invocation exclusively accesses the complete
        //initialized384-byte state after image completion. No image/pointer access.
        done.push(
            unsafe {
                self.commit_pipeline.dispatch(
                    &mut [&mut self.state],
                    &mut [],
                    &[],
                    &[],
                    &[ComputeDispatch {
                        root: &root(800, 600, kind),
                        groups: [1, 1, 1],
                    }],
                )
            }?
            .value(),
        );
        done.push(
            self.device
                .copy(&self.state, &mut self.state_readback, &STATE_COPY)?
                .value(),
        );
        Ok(self.state_readback.read()?)
    }
    #[allow(unsafe_code)]
    fn extra(&mut self, name: &str, done: &mut Vec<u64>) -> Result<()> {
        let root = root(800, 600, 0);
        if name == "quad" {
            // SAFETY: U-025. Six source-generated vertices, one instance, fixed
            //finite camera inputs and immutable buffer. Clear completed, Load
            //preserves its pixels. Covered fragments write full linear RGBA.
            done.push(
                unsafe {
                    self.device.render(
                        &mut [ColorAttachment {
                            texture: &mut self.image,
                            load: LoadOperation::Load,
                            store: StoreOperation::Store,
                            clear: [0.0; 4],
                        }],
                        None,
                        &RenderResources {
                            buffers: &[&self.state],
                            ..Default::default()
                        },
                        &[RenderDraw {
                            pipeline: &self.quad,
                            root: &root,
                            viewport: Viewport::whole(800, 600),
                            scissor: Scissor::whole(800, 600),
                            vertices: DrawVertices::Direct { count: 6, first: 0 },
                            instances: 1,
                            first_instance: 0,
                        }],
                    )
                }?
                .value(),
            );
        } else {
            let mut buffers = Vec::new();
            let mut sampled = Vec::new();
            let (pipeline, groups) = match name {
                "debug" => {
                    buffers.push(&mut self.state);
                    (&mut self.debug, [100, 75, 1])
                }
                "post" => {
                    buffers.push(&mut self.state);
                    (&mut self.quad_post, [100, 75, 1])
                }
                "font" => {
                    sampled.push(&self.atlas);
                    (&mut self.font, [100, 75, 1])
                }
                "scatter" => (&mut self.scatter, [1, 1, 1]),
                _ => return Err("unknown private pass".into()),
            };
            // SAFETY: U-025. Exact exclusive800x600 initialized destination.
            // Debug never writes the selected texel, other stores are local.
            // QuadPost is local read/write. Font reads a distinct complete768x8
            //SRGB atlas. Scatter has one writer and ordered bounded stores.
            done.push(
                unsafe {
                    pipeline.dispatch(
                        &mut buffers,
                        &mut [&mut self.image],
                        &sampled,
                        &[],
                        &[ComputeDispatch {
                            root: &root,
                            groups,
                        }],
                    )
                }?
                .value(),
            );
        }
        Ok(())
    }
    fn capture(&mut self, name: &str, atlas: bool, done: &mut Vec<u64>) -> Result<Value> {
        let (image, readback, info) = if atlas {
            (&mut self.atlas, &mut self.atlas_readback, ATLAS)
        } else {
            (&mut self.image, &mut self.image_readback, IMAGE)
        };
        done.push(
            self.device
                .copy_texture_to_buffer(image, readback, TextureCopy::whole(info))?
                .value(),
        );
        let bytes = readback.read()?;
        artifact(name, &bytes, info.byte_len()? as usize)
    }
}
fn artifact(name: &str, bytes: &[u8], size: usize) -> Result<Value> {
    if bytes.len() != size {
        return Err("incomplete readback".into());
    }
    std::fs::write(name, bytes)?;
    Ok(json!({"output":name,"bytes":bytes.len(),"sha256":hash(bytes)}))
}

fn run(token: &str, directory: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(directory, level)?;
    let cameras = cameras()?;
    let steps: Vec<Value> = serde_json::from_str(include_str!(
        "../../../tests/parity/fixtures/shader-to-human/features-inputs.json"
    ))?;
    if steps.len() != 54 {
        return Err("incomplete source sequence".into());
    }
    // Validate the entire fixed sequence before any Vulkan call.
    for step in &steps {
        if !step["name"].is_string()
            || ![
                "world",
                "gather",
                "scatter",
                "table",
                "two_d",
                "arrows",
                "quad",
                "font",
                "coordinates",
            ]
            .contains(&step["program"].as_str().unwrap_or(""))
            || step["camera"].as_u64().is_none_or(|c| c > 2)
            || step["time"]
                .as_f64()
                .is_none_or(|t| !t.is_finite() || t.abs() > 100.0)
            || step["mouse"].as_array().is_none_or(|a| {
                a.len() != 4
                    || a.iter().any(|v| {
                        v.as_f64()
                            .is_none_or(|f| !f.is_finite() || f.abs() > 10000.0)
                    })
            })
            || (!step["reset"].is_null()
                && !["zero", "seed"].contains(&step["reset"].as_str().unwrap_or("")))
        {
            return Err("invalid fixed Features input".into());
        }
    }
    let device = Device::new(true)?;
    if !device.info().signed_zero_inf_nan_preserve_f32 {
        return Err(agfx::Error::Unsupported(
            "Features requires float32 signed zero, infinity and NaN preservation".into(),
        )
        .into());
    }
    let mut runner = Runner::new(&device, &shader)?;
    let mut previous = [0.0; 4];
    let mut state = seeded_state(false);
    let mut cases = Vec::new();
    for step in steps {
        let name = step["name"].as_str().unwrap();
        let program = step["program"].as_str().unwrap();
        let camera = step["camera"].as_u64().unwrap() as usize;
        let mouse = core::array::from_fn(|i| step["mouse"][i].as_f64().unwrap() as f32);
        if let Some(reset) = step["reset"].as_str() {
            state = seeded_state(reset == "seed");
            previous = [0.0; 4];
        }
        let mut before = state.clone();
        before.extend(source_inputs(
            &cameras[camera],
            step["time"].as_f64().unwrap() as f32,
            mouse,
            previous,
        ));
        let id = format!("s2h.features.{name}.opt{level}");
        let mut done = Vec::new();
        runner.upload(&before, &mut done)?;
        let mut intermediate = Value::Null;
        let kind = match program {
            "gather" => 0,
            "table" => 1,
            "two_d" => 2,
            "arrows" => 3,
            "world" => 4,
            "coordinates" => 6,
            _ => 5,
        };
        if program == "font" {
            runner.image(7, &mut done)?;
            intermediate = runner.capture(&format!("{id}.atlas.rgba8"), true, &mut done)?;
            runner.extra("font", &mut done)?;
        } else {
            runner.image(kind, &mut done)?;
            if program == "quad" {
                runner.extra("quad", &mut done)?;
            }
            if ["gather", "quad", "scatter"].contains(&program) {
                intermediate = runner.capture(&format!("{id}.before.rgba8"), false, &mut done)?;
                runner.extra(
                    match program {
                        "gather" => "debug",
                        "quad" => "post",
                        _ => "scatter",
                    },
                    &mut done,
                )?;
            }
        }
        let image = runner.capture(&format!("{id}.rgba8"), false, &mut done)?;
        let post = runner.commit(kind, &mut done)?;
        if post[112..] != before[112..] {
            return Err("immutable input changed".into());
        }
        cases.push(
            json!({"case_id":id,"status":"pass","step":step,"previous_mouse":previous,
            "input":artifact(&format!("{id}.input"),&before,384)?,
            "post":artifact(&format!("{id}.post"),&post,384)?,
            "image":image,"intermediate":intermediate,"completion_values":done}),
        );
        state = post[..112].to_vec();
        previous = mouse;
        eprintln!("completed {name} opt{level}");
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"pass","optimization_level":level,
        "required":54,"executed":cases.len(),"cases":cases,"host_source_sha256":identity(),"shader":shader.metadata,
        "scope":"all nine Features targets and dependent source passes, followed by persistent UI sequences; source agreement separate",
        "device":{"name":info.name,"api_version":info.api_version,"loader_api_version":info.loader_api_version,
            "signed_zero_inf_nan_preserve_f32":info.signed_zero_inf_nan_preserve_f32,
            "driver_version":info.driver_version,"vendor_id":info.vendor_id,"device_id":info.device_id,
            "validation":info.validation,"synchronization_validation":true}}),
    )
}
fn main() -> std::process::ExitCode {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--identity"] {
        println!("{}", identity());
        return std::process::ExitCode::SUCCESS;
    }
    if args.len() != 6
        || args[0] != "--run-token"
        || args[1].is_empty()
        || args[2] != "--shader-dir"
        || args[4] != "--level"
    {
        eprintln!("usage: features --identity | --run-token TOKEN --shader-dir PATH --level 0|3");
        return std::process::ExitCode::FAILURE;
    }
    let result = args[5]
        .parse::<u32>()
        .map_err(|e| e.into())
        .and_then(|level| run(&args[1], Path::new(&args[3]), level));
    match result {
        Ok(record) => {
            println!("{record}");
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("{error}");
            std::process::ExitCode::FAILURE
        }
    }
}
