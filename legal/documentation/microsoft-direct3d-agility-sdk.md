# Microsoft `Direct3D 12 Agility SDK`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Microsoft.Direct3D.D3D12 1.717.1-preview](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.717.1-preview)
- Revision: `1.717.1-preview`
- Governing Terms: [Microsoft DirectX Prerelease License](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.717.1-preview/License)

## UVSR Relationship

The build downloads the NuGet package and copies `D3D12Core.dll` and
`D3D12SDKLayers.dll` into UVSR's runtime output. These binaries remain Microsoft
software and are not covered by UVSR's project license.

## Evidence

- [Build and Runtime Copy Rules](../../CMakeLists.txt)

## Commercial Clearance

This is a hard clearance issue. The pinned preview terms are time-sensitive and
state that the software may not be shared, published, distributed, leased, or
transferred. Do not distribute the copied DLLs until an applicable Microsoft
deployment grant is confirmed or the package is replaced with a redistributable
version.
