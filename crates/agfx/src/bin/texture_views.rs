//! Numeric tests of owned UNORM/sRGB storage, sampled and attachment views.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::{
    BlendState, ColorAttachment, ColorTarget, ComparisonFunction, ComputeDispatch,
    ComputeInterface, CopyRegion, Device, DrawVertices, Error, LoadOperation, Memory, RenderDraw,
    RenderInterface, RenderPipelineInfo, RenderResources, SamplerFilter, SamplerInfo, Scissor,
    ShaderCode, ShaderStage, StoreOperation, TextureCopy, TextureFormat, TextureInfo, TextureUsage,
    TextureViewFormats, Viewport,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, path::Path};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "e4e3fb7e4c922b38e362feb011abacf9c95c5083b64d3c2e2135db690d2c29c2",
    ),
    (
        3,
        "36002f56c8e3b7d2186ad5a6062bdb62ebe2f4e7c0aafbc0d2cbc4e74fe3b600",
    ),
];
const USAGE: TextureUsage = TextureUsage {
    storage: true,
    sampled: true,
    attachment: true,
};
const COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: 1024,
}];
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
        "crates/agfx/src/bin/texture_views.rs",
        "shaders/rust/texture_views.rs"
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
        let bytes = std::fs::read(path.join(format!("texture_views_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            path.join(format!("texture_views_opt{level}.metadata.json")),
        )?)?;
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("agfx.texture_views.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "vertex+fragment+compute"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["profile"] != "agfx-format-views"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["extensions"] != json!([])
            || metadata["entries"]
                != json!({"store_cs":["compute",16],"sample_cs":["compute",0],"load_cs":["compute",0],"view_vs":["vertex",0],"view_fs":["fragment",16]})
            || metadata["identity"]["source_sha256"] != identity()["shaders/rust/texture_views.rs"]
            || bytes.len() % 4 != 0
        {
            return Err("unreviewed view payload or metadata rejected before Vulkan".into());
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

fn capture(id: &str, kind: &str, data: &[u8], size: usize) -> Result<Value> {
    if data.len() != size {
        return Err("incomplete view readback".into());
    }
    let output = format!("{id}.{kind}");
    std::fs::write(&output, data)?;
    Ok(json!({"output":output,"bytes":size,"sha256":hash(data)}))
}

fn rejected<T>(
    name: &str,
    result: std::result::Result<T, Error>,
    rows: &mut Vec<Value>,
) -> Result<()> {
    if let Err(Error::Invalid(message)) = result {
        rows.push(json!({"case_id":name,"status":"pass","diagnostic":message}));
        Ok(())
    } else {
        Err(format!("{name}: invalid request was not rejected before recording").into())
    }
}

#[allow(unsafe_code)]
fn run(token: &str, path: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(path, level)?;
    let device = Device::new(true)?;
    let compute = |entry: &'static CStr, buffers, images, sampled_images, samplers, root_bytes| {
        // SAFETY: U-024. Fixed validated entries, declared0/1 resources at
        //bindings0/1/2/3, fixed0/16-byte roots and8x8x1 local size, no pointers.
        unsafe {
            device.storage_compute(
                shader.code(ShaderStage::Compute, entry),
                ComputeInterface {
                    buffers,
                    images,
                    sampled_images,
                    samplers,
                    root_bytes,
                    local_size: [8, 8, 1],
                },
            )
        }
    };
    let mut store = compute(c"store_cs", 0, 1, 0, 0, 16)?;
    let mut sample = compute(c"sample_cs", 1, 0, 1, 1, 0)?;
    let mut load = compute(c"load_cs", 1, 1, 0, 0, 0)?;
    let pipeline = |format| {
        // SAFETY: U-024. Reviewed compatible vertex/fragment pair, no descriptors,
        //six generated vertices, one float4 color output and16-byte fragment root.
        unsafe {
            device.render_pipeline(
                shader.code(ShaderStage::Vertex, c"view_vs"),
                Some(shader.code(ShaderStage::Fragment, c"view_fs")),
                RenderPipelineInfo {
                    interface: RenderInterface {
                        root_bytes: 16,
                        ..Default::default()
                    },
                    colors: vec![ColorTarget {
                        format,
                        blend: BlendState::default(),
                    }],
                    ..Default::default()
                },
                &[],
            )
        }
    };
    let unorm = pipeline(TextureFormat::Rgba8Unorm)?;
    let srgb = pipeline(TextureFormat::Rgba8Srgb)?;
    let sampler = device.sampler(SamplerInfo {
        filter: SamplerFilter::Nearest,
        comparison: ComparisonFunction::Always,
        max_lod: 0.0,
        ..Default::default()
    })?;
    let mut controls = Vec::new();
    let normal = TextureInfo {
        width: 8,
        height: 8,
        format: TextureFormat::Rgba8Unorm,
    };
    for (name, formats) in [
        (
            "incompatible_sampled",
            TextureViewFormats {
                sampled: TextureFormat::Rgba32Float,
                ..TextureViewFormats::same(normal.format)
            },
        ),
        (
            "incompatible_storage",
            TextureViewFormats {
                storage: TextureFormat::D32Float,
                ..TextureViewFormats::same(normal.format)
            },
        ),
        (
            "incompatible_attachment",
            TextureViewFormats {
                attachment: TextureFormat::D32Float,
                ..TextureViewFormats::same(normal.format)
            },
        ),
    ] {
        rejected(
            name,
            device.texture_with_views(normal, USAGE, formats),
            &mut controls,
        )?;
    }
    rejected(
        "empty_usage",
        device.texture_with_views(
            normal,
            TextureUsage::default(),
            TextureViewFormats::same(normal.format),
        ),
        &mut controls,
    )?;
    rejected(
        "depth_storage",
        device.texture_with_views(
            TextureInfo {
                format: TextureFormat::D32Float,
                ..normal
            },
            USAGE,
            TextureViewFormats::same(TextureFormat::D32Float),
        ),
        &mut controls,
    )?;
    let mut upload = device.buffer(1024, Memory::Upload)?;
    upload.write(&[0; 1024])?;
    let mut output = device.buffer(1024, Memory::Device)?;
    device.copy(&upload, &mut output, &COPY)?;
    let mut floats = device.buffer(1024, Memory::Readback)?;
    let mut encoded = device.buffer(256, Memory::Readback)?;
    let root: Vec<u8> = [0.2_f32, 0.4, 0.6, 0.8]
        .into_iter()
        .flat_map(f32::to_le_bytes)
        .collect();
    let mut cases = Vec::new();
    for (name, base, view) in [
        (
            "unorm",
            TextureFormat::Rgba8Unorm,
            TextureFormat::Rgba8Unorm,
        ),
        (
            "srgb_views",
            TextureFormat::Rgba8Unorm,
            TextureFormat::Rgba8Srgb,
        ),
        (
            "srgb_base",
            TextureFormat::Rgba8Srgb,
            TextureFormat::Rgba8Srgb,
        ),
    ] {
        let info = TextureInfo {
            format: base,
            ..normal
        };
        let formats = TextureViewFormats {
            storage: TextureFormat::Rgba8Unorm,
            sampled: view,
            attachment: view,
        };
        let mut texture = device.texture_with_views(info, USAGE, formats)?;
        if texture.info() != info || texture.view_formats() != formats {
            return Err("view identity changed".into());
        }
        texture.clear([0.0; 4])?;
        let draw = |pipeline| RenderDraw {
            pipeline,
            root: &root,
            viewport: Viewport::whole(8, 8),
            scissor: Scissor::whole(8, 8),
            vertices: DrawVertices::Direct { count: 6, first: 0 },
            instances: 1,
            first_instance: 0,
        };
        let wrong = if view == TextureFormat::Rgba8Unorm {
            &srgb
        } else {
            &unorm
        };
        // SAFETY: U-024. Interface mismatch is rejected before native recording.
        // Even the selected fixed shader/root and initialized image are bounded.
        let invalid = unsafe {
            device.render(
                &mut [ColorAttachment {
                    texture: &mut texture,
                    load: LoadOperation::Load,
                    store: StoreOperation::Store,
                    clear: [0.0; 4],
                }],
                None,
                &RenderResources::default(),
                &[draw(wrong)],
            )
        };
        rejected(&format!("{name}_attachment_format"), invalid, &mut controls)?;
        for phase in ["storage", "raster"] {
            let written = if phase == "storage" {
                // SAFETY: U-024. Initialized8x8 storage view is UNORM regardless
                //of base format. One8x8x1 group writes all distinct texels.
                unsafe {
                    store.dispatch(
                        &mut [],
                        &mut [&mut texture],
                        &[],
                        &[],
                        &[ComputeDispatch {
                            root: &root,
                            groups: [1, 1, 1],
                        }],
                    )
                }?
                .value()
            } else {
                let pipeline = if view == TextureFormat::Rgba8Unorm {
                    &unorm
                } else {
                    &srgb
                };
                // SAFETY: U-024. Matching queried attachment format, one full
                //viewport, fixed generated vertices and finite color root. Load
                //starts initialized. The previous compute/copies have completed.
                unsafe {
                    device.render(
                        &mut [ColorAttachment {
                            texture: &mut texture,
                            load: LoadOperation::Load,
                            store: StoreOperation::Store,
                            clear: [0.0; 4],
                        }],
                        None,
                        &RenderResources::default(),
                        &[draw(pipeline)],
                    )
                }?
                .value()
            };
            let raw_copied = device
                .copy_texture_to_buffer(&mut texture, &mut encoded, TextureCopy::whole(info))?
                .value();
            let raw = encoded.read()?;
            // SAFETY: U-024. Initialized read-only sampled view and non-comparison
            //nearest sampler, exclusive initialized64-float4 output. One8x8x1
            //group writes unique in-bounds elements, with no image writes.
            let sampled = unsafe {
                sample.dispatch(
                    &mut [&mut output],
                    &mut [],
                    &[&texture],
                    &[&sampler],
                    &[ComputeDispatch {
                        root: &[],
                        groups: [1, 1, 1],
                    }],
                )
            }?
            .value();
            let sampled_copied = device.copy(&output, &mut floats, &COPY)?.value();
            let sample_bytes = floats.read()?;
            // SAFETY: U-024. Same complete output bounds and initialized UNORM
            //storage view. This entry only reads the image. Completion prevents
            //overlap with the preceding sampled read/output write.
            let loaded = unsafe {
                load.dispatch(
                    &mut [&mut output],
                    &mut [&mut texture],
                    &[],
                    &[],
                    &[ComputeDispatch {
                        root: &[],
                        groups: [1, 1, 1],
                    }],
                )
            }?
            .value();
            let loaded_copied = device.copy(&output, &mut floats, &COPY)?.value();
            let storage_bytes = floats.read()?;
            let id = format!("agfx.views.{name}.{phase}.opt{level}");
            cases.push(json!({"case_id":id,"status":"pass","base_format":format!("{base:?}"),"view_format":format!("{view:?}"),
                "root_sha256":hash(&root),"encoded":capture(&id,"rgba8",&raw,256)?,
                "sampled":capture(&id,"sampled.f32",&sample_bytes,1024)?,"storage":capture(&id,"storage.f32",&storage_bytes,1024)?,
                "completion_values":[written,raw_copied,sampled,sampled_copied,loaded,loaded_copied]}));
        }
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"status":"pass","run_token":token,"optimization_level":level,"required":6,"executed":cases.len(),
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
            "usage: texture_views --identity | --run-token TOKEN --shader-dir PATH --level 0|3"
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
