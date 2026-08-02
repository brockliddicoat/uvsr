# San Miguel

This UVSR scene packages the full high-detail San Miguel 2.1 OBJ from Morgan
McGuire's research-scene archive. The model was created by Guillermo M. Leal
Llaguno. Morgan McGuire, Guedis Cardenas, Michael Mara, and Nicholas Hull
improved the 2017 version with permission from the creator.

## Source and Attribution

The supplied notice permits research and educational use with attribution. UVSR
preserves that notice verbatim in `LICENSE.txt`; it governs this asset package.

Source page: <https://casual-effects.com/data/>

## UVSR Conversion

The pinned Blender 5.1.2 importer converts the complete 1.14 GB OBJ to standard
glTF without decimation, Draco, or texture re-encoding. It preserves 9,963,191
renderable triangles and all 269 used source PNGs. The source contains 9,186
faces with repeated position indices that Blender cannot import, and required
mesh validation removes 8,322 additional invalid polygons before glTF export.
The audit records both operations explicitly.

The material audit proves that all 287 materials reach glTF, including the exact
264 source `map_Kd` material-to-image bindings, 95 alpha masks, and 56 explicit
`N_*` normal maps. The one ambiguous height/normal source map is omitted instead
of being mislabeled as tangent-space data.

UVSR has no transparent or transmissive draw pass. The single constant
half-dissolve material (`material_041`, used by Candle and Glass_B geometry) is
kept visible as opaque. The importer also flattens transmission for `materialn`,
`materialo`, and `material_79` while preserving their base PBR, specular, and IOR
metadata. That fallback keeps another 12 primitives and 63,910 triangles in the
draw stream; it does not preserve transmission. Three authored `map_Ks` textures
remain represented by `KHR_materials_specular`, which Donut currently ignores.

The exported geometry buffer is losslessly repacked by buffer view into five
external buffers. Every packaged file is strictly smaller than GitHub's
100,000,000-byte tracked-file limit. `blender-import-report.json` contains the
topology, image, and material audit; `components/buffer-repack-report.json`
contains the byte-level repack audit.

## Initial Camera

The descriptor uses the official PBRT San Miguel entry view, mapped from PBRT's
Z-up coordinates to glTF's Y-up coordinates. The camera is inside the hacienda
and is independently checked against transformed scene geometry for enclosure,
floor proximity, forward visibility, and collision clearance.
