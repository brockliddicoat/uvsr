# NVIDIA `NVRHI`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [NVIDIA NVRHI](https://github.com/NVIDIA-RTX/NVRHI)
- Revision: `8e8c36e37558acec333204619b95d9d2fcdc4a79`, nested under Donut
- Governing Terms: [MIT License](https://github.com/NVIDIA-RTX/NVRHI/blob/8e8c36e37558acec333204619b95d9d2fcdc4a79/LICENSE.txt)

## UVSR Relationship

NVRHI is the graphics abstraction used directly by UVSR for DirectX 12
resources, pipelines, command lists, synchronization, and ray tracing. UVSR
links it through Donut and does not maintain an NVRHI fork.

## Evidence

- [Build Integration](../../CMakeLists.txt)
- [Application Source](../../src/uvsr.cpp)
- [Donut Submodule Declarations](../../donut/.gitmodules) and nested revision `8e8c36e37558acec333204619b95d9d2fcdc4a79`

## Commercial Clearance

The MIT notice must accompany redistributed substantial portions. NVRHI's
fetched platform headers have their own records and terms.
