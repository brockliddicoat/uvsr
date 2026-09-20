//! Five translated ShaderToHuman fixtures, using the ordinary Rust Vulkan owner.
//! Dispatch/readback identity is checked here. The runner compares original PNGs.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use agfx::{
    ComputeDispatch, ComputeInterface, CopyRegion, Device, Memory, ShaderCode, ShaderStage,
    TextureCopy, TextureFormat, TextureInfo,
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, io::Cursor, path::Path};

const BYTES: usize = 800 * 600 * 16;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "dded0be4a2db3edeff70ce559564438d51919c8ec4f9bd882ce99f4dba25d440",
    ),
    (
        3,
        "f1f91b19dd83b4697403e0d13843c7f39475d763b21e52067c89eeb751bfe742",
    ),
];
const REVIEWED_IMAGES: [(u32, &str); 2] = [
    (
        0,
        "5d1272037274650ed9a550a132a52504ff38bbc6c3260bb21090c29307d9f4a9",
    ),
    (
        3,
        "e690bd68b5278b4a9319523a3b43611bb0a8f1f19485381d97edcda597d71b7d",
    ),
];

fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn identity() -> Value {
    macro_rules! sources {
        ($($path:literal),+ $(,)?) => {
            json!({$($path: hash(include_bytes!(concat!("../../../../", $path)))),+})
        };
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
        "crates/agfx/src/bin/shader_to_human.rs",
        "shaders/rust/shader_to_human_fixtures.rs",
        "shaders/rust/shader_to_human_images.rs",
        "crates/shader-to-human/src/lib.rs",
        "crates/shader-to-human/src/font.rs",
        "crates/shader-to-human/src/math.rs",
        "crates/shader-to-human/src/gather.rs",
        "crates/shader-to-human/src/widgets.rs",
        "crates/shader-to-human/src/scatter.rs",
        "crates/shader-to-human/src/world.rs",
        "crates/shader-to-human/fixtures/mod.rs",
        "crates/shader-to-human/fixtures/gather.rs",
        "crates/shader-to-human/fixtures/scatter.rs",
        "crates/shader-to-human/fixtures/table.rs",
        "crates/shader-to-human/fixtures/two_d.rs",
        "crates/shader-to-human/fixtures/world.rs",
        "tests/parity/fixtures/shader-to-human/camera.txt",
    )
}

struct Fixture {
    name: &'static str,
    entry: &'static CStr,
    local: [u32; 3],
    groups: [u32; 3],
    root: Vec<u8>,
}

impl Fixture {
    fn load(name: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let (name, entry) = match name {
            "GatherTest" => ("GatherTest", c"gather_cs"),
            "ScatterTest" => ("ScatterTest", c"scatter_cs"),
            "TableTest" => ("TableTest", c"table_cs"),
            "2DTest" => ("2DTest", c"two_d_cs"),
            "3DTest" => ("3DTest", c"world_cs"),
            _ => return Err("unknown source fixture".into()),
        };
        let mut root = Vec::new();
        if name == "3DTest" {
            for word in include_str!("../../../../tests/parity/fixtures/shader-to-human/camera.txt")
                .split_whitespace()
            {
                let value: f32 = word.parse()?;
                if !value.is_finite() {
                    return Err("nonfinite source camera".into());
                }
                root.extend_from_slice(&value.to_le_bytes());
            }
            if root.len() != 80 {
                return Err("source camera must contain 20 floats".into());
            }
        }
        let scatter = name == "ScatterTest";
        Ok(Self {
            name,
            entry,
            local: if scatter { [1, 1, 1] } else { [8, 8, 1] },
            groups: if scatter { [1, 1, 1] } else { [100, 75, 1] },
            root,
        })
    }
}

struct ReviewedShader {
    words: Vec<u32>,
    metadata: Value,
    images: bool,
}

struct Capture {
    bytes: Vec<u8>,
    completion: Vec<u64>,
    first_execution: Option<Vec<u8>>,
}

