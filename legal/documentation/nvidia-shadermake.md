# NVIDIA `ShaderMake`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [NVIDIA ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake)
- Revision: `5daebdbef45088fc2369d441391ecab0eba25e54`
- Governing Terms: [MIT License](https://github.com/NVIDIA-RTX/ShaderMake/blob/5daebdbef45088fc2369d441391ecab0eba25e54/LICENSE.txt)

## UVSR Relationship

Donut's shader build invokes ShaderMake to compile UVSR and dependency shaders.
UVSR does not copy ShaderMake implementation code into its renderer, but the
tool is part of the reproducible build chain and fetches DXC when configured.

## Evidence

- [Build Integration](../../CMakeLists.txt)
- [Donut Submodule Declarations](../../donut/.gitmodules) and nested revision `5daebdbef45088fc2369d441391ecab0eba25e54`

## Commercial Clearance

Ordinary UVSR binaries contain ShaderMake output, not ShaderMake itself. Any
redistribution of the tool must include its MIT notice and applicable compiler
notices.
