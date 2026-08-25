# NVIDIA `NVRHI`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [NVIDIA NVRHI](https://github.com/NVIDIA-RTX/NVRHI)
- Revision: `8e8c36e37558acec333204619b95d9d2fcdc4a79`
- Governing Terms: [MIT License](https://github.com/NVIDIA-RTX/NVRHI/blob/8e8c36e37558acec333204619b95d9d2fcdc4a79/LICENSE.txt)

## UVSR Relationship

NVRHI is the graphics abstraction used directly by UVSR for DirectX 12
resources, pipelines, command lists, synchronization, and ray tracing. The
immutable Git submodule at `third_party/nvrhi` supplies the active `nvrhi` and
`nvrhi_d3d12` targets. UVSR stages reviewed D3D12 overrides outside that
submodule; it does not edit or fork NVRHI. This completed direct-ownership slice
does not imply complete Donut detachment.

## Evidence

- [Direct Build Integration](../../cmake/DirectDonut.cmake)
- [Pinned Submodule](../../.gitmodules)
- [Application Source](../../src/uvsr.cpp)
- [Packaged License Source](../../third_party/nvrhi/LICENSE.txt)

## Commercial Clearance

The MIT notice must accompany redistributed substantial portions. NVRHI's
fetched platform headers have their own records and terms. Renderer packages
install the exact notice as `bin/licenses/NVIDIA-NVRHI-MIT.txt`.
