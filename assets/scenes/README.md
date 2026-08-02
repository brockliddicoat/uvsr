# Bundled Scenes

UVSR stages five ready-to-run scene descriptors:

- **Sponza Decorated** and **Sponza Plain** package Intel's PBR Sponza
  variants.
- **Bistro Interior** packages the Wine variant of Amazon Lumberyard
  Bistro Interior.
- **San Miguel** packages the full high-detail San Miguel 2.1 model.
- **Classroom Interior** packages Christophe Seux's official Blender Classroom
  demo under CC0 1.0 at
  [`blender_classroom/blender_classroom.scene.json`](blender_classroom/blender_classroom.scene.json).

Each scene directory contains its descriptor, attribution, source provenance,
and loadable components. CMake uses an explicit directory allowlist so source
downloads and working files cannot enter the runtime package accidentally.

## Large-File Packaging

The Bistro and San Miguel geometry uses ordinary glTF external buffers divided
at buffer-view boundaries. Blender Classroom uses one external buffer because
no cut is needed under the 90 MB cutting threshold. These are lossless resource
layouts: UVSR loads the files directly without reconstruction, and every
tracked file is strictly below GitHub's 100,000,000-byte limit. Per-scene
reports record the size and SHA-256 of every generated component. Because UVSR
has no blended or transmissive draw pass, the import reports also record the
small, explicit material-domain fallbacks used to keep otherwise skipped meshes
visible as opaque geometry.

## Blender Classroom Conversion

Blender Classroom comes from the exact 2024-09-26 Wayback capture of Blender's
[`classroom.zip`](https://web.archive.org/web/20240926142651/https://download.blender.org/demo/test/classroom.zip).
The static frame-1 export realizes the original curve objects and linked
collection instances, producing 545,830 visible triangles and 19 external
textures copied byte-for-byte from the archive. The exact spawn-side `dustBin`
owner hierarchy, comprising one wire bin and 13 crumpled-paper children, is
omitted as a 58,320-triangle presentation cleanup.

The source's legacy Cycles materials are mapped to UVSR's PBR path without
custom generated normal maps. Source bump networks are audited but omitted
because height-derived normals visibly distorted the original surfaces.
Image-backed colors use their original bytes with a white texture multiplier;
linked color networks use explicit audited approximations. The import removes
blended and transmissive material domains that UVSR cannot draw. It also
suppresses Blender's generated diagnostic UV checker: all eight affected
`drawing` sheets use the same blank-paper appearance as `drawing.004` instead.
The provenance and conversion reports preserve the source, policy, and hashes.

## Camera Placement

The Bistro Interior, San Miguel, and Classroom Interior descriptors each supply
a source-backed indoor camera. UVSR applies descriptor cameras after generic
framing, and automated geometry contracts verify that the poses are inside the
scene with nearby floors, enclosing geometry, forward visibility, and collision
clearance.
