# license inventory

## first party terms

- the repository root [project license](../../LICENSE.md) contains UVSR's
  controlling PolyForm Noncommercial License 1.0.0 and required attribution
  notice.
- the [UVSR Contributor License Agreement](UVSR-CONTRIBUTOR-LICENSE-AGREEMENT.md)
  records contribution rights. signing and merge enforcement are inactive, so
  maintainers verify coverage manually. the agreement does not change the
  license for using UVSR.

## incorporated code terms

- [Apache License 2.0](Apache-2.0.txt) is retained for Google Filament lineage
  and other Apache-licensed material.
- [BSD 2-Clause](BSD-2-Clause.txt) is retained for G3D lineage.
- [Microsoft DirectX Graphics Samples MIT](Microsoft-DirectX-Graphics-Samples-MIT.txt)
  covers historical adapted MiniEngine TAA portions.
- [IOLITE AgX MIT](IOLITE-AgX-MIT.txt) covers Benjamin Wrensch's Minimal AgX
  implementation adapted by UVSR.
- [Intel XeGTAO MIT](Intel-XeGTAO-MIT.txt) covers the retained fast-acos
  expression lineage.
- [Andrew Helmer Stochastic Generation MIT](Andrew-Helmer-Stochastic-Generation-MIT.txt)
  covers the historical generated Sobol table lineage.
- [TinyEXR and OpenEXR BSD](TinyEXR-and-OpenEXR-BSD.txt) preserves both
  notices carried by the pinned TinyEXR header.
- [NVIDIA Donut and NVRHI MIT](NVIDIA-Donut-NVRHI-MIT.txt) is the common
  canonical notice from Donut commit
  `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937` and NVRHI commit
  `8e8c36e37558acec333204619b95d9d2fcdc4a79`. while Donut is attached,
  `donut/LICENSE.txt` remains its adjacent upstream notice; this checked-in
  copy remains the package source while Donut is retained.
- [Microsoft DirectX-Headers MIT](Microsoft-DirectX-Headers-MIT.txt) is the
  canonical notice from commit
  `ee479f0bd5f7b884f202bcf0c3f076cc050dd256`, retained independently of its
  configure-time materialization.

additional dependency and asset license files remain adjacent to their
upstream source or asset, including `donut/LICENSE.txt`, direct fetched
dependency licenses, and license files under `assets/`. the [documentation registry](../documentation/README.md)
maps each substantial source to its controlling terms.

## interpretation

the presence of a license text here does not put every repository file under
that license. follow file headers, source records, adjacent notices, and the
original licensor's terms. when terms conflict or provenance is incomplete,
the narrower verified permission controls until the issue is cleared.
