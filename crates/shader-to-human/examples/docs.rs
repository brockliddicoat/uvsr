//! Native Vulkan documentation demos and deterministic persistent UI sequences.
//! Uses the existing AGFX owners. No native handle or mapping escapes.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
#[path = "../demos/mod.rs"]
mod demos;

use agfx::{
    Buffer, ComputeDispatch, ComputeInterface, CopyRegion, Device, Memory, ShaderCode, ShaderStage,
    StorageCompute, Texture, TextureCopy, TextureFormat, TextureInfo,
};
use demos::{Inputs, State, BRANCH_COUNTS, CATEGORY_NAMES};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use shader_to_human::{Mat4, UVec4, Vec4};
use std::{ffi::CStr, path::Path};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "bfc15cd9e6f52a199bb3dcd61c87ef5ad51f38f0e05a54b4316d3cafb547a0fc",
    ),
    (
        3,
        "8c672179232dc6e158063887040b2c5f240f6a646d15dceacfb3b5d7cb300151",
    ),
];
const IMAGE: TextureInfo = TextureInfo {
    width: 800,
    height: 600,
    format: TextureFormat::Rgba8Unorm,
};
const STATE_COPY: [CopyRegion; 1] = [CopyRegion {
    source: 0,
    destination: 0,
    bytes: 80,
}];

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
        "crates/shader-to-human/examples/docs.rs",
        "crates/shader-to-human/src/lib.rs",
        "crates/shader-to-human/src/font.rs",
        "crates/shader-to-human/src/math.rs",
        "crates/shader-to-human/src/gather.rs",
        "crates/shader-to-human/src/widgets.rs",
        "crates/shader-to-human/src/scatter.rs",
        "crates/shader-to-human/src/world.rs",
        "crates/shader-to-human/demos/mod.rs",
        "crates/shader-to-human/demos/gather.rs",
        "crates/shader-to-human/demos/scatter.rs",
        "crates/shader-to-human/demos/two_d.rs",
        "crates/shader-to-human/demos/ui.rs",
        "crates/shader-to-human/demos/world.rs",
        "shaders/rust/shader_to_human_demos.rs",
        "tests/parity/fixtures/shader-to-human/camera.txt",
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
            .find(|row| row.0 == level)
            .ok_or("unreviewed optimization level")?
            .1;
        let bytes = std::fs::read(directory.join(format!("demos_opt{level}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("demos_opt{level}.metadata.json")),
        )?)?;
        let sources = identity();
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["case_id"] != format!("s2h.demos.compile.opt{level}")
            || metadata["language"] != "Rust"
            || metadata["stage"] != "compute"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "ordinary-storage-buffer-image"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["entry_points"] != json!(["demos_cs", "update_ui_cs"])
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["entry_sha256"] != sources["shaders/rust/shader_to_human_demos.rs"]
        {
            return Err("unreviewed shader or metadata rejected before Vulkan".into());
        }
        for (field, directory, count) in
            [("library_sources", "src", 7), ("demo_sources", "demos", 6)]
        {
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
            return Err("malformed SPIR-V bytes".into());
        }
        let words = bytes
            .chunks_exact(4)
            .map(|word| u32::from_le_bytes(word.try_into().unwrap()))
            .collect();
        Ok(Self { words, metadata })
    }
}

fn inputs(category: u32, branch: u32) -> Result<Inputs> {
    let values: Vec<f32> =
        include_str!("../../../tests/parity/fixtures/shader-to-human/camera.txt")
            .split_whitespace()
            .map(str::parse)
            .collect::<std::result::Result<_, _>>()?;
    if values.len() != 20 || !values.iter().all(|value| value.is_finite()) {
        return Err("invalid frozen camera".into());
    }
    Ok(Inputs {
        control: UVec4::new(800, 600, category, branch),
        origin_and_depth: Vec4::from_slice(&values[..4]),
        world_from_clip: Mat4::from_cols_array(values[4..].try_into()?),
        mouse: Vec4::ZERO,
        previous_mouse: Vec4::ZERO,
    })
}

fn pack(input: &Inputs) -> Result<Vec<u8>> {
    if input.control.x != 800
        || input.control.y != 600
        || input.control.z >= BRANCH_COUNTS.len() as u32
        || input.control.w >= BRANCH_COUNTS[input.control.z as usize]
        || !input.mouse.is_finite()
        || !input.previous_mouse.is_finite()
        || input.mouse.abs().max_element() > 10000.0
        || input.previous_mouse.abs().max_element() > 10000.0
    {
        return Err("invalid fixed demo input".into());
    }
    let mut bytes = Vec::with_capacity(128);
    for value in input.control.to_array() {
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    for value in input
        .origin_and_depth
        .to_array()
        .into_iter()
        .chain(input.world_from_clip.to_cols_array())
        .chain(input.mouse.to_array())
        .chain(input.previous_mouse.to_array())
    {
        if !value.is_finite() {
            return Err("nonfinite root".into());
        }
        bytes.extend_from_slice(&value.to_le_bytes());
    }
    Ok(bytes)
}

fn state_bytes(state: State) -> Vec<u8> {
    state
        .to_words()
        .into_iter()
        .flat_map(|word| word.to_array())
        .flat_map(u32::to_le_bytes)
        .collect()
}

struct Runner<'d> {
    device: &'d Device,
    render: StorageCompute<'d>,
    update: StorageCompute<'d>,
    image: Texture<'d>,
    image_readback: Buffer<'d>,
    upload: Buffer<'d>,
    state: Buffer<'d>,
    state_readback: Buffer<'d>,
}

