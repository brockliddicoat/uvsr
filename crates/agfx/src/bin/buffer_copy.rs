//! Explicit GPU executable. `cargo test` only compiles this target.
//! AGFX f91b108a test_copy_buffer_to_buffer.cpp / GpuFixture transfer behavior.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
#![forbid(unsafe_code)]

use agfx::{CopyRegion, Device, Error, Memory};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

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
        "crates/agfx/src/vulkan/ownership.rs": hash(include_bytes!("../vulkan/ownership.rs")),
        "crates/agfx/src/vulkan/texture.rs": hash(include_bytes!("../vulkan/texture.rs")),
        "crates/agfx/src/bin/buffer_copy.rs": hash(include_bytes!("buffer_copy.rs")),
        "tests/parity/fixtures/agfx/copy_buffer_to_buffer.bin": hash(include_bytes!("../../../../tests/parity/fixtures/agfx/copy_buffer_to_buffer.bin")),
    })
}

fn reject<T>(id: &str, result: Result<T, Error>, controls: &mut Vec<Value>) -> Result<(), String> {
    match result {
        Err(Error::Invalid(message)) => {
            controls.push(json!({"case_id": id, "status": "pass", "diagnostic": message}));
            Ok(())
        }
        Err(other) => Err(format!("{id}: wrong rejection {other}")),
        Ok(_) => Err(format!("{id}: invalid operation accepted")),
    }
}

fn run(token: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let device = Device::new(true)?;
    let mut controls = Vec::new();
    reject(
        "agfx.control.zero_size",
        device.buffer(0, Memory::Device),
        &mut controls,
    )?;
    reject(
        "agfx.control.unaligned_size",
        device.buffer(3, Memory::Device),
        &mut controls,
    )?;
    reject(
        "agfx.control.unrepresentable_size",
        device.buffer(1_u64 << 63, Memory::Device),
        &mut controls,
    )?;
    let mut upload = device.buffer(256, Memory::Upload)?;
    let mut source = device.buffer(256, Memory::Device)?;
    let mut destination = device.buffer(256, Memory::Device)?;
    let mut readback = device.buffer(256, Memory::Readback)?;
    let full = [CopyRegion {
        source: 0,
        destination: 0,
        bytes: 256,
    }];
    reject(
        "agfx.control.uninitialized_read",
        readback.read(),
        &mut controls,
    )?;
    reject(
        "agfx.control.partial_write",
        upload.write(&[0; 128]),
        &mut controls,
    )?;
    reject(
        "agfx.control.device_write",
        source.write(&[0; 256]),
        &mut controls,
    )?;
    reject(
        "agfx.control.readback_write",
        readback.write(&[0; 256]),
        &mut controls,
    )?;
    reject("agfx.control.upload_read", upload.read(), &mut controls)?;
    reject(
        "agfx.control.uninitialized_source",
        device.copy(&upload, &mut source, &full),
        &mut controls,
    )?;
    let words: Vec<u8> = (0..64_u32)
        .flat_map(|i| (0xc0de0000 | (i * 7 + 1)).to_le_bytes())
        .collect();
    upload.write(&words)?;
    let upload_done = device.copy(&upload, &mut source, &full)?;
    if upload_done.value() != 1 || !upload_done.belongs_to(&device) {
        return Err("invalid first completion".into());
    }
    reject(
        "agfx.control.incomplete_destination",
        device.copy(
            &source,
            &mut destination,
            &[CopyRegion {
                bytes: 128,
                ..full[0]
            }],
        ),
        &mut controls,
    )?;
    reject(
        "agfx.control.overrun",
        device.copy(
            &source,
            &mut destination,
            &[CopyRegion {
                destination: 4,
                ..full[0]
            }],
        ),
        &mut controls,
    )?;
    reject(
        "agfx.control.overlapping_destination",
        device.copy(&source, &mut destination, &[full[0], full[0]]),
        &mut controls,
    )?;
    reject(
        "agfx.control.empty_regions",
        device.copy(&source, &mut destination, &[]),
        &mut controls,
    )?;
    reject(
        "agfx.control.unaligned_region",
        device.copy(
            &source,
            &mut destination,
            &[CopyRegion {
                source: 1,
                bytes: 4,
                destination: 0,
            }],
        ),
        &mut controls,
    )?;
    {
        let foreign = Device::new(true)?;
        let mut foreign_buffer = foreign.buffer(256, Memory::Device)?;
        reject(
            "agfx.control.foreign_destination",
            device.copy(&source, &mut foreign_buffer, &full),
            &mut controls,
        )?;
        reject(
            "agfx.control.foreign_source",
            foreign.copy(&source, &mut foreign_buffer, &full),
            &mut controls,
        )?;
        if upload_done.belongs_to(&foreign) {
            return Err("foreign completion accepted".into());
        }
        controls.push(json!({"case_id": "agfx.control.foreign_completion", "status": "pass"}));
    }
    let regions = [
        CopyRegion {
            source: 0,
            destination: 0,
            bytes: 128,
        },
        CopyRegion {
            source: 64,
            destination: 128,
            bytes: 128,
        },
    ];
    let copied = device.copy(&source, &mut destination, &regions)?;
    let completed = device.copy(&destination, &mut readback, &full)?;
    if copied.value() != 2 || completed.value() != 3 || !completed.belongs_to(&device) {
        return Err("invalid ordered completions".into());
    }
    let actual = readback.read()?;
    let expected =
        include_bytes!("../../../../tests/parity/fixtures/agfx/copy_buffer_to_buffer.bin");
    if let Some(index) = actual.iter().zip(expected).position(|(a, b)| a != b) {
        return Err(format!(
            "agfx.copy_buffer_to_buffer byte {index}: expected {}, actual {}",
            expected[index], actual[index]
        )
        .into());
    }
    std::fs::write("copy-buffer.bin", &actual)?;
    let info = device.info();
    Ok(json!({
        "schema_version": 1, "run_token": token, "status": "pass", "host_source_sha256": identity(),
        "device": {"name": info.name, "loader_api_version": info.loader_api_version, "api_version": info.api_version, "driver_version": info.driver_version,
            "vendor_id": info.vendor_id, "device_id": info.device_id, "queue_family": info.queue_family,
            "validation": info.validation, "synchronization_validation": true,
            "enabled_features": ["timelineSemaphore", "vulkanMemoryModel", "runtimeDescriptorArray", "shaderStorageBufferArrayDynamicIndexing"]},
        "memory_flags": {"upload": upload.memory_flags().as_raw(), "device": destination.memory_flags().as_raw(), "readback": readback.memory_flags().as_raw()},
        "required": 1, "executed": 1, "passed": 1,
        "cases": [{"case_id": "agfx.copy_buffer_to_buffer", "status": "pass", "bytes": actual.len(),
            "expected_sha256": hash(expected), "actual_sha256": hash(&actual), "completion_values": [upload_done.value(), copied.value(), completed.value()]}],
        "controls": controls,
    }))
}

fn main() -> std::process::ExitCode {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--identity"] {
        println!("{}", identity());
        return std::process::ExitCode::SUCCESS;
    }
    if args.len() != 2 || args[0] != "--run-token" || args[1].is_empty() {
        eprintln!("usage: buffer_copy --identity | --run-token TOKEN");
        return std::process::ExitCode::FAILURE;
    }
    match run(&args[1]) {
        Ok(record) => {
            println!("{record}");
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("agfx.copy_buffer_to_buffer failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
