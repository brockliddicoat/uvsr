//! Native Vulkan Zoom2D source sequence using the existing AGFX owners.
//! Electronic Arts 2024-2025, BSD-3-Clause, source d6f98b7d.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::{
    Buffer, ComputeDispatch, ComputeInterface, CopyRegion, Device, Memory, ShaderCode, ShaderStage,
    StorageCompute, Texture, TextureCopy, TextureFormat, TextureInfo,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, path::Path};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "8a276010649038c3786b74a3544ce4e9344163c3f70d8ddbdc85388aa11420a5",
    ),
    (
        3,
        "c48f6aa14d8ea882eb2d4821c6fd2d0a07f8027b151a7813dcd125a198745842",
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
        "crates/shader-to-human/examples/zoom.rs",
        "crates/shader-to-human/src/lib.rs",
        "crates/shader-to-human/src/font.rs",
        "crates/shader-to-human/src/math.rs",
        "crates/shader-to-human/src/gather.rs",
        "crates/shader-to-human/src/widgets.rs",
        "crates/shader-to-human/src/scatter.rs",
        "crates/shader-to-human/src/world.rs",
        "crates/shader-to-human/programs/zoom.rs",
        "shaders/rust/shader_to_human_zoom.rs",
        "tests/parity/fixtures/shader-to-human/zoom-inputs.json",
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
        let bytes = std::fs::read(directory.join(format!("zoom_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("zoom_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.zoom.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "ordinary-storage-buffer-image"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["entry_points"] != json!(["zoom_cs", "zoom_post_cs", "zoom_pre_cs"])
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["entry_sha256"] != sources["shaders/rust/shader_to_human_zoom.rs"]
        {
            return Err("unreviewed shader or metadata rejected before Vulkan".into());
        }
        for (field, directory, count) in [
            ("library_sources", "src", 7),
            ("program_sources", "programs", 1),
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

const IMAGE: TextureInfo = TextureInfo {
    width: 800,
    height: 600,
    format: TextureFormat::Rgba8Unorm,
};
const COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: 112,
}];

fn root_bytes(mouse: [f32; 4], previous: [f32; 4]) -> Result<Vec<u8>> {
    if mouse
        .into_iter()
        .chain(previous)
        .any(|v| !v.is_finite() || v.abs() > 10000.0)
    {
        return Err("invalid fixed Zoom2D input".into());
    }
    Ok([800_u32, 600, 0, 0]
        .into_iter()
        .flat_map(u32::to_le_bytes)
        .chain(mouse.into_iter().chain(previous).flat_map(f32::to_le_bytes))
        .collect())
}

struct Runner<'d> {
    device: &'d Device,
    pre: StorageCompute<'d>,
    render: StorageCompute<'d>,
    post: StorageCompute<'d>,
    image: Texture<'d>,
    image_readback: Buffer<'d>,
    state: Buffer<'d>,
    state_readback: Buffer<'d>,
}
impl<'d> Runner<'d> {
    #[allow(unsafe_code)]
    fn new(device: &'d Device, shader: &ReviewedShader) -> Result<Self> {
        let pipeline = |entry: &'static CStr, images, local_size| {
            // SAFETY: U-023. Private exact validated modules,48-byte root,
            //112-byte state at binding0 and optional Rgba8 image at binding1.
            // Only Shader/VulkanMemoryModel, inspected local sizes, no pointers.
            unsafe {
                device.storage_compute(
                    shader.stage(ShaderStage::Compute, entry),
                    ComputeInterface {
                        buffers: 1,
                        images,
                        sampled_images: 0,
                        samplers: 0,
                        root_bytes: 48,
                        local_size,
                    },
                )
            }
        };
        let mut runner = Self {
            device,
            pre: pipeline(c"zoom_pre_cs", 0, [1, 1, 1])?,
            render: pipeline(c"zoom_cs", 1, [8, 8, 1])?,
            post: pipeline(c"zoom_post_cs", 0, [1, 1, 1])?,
            image: device.texture(IMAGE)?,
            image_readback: device.buffer(IMAGE.byte_len()?, Memory::Readback)?,
            state: device.buffer(112, Memory::Device)?,
            state_readback: device.buffer(112, Memory::Readback)?,
        };
        runner.image.clear([0.0; 4])?;
        let mut upload = device.buffer(112, Memory::Upload)?;
        upload.write(&[0; 112])?;
        device.copy(&upload, &mut runner.state, &COPY)?;
        Ok(runner)
    }

    #[allow(unsafe_code)]
    fn update(&mut self, root: &[u8], before: bool) -> Result<(Vec<u8>, [u64; 2])> {
        let pipeline = if before {
            &mut self.pre
        } else {
            &mut self.post
        };
        // SAFETY: U-023. Exactly one invocation exclusively accesses initialized
        //112-byte state,48-byte fixed finite root. The preceding pass completed.
        // Source operations only touch this state, no image/physical access.
        let done = unsafe {
            pipeline.dispatch(
                &mut [&mut self.state],
                &mut [],
                &[],
                &[],
                &[ComputeDispatch {
                    root,
                    groups: [1, 1, 1],
                }],
            )
        }?
        .value();
        let copied = self
            .device
            .copy(&self.state, &mut self.state_readback, &COPY)?
            .value();
        Ok((self.state_readback.read()?, [done, copied]))
    }

    #[allow(unsafe_code)]
    fn draw(&mut self, root: &[u8]) -> Result<(Vec<u8>, [u64; 2])> {
        // SAFETY: U-023. Exact initialized800x600 Rgba8 image, immutable112-byte
        //state and48-byte fixed finite root.100x75x1 groups produce distinct
        //guarded texels. Pre finished, and Post cannot overlap any image read.
        let done = unsafe {
            self.render.dispatch(
                &mut [&mut self.state],
                &mut [&mut self.image],
                &[],
                &[],
                &[ComputeDispatch {
                    root,
                    groups: [100, 75, 1],
                }],
            )
        }?
        .value();
        let copied = self
            .device
            .copy_texture_to_buffer(
                &mut self.image,
                &mut self.image_readback,
                TextureCopy::whole(IMAGE),
            )?
            .value();
        Ok((self.image_readback.read()?, [done, copied]))
    }
}

fn artifact(name: &str, bytes: &[u8], expected: usize) -> Result<Value> {
    if bytes.len() != expected {
        return Err("incomplete readback".into());
    }
    std::fs::write(name, bytes)?;
    Ok(json!({"output": name, "bytes": bytes.len(), "sha256": hash(bytes)}))
}

fn run(token: &str, directory: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(directory, level)?;
    // These are compiled-in, reviewed source inputs. There is no arbitrary
    //input/shader interface. Check every root before creating the device.
    let steps: Vec<(String, f32, f32, f32, f32)> = serde_json::from_str(include_str!(
        "../../../tests/parity/fixtures/shader-to-human/zoom-inputs.json"
    ))?;
    if steps.len() != 29 {
        return Err("incomplete fixed Zoom2D sequence".into());
    }
    let mut previous = [0.0; 4];
    let mut roots = Vec::new();
    for (_, x, y, left, right) in &steps {
        let mouse = [*x, *y, *left, *right];
        roots.push(root_bytes(mouse, previous)?);
        previous = mouse;
    }
    let device = Device::new(true)?;
    let mut runner = Runner::new(&device, &shader)?;
    previous = [0.0; 4];
    let mut before = vec![0; 112];
    let mut cases = Vec::new();
    for ((name, x, y, left, right), root) in steps.iter().zip(roots) {
        let mouse = [*x, *y, *left, *right];
        let (pre, a) = runner.update(&root, true)?;
        let (image, b) = runner.draw(&root)?;
        let (post, c) = runner.update(&root, false)?;
        if image.chunks_exact(4).any(|p| p[3] != 255) {
            return Err("nonopaque source image".into());
        }
        let id = format!("s2h.zoom.{name}.opt{level}");
        cases.push(
            json!({"case_id":id, "status":"pass", "mouse":mouse, "previous_mouse":previous,
            "root_sha256":hash(&root), "before_state_sha256":hash(&before),
            "pre":artifact(&format!("{id}.pre"), &pre, 112)?,
            "image":artifact(&format!("{id}.rgba8"), &image, 1920000)?,
            "post":artifact(&format!("{id}.post"), &post, 112)?,
            "completion_values":[a[0],a[1],b[0],b[1],c[0],c[1]]}),
        );
        before = post;
        previous = mouse;
        eprintln!("completed {name} opt{level}");
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"pass","optimization_level":level,
        "required":29,"executed":cases.len(),"cases":cases,"host_source_sha256":identity(),"shader":shader.metadata,
        "scope":"29 ordered Zoom2D pre/image/post frames; source image agreement separate",
        "device":{"name":info.name,"api_version":info.api_version,"loader_api_version":info.loader_api_version,
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
        eprintln!("usage: zoom --identity | --run-token TOKEN --shader-dir PATH --level 0|3");
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
