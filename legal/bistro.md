# Amazon Lumberyard Bistro

## Record

- relationship: incorporated upstream material and generated derivative
- status: current
- confidence: confirmed for the retained bytes, with an uncertain chain of title for the immediate GLB
- upstream: [Amazon Lumberyard Bistro on NVIDIA ORCA](https://developer.nvidia.com/orca/amazon-lumberyard-bistro) and [McGuire Computer Graphics Archive](https://casual-effects.com/data/)
- revision: Amazon Lumberyard 2017 Bistro, Wine interior variant, with exact ZIP and GLB hashes in the provenance manifest
- governing terms: the supporting upstream package states Creative Commons Attribution 4.0. applicability to the separately supplied Blender-exported GLB requires confirmation

## Relationship

the repository retains a converted, user-supplied `BistroInterior_Wine.glb`.
the conversion losslessly repacks its buffer views and changes five blended
materials to opaque compatibility fallbacks. the GLB is associated with the
McGuire archive entry but is not a member of the cited `Bistro_v5_2.zip`. the
supporting ZIP cannot by itself prove the immediate GLB's license lineage.

the subsequent scene cleanup removes 32 placemats, four unsupported wine glasses,
and their four liquid children. rack-hung and surface-supported glasses remain.
the cleanup changes scene nodes and meshes only, preserving all binary buffers,
materials, textures, and retained transforms. the original attribution and
license notices are unchanged.

## Evidence

- [scene overview](../assets/scenes/bistro_interior_retextured/README.md)
- [source provenance](../assets/scenes/bistro_interior_retextured/source-provenance.json)
- [bundled license](../assets/scenes/bistro_interior_retextured/LICENSE.txt)
- [buffer repack report](../assets/scenes/bistro_interior_retextured/components/buffer-repack-report.json)
- [scene cleanup report](../assets/scenes/bistro_interior_retextured/scene-cleanup-report.json)
- packaging commit `f7c0c87d8cba6880428fbc34400eb2882fb5182e`

## Commercial Clearance

preserve the Amazon attribution, CC BY 4.0 notice, source link, and modification
disclosures. obtain documentary confirmation that the CC BY 4.0 grant covers
the exact hashed GLB, or replace it from a traceable upstream package, before
commercial distribution.