impl ReviewedShader {
    fn load(
        directory: &Path,
        level: u32,
        images: bool,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let reviewed = if images { &REVIEWED_IMAGES } else { &REVIEWED };
        let stem = if images { "images" } else { "fixtures" };
        let profile = if images {
            "ordinary-storage-image"
        } else {
            "ordinary-storage-buffer"
        };
        let expected = reviewed
            .iter()
            .find(|(opt, _)| *opt == level)
            .ok_or("unreviewed optimization level")?
            .1;
        let bytes = std::fs::read(directory.join(format!("{stem}_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("{stem}_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.{stem}.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != profile
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["entry_points"]
                != json!([
                    "gather_cs",
                    "scatter_cs",
                    "table_cs",
                    "two_d_cs",
                    "world_cs"
                ])
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["entry_sha256"]
                != sources[format!("shaders/rust/shader_to_human_{stem}.rs")]
        {
            return Err("unreviewed module or incompatible metadata rejected before Vulkan".into());
        }
        for (field, directory) in [("library_sources", "src"), ("fixture_sources", "fixtures")] {
            let entries = metadata[field]
                .as_object()
                .ok_or("missing shader source identities")?;
            let count = if directory == "src" { 7 } else { 6 };
            if entries.len() != count {
                return Err("incomplete shader source identities".into());
            }
            for (name, digest) in entries {
                if !digest.is_string()
                    || sources[format!("crates/shader-to-human/{directory}/{name}")] != *digest
                {
                    return Err(format!("stale {directory}/{name}").into());
                }
            }
        }
        Ok(Self {
            words: ash::util::read_spv(&mut Cursor::new(bytes))?,
            metadata,
            images,
        })
    }

    #[allow(unsafe_code)]
    fn execute(
        &self,
        fixture: &Fixture,
        device: &Device,
    ) -> Result<Capture, Box<dyn std::error::Error>> {
        // SAFETY: U-016/U-018. Only the four independently validated and reviewed
        // exact modules above are accepted. Each selected entry has the stated
        // local size and either one set0/binding0 buffer (480000 vec4, stride16)
        // or one set0/binding1 Rgba8 2D storage image, with no other resources.
        // world_cs alone has an 80-byte root, containing
        // vec4 at0 and four matrix columns at16/32/48/64. Shader and VulkanMemoryModel
        // are supported/enabled by Device. No physical addresses are present.
        let mut pipeline = unsafe {
            device.storage_compute(
                ShaderCode {
                    words: &self.words,
                    stage: ShaderStage::Compute,
                    entry_point: fixture.entry,
                },
                ComputeInterface {
                    buffers: u32::from(!self.images),
                    images: u32::from(self.images),
                    sampled_images: 0,
                    samplers: 0,
                    root_bytes: fixture.root.len() as u32,
                    local_size: fixture.local,
                },
            )
        }?;
        if self.images {
            let info = TextureInfo {
                width: 800,
                height: 600,
                format: TextureFormat::Rgba8Unorm,
            };
            let mut output = device.texture(info)?;
            let mut readback = device.buffer(info.byte_len()?, Memory::Readback)?;
            let mut completion = vec![output.clear([0.0; 4])?.value()];
            let mut first_execution = None;
            let mut bytes = Vec::new();
            for execution in 0..2 {
                // SAFETY: U-018. The fixed reviewed entries target this exact
                // initialized 800x600 RGBA8 image in GENERAL. Guarded unique x/y
                // writes cover one z plane; scatter has one invocation and clips
                // all writes. No buffer/physical access or cross-lane state exists.
                // Fixed finite camera/root and zero idle inputs match source.
                // Each execution completes before readback or the following run.
                completion.push(
                    unsafe {
                        pipeline.dispatch(
                            &mut [],
                            &mut [&mut output],
                            &[],
                            &[],
                            &[ComputeDispatch {
                                root: &fixture.root,
                                groups: fixture.groups,
                            }],
                        )
                    }?
                    .value(),
                );
                completion.push(
                    device
                        .copy_texture_to_buffer(
                            &mut output,
                            &mut readback,
                            TextureCopy::whole(info),
                        )?
                        .value(),
                );
                bytes = readback.read()?;
                if execution == 0 {
                    first_execution = Some(bytes.clone());
                }
            }
            return Ok(Capture {
                bytes,
                completion,
                first_execution,
            });
        }
        let mut upload = device.buffer(BYTES as u64, Memory::Upload)?;
        let mut output = device.buffer(BYTES as u64, Memory::Device)?;
        let mut readback = device.buffer(BYTES as u64, Memory::Readback)?;
        upload.write(&vec![0; BYTES])?;
        let regions = [CopyRegion {
            source: 0,
            destination: 0,
            bytes: BYTES as u64,
        }];
        let initialized = device.copy(&upload, &mut output, &regions)?.value();
        // SAFETY: U-016. Private fixed Fixture values prevent arbitrary roots or
        // dispatches. Four entries write exactly their own guarded x/y pixel for
        // 800x600 invocations. Scatter has one invocation and clips every callback
        // coordinate. All indices are within 480000, no inter-invocation sharing
        // exists, and all private fixture state is initialized. The finite camera
        // words use the reviewed scalar column layout. The full allocation was
        // zeroed, including scatter's untouched pixels, and stays uniquely borrowed
        // through synchronous completion. Vec4 bytes create no host typed reference.
        let dispatched = unsafe {
            pipeline.dispatch(
                &mut [&mut output],
                &mut [],
                &[],
                &[],
                &[ComputeDispatch {
                    root: &fixture.root,
                    groups: fixture.groups,
                }],
            )
        }?
        .value();
        let copied = device.copy(&output, &mut readback, &regions)?.value();
        let bytes = readback.read()?;
        Ok(Capture {
            bytes,
            completion: vec![initialized, dispatched, copied],
            first_execution: None,
        })
    }
}

fn run(
    token: &str,
    directory: &Path,
    level: u32,
    name: &str,
    images: bool,
) -> Result<Value, Box<dyn std::error::Error>> {
    let fixture = Fixture::load(name)?;
    let shader = ReviewedShader::load(directory, level, images)?;
    let device = Device::new(true)?;
    let Capture {
        bytes,
        completion,
        first_execution,
    } = shader.execute(&fixture, &device)?;
    let values = || {
        bytes
            .chunks_exact(4)
            .map(|chunk| f32::from_le_bytes(chunk.try_into().unwrap()))
    };
    if !images && (!values().all(f32::is_finite) || values().all(|value| value == 0.0)) {
        return Err("nonfinite or empty fixture readback".into());
    }
    let extension = if images { "rgba8" } else { "rgba32f" };
    let filename = format!("{}-opt{level}.{extension}", fixture.name);
    std::fs::write(&filename, &bytes)?;
    let first = if let Some(first) = first_execution {
        if first != bytes || !bytes.iter().any(|&byte| byte != 0) {
            return Err("idle image executions differ or are empty".into());
        }
        let output = format!("{}-opt{level}-execution1.rgba8", fixture.name);
        std::fs::write(&output, &first)?;
        Some(json!({"output": output, "bytes": first.len(), "sha256": hash(&first)}))
    } else {
        None
    };
    let info = device.info();
    Ok(json!({"schema_version": 1, "run_token": token,
        "case_id": if images { format!("s2h.image.{}.opt{level}", fixture.name) } else { format!("s2h.{}.opt{level}", fixture.name) },
        "status": "pass", "scope": if images { "complete native RGBA8 two-execution readback" } else { "finite complete float readback only" },
        "golden_agreement": "not checked", "source_executions": if images { 2 } else { 1 }, "first_execution": first,
        "host_source_sha256": identity(), "shader": shader.metadata, "optimization_level": level,
        "entry_point": fixture.entry.to_str()?, "dispatch_groups": fixture.groups, "workgroup_size": fixture.local,
        "root_bytes": fixture.root.len(), "root_sha256": hash(&fixture.root), "bytes": bytes.len(),
        "resolution": [800, 600], "format": if images { "RGBA8_UNORM" } else { "RGBA32_FLOAT" },
        "output": filename, "actual_sha256": hash(&bytes),
        "completion_values": completion,
        "device": {"name": info.name, "loader_api_version": info.loader_api_version, "api_version": info.api_version,
            "driver_version": info.driver_version, "vendor_id": info.vendor_id, "device_id": info.device_id,
            "queue_family": info.queue_family, "validation": info.validation, "synchronization_validation": true,
            "enabled_features": ["timelineSemaphore", "vulkanMemoryModel", "runtimeDescriptorArray", "shaderStorageBufferArrayDynamicIndexing"]}}))
}

fn main() -> std::process::ExitCode {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--identity"] {
        println!("{}", identity());
        return std::process::ExitCode::SUCCESS;
    }
    let images = args.len() == 9 && args[8] == "--images";
    if (args.len() != 8 && !images)
        || args[0] != "--run-token"
        || args[1].is_empty()
        || args[2] != "--shader-dir"
        || args[4] != "--level"
        || args[6] != "--case"
    {
        eprintln!("usage: shader_to_human --identity | --run-token TOKEN --shader-dir PATH --level 0|3 --case NAME [--images]");
        return std::process::ExitCode::FAILURE;
    }
    let result = args[5]
        .parse::<u32>()
        .map_err(|e| e.into())
        .and_then(|level| run(&args[1], Path::new(&args[3]), level, &args[7], images));
    match result {
        Ok(record) => {
            println!("{record}");
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("s2h.fixtures failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
