# Notices

## Inspirations, Translation Sources, and Research References

the first [AGFX slice](tests/parity/primary-slice.md) imports two unchanged binary golden fixtures under `tests/parity/fixtures/agfx`, copyright 2026 Amélie Heinrich, with the complete [MIT notice](legal/licenses/AGFX-MIT.txt). its source manifest records file hashes and Rust mappings. `shaders/rust/compute_multi_dispatch.rs` translates the source HLSL recurrence, and `crates/agfx/src/bin/buffer_copy.rs` and `multi_dispatch.rs` translate the source copy and compute fixture behavior under that same MIT notice. the native Rust owners implement the mapped Vulkan behavior directly. AGFX and ShaderToHuman are inspirations and translation sources with the [source-parity contract](docs/specs/001-theta-prototype/contracts/source-parity.md). future imports and translations must retain exact licenses, copyright notices, provenance, and file-level dispositions.

the [ShaderToHuman translation](tests/parity/shader-to-human.md) includes its packed font, gather/scatter drawing, UI, color and 3D algorithms in `crates/shader-to-human`, copyright 2024-2025 Electronic Arts Inc. the five unchanged golden PNGs under `tests/parity/fixtures/shader-to-human` are original reference inputs. the complete [BSD-3-Clause notice](legal/licenses/ShaderToHuman-BSD-3-Clause.txt) applies to these translations and inputs. the source's intersection attribution to Inigo Quilez is retained. source identities and dispositions are recorded in the mapping and manifest.

AGFX's [MIT license](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/LICENSE) requires retaining its copyright and permission notice in copies or substantial portions. ShaderToHuman's [BSD-3-Clause text](https://github.com/electronicarts/ShaderToHuman/blob/d6f98b7d67da802053cd9c702082fa741dec42e7/LICENSE.txt) requires source and binary notice preservation and prohibits implied endorsement without permission. preserve additional dependency and asset notices where applicable. this document does not relabel translated material as independently authored first-party code.

| project | inspected revision | role |
| --- | --- | --- |
| [AGFX](https://github.com/AmelieHeinrich/agfx) | `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7` | inspiration, close Rust port source, source-parity and report reference |
| [RustGPU](https://github.com/Rust-GPU/rust-gpu) | `e6394e08eb3356083b12f732a01906f8e49f7c4a` | Rust shader compiler and library contribution target |
| [NoGraphicsAPI](https://github.com/sebbbi/NoGraphicsAPI) | `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38` | physical-pointer and native descriptor-heap consumer baseline |
| [ShaderToHuman](https://github.com/electronicarts/ShaderToHuman) | `d6f98b7d67da802053cd9c702082fa741dec42e7` | inspiration and library, regression, documentation, and example source-parity reference |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | `a9193e76134de63810ee1a57b5343f236d6ff102` | shader translation feasibility reference |

the repository's [Polyfrom Noncommercial license](LICENSE.md) covers first-party material only. it does not replace upstream licenses or grant rights the project does not control.

## Rust host dependencies

[Cargo.lock](Cargo.lock) records exact registry versions and checksums. ash 0.38.0+1.3.281 provides Vulkan bindings under MIT OR Apache-2.0. libloading 0.8.9 provides dynamic loading under ISC. serde_json 1.0.151 and sha2 0.10.9 provide records and hashes under MIT OR Apache-2.0. glam 0.33.3 supplies scalar vectors under MIT OR Apache-2.0. libm 0.2.16 supplies no_std math under MIT. the library uses the same exact math versions as the pinned RustGPU shader sysroot. their source/license files remain in their published packages, not vendored here. any binary distribution must preserve the applicable direct and transitive dependency notices. no runtime binary is published by this source checkpoint.

## Compiler Patches

the [compiler prerequisite patches](patches/rustgpu-prerequisites/README.md) include rspirv source context and modifications under its original [Apache-2.0 license](legal/licenses/rspirv-Apache-2.0.txt). their manifest records exact source and patch identities. no separate NOTICE file was present in the pinned rspirv source. these patch contents are excluded from the repository's first-party license. the patch record acknowledges overlapping existing upstream work.

the SPIR-T and RustGPU prerequisite patches retain their upstream MIT OR Apache-2.0 licensing. complete texts are linked from the same patch record. SPIR-T's MIT notice preserves copyright 2019-2024 Embark Studios and 2024 SPIR-T developers. the SPIRV-Headers update is a source-pin reference, not a vendored header copy. no upstream compiler contribution or endorsement is implied.

the SPIRV-Tools wrapper patch retains MIT OR Apache-2.0 licensing, including copyright 2019 Embark Studios. its full license texts are linked from the patch record. native SPIRV-Tools and SPIRV-Headers are pinned submodule references. their source and generated grammar tables are not vendored here. the new generic heap regression is included under the wrapper's contribution terms.

## Retained Scene Assets

`assets/scenes` retains converted Bistro Interior and San Miguel data. their adjacent licenses, provenance manifests, and generated conversion reports control the exact files:

- [Bistro Interior](assets/scenes/bistro_interior_retextured/README.md) is associated with Amazon Lumberyard Bistro and includes a CC BY 4.0 notice. the exact supplied GLB still has an unresolved chain-of-title boundary recorded in its [legal record](legal/bistro.md).
- [San Miguel](assets/scenes/san_miguel_retextured/README.md) is limited by its supplied notice to research and educational use with attribution. its [legal record](legal/san-miguel.md) and the separate [camera-data record](legal/san-miguel-camera.md) preserve the known distribution limits.

retaining these assets does not establish a build, load, rendering, redistribution, or commercial-use result for the new Rust project.

## Fonts

the repository retains standalone ProggyClean and ProggyForever Regular fonts
under the MIT License, with complete notices beside their files. ProggyClean
is copyright (c) 2004, 2005 Tristan Grimmer. ProggyForever is copyright (c) 2026
Disco Hello and copyright (c) 2019,2023 Tristan Grimmer. their
[legal record](legal/imgui-fonts.md) owns source pins, hashes, and redistribution
conditions. these are source assets, not a selected runtime font or packaged
framework feature.

Segoe UI belongs to Microsoft. local copies under `assets/fonts/segoe-ui` are
ignored by Git and are not included in repository distribution. no separate
redistribution license is recorded. the [font legal record](legal/segoe-ui.md)
states the known restrictions. the [legal index](legal/README.md) links all
font and scene records.
