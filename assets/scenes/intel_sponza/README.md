# Intel PBR Sponza Runtime Assets

UVSR includes its complete Intel Sponza 2022 runtime bundle in this directory.
A fresh checkout needs no separate scene download, conversion, material repair,
or path setup; CMake stages the bundle automatically.

## Standard Scenes

- **Intel PBR Sponza** is the default. It composes the flat-roof architecture,
  Intel's curtains, and the roof-trimmed ivy package at their authored identity
  transforms.
- **Intel PBR Sponza - Plain** loads the same architecture without curtains or
  ivy.

The architecture and add-ons remain separate GLBs. The `.scene.json`
descriptors compose them at load time, and the scene catalog hides their
component files so these are the only two Intel Sponza picker entries.

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
