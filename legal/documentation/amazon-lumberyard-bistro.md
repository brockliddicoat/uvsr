# Amazon Lumberyard Bistro

## Record

- Relationship: Incorporated Upstream Material and Generated Derivative
- Status: Current
- Confidence: Confirmed for the packaged bytes; uncertain chain of title for the immediate GLB
- Upstream: [Amazon Lumberyard Bistro On NVIDIA ORCA](https://developer.nvidia.com/orca/amazon-lumberyard-bistro) and [McGuire Computer Graphics Archive](https://casual-effects.com/data/)
- Revision: Amazon Lumberyard 2017 Bistro, Wine interior variant; exact ZIP and GLB hashes are in the provenance manifest
- Governing Terms: The supporting upstream package states Creative Commons Attribution 4.0; applicability to the separately supplied Blender-exported GLB requires confirmation

## UVSR Relationship

UVSR packages a user-supplied `BistroInterior_Wine.glb`, losslessly repacks its
buffer views, and changes five blended materials to opaque compatibility
fallbacks. The GLB is associated with the McGuire archive entry but is not a
member of the cited `Bistro_v5_2.zip`; the supporting ZIP cannot by itself prove
the immediate GLB's license lineage.

## Evidence

- [Scene Overview](../../assets/scenes/bistro_interior_retextured/README.md)
- [Source Provenance](../../assets/scenes/bistro_interior_retextured/source-provenance.json)
- [Bundled License](../../assets/scenes/bistro_interior_retextured/LICENSE.txt)
- [Buffer Repack Report](../../assets/scenes/bistro_interior_retextured/components/buffer-repack-report.json)
- Packaging commit `f7c0c87d8cba6880428fbc34400eb2882fb5182e`

## Commercial Clearance

Preserve the Amazon attribution, CC BY 4.0 notice, source link, and modification
disclosures. Obtain documentary confirmation that the CC BY 4.0 grant covers
the exact hashed GLB, or replace it from a traceable upstream package, before
commercial distribution.
