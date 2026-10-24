# NVIDIA ShaderMake

## record

- relationship: nested upstream source through Donut
- status: current source presence; inactive build, runtime, and package
- confidence: confirmed
- upstream: [NVIDIA ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake/tree/5daebdbef45088fc2369d441391ecab0eba25e54)
- revision: `5daebdbef45088fc2369d441391ecab0eba25e54`
- terms: [adjacent MIT license](../../donut/ShaderMake/LICENSE.txt)
- license identity: 1,108 bytes, SHA-256
  `8F0B12E3A6D4D714BB7993C74270A3806F11AE1F6B6D7301629215C21939E37B`

## UVSR relationship

Donut's `.gitmodules` declares ShaderMake, so a recursive checkout materializes
this exact nested revision. UVSR's current direct DXC build and first party blob
reader do not invoke or link ShaderMake. no ShaderMake executable, source,
license, or tool output is copied into the renderer package.

ShaderMake remains a source distribution obligation while the nested checkout
is present. preserve its adjacent MIT license in any source copy that includes
it. Donut and its nested ShaderMake checkout are retained, so keep this record
and the exact revision even while ShaderMake remains inactive.

## evidence

- [Donut nested submodule declaration](../../donut/.gitmodules)
- [root Donut declaration](../../.gitmodules)
- [current direct shader build](../../CMakeLists.txt)
