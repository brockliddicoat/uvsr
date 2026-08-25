# Microsoft DirectX Shader Compiler

## Record

- Relationship: Direct Build-Tool Integration
- Status: Current, Build Only
- Confidence: Confirmed
- Upstream: [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler)
- Revision: release `v1.9.2602`, asset dated `2026_02_20`
- Archive SHA-256: `a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e`
- `dxc.exe`: version 1.9.2602.17, SHA-256 `b9cff94181248e080804b385da8964b6319fd07760721baa9053a891cf7a727f`
- Governing Terms: University of Illinois Open Source License and upstream `ThirdPartyNotices.txt`

## UVSR Relationship

UVSR fetches and verifies the pinned archive directly. Its CMake shader catalog
invokes `dxc.exe` per task, consumes compiler depfiles, and builds deterministic
owned shader-family blobs for UVSR, Donut, and retained NRD shaders. No
transitive compiler wrapper owns the active build.

DXC is a development tool. The production renderer package contains compiled
DXIL, not DXC executables, libraries, source, or notices. Anyone redistributing
the compiler bundle must include its complete license and third-party notices;
they are retained in the fetched archive rather than mirrored here.

## Evidence

- [Pinned Compiler Fetch](../../CMakeLists.txt)
- [Direct Shader Catalog](../../cmake/DirectXShaderCatalog.cmake)

## Commercial Clearance

Compiled output does not make the compiler part of the renderer package.
Redistributing DXC itself is a separate distribution action requiring the
upstream license and notices.
