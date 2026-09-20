//! AGFX f91b108a compute sampling cases with original inputs and goldens.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::{
    AddressMode, ComparisonFunction, ComputeDispatch, ComputeInterface, Device, Error, Memory,
    SamplerFilter, SamplerInfo, ShaderCode, ShaderStage, TextureCopy, TextureFormat, TextureInfo,
    TextureUsage,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{io::Cursor, path::Path};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const IMAGE: TextureInfo = TextureInfo {
    width: 64,
    height: 64,
    format: TextureFormat::Rgba8Unorm,
};
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "980cee4638fc3b0c075d820727327c414c35237e5b0082c339f37da810a48712",
    ),
    (
        3,
        "4c9ca213a7c544df722d005e834e6f07e2ca3e9a2663240ad0c146370920519a",
    ),
];

fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
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
        "crates/agfx/src/bin/texture_sampling.rs",
        "shaders/rust/texture_sampling.rs"
    )
}

struct ReviewedShader {
    level: u32,
    words: Vec<u32>,
    metadata: Value,
}
impl ReviewedShader {
    fn load(directory: &Path, level: u32, expected: &str) -> Result<Self> {
        let stem = format!("texture_sampling_opt{level}");
        let bytes = std::fs::read(directory.join(format!("{stem}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("{stem}.metadata.json")),
        )?)?;
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["identity"]["source_sha256"]
                != identity()["shaders/rust/texture_sampling.rs"]
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["language"] != "Rust"
            || metadata["stage"] != "compute"
            || metadata["entry_points"] != json!(["sample_cs", "seed_cs"])
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "agfx-ordinary-sampled-image"
            || metadata["root_bytes"] != json!([16, 48])
            || metadata["workgroup_size"] != json!([8, 8, 1])
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["extensions"] != json!([])
        {
            return Err("unreviewed sampling payload or metadata rejected before Vulkan".into());
        }
        Ok(Self {
            level,
            words: ash::util::read_spv(&mut Cursor::new(bytes))?,
            metadata,
        })
    }
}

fn root(scale: f32, offset: f32) -> Vec<u8> {
    [
        0,
        0,
        0,
        64,
        64,
        1,
        scale.to_bits(),
        scale.to_bits(),
        offset.to_bits(),
        offset.to_bits(),
        0,
        0,
    ]
    .into_iter()
    .flat_map(u32::to_ne_bytes)
    .collect()
}

fn reject<T>(
    name: &str,
    result: std::result::Result<T, Error>,
    controls: &mut Vec<Value>,
) -> Result<()> {
    match result {
        Err(Error::Invalid(message)) => {
            controls.push(json!({"case_id":name,"status":"pass","diagnostic":message}))
        }
        Err(other) => return Err(format!("{name}: unexpected {other}").into()),
        Ok(_) => return Err(format!("{name}: invalid operation accepted").into()),
    }
    Ok(())
}

fn capture(
    level: u32,
    name: &str,
    bytes: &[u8],
    completions: [u64; 2],
    root: &[u8],
) -> Result<Value> {
    let id = format!("agfx.sampling.{name}.opt{level}");
    if bytes.len() != 16384 {
        return Err("incomplete sampling image".into());
    }
    std::fs::write(format!("{id}.rgba8"), bytes)?;
    Ok(
        json!({"case_id":id,"status":"executed","bytes":bytes.len(),"sha256":hash(bytes),
        "completion_values":completions,"root_bytes":root.len(),"root_sha256":hash(root)}),
    )
}

#[allow(unsafe_code)]
fn run(shaders: &[ReviewedShader], token: &str) -> Result<Value> {
    let device = Device::new(true)?;
    let foreign = Device::new(true)?;
    let normal = SamplerInfo {
        filter: SamplerFilter::Linear,
        address: [AddressMode::ClampToEdge; 3],
        comparison: ComparisonFunction::Always,
        max_lod: 0.0,
        ..Default::default()
    };
    let mut controls = Vec::new();
    reject(
        "nonfinite_bias",
        device.sampler(SamplerInfo {
            mip_lod_bias: f32::NAN,
            ..normal
        }),
        &mut controls,
    )?;
    reject(
        "reversed_lod",
        device.sampler(SamplerInfo {
            min_lod: 1.0,
            ..normal
        }),
        &mut controls,
    )?;
    reject(
        "no_view_usage",
        device.texture_with_usage(
            IMAGE,
            TextureUsage {
                storage: false,
                sampled: false,
                attachment: false,
            },
        ),
        &mut controls,
    )?;
    // All source comparison modes must create successfully. Their depth sampling
    // behavior remains a separate required port, not claimed by these creations.
    for comparison in [
        ComparisonFunction::Never,
        ComparisonFunction::Less,
        ComparisonFunction::Equal,
        ComparisonFunction::LessEqual,
        ComparisonFunction::Greater,
        ComparisonFunction::NotEqual,
        ComparisonFunction::GreaterEqual,
        ComparisonFunction::Always,
    ] {
        drop(device.sampler(SamplerInfo {
            comparison,
            ..normal
        })?);
    }
    let mut cases = Vec::new();
    for shader in shaders {
        let pipeline = |entry: &std::ffi::CStr, sampled_images, samplers, root_bytes| {
            // SAFETY: U-020. Private loader admits only the two completely reviewed
            // modules. Exact per-entry bindings, local size, roots, enabled Shader
            // and VulkanMemoryModel capabilities, no physical accesses.
            unsafe {
                device.storage_compute(
                    ShaderCode {
                        words: &shader.words,
                        stage: ShaderStage::Compute,
                        entry_point: entry,
                    },
                    ComputeInterface {
                        buffers: 0,
                        images: 1,
                        sampled_images,
                        samplers,
                        root_bytes,
                        local_size: [8, 8, 1],
                    },
                )
            }
        };
        let mut seed = pipeline(c"seed_cs", 0, 0, 16)?;
        let mut sample = pipeline(c"sample_cs", 1, 1, 48)?;
        let mut source = device.texture_with_usage(
            IMAGE,
            TextureUsage {
                storage: true,
                sampled: true,
                attachment: false,
            },
        )?;
        let mut output = device.texture(IMAGE)?;
        let mut readback = device.buffer(16384, Memory::Readback)?;
        source.clear([0.0; 4])?;
        output.clear([0.0; 4])?;
        let sampler = device.sampler(normal)?;
        let foreign_sampler = foreign.sampler(normal)?;
        let mut foreign_image = foreign.texture_with_usage(
            IMAGE,
            TextureUsage {
                storage: false,
                sampled: true,
                attachment: false,
            },
        )?;
        foreign_image.clear([0.0; 4])?;
        let comparison = device.sampler(SamplerInfo {
            comparison: ComparisonFunction::Never,
            ..normal
        })?;
        let uninitialized = device.texture_with_usage(
            IMAGE,
            TextureUsage {
                storage: false,
                sampled: true,
                attachment: false,
            },
        )?;
        let mut sampled_only = device.texture_with_usage(
            IMAGE,
            TextureUsage {
                storage: false,
                sampled: true,
                attachment: false,
            },
        )?;
        sampled_only.clear([0.0; 4])?;
        let invalid_root = root(0.25, 0.375);
        let jobs = [ComputeDispatch {
            root: &invalid_root,
            groups: [8, 8, 1],
        }];
        let first_control = controls.len();
        // SAFETY: U-020. Fixed reviewed shader/root/dispatch. Each deliberately
        // invalid owner/count is rejected by U-016's checks before descriptor
        // updates or command recording. No invalid GPU access is submitted.
        unsafe {
            reject(
                "foreign_sampled",
                sample.dispatch(
                    &mut [],
                    &mut [&mut output],
                    &[&foreign_image],
                    &[&sampler],
                    &jobs,
                ),
                &mut controls,
            )?;
            reject(
                "missing_sampler",
                sample.dispatch(&mut [], &mut [&mut output], &[&source], &[], &jobs),
                &mut controls,
            )?;
            reject(
                "foreign_sampler",
                sample.dispatch(
                    &mut [],
                    &mut [&mut output],
                    &[&source],
                    &[&foreign_sampler],
                    &jobs,
                ),
                &mut controls,
            )?;
            reject(
                "comparison_color",
                sample.dispatch(
                    &mut [],
                    &mut [&mut output],
                    &[&source],
                    &[&comparison],
                    &jobs,
                ),
                &mut controls,
            )?;
            reject(
                "uninitialized_sampled",
                sample.dispatch(
                    &mut [],
                    &mut [&mut output],
                    &[&uninitialized],
                    &[&sampler],
                    &jobs,
                ),
                &mut controls,
            )?;
            reject(
                "missing_sampled_usage",
                sample.dispatch(&mut [], &mut [&mut source], &[&output], &[&sampler], &jobs),
                &mut controls,
            )?;
            reject(
                "missing_storage_usage",
                sample.dispatch(
                    &mut [],
                    &mut [&mut sampled_only],
                    &[&source],
                    &[&sampler],
                    &jobs,
                ),
                &mut controls,
            )?;
        }
        for row in &mut controls[first_control..] {
            row["case_id"] = json!(format!(
                "{}.opt{}",
                row["case_id"].as_str().unwrap(),
                shader.level
            ));
        }
        let seed_root: Vec<_> = [0_u32, 0, 64, 64]
            .into_iter()
            .flat_map(u32::to_ne_bytes)
            .collect();
        // SAFETY: U-020. Exact initialized64x64 RGBA8 source, root16 and8x8x1
        // groups. Guarded writes are disjoint and cover every texel once.
        let drawn = unsafe {
            seed.dispatch(
                &mut [],
                &mut [&mut source],
                &[],
                &[],
                &[ComputeDispatch {
                    root: &seed_root,
                    groups: [8, 8, 1],
                }],
            )
        }?
        .value();
        let copied = device
            .copy_texture_to_buffer(&mut source, &mut readback, TextureCopy::whole(IMAGE))?
            .value();
        cases.push(capture(
            shader.level,
            "seed",
            &readback.read()?,
            [drawn, copied],
            &seed_root,
        )?);
        for (name, filter, address, scale, offset) in [
            (
                "filter_nearest",
                SamplerFilter::Nearest,
                AddressMode::ClampToEdge,
                0.25,
                0.375,
            ),
            (
                "filter_linear",
                SamplerFilter::Linear,
                AddressMode::ClampToEdge,
                0.25,
                0.375,
            ),
            (
                "address_repeat",
                SamplerFilter::Linear,
                AddressMode::Repeat,
                3.0,
                -1.0,
            ),
            (
                "address_mirrored_repeat",
                SamplerFilter::Linear,
                AddressMode::MirroredRepeat,
                3.0,
                -1.0,
            ),
            (
                "address_clamp_to_edge",
                SamplerFilter::Linear,
                AddressMode::ClampToEdge,
                3.0,
                -1.0,
            ),
            (
                "address_border",
                SamplerFilter::Nearest,
                AddressMode::ClampToBorder,
                3.0,
                -1.0,
            ),
            (
                "sample_2d",
                SamplerFilter::Linear,
                AddressMode::ClampToEdge,
                0.25,
                0.375,
            ),
        ] {
            if name == "sample_2d" {
                // The original Sample2D uploads integer-truncated bytes. Its input
                // differs from the preceding GPU seed's RGBA8 rounded conversion.
                let bytes: Vec<_> = (0..64)
                    .flat_map(|y| {
                        (0..64).flat_map(move |x| {
                            [
                                (x * 255 / 63) as u8,
                                (y * 255 / 63) as u8,
                                if (x / 8 + y / 8) % 2 == 0 { 0 } else { 255 },
                                255,
                            ]
                        })
                    })
                    .collect();
                let mut upload = device.buffer(16384, Memory::Upload)?;
                upload.write(&bytes)?;
                device.copy_buffer_to_texture(&upload, &mut source, TextureCopy::whole(IMAGE))?;
            }
            let sampler = device.sampler(SamplerInfo {
                filter,
                address: [address; 3],
                ..normal
            })?;
            let root = root(scale, offset);
            // SAFETY: U-020. Distinct initialized64x64 images, normalized ordinary
            // sampler, fixed finite root48 and8x8x1 groups. Explicit LOD0 reads
            // immutable source through the checked address/filter modes. Unique
            // guarded output writes cover one z plane, then complete before copy.
            let drawn = unsafe {
                sample.dispatch(
                    &mut [],
                    &mut [&mut output],
                    &[&source],
                    &[&sampler],
                    &[ComputeDispatch {
                        root: &root,
                        groups: [8, 8, 1],
                    }],
                )
            }?
            .value();
            let copied = device
                .copy_texture_to_buffer(&mut output, &mut readback, TextureCopy::whole(IMAGE))?
                .value();
            cases.push(capture(
                shader.level,
                name,
                &readback.read()?,
                [drawn, copied],
                &root,
            )?);
        }
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"executed","required":16,"executed":cases.len(),
        "host_source_sha256":identity(),"cases":cases,"controls":controls,"comparison_creations":8,
        "shaders":shaders.iter().map(|s| &s.metadata).collect::<Vec<_>>(),
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
    let result = (|| -> Result<Value> {
        if args.len() != 4
            || args[0] != "--shader-dir"
            || args[2] != "--run-token"
            || args[3].is_empty()
        {
            return Err(
                "usage: texture_sampling --identity | --shader-dir DIR --run-token TOKEN".into(),
            );
        }
        let shaders = REVIEWED
            .into_iter()
            .map(|(level, hash)| ReviewedShader::load(Path::new(&args[1]), level, hash))
            .collect::<Result<Vec<_>>>()?;
        run(&shaders, &args[3])
    })();
    match result {
        Ok(record) => {
            println!("{record}");
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("agfx.sampling failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
