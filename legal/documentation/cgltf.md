# `cgltf`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [cgltf](https://github.com/jkuhlmann/cgltf)
- Revision: `fa3b80fa762790192c9532b63c441627416ff300`
- Archive SHA-256: `89351d82a140337ac876e018b091f26176fcc8c227479796993ce79be33ed8a3`
- Governing Terms: MIT License; copyright Johannes Kuhlmann
- License SHA-256: `f619925f80ef862497aaf8e8155ef218fa6a2190055129523ca3df9119a9ba95`

## UVSR Relationship

UVSR fetches the immutable upstream archive directly and owns the `cgltf`
interface target. Donut's glTF importer still consumes that target while its
loading slice is replaced; UVSR does not patch cgltf.

## Evidence

- [Direct Pin and Target](../../cmake/DirectThirdParty.cmake)
- [Current Importer](../../donut/src/engine/GltfImporter.cpp)

## Commercial Clearance

The fetched license is installed as `bin/licenses/cgltf-MIT.txt`.
