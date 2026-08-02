# Bistro Interior

This UVSR scene packages a user-supplied Blender GLB of the Wine variant of the
Amazon Lumberyard Bistro Interior associated with Morgan McGuire's Computer
Graphics Archive. It retains every geometry buffer view, material, embedded
texture, and camera represented by that GLB. The GLB contains no analytic lights.

## Source and Attribution

Amazon Lumberyard created the Bistro scene and released it under the Creative
Commons Attribution 4.0 International license. The original citation and scene
notes are preserved in `SOURCE-README.txt`, and the complete license is preserved
in `LICENSE.txt`.

Archive entry: <https://casual-effects.com/data>

Upstream ORCA page:
<https://developer.nvidia.com/orca/amazon-lumberyard-bistro>

The user-supplied `BistroInterior_Wine.glb` is a separate Blender-exported file,
not a member of the supporting `Bistro_v5_2.zip` upstream archive. Its asset
metadata records UVSR ORM repair version 4. The model, ZIP, license, and source
README identities are recorded independently so their relationship is not
implied by proximity.

## UVSR Packaging

The GLB was rewritten as standard glTF with five external buffers. Every buffer
view was copied byte-for-byte, with only alignment padding added between views.
No geometry or texture data was decoded, simplified, or re-encoded.

UVSR does not submit blended material domains. To keep all meshes visible, the
repacker changes the five intentional BLEND materials (`Water`, `Ice`, `Beer`,
`Red_Wine`, and `White_Wine`) to OPAQUE while preserving their authored RGB,
base alpha, roughness, metallic value, and two-sided state. This opaque fallback
keeps 227 primitives and 109,600 triangles in the draw stream; it does not
preserve liquid transparency. The report records every changed material index
and source value. Each packaged file is strictly smaller than GitHub's
100,000,000-byte tracked-file limit.

`source-provenance.json` records the immutable archive and model hashes.
`components/buffer-repack-report.json` records and hashes every generated
component.

## Initial Camera

The scene descriptor preserves the source GLB camera's direction, up vector, and
approximately 34-degree vertical field of view. Its position is translated
exactly one metre along +X and half a metre along -Z from the embedded pose into
a nearby enclosed portion of the room. UVSR applies that pose after generic
scene framing. The geometry contract verifies floor proximity, collision
clearance, forward visibility, and enclosure on four horizontal sides.
