//! Native Vulkan GaussianSplatting, source Electronic Arts 2024-2025, BSD-3-Clause.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::{
    BlendState, Buffer, ColorAttachment, ColorTarget, ComparisonFunction, ComputeDispatch,
    ComputeInterface, CopyRegion, DepthAttachment, DepthState, Device, DrawVertices, FrontFace,
    LoadOperation, Memory, RenderDraw, RenderInterface, RenderPipeline, RenderPipelineInfo,
    RenderResources, SampleCount, Scissor, ShaderCode, ShaderStage, StorageCompute, StoreOperation,
    Texture, TextureCopy, TextureFormat, TextureInfo, TextureUsage, TextureViewFormats, Viewport,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, path::Path};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "e1c2a6d37b77f264a2855b1d2e80087ff7a66a28533d6557f7224d4971642539",
    ),
    (
        3,
        "8bb75865484eb44e022e8c76dd196c9af2cfb6478f97ca92114ca2e68b3a9188",
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
        "crates/shader-to-human/examples/gaussian.rs",
        "crates/shader-to-human/src/lib.rs",
        "crates/shader-to-human/src/font.rs",
        "crates/shader-to-human/src/math.rs",
        "crates/shader-to-human/src/gather.rs",
        "crates/shader-to-human/src/widgets.rs",
        "crates/shader-to-human/src/scatter.rs",
        "crates/shader-to-human/src/world.rs",
        "crates/shader-to-human/programs/gaussian/mod.rs",
        "crates/shader-to-human/programs/gaussian/math.rs",
        "crates/shader-to-human/programs/gaussian/ply.rs",
        "crates/shader-to-human/programs/gaussian/programs.rs",
        "tests/parity/fixtures/shader-to-human/gaussian-debug.ply.bin",
        "tests/parity/fixtures/shader-to-human/gaussian-cameras.txt",
        "shaders/rust/shader_to_human_gaussian.rs",
        "tests/parity/fixtures/shader-to-human/gaussian-inputs.json",
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
        let bytes = std::fs::read(directory.join(format!("gaussian_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("gaussian_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.gaussian.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "vertex+fragment+compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "ordinary-raster-storage-msaa-image-int64"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["entry_points"]
                != json!([
                    "gaussian_base_cs",
                    "gaussian_clear_fs",
                    "gaussian_clear_vs",
                    "gaussian_init_cs",
                    "gaussian_main_cs",
                    "gaussian_resolve_cs",
                    "gaussian_samples_cs",
                    "gaussian_splat_fs",
                    "gaussian_splat_vs"
                ])
            || metadata["capabilities"]
                != json!([
                    "Shader",
                    "Int64",
                    "SignedZeroInfNanPreserve",
                    "VulkanMemoryModel"
                ])
            || metadata["entry_sha256"] != sources["shaders/rust/shader_to_human_gaussian.rs"]
        {
            return Err("unreviewed shader or metadata rejected before Vulkan".into());
        }
        for (field, directory, count) in [
            ("library_sources", "src", 7),
            ("program_sources", "programs", 4),
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

const BYTES: u64 = 51520;
const PROBE_BYTES: u64 = 8192;
const COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: BYTES,
}];
const PROBE_COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: PROBE_BYTES,
}];
const IMAGE: TextureInfo = TextureInfo {
    width: 800,
    height: 600,
    format: TextureFormat::Rgba8Srgb,
};
const DEPTH: TextureInfo = TextureInfo {
    format: TextureFormat::D32Float,
    ..IMAGE
};
fn root(kind: u32) -> Vec<u8> {
    [800_u32, 600, kind, 0]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .collect()
}
fn artifact(name: &str, bytes: &[u8], size: usize) -> Result<Value> {
    if bytes.len() != size {
        return Err("incomplete readback".into());
    }
    std::fs::write(name, bytes)?;
    Ok(json!({"output":name,"bytes":bytes.len(),"sha256":hash(bytes)}))
}
fn cameras() -> Result<Vec<Vec<f32>>> {
    let rows: Vec<Vec<f32>> =
        include_str!("../../../tests/parity/fixtures/shader-to-human/gaussian-cameras.txt")
            .lines()
            .map(|l| l.split_whitespace().map(str::parse).collect())
            .collect::<std::result::Result<_, _>>()?;
    if rows.len() != 3
        || rows
            .iter()
            .any(|r| r.len() != 68 || r.iter().any(|x| !x.is_finite()))
    {
        return Err("invalid fixed camera matrices".into());
    }
    Ok(rows)
}
fn input(camera: &[f32], step: &Value) -> Result<Vec<u8>> {
    let mut bytes = vec![0; 16];
    for f in camera {
        bytes.extend(f.to_le_bytes());
    }
    for f in [800_f32, 600.0, 0.0, 1000.0, 100.0, 100.0, 0.0, 0.0] {
        bytes.extend(f.to_le_bytes());
    }
    for field in ["offset", "ray_bounds"] {
        let values = step[field].as_array().ok_or("missing source input")?;
        let count = if field == "offset" { 3 } else { 2 };
        if values.len() != count {
            return Err("invalid fixed input length".into());
        }
        for value in values {
            let value = value.as_f64().ok_or("invalid input value")? as f32;
            if !value.is_finite() || value.abs() > 10000.0 {
                return Err("unbounded input".into());
            }
            bytes.extend(value.to_le_bytes());
        }
        bytes.extend(vec![0; (4 - count) * 4]);
    }
    for field in ["frame", "random"] {
        let v = step[field].as_u64().ok_or("invalid frame")?;
        if v > 17 || (field == "random" && v > 1) {
            return Err("unbounded frame".into());
        }
        bytes.extend((v as u32).to_le_bytes());
    }
    bytes.extend([0; 8]);
    for f in [0_f32, 1.0, 1.0, 0.0] {
        bytes.extend(f.to_le_bytes());
    }
    let ply =
        include_bytes!("../../../tests/parity/fixtures/shader-to-human/gaussian-debug.ply.bin");
    if hash(ply) != "d4309642c238abed54ecfc7426404735457b08a0aec9b40302def929fa4ee28f"
        || bytes.len() != 384
    {
        return Err("unexpected PLY/input identity".into());
    }
    bytes.extend(ply);
    bytes.extend([0; 8]);
    if bytes.len() != BYTES as usize {
        return Err("invalid buffer ABI".into());
    }
    Ok(bytes)
}
fn color(device: &Device, samples: SampleCount) -> Result<Texture<'_>> {
    let mut texture = device.texture_with_samples(
        IMAGE,
        TextureUsage {
            storage: samples == SampleCount::One,
            sampled: true,
            attachment: true,
        },
        TextureViewFormats {
            storage: TextureFormat::Rgba8Unorm,
            sampled: IMAGE.format,
            attachment: IMAGE.format,
        },
        samples,
    )?;
    texture.clear([0.0; 4])?;
    Ok(texture)
}
fn depth(device: &Device, samples: SampleCount) -> Result<Texture<'_>> {
    let mut texture = device.texture_with_samples(
        DEPTH,
        TextureUsage {
            storage: false,
            sampled: true,
            attachment: true,
        },
        TextureViewFormats::same(DEPTH.format),
        samples,
    )?;
    texture.clear_depth(0.0)?;
    Ok(texture)
}
struct Runner<'d> {
    device: &'d Device,
    init: StorageCompute<'d>,
    base: StorageCompute<'d>,
    main: StorageCompute<'d>,
    resolve: StorageCompute<'d>,
    samples: StorageCompute<'d>,
    clear: RenderPipeline<'d>,
    one: RenderPipeline<'d>,
    eight: RenderPipeline<'d>,
    image: Texture<'d>,
    msaa: Texture<'d>,
    depth_one: Texture<'d>,
    depth_eight: Texture<'d>,
    data: Buffer<'d>,
    upload: Buffer<'d>,
    data_readback: Buffer<'d>,
    image_readback: Buffer<'d>,
    probe: Buffer<'d>,
    probe_readback: Buffer<'d>,
}
impl<'d> Runner<'d> {
    #[allow(unsafe_code)]
    fn new(device: &'d Device, shader: &ReviewedShader) -> Result<Self> {
        let compute = |entry: &'static CStr, buffers, images, sampled_images, local_size| {
            // SAFETY: U-027. Private validated allowlisted module. Root16, binding0
            //51520-byte inputs or8192-byte sample output, Rgba8 storage and an
            //eight-sample sampled image. The exact entry interfaces are reviewed.
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
        let raster = |samples, clear| {
            // SAFETY: U-027. Linked reviewed vertex/fragment entries, readonly
            //binding0, no native pointers, matched SRGB/depth formats and samples.
            unsafe {
                device.render_pipeline(
                    shader.stage(
                        ShaderStage::Vertex,
                        if clear {
                            c"gaussian_clear_vs"
                        } else {
                            c"gaussian_splat_vs"
                        },
                    ),
                    Some(shader.stage(
                        ShaderStage::Fragment,
                        if clear {
                            c"gaussian_clear_fs"
                        } else {
                            c"gaussian_splat_fs"
                        },
                    )),
                    RenderPipelineInfo {
                        interface: RenderInterface {
                            buffers: 1,
                            root_bytes: 16,
                            ..Default::default()
                        },
                        samples,
                        front_face: FrontFace::CounterClockwise,
                        colors: vec![ColorTarget {
                            format: IMAGE.format,
                            blend: BlendState::default(),
                        }],
                        depth: if clear {
                            None
                        } else {
                            Some(DepthState {
                                format: DEPTH.format,
                                test: true,
                                write: true,
                                comparison: ComparisonFunction::Greater,
                            })
                        },
                        ..Default::default()
                    },
                    &[],
                )
            }
        };
        let mut upload = device.buffer(BYTES, Memory::Upload)?;
        let mut probe = device.buffer(PROBE_BYTES, Memory::Device)?;
        upload.write(&vec![0; BYTES as usize])?;
        device.copy(&upload, &mut probe, &PROBE_COPY)?;
        Ok(Self {
            device,
            init: compute(c"gaussian_init_cs", 1, 0, 0, [1, 1, 1])?,
            base: compute(c"gaussian_base_cs", 1, 1, 0, [8, 8, 1])?,
            main: compute(c"gaussian_main_cs", 1, 1, 0, [8, 8, 1])?,
            resolve: compute(c"gaussian_resolve_cs", 0, 1, 1, [8, 8, 1])?,
            samples: compute(c"gaussian_samples_cs", 1, 0, 1, [8, 8, 1])?,
            clear: raster(SampleCount::Eight, true)?,
            one: raster(SampleCount::One, false)?,
            eight: raster(SampleCount::Eight, false)?,
            image: color(device, SampleCount::One)?,
            msaa: color(device, SampleCount::Eight)?,
            depth_one: depth(device, SampleCount::One)?,
            depth_eight: depth(device, SampleCount::Eight)?,
            data: device.buffer(BYTES, Memory::Device)?,
            upload,
            data_readback: device.buffer(BYTES, Memory::Readback)?,
            image_readback: device.buffer(IMAGE.byte_len()?, Memory::Readback)?,
            probe,
            probe_readback: device.buffer(PROBE_BYTES, Memory::Readback)?,
        })
    }
    #[allow(unsafe_code)]
    fn prepare(&mut self, bytes: &[u8], done: &mut Vec<u64>) -> Result<Vec<u8>> {
        self.upload.write(bytes)?;
        done.push(
            self.device
                .copy(&self.upload, &mut self.data, &COPY)?
                .value(),
        );
        // SAFETY: U-027. Exactly one invocation, initialized complete buffer,
        //exclusive header words0..3, bounded parser reads only the PLY suffix.
        done.push(
            unsafe {
                self.init.dispatch(
                    &mut [&mut self.data],
                    &mut [],
                    &[],
                    &[],
                    &[ComputeDispatch {
                        root: &root(0),
                        groups: [1, 1, 1],
                    }],
                )
            }?
            .value(),
        );
        done.push(
            self.device
                .copy(&self.data, &mut self.data_readback, &COPY)?
                .value(),
        );
        let post = self.data_readback.read()?;
        let header: Vec<u8> = [382_u32, 62, 0, 200]
            .into_iter()
            .flat_map(u32::to_le_bytes)
            .collect();
        if post.len() != bytes.len() || post[..16] != header || post[16..] != bytes[16..] {
            return Err("PLY header or immutable input mismatch".into());
        }
        Ok(post)
    }
    #[allow(unsafe_code)]
    fn compute(&mut self, kind: u32, base: bool, done: &mut Vec<u64>) -> Result<()> {
        let pipeline = if base { &mut self.base } else { &mut self.main };
        // SAFETY: U-027. Initialized800x600 UNORM view, immutable full input,
        //kind0/1/3, distinct guarded texels. Main follows completed base writes.
        done.push(
            unsafe {
                pipeline.dispatch(
                    &mut [&mut self.data],
                    &mut [&mut self.image],
                    &[],
                    &[],
                    &[ComputeDispatch {
                        root: &root(kind),
                        groups: [100, 75, 1],
                    }],
                )
            }?
            .value(),
        );
        Ok(())
    }
    #[allow(unsafe_code)]
    fn draw(&mut self, eight: bool, clear: bool, done: &mut Vec<u64>) -> Result<()> {
        let (color, depth) = if eight {
            (&mut self.msaa, &mut self.depth_eight)
        } else {
            (&mut self.image, &mut self.depth_one)
        };
        let pipeline = if clear {
            &self.clear
        } else if eight {
            &self.eight
        } else {
            &self.one
        };
        // SAFETY: U-027. Completed initial clears, readonly complete inputs,
        //six vertices, one fullscreen instance or200 checked PLY instances.
        //Matching SRGB/D32 samples, Greater depth and complete sample outputs.
        done.push(
            unsafe {
                self.device.render(
                    &mut [ColorAttachment {
                        texture: color,
                        load: LoadOperation::Load,
                        store: StoreOperation::Store,
                        clear: [0.0; 4],
                    }],
                    if clear {
                        None
                    } else {
                        Some(DepthAttachment {
                            texture: depth,
                            load: LoadOperation::Load,
                            store: StoreOperation::Store,
                            clear: 0.0,
                        })
                    },
                    &RenderResources {
                        buffers: &[&self.data],
                        ..Default::default()
                    },
                    &[RenderDraw {
                        pipeline,
                        root: &root(0),
                        viewport: Viewport::whole(800, 600),
                        scissor: Scissor::whole(800, 600),
                        vertices: DrawVertices::Direct { count: 6, first: 0 },
                        instances: if clear { 1 } else { 200 },
                        first_instance: 0,
                    }],
                )
            }?
            .value(),
        );
        Ok(())
    }
    #[allow(unsafe_code)]
    fn resolve(&mut self, done: &mut Vec<u64>) -> Result<()> {
        // SAFETY: U-027. Distinct initialized image allocations, complete prior
        //raster writes, exact800x600 extents and eight samples. One z plane.
        done.push(
            unsafe {
                self.resolve.dispatch(
                    &mut [],
                    &mut [&mut self.image],
                    &[&self.msaa],
                    &[],
                    &[ComputeDispatch {
                        root: &root(0),
                        groups: [100, 75, 1],
                    }],
                )
            }?
            .value(),
        );
        Ok(())
    }
    #[allow(unsafe_code)]
    fn probe(&mut self, name: &str, depth: bool, done: &mut Vec<u64>) -> Result<Value> {
        // SAFETY: U-027. One8x8x1 group samples64 bounded pixels, eight samples
        //each. Initialized8192-byte output is disjoint and exclusive. D32 only
        //defines the X result used by the numeric oracle. All prior work finished.
        done.push(
            unsafe {
                self.samples.dispatch(
                    &mut [&mut self.probe],
                    &mut [],
                    &[if depth { &self.depth_eight } else { &self.msaa }],
                    &[],
                    &[ComputeDispatch {
                        root: &root(0),
                        groups: [1, 1, 1],
                    }],
                )
            }?
            .value(),
        );
        done.push(
            self.device
                .copy(&self.probe, &mut self.probe_readback, &PROBE_COPY)?
                .value(),
        );
        artifact(name, &self.probe_readback.read()?, PROBE_BYTES as usize)
    }
    fn capture(&mut self, name: &str, depth: bool, done: &mut Vec<u64>) -> Result<Value> {
        let (texture, info) = if depth {
            (&mut self.depth_one, DEPTH)
        } else {
            (&mut self.image, IMAGE)
        };
        done.push(
            self.device
                .copy_texture_to_buffer(
                    texture,
                    &mut self.image_readback,
                    TextureCopy::whole(info),
                )?
                .value(),
        );
        artifact(
            name,
            &self.image_readback.read()?,
            IMAGE.byte_len()? as usize,
        )
    }
}
fn run(token: &str, directory: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(directory, level)?;
    let cameras = cameras()?;
    let steps: Vec<Value> = serde_json::from_str(include_str!(
        "../../../tests/parity/fixtures/shader-to-human/gaussian-inputs.json"
    ))?;
    let names = [
        "saved",
        "frozen-frame17",
        "random-frame1",
        "random-frame2",
        "near-camera",
        "reverse-camera",
        "offset",
        "clipped",
    ];
    if steps.len() != names.len() {
        return Err("incomplete fixed source inputs".into());
    }
    let mut inputs = Vec::new();
    for (step, name) in steps.iter().zip(names) {
        if step["name"] != name {
            return Err("reordered fixed inputs".into());
        }
        let camera = step["camera"]
            .as_u64()
            .filter(|c| *c < 3)
            .ok_or("invalid camera")? as usize;
        inputs.push(input(&cameras[camera], step)?);
    }
    let device = Device::new(true)?;
    if !device.info().shader_int64 || !device.info().signed_zero_inf_nan_preserve_f32 {
        return Err(agfx::Error::Unsupported(
            "Gaussian requires shaderInt64 and float32 signed zero/Inf/NaN preservation".into(),
        )
        .into());
    }
    let mut runner = Runner::new(&device, &shader)?;
    let mut cases = Vec::new();
    let mut buffers = Vec::new();
    for (step, bytes) in steps.iter().zip(&inputs) {
        let name = step["name"].as_str().unwrap();
        let id = format!("s2h.gaussian.{name}.opt{level}");
        let mut done = Vec::new();
        let post = runner.prepare(bytes, &mut done)?;
        buffers.push(
            json!({"case_id":id,"input":artifact(&format!("{id}.input"),bytes,BYTES as usize)?,
            "post":artifact(&format!("{id}.post"),&post,BYTES as usize)?,"step":step}),
        );
        for kind in [0, 1, 3] {
            runner.compute(kind, true, &mut done)?;
            let base = runner.capture(&format!("{id}.C{kind}.base.rgba8"), false, &mut done)?;
            runner.compute(kind, false, &mut done)?;
            let image = runner.capture(&format!("{id}.C{kind}.rgba8"), false, &mut done)?;
            cases.push(json!({"case_id":format!("{id}.C{kind}"),"status":"pass","image":image,"base":base,"completion_values":done}));
            done.clear();
        }
        // Source C4 Clear.hlsl does no work. Pinned Gigi initializes transient
        //single-sample textures with zero bytes, separately from that shader.
        runner.image.clear([0.0; 4])?;
        runner.depth_one.clear_depth(0.0)?;
        runner.draw(false, false, &mut done)?;
        let image = runner.capture(&format!("{id}.C4.rgba8"), false, &mut done)?;
        let depth = runner.capture(&format!("{id}.C4.depth"), true, &mut done)?;
        cases.push(json!({"case_id":format!("{id}.C4"),"status":"pass","image":image,"depth":depth,"completion_values":done}));
        done.clear();
        runner.msaa.clear([0.0; 4])?;
        runner.depth_eight.clear_depth(0.0)?;
        runner.draw(true, true, &mut done)?;
        runner.draw(true, false, &mut done)?;
        runner.resolve(&mut done)?;
        let image = runner.capture(&format!("{id}.C5.rgba8"), false, &mut done)?;
        let color = runner.probe(&format!("{id}.C5.samples"), false, &mut done)?;
        let depth = runner.probe(&format!("{id}.C5.depth-samples"), true, &mut done)?;
        cases.push(json!({"case_id":format!("{id}.C5"),"status":"pass","image":image,"samples":color,"depth_samples":depth,"completion_values":done}));
        eprintln!("completed {name} opt{level}");
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"pass","optimization_level":level,
        "required":40,"executed":cases.len(),"cases":cases,"buffers":buffers,"host_source_sha256":identity(),"shader":shader.metadata,
        "scope":"all five active Gaussian source targets, original200-splat PLY, fixed8xMSAA and per-sample probes; source agreement separate",
        "device":{"name":info.name,"api_version":info.api_version,"loader_api_version":info.loader_api_version,
            "shader_int64":info.shader_int64,"signed_zero_inf_nan_preserve_f32":info.signed_zero_inf_nan_preserve_f32,
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
        eprintln!("usage: gaussian --identity | --run-token TOKEN --shader-dir PATH --level 0|3");
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
