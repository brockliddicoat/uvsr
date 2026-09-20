//! Fixed AGFX source raster cases. Arbitrary modules/commands are not admitted.
//! Copyright (c) 2026 Amélie Heinrich. See legal/licenses/AGFX-MIT.txt.
#![deny(unsafe_code)]
#![deny(unsafe_op_in_unsafe_fn)]
use agfx::*;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{ffi::CStr, io::Cursor, path::Path};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const IMAGE: TextureInfo = TextureInfo {
    width: 128,
    height: 128,
    format: TextureFormat::Rgba8Unorm,
};
const REVIEWED: [(u32, &str); 2] = [
    (
        0,
        "4b83c11385299152288ea997ffb70fa2ffeab9776c6d11ce773894f577ff4da0",
    ),
    (
        3,
        "b13da2b8573daaf71e8df0fbb96e768adeadaba1ed3c357b3f5919ecd3e747d2",
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
        "crates/agfx/src/vulkan/bindings.rs",
        "crates/agfx/src/vulkan/compute.rs",
        "crates/agfx/src/vulkan/graphics.rs",
        "crates/agfx/src/vulkan/ownership.rs",
        "crates/agfx/src/vulkan/sampler.rs",
        "crates/agfx/src/vulkan/texture.rs",
        "crates/agfx/src/bin/raster.rs",
        "shaders/rust/raster.rs"
    )
}
struct ReviewedShader {
    level: u32,
    words: Vec<u32>,
    metadata: Value,
}
impl ReviewedShader {
    fn load(directory: &Path, level: u32, expected: &str) -> Result<Self> {
        let stem = format!("raster_opt{level}");
        let bytes = std::fs::read(directory.join(format!("{stem}.spv")))?;
        let metadata: Value = serde_json::from_slice(&std::fs::read(
            directory.join(format!("{stem}.metadata.json")),
        )?)?;
        if hash(&bytes) != expected
            || metadata["payload_sha256"] != expected
            || metadata["identity"]["source_sha256"] != identity()["shaders/rust/raster.rs"]
            || metadata["schema_version"] != 1
            || metadata["status"] != "pass"
            || metadata["language"] != "Rust"
            || metadata["stage"] != "vertex+fragment"
            || metadata["target"] != "spirv-unknown-vulkan1.3"
            || metadata["payload_type"] != "SPIR-V"
            || metadata["profile"] != "agfx-ordinary-raster"
            || metadata["capabilities"] != json!(["Shader", "VulkanMemoryModel"])
            || metadata["extensions"] != json!([])
        {
            return Err("unreviewed raster payload or metadata rejected before Vulkan".into());
        }
        Ok(Self {
            level,
            words: ash::util::read_spv(&mut Cursor::new(bytes))?,
            metadata,
        })
    }
    fn stage(&self, entry: &'static CStr, stage: ShaderStage) -> ShaderCode<'_> {
        ShaderCode {
            words: &self.words,
            stage,
            entry_point: entry,
        }
    }
}

#[derive(Clone)]
struct DrawSpec {
    vs: &'static CStr,
    fs: Option<&'static CStr>,
    info: RenderPipelineInfo,
    root: Vec<u8>,
    count: u32,
    first: u32,
    index_type: Option<IndexType>,
    vertex_offset: i32,
    viewport: Viewport,
    scissor: Scissor,
}
impl DrawSpec {
    fn new(vs: &'static CStr, fs: &'static CStr, root: Vec<u8>, count: u32) -> Self {
        Self {
            vs,
            fs: Some(fs),
            info: RenderPipelineInfo {
                interface: RenderInterface {
                    root_bytes: root.len() as u32,
                    ..Default::default()
                },
                front_face: FrontFace::CounterClockwise,
                colors: vec![ColorTarget {
                    format: IMAGE.format,
                    blend: BlendState::default(),
                }],
                ..Default::default()
            },
            root,
            count,
            first: 0,
            index_type: None,
            vertex_offset: 0,
            viewport: Viewport::whole(128, 128),
            scissor: Scissor::whole(128, 128),
        }
    }
}
struct Case {
    name: String,
    golden: String,
    draws: Vec<DrawSpec>,
    clear: [f32; 4],
    depth_clear: Option<f32>,
    load: LoadOperation,
    seed: bool,
    indices: Vec<u32>,
    cache_roundtrip: bool,
}
impl Case {
    fn new(name: &str, draws: Vec<DrawSpec>) -> Self {
        Self {
            name: name.into(),
            golden: format!("{name}.png"),
            draws,
            clear: [0.0, 0.0, 0.0, 1.0],
            depth_clear: None,
            load: LoadOperation::Clear,
            seed: false,
            indices: vec![],
            cache_roundtrip: false,
        }
    }
}
fn floats(values: &[f32]) -> Vec<u8> {
    values.iter().flat_map(|f| f.to_ne_bytes()).collect()
}
fn words(values: &[u32]) -> Vec<u8> {
    values.iter().flat_map(|n| n.to_ne_bytes()).collect()
}
fn raster_root(color: [f32; 4], discard: f32, vertex_color: u32) -> Vec<u8> {
    let mut bytes = floats(&color);
    bytes.extend(floats(&[discard]));
    bytes.extend(words(&[vertex_color, 0, 0]));
    bytes
}
fn depth_draw(
    fullscreen: bool,
    depths: [f32; 3],
    tint: [f32; 3],
    comparison: ComparisonFunction,
    write: bool,
) -> DrawSpec {
    let root = floats(&[
        depths[0], depths[1], depths[2], 0.0, tint[0], tint[1], tint[2], 1.0,
    ]);
    let mut draw = DrawSpec::new(
        if fullscreen {
            c"depth_fullscreen_vs"
        } else {
            c"depth_vs"
        },
        c"color3_fs",
        root,
        if fullscreen { 6 } else { 18 },
    );
    draw.info.depth = Some(DepthState {
        format: TextureFormat::D32Float,
        test: true,
        write,
        comparison,
    });
    draw
}
fn blend_case(
    name: &str,
    source: BlendFactor,
    destination: BlendFactor,
    operation: BlendOperation,
) -> Case {
    let base = DrawSpec::new(
        c"blend_vs",
        c"color4_fs",
        floats(&[0.0, 0.0, 0.0, 0.0, 0.4, 0.4, 0.4, 0.0, 1.0, 0.6, 0.2, 0.0]),
        18,
    );
    let mut over = DrawSpec::new(
        c"blend_fullscreen_vs",
        c"color4_fs",
        floats(&[
            0.5, 0.25, 0.125, 0.25, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        ]),
        6,
    );
    over.info.colors[0].blend = BlendState {
        enabled: true,
        source_color: source,
        destination_color: destination,
        color_operation: operation,
        source_alpha: alpha_equivalent(source),
        destination_alpha: alpha_equivalent(destination),
        alpha_operation: operation,
    };
    let mut case = Case::new(name, vec![base, over]);
    case.clear = [0.0; 4];
    case
}

fn alpha_equivalent(factor: BlendFactor) -> BlendFactor {
    match factor {
        BlendFactor::SourceColor => BlendFactor::SourceAlpha,
        BlendFactor::OneMinusSourceColor => BlendFactor::OneMinusSourceAlpha,
        BlendFactor::DestinationColor => BlendFactor::DestinationAlpha,
        BlendFactor::OneMinusDestinationColor => BlendFactor::OneMinusDestinationAlpha,
        other => other,
    }
}
fn cases() -> Vec<Case> {
    let base = DrawSpec::new(c"raster_vs", c"raster_fs", raster_root([1.0; 4], 2.0, 1), 6);
    let mut cases = Vec::new();
    for (name, cull, front) in [
        (
            "pipeline_cull_mode_none",
            CullMode::None,
            FrontFace::CounterClockwise,
        ),
        (
            "pipeline_cull_mode_back",
            CullMode::Back,
            FrontFace::CounterClockwise,
        ),
        (
            "pipeline_cull_mode_front",
            CullMode::Front,
            FrontFace::CounterClockwise,
        ),
        ("draw_ccw", CullMode::Back, FrontFace::CounterClockwise),
        ("draw_cw", CullMode::Back, FrontFace::Clockwise),
    ] {
        let mut draw = base.clone();
        draw.info.cull = cull;
        draw.info.front_face = front;
        cases.push(Case::new(name, vec![draw]));
    }
    for (name, fill, topology) in [
        ("draw_wireframe", FillMode::Wireframe, Topology::Triangles),
        ("draw_points", FillMode::Solid, Topology::Points),
        ("draw_lines", FillMode::Solid, Topology::Lines),
    ] {
        let mut draw = base.clone();
        draw.info.fill = fill;
        draw.info.topology = topology;
        cases.push(Case::new(name, vec![draw]));
    }
    for (name, root) in [
        ("draw_fragment_discard", raster_root([1.0; 4], 0.5, 1)),
        (
            "draw_push_constants",
            raster_root([0.25, 0.75, 0.5, 1.0], 2.0, 0),
        ),
    ] {
        let mut draw = base.clone();
        draw.root = root;
        cases.push(Case::new(name, vec![draw]));
    }
    for name in [
        "draw_triangle",
        "viewport",
        "scissor_rect",
        "cache_roundtrip",
    ] {
        let mut draw = DrawSpec::new(c"triangle_vs", c"color3_fs", vec![], 3);
        if name == "viewport" {
            draw.viewport = Viewport {
                x: 64.0,
                y: 64.0,
                ..Viewport::whole(64, 64)
            };
        }
        if name == "scissor_rect" {
            draw.scissor = Scissor {
                x: 24,
                y: 40,
                width: 64,
                height: 64,
            };
        }
        let mut case = Case::new(name, vec![draw]);
        if name == "cache_roundtrip" {
            case.cache_roundtrip = true;
            case.golden = "draw_triangle.png".into();
        }
        cases.push(case);
    }
    for (name, index_type) in [
        ("draw_indexed", IndexType::U32),
        ("draw_indexed_u16", IndexType::U16),
        ("draw_lines_indexed", IndexType::U32),
        ("indexed_offset", IndexType::U32),
    ] {
        let mut draw = DrawSpec::new(c"indexed_vs", c"color4_fs", words(&[0, 0, 0, 0]), 6);
        draw.info.interface.buffers = 1;
        draw.index_type = Some(index_type);
        let mut indices = vec![0, 1, 2, 0, 2, 3];
        if name == "draw_lines_indexed" {
            draw.info.topology = Topology::Lines;
            draw.count = 12;
            indices = vec![0, 1, 1, 2, 2, 3, 3, 0, 0, 2, 1, 3];
        }
        if name == "indexed_offset" {
            draw.first = 3;
            draw.vertex_offset = -4;
            indices = vec![0, 0, 0, 4, 5, 6, 4, 6, 7];
        }
        let mut case = Case::new(name, vec![draw]);
        case.indices = indices;
        if name == "draw_indexed_u16" || name == "indexed_offset" {
            case.golden = "draw_indexed.png".into();
        }
        cases.push(case);
    }
    for (name, compare) in [
        ("never", ComparisonFunction::Never),
        ("less", ComparisonFunction::Less),
        ("equal", ComparisonFunction::Equal),
        ("less_equal", ComparisonFunction::LessEqual),
        ("greater", ComparisonFunction::Greater),
        ("not_equal", ComparisonFunction::NotEqual),
        ("greater_equal", ComparisonFunction::GreaterEqual),
        ("always", ComparisonFunction::Always),
    ] {
        let floor = depth_draw(
            true,
            [0.5, 0.0, 0.0],
            [0.0; 3],
            ComparisonFunction::Always,
            true,
        );
        let probes = depth_draw(false, [0.25, 0.5, 0.75], [1.0; 3], compare, false);
        let mut case = Case::new(&format!("draw_depth_test_{name}"), vec![floor, probes]);
        case.depth_clear = Some(1.0);
        cases.push(case);
    }
    let mut case = Case::new(
        "draw_depth_clear_value",
        vec![depth_draw(
            false,
            [0.25, 0.5, 0.75],
            [1.0; 3],
            ComparisonFunction::Less,
            false,
        )],
    );
    case.depth_clear = Some(0.5);
    cases.push(case);
    for enabled in [false, true] {
        let suffix = if enabled { "enabled" } else { "disabled" };
        let mut draw = depth_draw(
            false,
            [-0.5, 0.5, 1.5],
            [1.0; 3],
            ComparisonFunction::Always,
            false,
        );
        draw.info.depth_clamp = enabled;
        let mut case = Case::new(&format!("draw_depth_clamp_{suffix}"), vec![draw]);
        case.depth_clear = Some(1.0);
        cases.push(case);
        let columns = depth_draw(
            false,
            [0.5; 3],
            [1.0; 3],
            ComparisonFunction::Always,
            enabled,
        );
        let cover = depth_draw(
            true,
            [0.75, 0.0, 0.0],
            [0.5; 3],
            ComparisonFunction::Less,
            false,
        );
        let mut case = Case::new(&format!("depth_write_{suffix}"), vec![columns, cover]);
        case.depth_clear = Some(1.0);
        cases.push(case);
    }
    for (name, factor) in [
        ("zero", BlendFactor::Zero),
        ("one", BlendFactor::One),
        ("src_color", BlendFactor::SourceColor),
        ("one_minus_src_color", BlendFactor::OneMinusSourceColor),
        ("dst_color", BlendFactor::DestinationColor),
        ("one_minus_dst_color", BlendFactor::OneMinusDestinationColor),
        ("src_alpha", BlendFactor::SourceAlpha),
        ("one_minus_src_alpha", BlendFactor::OneMinusSourceAlpha),
        ("dst_alpha", BlendFactor::DestinationAlpha),
        ("one_minus_dst_alpha", BlendFactor::OneMinusDestinationAlpha),
    ] {
        cases.push(blend_case(
            &format!("draw_blend_factor_{name}"),
            factor,
            BlendFactor::One,
            BlendOperation::Add,
        ));
    }
    for (name, op) in [
        ("add", BlendOperation::Add),
        ("subtract", BlendOperation::Subtract),
        ("reverse_subtract", BlendOperation::ReverseSubtract),
        ("min", BlendOperation::Min),
        ("max", BlendOperation::Max),
    ] {
        cases.push(blend_case(
            &format!("draw_blend_op_{name}"),
            BlendFactor::One,
            BlendFactor::One,
            op,
        ));
    }
    cases.push(blend_case(
        "draw_alpha_blend",
        BlendFactor::SourceAlpha,
        BlendFactor::OneMinusSourceAlpha,
        BlendOperation::Add,
    ));
    for (name, load) in [
        ("clear", LoadOperation::Clear),
        ("load", LoadOperation::Load),
        ("dont_care", LoadOperation::DontCare),
    ] {
        let mut root = floats(&[1.0, 128.0 / 255.0, 0.0, 1.0]);
        root.extend(words(&[
            u32::from(load == LoadOperation::DontCare),
            0,
            0,
            0,
        ]));
        let mut case = Case::new(
            &format!("render_pass_action_{name}"),
            vec![DrawSpec::new(c"pass_vs", c"pass_fs", root, 3)],
        );
        case.load = load;
        case.clear = [51.0 / 255.0, 102.0 / 255.0, 204.0 / 255.0, 1.0];
        case.seed = true;
        cases.push(case);
    }
    assert_eq!(cases.len(), 50);
    cases
}

fn vertex_bytes() -> Vec<u8> {
    floats(&[
        -0.8, -0.8, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.8, -0.8, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.8,
        0.8, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, -0.8, 0.8, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0,
    ])
}
fn attachment_usage() -> TextureUsage {
    TextureUsage {
        attachment: true,
        sampled: true,
        ..Default::default()
    }
}

#[allow(unsafe_code)]
fn run_case(device: &Device, shader: &ReviewedShader, case: &Case) -> Result<Value> {
    let mut target = device.texture_with_usage(IMAGE, attachment_usage())?;
    if case.seed {
        let data: Vec<u8> = (0..128_u8)
            .flat_map(|y| (0..128_u8).flat_map(move |x| [x, y, x ^ y, 255]))
            .collect();
        let mut upload = device.buffer(data.len() as u64, Memory::Upload)?;
        upload.write(&data)?;
        device.copy_buffer_to_texture(&upload, &mut target, TextureCopy::whole(IMAGE))?;
    }
    let mut depth = if case.depth_clear.is_some() {
        Some(device.texture_with_usage(
            TextureInfo {
                format: TextureFormat::D32Float,
                ..IMAGE
            },
            TextureUsage {
                attachment: true,
                ..Default::default()
            },
        )?)
    } else {
        None
    };
    let mut vertices = device.buffer(128, Memory::Upload)?;
    vertices.write(&vertex_bytes())?;
    let index_bytes = match case.draws[0].index_type {
        Some(IndexType::U16) => case
            .indices
            .iter()
            .flat_map(|i| (*i as u16).to_ne_bytes())
            .collect(),
        _ => words(&case.indices),
    };
    let mut indices = device.buffer(index_bytes.len().max(4) as u64, Memory::Upload)?;
    indices.write(if index_bytes.is_empty() {
        &[0; 4]
    } else {
        &index_bytes
    })?;
    let mut pipelines = Vec::new();
    let mut cache_sizes = Vec::new();
    for draw in &case.draws {
        // SAFETY: U-021. Private fixed payload allowlist, independently validated
        // and reviewed entry interfaces. Specs below admit only matching stage
        // pairs, exact roots, readonly buffer0 for indexed vertices and no other
        // shader resources. PointSize=1 is explicit. Cache starts empty.
        let mut pipeline = unsafe {
            device.render_pipeline(
                shader.stage(draw.vs, ShaderStage::Vertex),
                draw.fs.map(|s| shader.stage(s, ShaderStage::Fragment)),
                draw.info.clone(),
                &[],
            )
        }?;
        if case.cache_roundtrip {
            let cache = pipeline.cache_data()?;
            if cache.len() < 32 {
                return Err("incomplete native pipeline cache".into());
            }
            cache_sizes.push(cache.len());
            // SAFETY: U-021. Identical reviewed shader/state/device. These exact
            // cache bytes were just retrieved from this device's live cache.
            pipeline = unsafe {
                device.render_pipeline(
                    shader.stage(draw.vs, ShaderStage::Vertex),
                    draw.fs.map(|s| shader.stage(s, ShaderStage::Fragment)),
                    draw.info.clone(),
                    &cache,
                )
            }?;
        }
        pipelines.push(pipeline);
    }
    let buffers = if case.indices.is_empty() {
        vec![]
    } else {
        vec![&vertices]
    };
    let draws: Vec<_> = case
        .draws
        .iter()
        .zip(&pipelines)
        .map(|(d, p)| RenderDraw {
            pipeline: p,
            root: &d.root,
            viewport: d.viewport,
            scissor: d.scissor,
            instances: 1,
            first_instance: 0,
            vertices: if let Some(index_type) = d.index_type {
                DrawVertices::Indexed {
                    buffer: &indices,
                    index_type,
                    count: d.count,
                    first: d.first,
                    vertex_offset: d.vertex_offset,
                }
            } else {
                DrawVertices::Direct {
                    count: d.count,
                    first: d.first,
                }
            },
        })
        .collect();
    // SAFETY: U-021. Fixed draw specs bound raster IDs0..5, triangle/pass0..2,
    // columns0..17 or fullscreen0..5. Initialized vertex stride32 has four
    // elements. Reviewed index streams plus first/base select exactly0..3.
    // One instance, finite exact roots, no shader resource writes or aliases.
    // Clear initializes every attachment before depth/blend/discard. LOAD uses
    // the complete seed. DontCare uses the source oversized triangle, no culling,
    // blending or discard, and writes all four channels across the full viewport.
    let rendered = unsafe {
        device.render(
            &mut [ColorAttachment {
                texture: &mut target,
                load: case.load,
                store: StoreOperation::Store,
                clear: case.clear,
            }],
            depth.as_mut().map(|d| DepthAttachment {
                texture: d,
                load: LoadOperation::Clear,
                store: StoreOperation::Store,
                clear: case.depth_clear.unwrap(),
            }),
            &RenderResources {
                buffers: &buffers,
                ..Default::default()
            },
            &draws,
        )
    }?
    .value();
    let mut readback = device.buffer(IMAGE.byte_len()?, Memory::Readback)?;
    let copied = device
        .copy_texture_to_buffer(&mut target, &mut readback, TextureCopy::whole(IMAGE))?
        .value();
    let bytes = readback.read()?;
    let id = format!("agfx.raster.{}.opt{}", case.name, shader.level);
    std::fs::write(format!("{id}.rgba8"), &bytes)?;
    let depth_record = if let Some(d) = &mut depth {
        let info = d.info();
        let completed = device
            .copy_texture_to_buffer(d, &mut readback, TextureCopy::whole(info))?
            .value();
        let bytes = readback.read()?;
        std::fs::write(format!("{id}.depth32"), &bytes)?;
        Some(json!({"bytes":bytes.len(),"sha256":hash(&bytes),"completion":completed}))
    } else {
        None
    };
    eprintln!("completed {id}");
    Ok(
        json!({"case_id":id,"status":"executed","golden":case.golden,"bytes":bytes.len(),"sha256":hash(&bytes),
        "completion_values":[rendered,copied],"depth":depth_record,"cache_sizes":cache_sizes,
        "draws":case.draws.iter().map(|d| json!({"vertex":d.vs.to_string_lossy(),"fragment":d.fs.map(|s|s.to_string_lossy()),
            "root_bytes":d.root.len(),"root_sha256":hash(&d.root),"count":d.count,"first":d.first,"vertex_offset":d.vertex_offset,
            "state":format!("{:?}",d.info)})).collect::<Vec<_>>()}),
    )
}

fn rejected<T>(
    name: &str,
    result: std::result::Result<T, Error>,
    rows: &mut Vec<Value>,
) -> Result<()> {
    match result {
        Err(Error::Invalid(reason)) => {
            rows.push(json!({"case_id":name,"status":"pass","diagnostic":reason}))
        }
        Err(error) => return Err(format!("{name}: unexpected {error}").into()),
        Ok(_) => return Err(format!("{name}: invalid operation accepted").into()),
    }
    Ok(())
}
fn attach<'a, 'd>(texture: &'a mut Texture<'d>, load: LoadOperation) -> ColorAttachment<'a, 'd> {
    ColorAttachment {
        texture,
        load,
        store: StoreOperation::Store,
        clear: [0.0; 4],
    }
}

#[allow(unsafe_code)]
fn controls(device: &Device, shader: &ReviewedShader) -> Result<Vec<Value>> {
    let mut rows = Vec::new();
    let foreign = Device::new(true)?;
    let mut target = device.texture_with_usage(IMAGE, attachment_usage())?;
    let mut other = foreign.texture_with_usage(IMAGE, attachment_usage())?;
    let mut storage = device.texture(IMAGE)?;
    let mut smaller =
        device.texture_with_usage(TextureInfo { width: 64, ..IMAGE }, attachment_usage())?;
    let mut depth = device.texture_with_usage(
        TextureInfo {
            format: TextureFormat::D32Float,
            ..IMAGE
        },
        TextureUsage {
            attachment: true,
            ..Default::default()
        },
    )?;
    rejected("color_clear_on_depth", depth.clear([0.0; 4]), &mut rows)?;
    rejected("depth_clear_on_color", target.clear_depth(0.5), &mut rows)?;
    rejected(
        "nonfinite_depth_clear",
        depth.clear_depth(f32::NAN),
        &mut rows,
    )?;
    let mut readback = device.buffer(IMAGE.byte_len()?, Memory::Readback)?;
    let clear = depth.clear_depth(0.375)?.value();
    let region = TextureCopy::whole(depth.info());
    let copied = device
        .copy_texture_to_buffer(&mut depth, &mut readback, region)?
        .value();
    let bytes = readback.read()?;
    if bytes.chunks_exact(4).any(|b| b != 0.375_f32.to_ne_bytes()) {
        return Err("D32 clear/readback mismatch".into());
    }
    rows.push(json!({"case_id":"depth_transfer_clear","status":"pass","sha256":hash(&bytes),"completion_values":[clear,copied]}));
    let empty = RenderResources::default();
    // SAFETY: U-021. No draws occur in these controls. Valid calls clear the full
    // area. Invalid owner/load/format/extent values are rejected by reviewed
    // checks before recording. Discarded contents are never submitted as input.
    unsafe {
        rejected(
            "no_attachments",
            device.render(&mut [], None, &empty, &[]),
            &mut rows,
        )?;
        rejected(
            "uninitialized_load",
            device.render(
                &mut [attach(&mut target, LoadOperation::Load)],
                None,
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        rejected(
            "foreign_attachment",
            device.render(
                &mut [attach(&mut other, LoadOperation::Clear)],
                None,
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        rejected(
            "missing_attachment_usage",
            device.render(
                &mut [attach(&mut storage, LoadOperation::Clear)],
                None,
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        rejected(
            "attachment_extent",
            device.render(
                &mut [
                    attach(&mut target, LoadOperation::Clear),
                    attach(&mut smaller, LoadOperation::Clear),
                ],
                None,
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        rejected(
            "color_as_depth",
            device.render(
                &mut [],
                Some(DepthAttachment {
                    texture: &mut target,
                    load: LoadOperation::Clear,
                    store: StoreOperation::Store,
                    clear: 0.5,
                }),
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        rejected(
            "depth_as_color",
            device.render(
                &mut [attach(&mut depth, LoadOperation::Clear)],
                None,
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        rejected(
            "invalid_pass_depth_clear",
            device.render(
                &mut [],
                Some(DepthAttachment {
                    texture: &mut depth,
                    load: LoadOperation::Clear,
                    store: StoreOperation::Store,
                    clear: 1.1,
                }),
                &empty,
                &[],
            ),
            &mut rows,
        )?;
        let mut discarded = attach(&mut target, LoadOperation::Clear);
        discarded.store = StoreOperation::DontCare;
        device.render(&mut [discarded], None, &empty, &[])?;
        rejected(
            "discarded_store_load",
            device.render(
                &mut [attach(&mut target, LoadOperation::Load)],
                None,
                &empty,
                &[],
            ),
            &mut rows,
        )?;
    }
    rejected(
        "discarded_store_readback",
        device.copy_texture_to_buffer(&mut target, &mut readback, TextureCopy::whole(IMAGE)),
        &mut rows,
    )?;
    let spec = DrawSpec::new(c"triangle_vs", c"color3_fs", vec![], 3);
    // SAFETY: U-021. Reviewed resource-free triangle pair, zero root, one RGBA8
    // target. Fixed source vertices0..2, one instance, no arbitrary shader input.
    let pipeline = unsafe {
        device.render_pipeline(
            shader.stage(spec.vs, ShaderStage::Vertex),
            Some(shader.stage(c"color3_fs", ShaderStage::Fragment)),
            spec.info,
            &[],
        )
    }?;
    let mut indices = device.buffer(12, Memory::Upload)?;
    indices.write(&words(&[0, 1, 2]))?;
    let uninitialized = device.buffer(12, Memory::Device)?;
    let mut foreign_indices = foreign.buffer(12, Memory::Upload)?;
    foreign_indices.write(&words(&[0, 1, 2]))?;
    for name in [
        "root_length",
        "viewport_nan",
        "scissor_negative",
        "index_range",
        "index_uninitialized",
        "foreign_index",
    ] {
        let mut draw = RenderDraw {
            pipeline: &pipeline,
            root: &[],
            viewport: Viewport::whole(128, 128),
            scissor: Scissor::whole(128, 128),
            vertices: DrawVertices::Direct { count: 3, first: 0 },
            instances: 1,
            first_instance: 0,
        };
        match name {
            "root_length" => draw.root = &[0; 4],
            "viewport_nan" => draw.viewport.x = f32::NAN,
            "scissor_negative" => draw.scissor.x = -1,
            "index_range" => {
                draw.vertices = DrawVertices::Indexed {
                    buffer: &indices,
                    index_type: IndexType::U32,
                    count: 3,
                    first: 1,
                    vertex_offset: 0,
                }
            }
            "index_uninitialized" => {
                draw.vertices = DrawVertices::Indexed {
                    buffer: &uninitialized,
                    index_type: IndexType::U32,
                    count: 3,
                    first: 0,
                    vertex_offset: 0,
                }
            }
            "foreign_index" => {
                draw.vertices = DrawVertices::Indexed {
                    buffer: &foreign_indices,
                    index_type: IndexType::U32,
                    count: 3,
                    first: 0,
                    vertex_offset: 0,
                }
            }
            _ => unreachable!(),
        }
        // SAFETY: U-021. Reviewed native argument checks reject each changed
        // value before commands. All other fields use the fixed triangle
        // contract. No rejected index range or owner reaches shader execution.
        rejected(
            name,
            unsafe {
                device.render(
                    &mut [attach(&mut target, LoadOperation::Clear)],
                    None,
                    &empty,
                    &[draw],
                )
            },
            &mut rows,
        )?;
    }
    Ok(rows)
}

fn run(shaders: &[ReviewedShader], token: &str) -> Result<Value> {
    let device = Device::new(true)?;
    let controls = controls(&device, &shaders[0])?;
    let cases = cases();
    let mut rows = Vec::new();
    for shader in shaders {
        for case in &cases {
            rows.push(run_case(&device, shader, case)?);
        }
    }
    let info = device.info();
    Ok(
        json!({"schema_version":1,"run_token":token,"status":"executed","required":100,"executed":rows.len(),
        "host_source_sha256":identity(),"cases":rows,"controls":controls,
        "shaders":shaders.iter().map(|s|&s.metadata).collect::<Vec<_>>(),
        "device":{"name":info.name,"api_version":info.api_version,"loader_api_version":info.loader_api_version,
            "driver_version":info.driver_version,"vendor_id":info.vendor_id,"device_id":info.device_id,
            "validation":info.validation,"synchronization_validation":true,"graphics":{
                "dynamic_rendering":info.graphics.dynamic_rendering,"wireframe":info.graphics.wireframe,
                "depth_clamp":info.graphics.depth_clamp,"independent_blend":info.graphics.independent_blend}}}),
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
            return Err("usage: raster --identity | --shader-dir DIR --run-token TOKEN".into());
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
            eprintln!("agfx.raster failed: {error}");
            std::process::ExitCode::FAILURE
        }
    }
}
