//! Translated AGFX f91b108a multi-dispatch fixture and frozen source golden.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
//! Only the explicit executable runs GPU work, never ordinary Cargo tests.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]

use agfx::{BufferCompute, CopyRegion, Device, Error, Memory, ShaderCode, ShaderStage};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{io::Cursor, path::Path};

fn hash(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn identity() -> Value {
    json!({
        "Cargo.toml": hash(include_bytes!("../../../../Cargo.toml")),
        "Cargo.lock": hash(include_bytes!("../../../../Cargo.lock")),
        "crates/agfx/Cargo.toml": hash(include_bytes!("../../Cargo.toml")),
        "crates/agfx/src/lib.rs": hash(include_bytes!("../lib.rs")),
        "crates/agfx/src/vulkan.rs": hash(include_bytes!("../vulkan.rs")),
        "crates/agfx/src/vulkan/compute.rs": hash(include_bytes!("../vulkan/compute.rs")),
        "crates/agfx/src/vulkan/bindings.rs": hash(include_bytes!("../vulkan/bindings.rs")),
        "crates/agfx/src/vulkan/graphics.rs": hash(include_bytes!("../vulkan/graphics.rs")),
        "crates/agfx/src/vulkan/ownership.rs": hash(include_bytes!("../vulkan/ownership.rs")),
        "crates/agfx/src/vulkan/sampler.rs": hash(include_bytes!("../vulkan/sampler.rs")),
        "crates/agfx/src/vulkan/texture.rs": hash(include_bytes!("../vulkan/texture.rs")),
        "crates/agfx/src/bin/multi_dispatch.rs": hash(include_bytes!("multi_dispatch.rs")),
        "shaders/rust/compute_multi_dispatch.rs": hash(include_bytes!("../../../../shaders/rust/compute_multi_dispatch.rs")),
        "tests/parity/fixtures/agfx/compute_multi_dispatch_buffer.bin": hash(include_bytes!("../../../../tests/parity/fixtures/agfx/compute_multi_dispatch_buffer.bin")),
    })
}

// Fixed acceptance of the two fully inspected U-009 modules. Updating the
// compiler/shader requires renewed review and explicit replacement identities.
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "e191ccd221de55e383fce98ad60f79ed1dbe8ec4b5fa1a8389c8a665e0daf655",
    ),
    (
        3,
        "8bb34555fa9101ad52ecee2a01991f90868c7d0d26c188410e224ee674202d3a",
    ),
];

struct ReviewedShader {
    level: u32,
    words: Vec<u32>,
    metadata: Value,
}

impl ReviewedShader {
    fn load(
        directory: &Path,
        level: u32,
        expected_hash: &str,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let stem = format!("compute_multi_dispatch_opt{level}");
        let bytes = std::fs::read(directory.join(format!("{stem}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("{stem}.metadata.json")),
        )?)?;
        let mut capabilities: Vec<_> = metadata["capabilities"]
            .as_array()
            .ok_or("missing capabilities")?
            .iter()
            .map(|value| value.as_str().ok_or("invalid capability name"))
            .collect::<Result<Vec<_>, _>>()?;
        capabilities.sort_unstable();
        if hash(&bytes) != expected_hash
            || metadata["payload_sha256"] != expected_hash
            || metadata["identity"]["source_sha256"]
                != identity()["shaders/rust/compute_multi_dispatch.rs"]
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["language"] != "Rust"
            || metadata["stage"] != "compute"
            || metadata["entry_point"] != "main_cs"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "agfx-ordinary-storage-buffer-array"
            || metadata["root_bytes"] != 16
            || metadata["workgroup_size"] != json!([64, 1, 1])
            || metadata["extensions"] != json!(["SPV_EXT_descriptor_indexing"])
            || capabilities
                != [
                    "RuntimeDescriptorArray",
                    "Shader",
                    "StorageBufferArrayDynamicIndexing",
                    "VulkanMemoryModel",
                ]
        {
            return Err(format!(
                "opt{level}: unreviewed payload or incompatible metadata rejected before Vulkan"
            )
            .into());
        }
        let words = ash::util::read_spv(&mut Cursor::new(bytes))?;
        Ok(Self {
            level,
            words,
            metadata,
        })
    }

    #[allow(unsafe_code)]
    fn pipeline<'d>(&self, device: &'d Device) -> Result<BufferCompute<'d>, Error> {
        // SAFETY: U-011. load() admits only the two fully reviewed U-009 byte
        // identities and the matching source/profile/ABI metadata. Their sole
        // compute entry uses four ordinary storage descriptors, a 16-byte root,
        // 64 disjoint u32 lanes and no other resource access. Device creation
        // enabled every declared capability's required feature. words are private
        // and immutable after acceptance; the native owner enforces dispatch use.
        unsafe {
            device.buffer_compute(ShaderCode {
                words: &self.words,
                stage: ShaderStage::Compute,
                entry_point: c"main_cs",
            })
        }
    }
}