impl<'d> Runner<'d> {
    #[allow(unsafe_code)]
    fn new(device: &'d Device, shader: &ReviewedShader) -> Result<Self> {
        let pipeline = |entry: &'static CStr, images, local_size| {
            // SAFETY: U-019. ReviewedShader can contain only the fixed validated
            // modules. Binding0 is an80-byte state buffer, binding1 is Rgba8 for
            // demos_cs only. Root is128 bytes. Both use only enabled Shader and
            // VulkanMemoryModel, with the inspected1/8x8 local sizes and no physical access.
            unsafe {
                device.storage_compute(
                    ShaderCode {
                        words: &shader.words,
                        stage: ShaderStage::Compute,
                        entry_point: entry,
                    },
                    ComputeInterface {
                        buffers: 1,
                        images,
                        sampled_images: 0,
                        samplers: 0,
                        root_bytes: 128,
                        local_size,
                    },
                )
            }
        };
        let mut runner = Self {
            device,
            render: pipeline(c"demos_cs", 1, [8, 8, 1])?,
            update: pipeline(c"update_ui_cs", 0, [1, 1, 1])?,
            image: device.texture(IMAGE)?,
            image_readback: device.buffer(IMAGE.byte_len()?, Memory::Readback)?,
            upload: device.buffer(80, Memory::Upload)?,
            state: device.buffer(80, Memory::Device)?,
            state_readback: device.buffer(80, Memory::Readback)?,
        };
        runner.image.clear([0.0; 4])?;
        runner.reset(State::default())?;
        Ok(runner)
    }

    fn reset(&mut self, state: State) -> Result<()> {
        if state.radio > 3
            || state.checkbox > 1
            || !state.color.is_finite()
            || !state.color_rgba.is_finite()
            || !state.sizes.is_finite()
        {
            return Err("invalid initialized UI state".into());
        }
        self.upload.write(&state_bytes(state))?;
        self.device
            .copy(&self.upload, &mut self.state, &STATE_COPY)?;
        Ok(())
    }

    #[allow(unsafe_code)]
    fn draw(&mut self, inputs: &Inputs) -> Result<(Vec<u8>, [u64; 2])> {
        let root = pack(inputs)?;
        // SAFETY: U-019. Private reviewed pipeline, exact initialized800x600
        // RGBA8 image in GENERAL and80-byte buffer.128-byte finite root and
        //100x75x1 groups give guarded distinct texels. All state modifications
        // are private to an invocation. No shared state writer overlaps this pass.
        let rendered = unsafe {
            self.render.dispatch(
                &mut [&mut self.state],
                &mut [&mut self.image],
                &[],
                &[],
                &[ComputeDispatch {
                    root: &root,
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
        Ok((self.image_readback.read()?, [rendered, copied]))
    }

    #[allow(unsafe_code)]
    fn advance(&mut self, inputs: &Inputs) -> Result<(Vec<u8>, [u64; 2])> {
        let root = pack(inputs)?;
        // SAFETY: U-019. Exactly one invocation reads/writes the complete known
        //80-byte state array after rendering has completed. No image or physical
        //access exists. Fixed source widget ranges preserve initialized values.
        let updated = unsafe {
            self.update.dispatch(
                &mut [&mut self.state],
                &mut [],
                &[],
                &[],
                &[ComputeDispatch {
                    root: &root,
                    groups: [1, 1, 1],
                }],
            )
        }?
        .value();
        let copied = self
            .device
            .copy(&self.state, &mut self.state_readback, &STATE_COPY)?
            .value();
        Ok((self.state_readback.read()?, [updated, copied]))
    }
}

fn capture(name: &str, level: u32, bytes: &[u8]) -> Result<Value> {
    if bytes.len() != 800 * 600 * 4 || bytes.chunks_exact(4).any(|pixel| pixel[3] != 255) {
        return Err(format!("{name}: incomplete image or unexpected source alpha").into());
    }
    let output = format!("{name}-opt{level}.rgba8");
    std::fs::write(&output, bytes)?;
    Ok(json!({"output": output, "bytes": bytes.len(), "sha256": hash(bytes)}))
}

fn run(token: &str, directory: &Path, level: u32) -> Result<Value> {
    let shader = ReviewedShader::load(directory, level)?;
    let device = Device::new(true)?;
    let mut runner = Runner::new(&device, &shader)?;
    let mut cases = Vec::new();
    for (category, &count) in BRANCH_COUNTS.iter().enumerate() {
        for branch in 0..count {
            runner.reset(State::default())?;
            let input = inputs(category as u32, branch)?;
            let (bytes, completion) = runner.draw(&input)?;
            let name = format!("docs-{}-{branch}", CATEGORY_NAMES[category]);
            let image = capture(&name, level, &bytes)?;
            cases.push(json!({"case_id": format!("s2h.{name}.opt{level}"), "status": "pass",
                "category": category, "branch": branch, "image": image, "root_sha256": hash(&pack(&input)?),
                "completion_values": completion, "oracle": "complete image and opaque alpha", "source_agreement": "not checked"}));
            eprintln!("completed {name} opt{level}");
        }
    }
    // Frame order is render from the old state, then commit exactly one update.
    // Values hit exact source slider endpoints to avoid using implementation
    // floating-point rounding as the state oracle. Fractional CPU cases are separate.
    let steps = [
        ("radio-red", 1, 113.0, 17.0, 1.0),
        ("radio-green", 1, 129.0, 17.0, 1.0),
        ("radio-blue", 1, 145.0, 17.0, 1.0),
        ("clear", 0, 146.0, 18.0, 1.0),
        ("checkbox-idle", 2, 113.0, 17.0, 0.0),
        ("checkbox-press", 2, 113.0, 17.0, 1.0),
        ("checkbox-hold", 2, 113.0, 17.0, 1.0),
        ("checkbox-release", 2, 113.0, 17.0, 0.0),
        ("checkbox-second-press", 2, 113.0, 17.0, 1.0),
        ("float-high", 3, 169.0, 15.0, 1.0),
        ("float-outside", 3, 300.0, 15.0, 1.0),
        ("float-low", 3, 44.0, 15.0, 1.0),
        ("rgb-blue", 4, 169.0, 47.0, 1.0),
        ("rgba-alpha", 5, 169.0, 64.0, 1.0),
        ("rgba-release", 5, 169.0, 64.0, 0.0),
    ];
    let mut state = State::default();
    runner.reset(state)?;
    let mut previous = Vec4::ZERO;
    let mut interactions = Vec::new();
    for (name, branch, x, y, pressed) in steps {
        let mut input = inputs(4, branch)?;
        input.mouse = Vec4::new(x, y, pressed, 0.0);
        input.previous_mouse = previous;
        let before = state_bytes(state);
        let (bytes, draw_completion) = runner.draw(&input)?;
        let image = capture(&format!("ui-{name}"), level, &bytes)?;
        demos::update(&input, &mut state);
        let expected = state_bytes(state);
        let (actual, update_completion) = runner.advance(&input)?;
        if actual != expected {
            return Err(
                format!("{name}: GPU UI words differ from source-contract CPU state").into(),
            );
        }
        let words = std::array::from_fn(|index| {
            UVec4::from_array(std::array::from_fn(|channel| {
                let offset = (index * 4 + channel) * 4;
                u32::from_le_bytes(actual[offset..offset + 4].try_into().unwrap())
            }))
        });
        state = State::from_words(words);
        let output = format!("ui-{name}-opt{level}.state");
        std::fs::write(&output, &actual)?;
        interactions.push(json!({"case_id": format!("s2h.docs-ui.{name}.opt{level}"), "status": "pass",
            "mouse": input.mouse.to_array(), "previous_mouse": previous.to_array(), "branch": branch,
            "root_sha256": hash(&pack(&input)?), "before_state_sha256": hash(&before),
            "after_state_words": actual.chunks_exact(4).map(|word| u32::from_le_bytes(word.try_into().unwrap())).collect::<Vec<_>>(),
            "state": {"output": output, "bytes": 80, "sha256": hash(&actual), "expected_sha256": hash(&expected)},
            "image": image, "completion_values": [draw_completion[0], draw_completion[1], update_completion[0], update_completion[1]]}));
        previous = input.mouse;
    }
    let info = device.info();
    Ok(
        json!({"schema_version": 1, "run_token": token, "status": "pass", "optimization_level": level,
        "scope": "37 documentation images and 15 ordered UI frames; source image agreement remains separate",
        "required": 37, "executed": cases.len(), "cases": cases, "interactions": interactions,
        "host_source_sha256": identity(), "shader": shader.metadata,
        "device": {"name": info.name, "loader_api_version": info.loader_api_version, "api_version": info.api_version,
            "driver_version": info.driver_version, "vendor_id": info.vendor_id, "device_id": info.device_id,
            "validation": info.validation, "synchronization_validation": true,
            "enabled_features": ["timelineSemaphore", "vulkanMemoryModel", "runtimeDescriptorArray", "shaderStorageBufferArrayDynamicIndexing"]}}),
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
        eprintln!("usage: docs --identity | --run-token TOKEN --shader-dir PATH --level 0|3");
        return std::process::ExitCode::FAILURE;
    }
    let result = args[5]
        .parse::<u32>()
        .map_err(|error| error.into())
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
