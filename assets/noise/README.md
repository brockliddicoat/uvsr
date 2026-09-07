# noise assets

UVSR packages 12 deterministic `R8_UNORM` arrays. the
[`manifest.json`](manifest.json) file is authoritative for names, dimensions,
layer counts, byte counts, algorithm identity, seed, and SHA-256 values. it is
3,826 bytes with SHA-256
`c702ac176577a913ae21876c3f86e256fced47fe51b32c1270c3ec8bace2af50`.

## generation identity

- algorithm: `uvsr-spectral-stbn-v1`
- seed: `1431720786`
- source: first party spectral construction based on the published spatial and
  temporal blue noise objective
- layout: row major slices, one layer for spatial arrays and 64 layers for
  spatiotemporal arrays

the retired generator is recoverable as `tools/generate_noise_assets.py` from
commit `e29a41245dbd0e6fd7a819d2341646419ab76e72`. it is not part of the current
build, tools, or package. the retained bytes are first party material under the
root [UVSR license](../../LICENSE.md). NVIDIA's generator, source, and texture
assets are not included. the publication influence is recorded in the
[NVIDIA STBN source record](../../legal/documentation/nvidia-spatiotemporal-blue-noise.md).

## file identities

| file | layers | bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `spatial-white-64x64x1-r8.bin` | 1 | 4,096 | `fe4cb771dcf6631e45d10e416794abc6cb263143eb9b66626651994aa5125de8` |
| `spatial-blue-64x64x1-r8.bin` | 1 | 4,096 | `88d47915ec8a00a1e0e806440e91ee2c20db7aa22794eee8016e9fda30013e46` |
| `spatiotemporal-blue-64x64x64-r8.bin` | 64 | 262,144 | `c637a502c36359aeb0193d718a00ec286b0fe1c8499fb173631fad58f7c6c7fc` |
| `spatial-white-128x128x1-r8.bin` | 1 | 16,384 | `753043935f1cb58d35e4cf651e11397a7b1e18fa99296f627d2597f20ca2cc22` |
| `spatial-blue-128x128x1-r8.bin` | 1 | 16,384 | `f0c18c9d5869eb6d5afabecf7a3969efa41be7378e0429d1ddd8747bbb4d8ff1` |
| `spatiotemporal-blue-128x128x64-r8.bin` | 64 | 1,048,576 | `bed4f4bf7705885db4af2becf8409cf688042d6709cbd8092ce0b316a64b63dc` |
| `spatial-white-256x256x1-r8.bin` | 1 | 65,536 | `e7e03f79f879fed6dac81b0dd4735ffc0c46f8c76ef0d89b4eb9ea4058625932` |
| `spatial-blue-256x256x1-r8.bin` | 1 | 65,536 | `c7eb79c2217d79da5a670bea361ab07a04f450babf7ea043c18557690052c972` |
| `spatiotemporal-blue-256x256x64-r8.bin` | 64 | 4,194,304 | `da89bc55d2b825ee6d27899a45f1678ed99637c1527e5228eea4d4896c3056ae` |
| `spatial-white-512x512x1-r8.bin` | 1 | 262,144 | `3672e6338bdf1ac366387f0db8d336361dac292007b2b14da0c3ae5fba260ab5` |
| `spatial-blue-512x512x1-r8.bin` | 1 | 262,144 | `5d2618fd124f1c73a56d73a7ad45418c08e4b6d032896fd2b1f11b029aa9c4fb` |
| `spatiotemporal-blue-512x512x64-r8.bin` | 64 | 16,777,216 | `c4292f0e2d3d57d49334bdfe665b4aeff8ddf86ca101a6cb5364dbf2fb00a703` |

change these files only with new provenance, manifest identities, package
inventory, and rendered quality evidence.
