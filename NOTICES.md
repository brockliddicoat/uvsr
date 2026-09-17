# notices

## inspirations and research references

no third-party implementation has been copied or translated into the new Rust project. these pinned revisions are inspected inspirations, research references, and contribution or consumer baselines. they do not define this project's implementation or compatibility contract. any future import or translation must include its exact license, copyright notices, provenance, and file-level disposition.

| project | inspected revision | role |
| --- | --- | --- |
| [AGFX](https://github.com/AmelieHeinrich/agfx) | `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7` | explicit API, backend structure, GPU-test, FLIP-result, and report-presentation inspiration |
| [rust-gpu](https://github.com/Rust-GPU/rust-gpu) | `e6394e08eb3356083b12f732a01906f8e49f7c4a` | Rust shader compiler and library contribution target |
| [NoGraphicsAPI](https://github.com/sebbbi/NoGraphicsAPI) | `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38` | physical-pointer and native descriptor-heap consumer baseline |
| [ShaderToHuman](https://github.com/electronicarts/ShaderToHuman) | `d6f98b7d67da802053cd9c702082fa741dec42e7` | shader example, golden regression, and documentation-presentation inspiration |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | `a9193e76134de63810ee1a57b5343f236d6ff102` | shader translation feasibility reference |

the repository's [PolyForm Noncommercial license](LICENSE.md) covers first-party material only. it does not replace upstream licenses or grant rights the project does not control.

## retained scene assets

`assets/scenes` retains converted Bistro Interior and San Miguel data. their adjacent licenses, provenance manifests, and generated conversion reports control the exact files:

- [Bistro Interior](assets/scenes/bistro_interior_retextured/README.md) is associated with Amazon Lumberyard Bistro and includes a CC BY 4.0 notice. the exact supplied GLB still has an unresolved chain-of-title boundary recorded in its [legal record](legal/documentation/amazon-lumberyard-bistro.md).
- [San Miguel](assets/scenes/san_miguel_retextured/README.md) is limited by its supplied notice to research and educational use with attribution. its [legal record](legal/documentation/san-miguel-2-1.md) and the separate [camera-data record](legal/documentation/pbrt-v4-scenes-san-miguel-camera.md) preserve the known distribution limits.

retaining these assets does not establish a build, load, rendering, redistribution, or commercial-use result for the new Rust project.
