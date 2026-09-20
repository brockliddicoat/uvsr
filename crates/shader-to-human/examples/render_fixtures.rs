//! CPU diagnostic renderer for the unchanged source fixture inputs.
//! Raw float results are compared separately with frozen source PNGs.
#![forbid(unsafe_code)]

#[path = "../fixtures/mod.rs"]
mod fixtures;

use shader_to_human::{Mat4, UVec2, Vec3, Vec4};
use std::{
    env,
    error::Error,
    fs,
    io::{BufWriter, Write},
    path::Path,
};

fn camera(path: &str) -> Result<fixtures::Camera, Box<dyn Error>> {
    // origin.xyz, depth_near, then sixteen column-major matrix elements.
    let values = fs::read_to_string(path)?
        .split_ascii_whitespace()
        .map(str::parse::<f32>)
        .collect::<Result<Vec<_>, _>>()?;
    if values.len() != 20 || values.iter().any(|v| !v.is_finite()) {
        return Err("camera requires exactly twenty finite floats".into());
    }
    Ok(fixtures::Camera {
        origin: Vec3::new(values[0], values[1], values[2]),
        depth_near: values[3],
        inverse_view_projection: Mat4::from_cols_array(values[4..].try_into()?),
    })
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = env::args().collect();
    if args.len() < 3 || args.len() > 4 {
        return Err("usage: render_fixtures <GatherTest|ScatterTest|2DTest|TableTest|3DTest> <output.rgba32f> [camera.txt]".into());
    }
    let case = args[1].as_str();
    let camera = if case == "3DTest" {
        Some(camera(
            args.get(3)
                .ok_or("3DTest requires a source-derived camera file")?,
        )?)
    } else {
        None
    };
    let mut pixels = vec![Vec4::ZERO; (fixtures::WIDTH * fixtures::HEIGHT) as usize];
    if case == "ScatterTest" {
        fixtures::scatter(&mut |position, color| {
            // Match defined texture coordinates, never create an invalid index.
            if position.x >= 0
                && position.y >= 0
                && position.x < fixtures::WIDTH as i32
                && position.y < fixtures::HEIGHT as i32
            {
                pixels[(position.y as u32 * fixtures::WIDTH + position.x as u32) as usize] = color;
            }
        });
    } else {
        for y in 0..fixtures::HEIGHT {
            for x in 0..fixtures::WIDTH {
                let pixel = UVec2::new(x, y);
                let mut state = fixtures::UiState::default();
                let value = match case {
                    "GatherTest" => fixtures::gather(pixel, &mut state, Vec4::ZERO, Vec4::ZERO),
                    "2DTest" => fixtures::two_d(pixel),
                    "TableTest" => fixtures::table(pixel, &mut state, Vec4::ZERO),
                    "3DTest" => fixtures::world(pixel, camera.ok_or("missing camera")?, Vec4::ZERO),
                    _ => return Err("unknown fixture case".into()),
                };
                pixels[(y * fixtures::WIDTH + x) as usize] = value;
            }
        }
    }
    if let Some(parent) = Path::new(&args[2]).parent() {
        fs::create_dir_all(parent)?;
    }
    let mut file = BufWriter::new(fs::File::create(&args[2])?);
    for pixel in pixels {
        for channel in pixel.to_array() {
            file.write_all(&channel.to_le_bytes())?;
        }
    }
    file.flush()?;
    println!("{{\"case_id\":\"s2h.{case}.cpu_render\",\"status\":\"rendered\",\"width\":{},\"height\":{},\"parity\":\"not checked\"}}", fixtures::WIDTH, fixtures::HEIGHT);
    Ok(())
}
