//! Five translated ShaderToHuman fixtures, using the ordinary Rust Vulkan owner.
//! This executable checks dispatch/readback, not original image agreement.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use agfx::{
    ComputeDispatch, ComputeInterface, CopyRegion, Device, Memory, ShaderCode, ShaderStage,
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
        "crates/agfx/src/bin/shader_to_human.rs",
        "shaders/rust/shader_to_human_fixtures.rs",
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
}

impl ReviewedShader {
    fn load(directory: &Path, level: u32) -> Result<Self, Box<dyn std::error::Error>> {
        let expected = REVIEWED
            .iter()
            .find(|(opt, _)| *opt == level)
            .ok_or("unreviewed optimization level")?
            .1;
        let bytes = std::fs::read(directory.join(format!("fixtures_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("fixtures_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.fixtures.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "ordinary-storage-buffer"
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
            || metadata["entry_sha256"] != sources["shaders/rust/shader_to_human_fixtures.rs"]
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
        })
    }

    #[allow(unsafe_code)]
    fn execute(
        &self,
        fixture: &Fixture,
        device: &Device,
    ) -> Result<(Vec<u8>, [u64; 3]), Box<dyn std::error::Error>> {
        // SAFETY: U-016. Only the two independently validated and source-reviewed
        // exact modules above are accepted. Each selected entry has the stated
        // local size, one set0/binding0 storage buffer (480000 vec4, stride16),
        // and no other resources. world_cs alone has an 80-byte root, containing
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
                    buffers: 1,
                    root_bytes: fixture.root.len() as u32,
                    local_size: fixture.local,
                },
            )
        }?;
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
                &[ComputeDispatch {
                    root: &fixture.root,
                    groups: fixture.groups,
                }],
            )
        }?
        .value();
        let copied = device.copy(&output, &mut readback, &regions)?.value();
        let bytes = readback.read()?;
        Ok((bytes, [initialized, dispatched, copied]))
    }
}

fn run(
    token: &str,
    directory: &Path,
    level: u32,
    name: &str,
) -> Result<Value, Box<dyn std::error::Error>> {
    let fixture = Fixture::load(name)?;
    let shader = ReviewedShader::load(directory, level)?;
    let device = Device::new(true)?;
    let (bytes, completion) = shader.execute(&fixture, &device)?;
    let values = || {
        bytes
            .chunks_exact(4)
            .map(|chunk| f32::from_le_bytes(chunk.try_into().unwrap()))
    };
    if !values().all(f32::is_finite) || values().all(|value| value == 0.0) {
        return Err("nonfinite or empty fixture readback".into());
    }
    let filename = format!("{}-opt{level}.rgba32f", fixture.name);
    std::fs::write(&filename, &bytes)?;
    let info = device.info();
    Ok(
        json!({"schema_version": 1, "run_token": token, "case_id": format!("s2h.{}.opt{level}", fixture.name),
        "status": "pass", "scope": "finite complete float readback only", "golden_agreement": "not checked",
        "host_source_sha256": identity(), "shader": shader.metadata, "optimization_level": level,
        "entry_point": fixture.entry.to_str()?, "dispatch_groups": fixture.groups, "workgroup_size": fixture.local,
        "root_bytes": fixture.root.len(), "root_sha256": hash(&fixture.root), "bytes": bytes.len(),
        "resolution": [800, 600], "format": "RGBA32_FLOAT", "output": filename, "actual_sha256": hash(&bytes),
        "completion_values": completion,
        "device": {"name": info.name, "loader_api_version": info.loader_api_version, "api_version": info.api_version,
            "driver_version": info.driver_version, "vendor_id": info.vendor_id, "device_id": info.device_id,
            "queue_family": info.queue_family, "validation": info.validation, "synchronization_validation": true,
            "enabled_features": ["timelineSemaphore", "vulkanMemoryModel", "runtimeDescriptorArray", "shaderStorageBufferArrayDynamicIndexing"]}}),
    )
}

fn main() -> std::process::ExitCode {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--identity"] {
        println!("{}", identity());
        return std::process::ExitCode::SUCCESS;
    }
    if args.len() != 8
        || args[0] != "--run-token"
        || args[1].is_empty()
        || args[2] != "--shader-dir"
        || args[4] != "--level"
        || args[6] != "--case"
    {
        eprintln!("usage: shader_to_human --identity | --run-token TOKEN --shader-dir PATH --level 0|3 --case NAME");
        return std::process::ExitCode::FAILURE;
    }
    let result = args[5]
        .parse::<u32>()
        .map_err(|e| e.into())
        .and_then(|level| run(&args[1], Path::new(&args[3]), level, &args[7]));
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
