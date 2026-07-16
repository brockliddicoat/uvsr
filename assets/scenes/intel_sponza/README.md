# Intel PBR Sponza Runtime Assets

UVSR includes its complete Intel Sponza 2022 runtime bundle in this directory.
A fresh checkout needs no separate scene download, conversion, material repair,
or path setup; CMake stages the bundle automatically.

## Standard Scenes

- **PBR Sponza Decorated** is the default. It composes the flat-roof architecture,
  Intel's curtains, and the roof-trimmed ivy package at their authored identity
  transforms.
- **PBR Sponza Plain** loads the same architecture without curtains or
  ivy.

The architecture and add-ons remain separate GLBs. The `.scene.json`
descriptors compose them at load time, and the scene catalog hides their
component files so these are the only two Intel Sponza picker entries.

## Camera Locations

Both standardized scene descriptors declare
`intel-pbr-sponza-courtyard-simplified-v1` as their default camera metadata, so
staged picker loads and explicit source-tree descriptor loads behave
identically. **PBR Sponza Decorated** and **PBR Sponza Plain** open in
**Freelook** at **Benchmark Position 1**. The **Camera Location** dropdown
retains this sole named location so more locations can be added later and also
includes an always-selectable **Free** entry.

### Benchmark Position 1

**Benchmark Position 1** uses the requested rounded position. Its supplied
direction and right components of `±0.7` are normalized
to `±0.707106769`, preserving an orthonormal camera basis and the intended
45-degree heading without scaling the view matrix. Its stable default and
benchmark ID is
`intel-pbr-sponza-courtyard-simplified-v1`:

```text
Position:     (11.0, 7.7, -2.2)
Direction:    (-0.707106769, 0.0, 0.707106769)
Up:           (0.0, 1.0, 0.0)
Right:        (-0.707106769, 0.0, -0.707106769)
Vertical FOV: 60 degrees
```

The preset uses 1920x1080 reference framing at a 16:9 aspect ratio. Choosing it
recalls its complete pose immediately without changing the current Camera Mode.
After translation or rotation moves the view away from that pose, the dropdown
reports **Free** while Benchmark Position 1 remains available for recall.
Choosing Free explicitly detaches the location name without moving or
reorienting the camera. Both standardized scenes use Benchmark Position 1 on a
fresh load.

Launching with `--benchmark-camera` starts and locks Benchmark Position 1 in
the disabled Camera Location dropdown, enforces the 1920x1080 reference frame,
and selects **Locked** so the benchmark view cannot move or switch modes.
Benchmark records use `intel-pbr-sponza-courtyard-simplified-v1` in their
`camera` field and identify **PBR Sponza Decorated** or **PBR Sponza Plain**
separately in their `scene` field.

## Runtime Components

Every file is self-contained and below GitHub's 100 MB per-file limit:

- `intel_pbr_sponza_flat_roof_part1.glb`: 76,116,224 bytes
- `intel_pbr_sponza_flat_roof_part2.glb`: 76,712,936 bytes
- `intel_pbr_sponza_curtains.glb`: 86,387,872 bytes
- `ivy/intel_pbr_sponza_ivy_part1.glb`: 89,437,540 bytes
- `ivy/intel_pbr_sponza_ivy_part2.glb`: 89,437,452 bytes

## UVSR Runtime Edits

- The pitched roof, dark sawtooth strip, and detached material swatches are
  removed at the gray courtyard trim's exact world-Y `16.0414255217268` top.
- A watertight 5 cm flat roof uses the exterior `brickwall_01` material and
  keeps only the central courtyard opening. True 2 cm, four-segment circular
  fillets round the complete exterior and courtyard top-edge loops.
- All six inner and four corner first-floor pillars use the strengthened
  rounding. The building's four vertical exterior corners use 10 cm,
  four-segment circular fillets instead of chamfers.
- Unreachable roof materials, textures, meshes, accessors, and binary ranges
  are removed from the architecture components.
- Ivy is retained by complete connected component: a leaf or vine survives
  only when its highest transformed vertex is below the new roof seam. This
  removes 12,202 formerly roof-supported components without leaving cut or
  floating pieces. The two balanced ivy GLBs preserve the authored float32
  attributes, embed 1024 px textures, and use 16-bit indexed primitives.
- The main architecture textures are capped at 512 px. Curtains and ivy retain
  textures up to 1024 px. The ivy normal map is renormalized after resizing.

`components/ivy/import-report.json` records source hashes, the exact trim
predicate, retained and removed geometry counts, texture processing, output
hashes, and resource audits.

## Attribution and License

The scene was commissioned by Frank Meinl and sponsored by Anton Kaplanyan.
Curtains and ivy are from Intel's Sponza add-on packages. Publication citation,
contributor credits, and the Creative Commons Attribution 4.0 terms are retained
verbatim in `LICENSE.txt`.

Source: <https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-processing-research/samples.html>
