//! Native Vulkan hosts for ShaderToHuman's four Hello example families.
//! Source d6f98b7d, Electronic Arts 2024-2025, BSD-3-Clause.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use agfx::{
    BlendState, ColorAttachment, ColorTarget, ComputeDispatch, ComputeInterface, Device,
    DrawVertices, FrontFace, LoadOperation, Memory, RenderDraw, RenderInterface, RenderPipeline,
    RenderPipelineInfo, RenderResources, Scissor, ShaderCode, ShaderStage, StoreOperation, Texture,
    TextureCopy, TextureFormat, TextureInfo, TextureUsage, Viewport,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, path::Path};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "f6ccb91aa93edf037c8afdd135f405ce2d4cd630689d0a0921729b3107f5b46d",
    ),
    (
        3,
        "a8e6957417307a461f66284c7b14be8bdfe5f327011ac6d12587c7d9e6460dcb",
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
        "crates/shader-to-human/examples/hello.rs",
        "crates/shader-to-human/src/lib.rs",
        "crates/shader-to-human/src/font.rs",
        "crates/shader-to-human/src/math.rs",
        "crates/shader-to-human/src/gather.rs",
        "crates/shader-to-human/src/widgets.rs",
        "crates/shader-to-human/src/scatter.rs",
        "crates/shader-to-human/src/world.rs",
        "crates/shader-to-human/programs/hello.rs",
        "shaders/rust/shader_to_human_hello.rs",
        "tests/parity/fixtures/shader-to-human/hello-cameras.txt",
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
        let bytes = std::fs::read(directory.join(format!("hello_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("hello_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.hello.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "vertex+fragment+compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "ordinary-raster-storage-image"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["entry_points"]
                != json!(["hello_cs", "quad_fs", "quad_vs", "screen_fs", "screen_vs"])
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["entry_sha256"] != sources["shaders/rust/shader_to_human_hello.rs"]
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

fn roots() -> Result<Vec<Vec<u8>>> {
    let mut roots = Vec::new();
    for row in
        include_str!("../../../tests/parity/fixtures/shader-to-human/hello-cameras.txt").lines()
    {
        let values: Vec<f32> = row
            .split_whitespace()
            .map(str::parse)
            .collect::<std::result::Result<_, _>>()?;
        if values.len() != 24
            || !values.iter().all(|f| f.is_finite())
            || values[20..] != [800.0, 600.0, 0.0, 0.0]
        {
            return Err("invalid fixed camera or dimensions".into());
        }
        roots.push(values.into_iter().flat_map(f32::to_le_bytes).collect());
    }
    if roots.len() != 3 {
        return Err("missing fixed cameras".into());
    }
    Ok(roots)
}

fn texture(device: &Device, compute: bool) -> Result<Texture<'_>> {
    let mut image = device.texture_with_usage(
        TextureInfo {
            width: 800,
            height: 600,
            format: if compute {
                TextureFormat::Rgba8Unorm
            } else {
                TextureFormat::Rgba8Srgb
            },
        },
        TextureUsage {
            storage: compute,
            sampled: false,
            attachment: !compute,
        },
    )?;
    // Gigi zero-initializes non-imported textures, including alpha.
    image.clear([0.0; 4])?;
    Ok(image)
}

#[allow(unsafe_code)]
fn pipeline<'d>(
    device: &'d Device,
    shader: &ReviewedShader,
    quad: bool,
) -> Result<RenderPipeline<'d>> {
    // SAFETY: U-022. Private validated payload, reviewed vertex/fragment linkage,
    // root96, no resources, finite builtins and a matching single SRGB target.
    Ok(unsafe {
        device.render_pipeline(
            shader.stage(
                ShaderStage::Vertex,
                if quad { c"quad_vs" } else { c"screen_vs" },
            ),
            Some(shader.stage(
                ShaderStage::Fragment,
                if quad { c"quad_fs" } else { c"screen_fs" },
            )),
            RenderPipelineInfo {
                interface: RenderInterface {
                    root_bytes: 96,
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
        )?
    })
}

#[allow(unsafe_code)]
fn draw<'d>(
    device: &'d Device,
    image: &mut Texture<'d>,
    pipeline: &RenderPipeline<'d>,
    root: &[u8],
) -> Result<u64> {
    // SAFETY: U-022. Private reviewed pipeline and root; vertices0..5, one
    // instance. No descriptors or shader writes besides complete covered RGBA.
    // Load preserves initialized zero/prior pixels, all owners live to completion.
    Ok(unsafe {
        device.render(
            &mut [ColorAttachment {
                texture: image,
                load: LoadOperation::Load,
                store: StoreOperation::Store,
                clear: [0.0; 4],
            }],
            None,
            &RenderResources::default(),
            &[RenderDraw {
                pipeline,
                root,
                viewport: Viewport::whole(800, 600),
                scissor: Scissor::whole(800, 600),
                vertices: DrawVertices::Direct { count: 6, first: 0 },
                instances: 1,
                first_instance: 0,
            }],
        )?
    }
    .value())
}

fn capture<'d>(
    device: &'d Device,
    image: &mut Texture<'d>,
    name: &str,
    level: u32,
    root: &[u8],
    submitted: u64,
) -> Result<Value> {
    let mut readback = device.buffer(800 * 600 * 4, Memory::Readback)?;
    let copied = device.copy_texture_to_buffer(
        image,
        &mut readback,
        TextureCopy {
            buffer_offset: 0,
            row_bytes: 0,
            origin: [0, 0],
            extent: [800, 600],
        },
    )?;
    let bytes = readback.read()?;
    let case_id = format!("s2h.hello.{name}.opt{level}");
    let output = format!("{case_id}.rgba8");
    std::fs::write(&output, &bytes)?;
    eprintln!("completed {case_id}");
    Ok(
        json!({"case_id":case_id,"output":output,"bytes":bytes.len(),"sha256":hash(&bytes),
        "root_sha256":hash(root),"completion_values":[submitted,copied.value()],"status":"pass"}),
    )
}

#[allow(unsafe_code)]
fn run(token: &str, directory: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(directory, level)?;
    let roots = roots()?;
    let device = Device::new(true)?;
    let screen = pipeline(&device, &shader, false)?;
    let quad = pipeline(&device, &shader, true)?;
    let mut image = texture(&device, false)?;
    let mut cases = Vec::new();
    let done = draw(&device, &mut image, &screen, &roots[0])?;
    cases.push(capture(
        &device, &mut image, "screen", level, &roots[0], done,
    )?);
    let mut compute_image = texture(&device, true)?;
    // SAFETY: U-022. Reviewed compute entry, local8x8x1, root96 and one binding1
    // RGBA8 storage image. No buffers, physical access or undeclared capabilities.
    let mut compute = unsafe {
        device.storage_compute(
            shader.stage(ShaderStage::Compute, c"hello_cs"),
            ComputeInterface {
                buffers: 0,
                images: 1,
                sampled_images: 0,
                samplers: 0,
                root_bytes: 96,
                local_size: [8, 8, 1],
            },
        )?
    };
    // SAFETY: U-022. Exact800x600 root/image, initialized exclusive output,
    // 100x75x1 groups. Each invocation writes one distinct complete texel.
    let done = unsafe {
        compute.dispatch(
            &mut [],
            &mut [&mut compute_image],
            &[],
            &[],
            &[ComputeDispatch {
                root: &roots[0],
                groups: [100, 75, 1],
            }],
        )?
    };
    cases.push(capture(
        &device,
        &mut compute_image,
        "compute",
        level,
        &roots[0],
        done.value(),
    )?);
    image.clear([0.0; 4])?;
    for (name, index, reset) in [
        ("quad_source", 0, false),
        ("quad_motion", 2, false),
        ("quad_frontal", 1, true),
        ("quad_moved", 2, true),
    ] {
        if reset {
            image.clear([0.0; 4])?;
        }
        let done = draw(&device, &mut image, &quad, &roots[index])?;
        cases.push(capture(
            &device,
            &mut image,
            name,
            level,
            &roots[index],
            done,
        )?);
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"pass","required":6,"executed":cases.len(),
        "optimization_level":level,"host_source_sha256":identity(),"shader":shader.metadata,"cases":cases,
        "device":{"name":info.name,"api_version":info.api_version,"driver_version":info.driver_version,
            "vendor_id":info.vendor_id,"device_id":info.device_id,"validation":info.validation,"synchronization_validation":true}}),
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
        eprintln!("usage: hello --identity | --run-token TOKEN --shader-dir PATH --level 0|3");
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
