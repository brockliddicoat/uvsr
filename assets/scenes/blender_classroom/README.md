# Classroom Interior

This UVSR scene packages the official Blender Classroom demo by Christophe
Seux. Blender distributes the source under CC0 1.0 Universal. The exact
September 26, 2024 Wayback capture supplied for this import, its original
`classroom.blend`, the packaged license, and every generated component are
identified in `source-provenance.json` and the two conversion reports.

## Source and License

Archived scene ZIP:
<https://web.archive.org/web/20240926142651/https://download.blender.org/demo/test/classroom.zip>

Archived Blender demo page:
<https://web.archive.org/web/20240926105256/https://www.blender.org/download/demo-files/>

The source README is preserved byte-for-byte in `SOURCE-README.txt`. The full
CC0 1.0 Universal legal code is preserved in `LICENSE.txt`. Attribution is not
required by CC0, but the scene remains credited here as “Classroom, by
Christophe Seux.”

## Source-Texture PBR Conversion

The source uses Blender 2.79-era Cycles shader graphs rather than glTF PBR
materials. `tools/export_blender_classroom.py` realizes the linked collection
instances, converts bevel curves to static meshes, and maps the audited legacy
materials to metallic-roughness PBR. The runtime package contains 19
file-backed images copied byte-for-byte from the archive. Image-backed color
materials use a white glTF base-color factor so inactive legacy socket defaults
cannot tint the original images. Images that are not reachable from an active
source color socket are not incorrectly rebound as color textures.

The source `drawing` material intentionally used Blender's generated
2048-by-2048 `checker` UV-test image on eight bulletin-board sheets. That
diagnostic grid is not part of the final package; those sheets now use the same
blank-paper PBR appearance as the source `drawing.004` material. No generated
checker image is packaged.

No generated normal maps are packaged or bound. The legacy Cycles Bump graphs
are audited in `components/blender-export-report.json` but omitted because
height-derived replacements visibly distorted the authored surfaces. Pure
Diffuse and Translucent materials retain a rough response, linked color graphs
use explicit audited approximations, and the lamp glass uses a neutral opaque
fallback instead of invented emission.

UVSR has no blended or transmissive draw pass. The conversion approximates
frosted and lamp glass as opaque and removes the Cycles daylight portal plus
the thin clock cover. After the masked trash-bin material is omitted, the final
package contains only OPAQUE materials. The export omits the exact spawn-side
`dustBin` owner hierarchy: one wire bin and its 13 crumpled-paper children.
That presentation cleanup removes 58,320 triangles. The export retains 545,830
visible triangles from the 607,484-triangle evaluated source, and the report
accounts for every removed triangle.

## GitHub Packaging

The standard glTF package contains 19 external textures and one geometry
buffer. One buffer is sufficient under the 90,000,000-byte cut target, so
creating extra cuts would add no GitHub compatibility. Every tracked file is
strictly below GitHub's 100,000,000-byte file limit. The repack operation copies
all buffer views and images without geometry or texture re-encoding.

## Initial Camera

The descriptor starts from the authored indoor camera and moves it 0.25 metres
forward into the room. It preserves the authored direction, up vector, and
approximately 39.6-degree vertical field of view. The geometry contract checks
floor proximity, collision clearance, forward visibility, and enclosure on four
horizontal sides so the renderer cannot regress to an exterior spawn.
