# Downloaded Scene Runtime Assets

UVSR includes two locally imported scenes from Morgan McGuire's
[Computer Graphics Archive](https://casual-effects.com/g3d/data10/index.html).
CMake stages
their friendly scene descriptors and self-contained GLB components with the
existing Intel PBR Sponza assets.

## Runtime Scenes

| Scene | Triangles | GLB Size | Runtime Scale |
| --- | ---: | ---: | ---: |
| Living Room | 580,637 | 20,398,044 bytes | 1.0 |
| Sponza | 262,267 | 69,939,676 bytes | 0.01 |

Each GLB embeds every texture used by its materials and stays below GitHub's
100 MiB per-file limit. The scene descriptors keep component GLBs out of the
World Scenes picker.

## Conversion Decisions

The source OBJ packages were imported and exported with Blender 5.1.2 by
`tools/import_downloaded_scene.py`. The complete source geometry is retained
without decimation. Each package is merged into one runtime mesh while keeping
its material primitives, and the GLBs use the glTF Y-up convention.

Sponza is scaled by 0.01 from centimeters to meters. Living Room retains its
authored scale.

The source MTL bump maps are height fields rather than tangent-space normal
maps. They are intentionally omitted from glTF normal-texture slots instead of
being mislabeled. Blender's legacy-MTL transmission interpretation is disabled
so opaque Sponza surfaces remain opaque. Sponza's chain, foliage, and masked
fabric use alpha testing with a 0.5 cutoff. The Living Room lampshade is made
opaque because UVSR's renderer instantiates only opaque and alpha-tested scene
geometry.

Exact archive, source OBJ, and generated GLB hashes are recorded in
`downloaded_scene_imports.json`.

## Attribution and Licenses

- **Living Room** is © 2012 Jay and distributed under
  [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/). Benedikt Bitterli
  first used it for rendering research, Nicholas Hull converted it to OBJ, and
  Morgan McGuire corrected its materials.
- **Sponza** is © 2010 Frank Meinl and Crytek and distributed under
  [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/). It is based on
  Marko Dabrovic's original scene. Morgan McGuire corrected the OBJ materials,
  textures, and geometry. The supplied source notice is retained in
  `sponza/SOURCE-NOTICE.txt`.

Models were downloaded from Morgan McGuire's
[Computer Graphics Archive](https://casual-effects.com/g3d/data10/index.html),
July 2017.
