# Blender Classroom

## Record

- Relationship: Incorporated Upstream Material and Generated Derivative
- Status: Current
- Confidence: Confirmed
- Upstream: [Archived Blender Demo Files Page](https://web.archive.org/web/20240926105256/https://www.blender.org/download/demo-files/) and [Archived Classroom ZIP](https://web.archive.org/web/20240926142651/https://download.blender.org/demo/test/classroom.zip)
- Revision: September 26, 2024 Wayback capture of Christophe Seux's Classroom; exact archive and blend-file hashes are recorded locally
- Governing Terms: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)

## UVSR Relationship

UVSR converts the Blender scene to static glTF, realizes instances, maps legacy
Cycles materials to metallic-roughness PBR, copies 19 source images, removes
documented geometry, and substitutes opaque approximations where the renderer
cannot reproduce the original material domain. It does not package the
source's generated checker image or locally generated normal maps.

## Evidence

- [Scene Overview](../../assets/scenes/blender_classroom/README.md)
- [Source Provenance](../../assets/scenes/blender_classroom/source-provenance.json)
- [CC0 Legal Code](../../assets/scenes/blender_classroom/LICENSE.txt)
- [Export Report](../../assets/scenes/blender_classroom/components/blender-export-report.json)
- Packaging commit `f7c0c87d8cba6880428fbc34400eb2882fb5182e`

## Commercial Clearance

CC0 permits commercial reuse without attribution, but retaining creator,
source, conversion, and archive identities is recommended for provenance. Do
not imply endorsement by Christophe Seux or Blender.
