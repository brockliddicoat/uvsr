# Wicked Engine architectural reference

## record

- relationship: pinned read only architectural reference
- status: current reference; not a dependency or source adaptation
- confidence: confirmed
- upstream: [Wicked Engine at `ad283cdf10ac4989078c77fc8b02a6d8daec6699`](https://github.com/turanszkij/WickedEngine/tree/ad283cdf10ac4989078c77fc8b02a6d8daec6699)
- revision: `ad283cdf10ac4989078c77fc8b02a6d8daec6699`
- terms: [upstream MIT license](https://github.com/turanszkij/WickedEngine/blob/ad283cdf10ac4989078c77fc8b02a6d8daec6699/LICENSE)

## UVSR relationship

the pinned tree is evidence for explicit high level render path orchestration,
CPU visibility and GPU preparation, concrete pass ownership, indexed GPU scene
data, meshlets, and shared CPU and shader contracts. relevant reference points
are `wiRenderPath3D`, `wiRenderer`, `wiScene_Components`,
`ShaderInterop_Renderer`, and `globals.hlsli`.

UVSR may study these boundaries while building its own focused DX12 renderer.
it does not adopt Wicked Engine's ECS, editor, render path inheritance, global
renderer state, cross platform layer, scripting, job system, or inactive
feature breadth. no Wicked Engine code, data, binary, or package dependency is
present.

## evidence

- [UVSR architectural constraint](../../AGENTS.md)
- original reference wording introduced by commit
  `b9287b04874cbdf0a3b805a37a6952104df77c29`

if recognizable upstream code or data is later incorporated, record the exact
files and preserve the MIT notice. architectural study alone does not make the
upstream source package content.
