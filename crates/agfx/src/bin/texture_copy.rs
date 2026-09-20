//! AGFX f91b108a texture transfer fixtures and initialization controls.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
//! Explicit native executable only. Ordinary Cargo tests never open Vulkan.
#![forbid(unsafe_code)]
use agfx::{CopyRegion, Device, Error, Memory, TextureCopy, TextureFormat, TextureInfo};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

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
        "crates/agfx/src/vulkan/ownership.rs",
        "crates/agfx/src/vulkan/sampler.rs",
        "crates/agfx/src/vulkan/texture.rs",
        "crates/agfx/src/bin/texture_copy.rs",
        "tests/parity/fixtures/agfx/copy_buffer_to_texture.png",
        "tests/parity/fixtures/agfx/copy_texture_to_buffer.bin"
    )
}

fn reject<T>(id: &str, result: Result<T, Error>, controls: &mut Vec<Value>) -> Result<(), String> {
    match result {
        Err(Error::Invalid(message)) => {
            controls.push(json!({"case_id":format!("agfx.texture.control.{id}"),"status":"pass","diagnostic":message}));
            Ok(())
        }
        Err(other) => Err(format!("{id}: wrong rejection {other}")),
        Ok(_) => Err(format!("{id}: invalid operation accepted")),
    }
}

fn record(
    id: &str,
    actual: &[u8],
    expected: &[u8],
    completions: &[u64],
    cases: &mut Vec<Value>,
) -> Result<(), Box<dyn std::error::Error>> {
    if actual != expected {
        let first = actual.iter().zip(expected).position(|(a, b)| a != b);
        return Err(format!(
            "{id}: bytes differ, first {first:?}, lengths {}/{}",
            actual.len(),
            expected.len()
        )
        .into());
    }
    std::fs::write(format!("{id}.bin"), actual)?;
    cases.push(
        json!({"case_id":id,"status":"pass","bytes":actual.len(),"actual_sha256":hash(actual),
        "expected_sha256":hash(expected),"completion_values":completions}),
    );
    Ok(())
}

