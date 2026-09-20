//! Numeric controls for the eight-sample GaussianSplatting dependency.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::{
    BlendState, ColorAttachment, ColorTarget, ComparisonFunction, ComputeDispatch,
    ComputeInterface, CopyRegion, DepthAttachment, DepthState, Device, DrawVertices, Error,
    LoadOperation, Memory, RenderDraw, RenderInterface, RenderPipelineInfo, RenderResources,
    SampleCount, Scissor, ShaderCode, ShaderStage, StoreOperation, TextureCopy, TextureFormat,
    TextureInfo, TextureUsage, TextureViewFormats, Viewport,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, path::Path};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "f753710d9dc4dac873e0746f5019c9477174e7be49714d3e271da536d44624ce",
    ),
    (
        3,
        "52f8bbbb5f2e21caf9bb9f00ae6540bd0b697b525129a19c5bd1fb4150015c99",
    ),
];
const COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: 8192,
}];
const USAGE: TextureUsage = TextureUsage {
    storage: false,
    sampled: true,
    attachment: true,
};
fn hash(data: &[u8]) -> String {
    format!("{:x}", Sha256::digest(data))
}
fn identity() -> Value {
    macro_rules! sources {
        ($($path:literal),+ $(,)?) => { json!({$($path: hash(include_bytes!(concat!("../../../../", $path)))),+}) };
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
        "crates/agfx/src/bin/multisample.rs",
        "shaders/rust/multisample.rs"
    )
}
struct ReviewedShader {
    words: Vec<u32>,
    metadata: Value,
}
impl ReviewedShader {
    fn load(path: &Path, level: u32) -> Result<Self> {
        let expected = REVIEWED
            .iter()
            .find(|r| r.0 == level)
            .ok_or("unreviewed level")?
            .1;
        let bytes = std::fs::read(path.join(format!("multisample_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            path.join(format!("multisample_opt{level}.metadata.json")),
        )?)?;
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("agfx.multisample.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "vertex+fragment+compute"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["profile"] != "agfx-multisample"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["extensions"] != json!([])
            || metadata["entries"]
                != json!({"samples_vs":["vertex",48],"samples_fs":["fragment",48],"read_ms_cs":["compute",0],"read_one_cs":["compute",0]})
            || metadata["identity"]["source_sha256"] != identity()["shaders/rust/multisample.rs"]
            || bytes.len() % 4 != 0
        {
            return Err("unreviewed multisample payload or metadata rejected before Vulkan".into());
        }
        Ok(Self {
            words: bytes
                .chunks_exact(4)
                .map(|w| u32::from_le_bytes(w.try_into().unwrap()))
                .collect(),
            metadata,
        })
    }
    fn code(&self, stage: ShaderStage, entry_point: &'static CStr) -> ShaderCode<'_> {
        ShaderCode {
            words: &self.words,
            stage,
            entry_point,
        }
    }
}
fn root(color: [f32; 4], depth: f32, mask: u32) -> Vec<u8> {
    color
        .into_iter()
        .chain([depth, 0.0, 0.0, 0.0])
        .flat_map(f32::to_le_bytes)
        .chain([mask, 0, 0, 0].into_iter().flat_map(u32::to_le_bytes))
        .collect()
}
fn rejected<T>(
    id: &str,
    result: std::result::Result<T, Error>,
    rows: &mut Vec<Value>,
) -> Result<()> {
    match result {
        Err(Error::Invalid(message)) => {
            rows.push(json!({"case_id":id,"status":"pass","diagnostic":message}));
            Ok(())
        }
        _ => Err(format!("{id}: request was not rejected before recording").into()),
    }
}

#[allow(unsafe_code)]
fn run(token: &str, path: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(path, level)?;
    let device = Device::new(true)?;
    let mut upload = device.buffer(8192, Memory::Upload)?;
    upload.write(&[0; 8192])?;
    let mut output = device.buffer(8192, Memory::Device)?;
    device.copy(&upload, &mut output, &COPY)?;
    let mut readback = device.buffer(8192, Memory::Readback)?;
    let mut cases = Vec::new();
    let mut controls = Vec::new();
    for (config, samples, format) in [
        ("one_float", SampleCount::One, TextureFormat::Rgba32Float),
        (
            "eight_float",
            SampleCount::Eight,
            TextureFormat::Rgba32Float,
        ),
        ("eight_srgb", SampleCount::Eight, TextureFormat::Rgba8Srgb),
    ] {
        let n = samples as u32;
        let color_info = TextureInfo {
            width: 8,
            height: 8,
            format,
        };
        let depth_info = TextureInfo {
            format: TextureFormat::D32Float,
            ..color_info
        };
        let mut color = device.texture_with_samples(
            color_info,
            USAGE,
            TextureViewFormats::same(format),
            samples,
        )?;
        let mut depth = device.texture_with_samples(
            depth_info,
            USAGE,
            TextureViewFormats::same(depth_info.format),
            samples,
        )?;
        if color.samples() != samples || depth.samples() != samples {
            return Err("sample identity differs".into());
        }
        color.clear([0.0; 4])?;
        let mut written = depth.clear_depth(0.25)?.value();
        let pipeline_info = RenderPipelineInfo {
            interface: RenderInterface {
                root_bytes: 48,
                ..Default::default()
            },
            colors: vec![ColorTarget {
                format,
                blend: BlendState::default(),
            }],
            depth: Some(DepthState {
                format: depth_info.format,
                test: true,
                write: true,
                comparison: ComparisonFunction::Greater,
            }),
            samples,
            ..Default::default()
        };
        // SAFETY: U-026. Reviewed linked pair, finite48-byte root, no descriptors,
        // one color output, depth from vertex position and one-word sample mask.
        let pipeline = unsafe {
            device.render_pipeline(
                shader.code(ShaderStage::Vertex, c"samples_vs"),
                Some(shader.code(ShaderStage::Fragment, c"samples_fs")),
                pipeline_info.clone(),
                &[],
            )
        }?;
        // SAFETY: U-026. Fixed0/2 bindings, one8192-byte buffer, one exact sampled
        // image type, no root/sampler, and one8x8x1 group with bounded fetches.
        let mut read = unsafe {
            device.storage_compute(
                shader.code(
                    ShaderStage::Compute,
                    if n == 1 {
                        c"read_one_cs"
                    } else {
                        c"read_ms_cs"
                    },
                ),
                ComputeInterface {
                    buffers: 1,
                    images: 0,
                    sampled_images: 1,
                    samplers: 0,
                    root_bytes: 0,
                    local_size: [8, 8, 1],
                },
            )
        }?;
        if n == 8 {
            rejected(
                &format!("{config}.upload"),
                device.copy_buffer_to_texture(&upload, &mut color, TextureCopy::whole(color_info)),
                &mut controls,
            )?;
            rejected(
                &format!("{config}.readback"),
                device.copy_texture_to_buffer(
                    &mut color,
                    &mut readback,
                    TextureCopy::whole(color_info),
                ),
                &mut controls,
            )?;
            match device.texture_with_samples(color_info,TextureUsage {storage:true,..USAGE},TextureViewFormats::same(format),samples) {
                Err(Error::Unsupported(message)) => controls.push(json!({"case_id":format!("{config}.storage"),"status":"pass","diagnostic":message})),
                _ => return Err("unavailable multisample storage feature accepted".into()),
            }
            let mut one_color = device.texture_with_usage(color_info, USAGE)?;
            let mut one_depth = device.texture_with_usage(depth_info, USAGE)?;
            one_color.clear([0.0; 4])?;
            one_depth.clear_depth(0.25)?;
            // SAFETY: U-026. Same reviewed pair/interface with supported one sample.
            let one_pipeline = unsafe {
                device.render_pipeline(
                    shader.code(ShaderStage::Vertex, c"samples_vs"),
                    Some(shader.code(ShaderStage::Fragment, c"samples_fs")),
                    RenderPipelineInfo {
                        samples: SampleCount::One,
                        ..pipeline_info
                    },
                    &[],
                )
            }?;
            let bounded_root = root([1.0; 4], 0.5, 1);
            for mismatch in ["depth", "color", "pipeline"] {
                let mut attachments = vec![ColorAttachment {
                    texture: &mut color,
                    load: LoadOperation::Load,
                    store: StoreOperation::Store,
                    clear: [0.0; 4],
                }];
                if mismatch == "color" {
                    attachments.push(ColorAttachment {
                        texture: &mut one_color,
                        load: LoadOperation::Load,
                        store: StoreOperation::Store,
                        clear: [0.0; 4],
                    });
                }
                let draw = RenderDraw {
                    pipeline: if mismatch == "pipeline" {
                        &one_pipeline
                    } else {
                        &pipeline
                    },
                    root: &bounded_root,
                    viewport: Viewport::whole(8, 8),
                    scissor: Scissor::whole(8, 8),
                    vertices: DrawVertices::Direct { count: 6, first: 0 },
                    instances: 1,
                    first_instance: 0,
                };
                // SAFETY: U-026. Each mismatch is rejected before recording.
                // All resources/roots are otherwise live, initialized and bounded.
                let result = unsafe {
                    device.render(
                        &mut attachments,
                        Some(DepthAttachment {
                            texture: if mismatch == "depth" {
                                &mut one_depth
                            } else {
                                &mut depth
                            },
                            load: LoadOperation::Load,
                            store: StoreOperation::Store,
                            clear: 0.25,
                        }),
                        &RenderResources::default(),
                        &[draw],
                    )
                };
                rejected(
                    &format!("{config}.{mismatch}_mismatch"),
                    result,
                    &mut controls,
                )?;
            }
        }
        for phase in ["clear", "partial", "full", "depth_reject", "zero_mask"] {
            let roots = match phase {
                "clear" => vec![],
                "depth_reject" => vec![root([0.9, 0.7, 0.3, 0.0], 0.125, 255)],
                "zero_mask" => vec![root([0.9, 0.7, 0.3, 0.0], 0.875, 0)],
                _ => {
                    let middle = n.div_ceil(2);
                    let range = if phase == "partial" {
                        0..middle
                    } else {
                        middle..n
                    };
                    range
                        .map(|sample| {
                            let color = if format == TextureFormat::Rgba8Srgb {
                                [
                                    (sample & 1) as f32,
                                    ((sample >> 1) & 1) as f32,
                                    ((sample >> 2) & 1) as f32,
                                    1.0,
                                ]
                            } else {
                                [sample as f32 / 8.0, (7 - sample) as f32 / 8.0, 0.25, 1.0]
                            };
                            root(color, 0.5 + sample as f32 / 32.0, 1 << sample)
                        })
                        .collect()
                }
            };
            if phase != "clear" {
                let draws: Vec<_> = roots
                    .iter()
                    .map(|root| RenderDraw {
                        pipeline: &pipeline,
                        root,
                        viewport: Viewport::whole(8, 8),
                        scissor: Scissor::whole(8, 8),
                        vertices: DrawVertices::Direct { count: 6, first: 0 },
                        instances: 1,
                        first_instance: 0,
                    })
                    .collect();
                // SAFETY: U-026. Six generated vertices per draw, finite fixed roots,
                // matched color/depth samples and valid sample-mask bits. Clear/load
                // initializes every sample before any depth test. No shader resources.
                written = unsafe {
                    device.render(
                        &mut [ColorAttachment {
                            texture: &mut color,
                            load: LoadOperation::Load,
                            store: StoreOperation::Store,
                            clear: [0.0; 4],
                        }],
                        Some(DepthAttachment {
                            texture: &mut depth,
                            load: LoadOperation::Load,
                            store: StoreOperation::Store,
                            clear: 0.25,
                        }),
                        &RenderResources::default(),
                        &draws,
                    )
                }?
                .value();
            }
            for (kind, source) in [("color", &color), ("depth", &depth)] {
                // SAFETY: U-026. Same-device initialized8x8 image with exact shader
                // sample type/count, no sampler, in-bounds indices0..n. Exclusive
                // initialized512-float4 output, disjoint writes, complete prior use.
                let fetched = unsafe {
                    read.dispatch(
                        &mut [&mut output],
                        &mut [],
                        &[source],
                        &[],
                        &[ComputeDispatch {
                            root: &[],
                            groups: [1, 1, 1],
                        }],
                    )
                }?
                .value();
                let copied = device.copy(&output, &mut readback, &COPY)?.value();
                let bytes = readback.read()?;
                if bytes.len() != 8192 {
                    return Err("incomplete sample readback".into());
                }
                let id = format!("agfx.msaa.{config}.{phase}.{kind}.opt{level}");
                let filename = format!("{id}.f32");
                std::fs::write(&filename, &bytes)?;
                cases.push(json!({"case_id":id,"status":"pass","samples":n,"format":format!("{:?}",source.info().format),
                    "output":filename,"bytes":8192,"sha256":hash(&bytes),"root_sha256":roots.iter().map(|r|hash(r)).collect::<Vec<_>>(),
                    "completion_values":[written,fetched,copied]}));
            }
        }
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"status":"pass","run_token":token,"optimization_level":level,"required":30,"executed":cases.len(),
        "cases":cases,"controls":controls,"host_source_sha256":identity(),"shader":shader.metadata,
        "device":{"name":info.name,"api_version":info.api_version,"driver_version":info.driver_version,"vendor_id":info.vendor_id,
            "device_id":info.device_id,"validation":info.validation,"synchronization_validation":true}}),
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
        eprintln!(
            "usage: multisample --identity | --run-token TOKEN --shader-dir PATH --level 0|3"
        );
        return std::process::ExitCode::FAILURE;
    }
    match args[5]
        .parse::<u32>()
        .map_err(|e| e.into())
        .and_then(|level| run(&args[1], Path::new(&args[3]), level))
    {
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