fn reject<T>(
    id: String,
    result: Result<T, Error>,
    controls: &mut Vec<Value>,
) -> Result<(), String> {
    match result {
        Err(Error::Invalid(message)) => {
            controls.push(json!({"case_id": id, "status": "pass", "diagnostic": message}));
            Ok(())
        }
        Err(other) => Err(format!("{id}: wrong rejection {other}")),
        Ok(_) => Err(format!("{id}: invalid operation accepted")),
    }
}

fn sentinel(slot: u32) -> Vec<u8> {
    (0..64_u32)
        .flat_map(|i| (0xa0000000 + slot * 0x1000 + i).to_le_bytes())
        .collect()
}

fn run(token: &str, directory: &Path) -> Result<Value, Box<dyn std::error::Error>> {
    // Both artifact/metadata checks precede device creation, even if opt0 fails.
    let shaders = REVIEWED
        .into_iter()
        .map(|(level, hash)| ReviewedShader::load(directory, level, hash))
        .collect::<Result<Vec<_>, _>>()?;
    let device = Device::new(true)?;
    let foreign = Device::new(true)?;
    let full = [CopyRegion {
        source: 0,
        destination: 0,
        bytes: 256,
    }];
    let mut upload = device.buffer(256, Memory::Upload)?;
    let mut readback = device.buffer(256, Memory::Readback)?;
    let golden =
        include_bytes!("../../../../tests/parity/fixtures/agfx/compute_multi_dispatch_buffer.bin");
    let mut cases = Vec::new();
    let mut controls = Vec::new();
    for shader in &shaders {
        let mut pipeline = shader.pipeline(&device)?;
        for resource in [1_u32, 3] {
            let mut buffers = [
                device.buffer(256, Memory::Device)?,
                device.buffer(256, Memory::Device)?,
                device.buffer(256, Memory::Device)?,
                device.buffer(256, Memory::Device)?,
            ];
            if resource == 1 {
                reject(
                    format!("agfx.compute.control.uninitialized.opt{}", shader.level),
                    pipeline.four_passes(buffers.each_mut(), resource),
                    &mut controls,
                )?;
            }
            for (slot, buffer) in buffers.iter_mut().enumerate() {
                upload.write(&if slot as u32 == resource {
                    vec![0; 256]
                } else {
                    sentinel(slot as u32)
                })?;
                device.copy(&upload, buffer, &full)?;
            }
            if resource == 1 {
                reject(
                    format!("agfx.compute.control.slot.opt{}", shader.level),
                    pipeline.four_passes(buffers.each_mut(), 4),
                    &mut controls,
                )?;
                let mut small = device.buffer(128, Memory::Device)?;
                device.copy(
                    &upload,
                    &mut small,
                    &[CopyRegion {
                        source: 0,
                        destination: 0,
                        bytes: 128,
                    }],
                )?;
                let [_, b, c, d] = buffers.each_mut();
                reject(
                    format!("agfx.compute.control.size.opt{}", shader.level),
                    pipeline.four_passes([&mut small, b, c, d], resource),
                    &mut controls,
                )?;
                let [_, b, c, d] = buffers.each_mut();
                reject(
                    format!("agfx.compute.control.role.opt{}", shader.level),
                    pipeline.four_passes([&mut upload, b, c, d], resource),
                    &mut controls,
                )?;
                let mut foreign_buffer = foreign.buffer(256, Memory::Device)?;
                let mut foreign_upload = foreign.buffer(256, Memory::Upload)?;
                foreign_upload.write(&[0; 256])?;
                foreign.copy(&foreign_upload, &mut foreign_buffer, &full)?;
                let [_, b, c, d] = buffers.each_mut();
                reject(
                    format!("agfx.compute.control.device.opt{}", shader.level),
                    pipeline.four_passes([&mut foreign_buffer, b, c, d], resource),
                    &mut controls,
                )?;
            }
            let dispatched = pipeline.four_passes(buffers.each_mut(), resource)?;
            let mut actual = Vec::with_capacity(1024);
            let mut final_completion = 0;
            for (slot, buffer) in buffers.iter().enumerate() {
                let done = device.copy(buffer, &mut readback, &full)?;
                final_completion = done.value();
                let bytes = readback.read()?;
                let expected = if slot as u32 == resource {
                    golden.to_vec()
                } else {
                    sentinel(slot as u32)
                };
                if let Some(index) = bytes.iter().zip(&expected).position(|(a, b)| a != b) {
                    return Err(format!("opt{} resource{resource} descriptor{slot} byte{index}: expected {}, actual {}", shader.level, expected[index], bytes[index]).into());
                }
                actual.extend_from_slice(&bytes);
            }
            std::fs::write(
                format!("compute-opt{}-slot{resource}.bin", shader.level),
                &actual,
            )?;
            cases.push(json!({"case_id": format!("agfx.compute_multi_dispatch_buffer.opt{}.slot{resource}", shader.level),
                "status": "pass", "optimization_level": shader.level, "resource": resource, "passes": 4,
                "dispatch_groups": [1,1,1], "workgroup_size": [64,1,1], "root_bytes": 16, "bytes": actual.len(),
                "selected_bytes": 256, "unchanged_sentinel_bytes": 768, "actual_sha256": hash(&actual), "golden_sha256": hash(golden),
                "completion_value": dispatched.value(), "readback_completion_value": final_completion,
                "payload_sha256": shader.metadata["payload_sha256"]}));
        }
    }
    let info = device.info();
    Ok(
        json!({"schema_version": 1, "run_token": token, "status": "pass", "required": 4, "executed": cases.len(), "passed": cases.len(),
        "host_source_sha256": identity(), "shaders": shaders.iter().map(|s| &s.metadata).collect::<Vec<_>>(),
        "device": {"name": info.name, "loader_api_version": info.loader_api_version, "api_version": info.api_version, "driver_version": info.driver_version, "vendor_id": info.vendor_id, "device_id": info.device_id,
            "queue_family": info.queue_family, "validation": info.validation, "synchronization_validation": true,
            "enabled_features": ["timelineSemaphore", "vulkanMemoryModel", "runtimeDescriptorArray", "shaderStorageBufferArrayDynamicIndexing"]},
        "cases": cases, "controls": controls}),
    )
}

fn main() -> std::process::ExitCode {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--identity"] {
        println!("{}", identity());
        return std::process::ExitCode::SUCCESS;
    }
    if args.len() != 4
        || args[0] != "--run-token"
        || args[1].is_empty()
        || args[2] != "--shader-dir"
    {
        eprintln!("usage: multi_dispatch --identity | --run-token TOKEN --shader-dir PATH");
        return std::process::ExitCode::FAILURE;
    }
    match run(&args[1], Path::new(&args[3])) {
        Ok(record) => {
            println!("{record}");
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("agfx.compute_multi_dispatch_buffer failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
