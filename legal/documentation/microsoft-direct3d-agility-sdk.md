# Microsoft `Direct3D 12 Agility SDK`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Microsoft.Direct3D.D3D12 1.619.5](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.619.5)
- Revision: `1.619.5`; archive size `35,271,083`; SHA-256
  `0e9bcf32aac9a79343ede9b21e4864950ee54577e3d8e19bfcdf002bb4e9bfd6`
- Governing Terms: the package's `LICENSE.txt`, `LICENSE-CODE.txt`, and
  `distributable files.txt`

## UVSR Relationship

First-party CMake fetches the immutable NuGet URL with an exact archive hash,
then checks package ID, version, SDK 619, and required file identities.
Production stages only `D3D12Core.dll` under `bin/D3D12`; developer builds may
stage `D3D12SDKLayers.dll` for diagnostics. `d3dconfig.exe` is never staged.
These files remain Microsoft software and are not covered by UVSR's license.

## Evidence

- [Direct Fetch and Identity Checks](../../cmake/DirectAgilitySDK.cmake)
- [Build and Package Mapping](../../CMakeLists.txt)

## Commercial Clearance

The package's exact distributable list permits object-code distribution of
`D3D12Core.dll`, `d3d12SDKLayers.dll`, and `d3dconfig.exe`. UVSR distributes
only the first. Distribution remains conditional: an application must add
significant primary functionality, limit use to Windows, impose terms that
protect Microsoft at least as much as the package agreement, satisfy its
indemnity obligation, avoid implied Microsoft endorsement, and avoid licensing
that would force disclosure or source distribution of Microsoft's code. The
software may not be offered standalone. Renderer packages include the complete
Microsoft terms, code license, and distributable list as
`bin/licenses/Microsoft-D3D12-Agility-SDK-Terms.txt`,
`bin/licenses/Microsoft-D3D12-Agility-SDK-Code-MIT.txt`, and
`bin/licenses/Microsoft-D3D12-Agility-SDK-Distributable-Files.txt`.
