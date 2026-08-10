# `cgltf`

## Record

- Relationship: Dependency Integration and Indirect Lineage
- Status: Current
- Confidence: Confirmed
- Upstream: [cgltf](https://github.com/jkuhlmann/cgltf)
- Revision: `fa3b80fa762790192c9532b63c441627416ff300`
- Governing Terms: MIT License; copyright Johannes Kuhlmann

## UVSR Relationship

Donut's glTF importer uses cgltf to load the scene packages shipped with UVSR.
UVSR patches surrounding Donut loading behavior, not cgltf itself.

## Evidence

- [Donut Loading Override](../../overrides/donut-loading.patch)
- [Donut Submodule Declarations](../../donut/.gitmodules)
- [Donut Third-Party License Inventory](../../donut/ThirdPartyLicenses.txt) at Donut revision `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`

## Commercial Clearance

The cgltf MIT notice must remain with redistributed substantial portions and in
the product's consolidated dependency notices.
