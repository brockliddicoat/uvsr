# bundled scenes

UVSR packages exactly two scenes:

- [Bistro Interior](bistro_interior_retextured/README.md), a converted Wine
  variant associated with Amazon Lumberyard Bistro
- [San Miguel](san_miguel_retextured/README.md), the converted full San Miguel
  2.1 model

each scene keeps its source provenance, license, conversion report, loadable
descriptor, and runtime components together. the JSON reports are generated
evidence and remain byte for byte records. tool names inside them describe how
the retained bytes were made; they are not active tool dependencies.

[`CMakeLists.txt`](../../CMakeLists.txt) stages only `.scene.json`, `.gltf`,
`.glb`, `.bin`, and `.png` files from these two directories. the exact package
allowlist is
[`cmake/runtime-asset-map.def`](../../cmake/runtime-asset-map.def).
provenance and legal files are not inferred from directory contents.

the scene assets are protected byte for byte. a replacement or repaired asset
must update its adjacent provenance, generated reports, package inventory, and
legal record in the same change.