fn run(token: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let device = Device::new(true)?;
    let foreign = Device::new(true)?;
    let mut cases = Vec::new();
    let mut controls = Vec::new();
    let info = TextureInfo {
        width: 128,
        height: 128,
        format: TextureFormat::Rgba8Unorm,
    };
    reject(
        "zero_extent",
        device.texture(TextureInfo { width: 0, ..info }),
        &mut controls,
    )?;
    reject(
        "size_overflow",
        device.texture(TextureInfo {
            width: u32::MAX,
            height: u32::MAX,
            ..info
        }),
        &mut controls,
    )?;
    let mut image = device.texture(info)?;
    let mut upload = device.buffer(info.byte_len()?, Memory::Upload)?;
    let mut readback = device.buffer(info.byte_len()?, Memory::Readback)?;
    let whole = TextureCopy::whole(info);
    reject(
        "uninitialized_image",
        device.copy_texture_to_buffer(&mut image, &mut readback, whole),
        &mut controls,
    )?;
    reject(
        "uninitialized_upload",
        device.copy_buffer_to_texture(&upload, &mut image, whole),
        &mut controls,
    )?;
    let mut base = Vec::with_capacity(info.byte_len()? as usize);
    for y in 0..128_u32 {
        for x in 0..128_u32 {
            base.extend_from_slice(&[
                (x * 255 / 127) as u8,
                (y * 255 / 127) as u8,
                if (x / 16 + y / 16) % 2 != 0 { 255 } else { 0 },
                255,
            ]);
        }
    }
    upload.write(&base)?;
    reject(
        "partial_initialization",
        device.copy_buffer_to_texture(
            &upload,
            &mut image,
            TextureCopy {
                extent: [1, 1],
                ..whole
            },
        ),
        &mut controls,
    )?;
    reject(
        "overrun",
        device.copy_buffer_to_texture(
            &upload,
            &mut image,
            TextureCopy {
                origin: [1, 0],
                ..whole
            },
        ),
        &mut controls,
    )?;
    reject(
        "bad_pitch",
        device.copy_buffer_to_texture(
            &upload,
            &mut image,
            TextureCopy {
                row_bytes: 511,
                ..whole
            },
        ),
        &mut controls,
    )?;
    let first = device
        .copy_buffer_to_texture(&upload, &mut image, whole)?
        .value();
    let mut patch = Vec::new();
    for y in 0..32 {
        for x in 0..32 {
            patch.extend_from_slice(if x >= y {
                &[255, 0, 0, 255]
            } else {
                &[32, 0, 255, 255]
            });
        }
    }
    let mut patch_upload = device.buffer(patch.len() as u64, Memory::Upload)?;
    patch_upload.write(&patch)?;
    let second = device
        .copy_buffer_to_texture(
            &patch_upload,
            &mut image,
            TextureCopy {
                buffer_offset: 0,
                row_bytes: 128,
                origin: [16, 64],
                extent: [32, 32],
            },
        )?
        .value();
    for y in 0..32 {
        let offset = ((y + 64) * 128 + 16) * 4;
        base[offset..offset + 128].copy_from_slice(&patch[y * 128..(y + 1) * 128]);
    }
    let third = device
        .copy_texture_to_buffer(&mut image, &mut readback, whole)?
        .value();
    record(
        "agfx.copy_buffer_to_texture",
        &readback.read()?,
        &base,
        &[first, second, third],
        &mut cases,
    )?;

    // Source nonzero destination offset and untouched 0xAB prefix.
    let info = TextureInfo {
        width: 64,
        height: 64,
        ..info
    };
    let mut image = device.texture(info)?;
    let whole = TextureCopy::whole(info);
    let bytes = info.byte_len()?;
    let mut pixels = Vec::new();
    for y in 0..64_u8 {
        for x in 0..64_u8 {
            pixels.extend_from_slice(&[x * 4, y * 4, (x ^ y) * 4, 255]);
        }
    }
    let mut upload = device.buffer(bytes, Memory::Upload)?;
    upload.write(&pixels)?;
    let first = device
        .copy_buffer_to_texture(&upload, &mut image, whole)?
        .value();
    let mut destination = device.buffer(bytes + 256, Memory::Device)?;
    let offset = TextureCopy {
        buffer_offset: 256,
        row_bytes: 256,
        ..whole
    };
    reject(
        "uninitialized_prefix",
        device.copy_texture_to_buffer(&mut image, &mut destination, offset),
        &mut controls,
    )?;
    let mut filler = device.buffer(bytes + 256, Memory::Upload)?;
    filler.write(&vec![0xab; (bytes + 256) as usize])?;
    let full = [CopyRegion {
        source: 0,
        destination: 0,
        bytes: bytes + 256,
    }];
    let second = device.copy(&filler, &mut destination, &full)?.value();
    let third = device
        .copy_texture_to_buffer(&mut image, &mut destination, offset)?
        .value();
    let mut readback = device.buffer(bytes + 256, Memory::Readback)?;
    let fourth = device.copy(&destination, &mut readback, &full)?.value();
    record(
        "agfx.copy_texture_to_buffer",
        &readback.read()?,
        include_bytes!("../../../../tests/parity/fixtures/agfx/copy_texture_to_buffer.bin"),
        &[first, second, third, fourth],
        &mut cases,
    )?;

    let mut other = foreign.buffer(bytes, Memory::Upload)?;
    other.write(&pixels)?;
    reject(
        "foreign_upload",
        device.copy_buffer_to_texture(&other, &mut image, whole),
        &mut controls,
    )?;
    let mut other_image = foreign.texture(info)?;
    other_image.clear([0.0; 4])?;
    reject(
        "foreign_image",
        device.copy_texture_to_buffer(&mut other_image, &mut readback, whole),
        &mut controls,
    )?;

    // Native format conversion: exact endpoints avoid a guessed halfway rule.
    let info = TextureInfo {
        width: 7,
        height: 5,
        format: TextureFormat::Rgba8Unorm,
    };
    let mut image = device.texture(info)?;
    let mut readback = device.buffer(info.byte_len()?, Memory::Readback)?;
    let first = image.clear([1.0, 0.0, 1.0, 1.0])?.value();
    let second = device
        .copy_texture_to_buffer(&mut image, &mut readback, TextureCopy::whole(info))?
        .value();
    record(
        "agfx.texture.clear_unorm",
        &readback.read()?,
        &[255, 0, 255, 255].repeat(35),
        &[first, second],
        &mut cases,
    )?;

    let info = TextureInfo {
        format: TextureFormat::Rgba32Float,
        ..info
    };
    let mut image = device.texture(info)?;
    let mut upload = device.buffer(info.byte_len()?, Memory::Upload)?;
    let mut readback = device.buffer(info.byte_len()?, Memory::Readback)?;
    let pattern: Vec<_> = (0..140)
        .flat_map(|i| ((i as f32 - 70.0) / 8.0).to_le_bytes())
        .collect();
    upload.write(&pattern)?;
    reject(
        "float_offset_alignment",
        device.copy_buffer_to_texture(
            &upload,
            &mut image,
            TextureCopy {
                buffer_offset: 4,
                extent: [1, 1],
                ..TextureCopy::whole(info)
            },
        ),
        &mut controls,
    )?;
    let first = device
        .copy_buffer_to_texture(&upload, &mut image, TextureCopy::whole(info))?
        .value();
    let second = device
        .copy_texture_to_buffer(&mut image, &mut readback, TextureCopy::whole(info))?
        .value();
    record(
        "agfx.texture.float_roundtrip",
        &readback.read()?,
        &pattern,
        &[first, second],
        &mut cases,
    )?;
    let first = image.clear([-2.0, 0.25, 8.0, 1.0])?.value();
    let second = device
        .copy_texture_to_buffer(&mut image, &mut readback, TextureCopy::whole(info))?
        .value();
    let pixel: Vec<_> = [-2.0_f32, 0.25, 8.0, 1.0]
        .into_iter()
        .flat_map(f32::to_le_bytes)
        .collect();
    record(
        "agfx.texture.clear_float",
        &readback.read()?,
        &pixel.repeat(35),
        &[first, second],
        &mut cases,
    )?;

    // Additional row-pitch oracle. Both upload and readback have untouched gaps.
    let info = TextureInfo {
        width: 5,
        height: 3,
        format: TextureFormat::Rgba8Unorm,
    };
    let mut image = device.texture(info)?;
    let mut upload = device.buffer(96, Memory::Upload)?;
    let mut source = vec![0x11; 96];
    let mut expected = vec![0xcd; 104];
    for y in 0..3 {
        for x in 0..20 {
            let value = (y * 20 + x + 1) as u8;
            source[8 + y * 28 + x] = value;
            expected[12 + y * 32 + x] = value;
        }
    }
    upload.write(&source)?;
    let first = device
        .copy_buffer_to_texture(
            &upload,
            &mut image,
            TextureCopy {
                buffer_offset: 8,
                row_bytes: 28,
                ..TextureCopy::whole(info)
            },
        )?
        .value();
    let mut readback = device.buffer(104, Memory::Readback)?;
    let region = TextureCopy {
        buffer_offset: 12,
        row_bytes: 32,
        ..TextureCopy::whole(info)
    };
    reject(
        "uninitialized_row_padding",
        device.copy_texture_to_buffer(&mut image, &mut readback, region),
        &mut controls,
    )?;
    let mut seed = device.buffer(104, Memory::Upload)?;
    seed.write(&[0xcd; 104])?;
    let second = device
        .copy(
            &seed,
            &mut readback,
            &[CopyRegion {
                source: 0,
                destination: 0,
                bytes: 104,
            }],
        )?
        .value();
    let third = device
        .copy_texture_to_buffer(&mut image, &mut readback, region)?
        .value();
    record(
        "agfx.texture.padded_rows",
        &readback.read()?,
        &expected,
        &[first, second, third],
        &mut cases,
    )?;

    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"pass","required":6,"executed":cases.len(),"passed":cases.len(),
        "host_source_sha256":identity(),"cases":cases,"controls":controls,
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
    if args.len() != 2 || args[0] != "--run-token" || args[1].is_empty() {
        eprintln!("usage: texture_copy --identity | --run-token TOKEN");
        return std::process::ExitCode::FAILURE;
    }
    match run(&args[1]) {
        Ok(record) => {
            println!("{record}");
            std::process::ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("agfx.texture failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
