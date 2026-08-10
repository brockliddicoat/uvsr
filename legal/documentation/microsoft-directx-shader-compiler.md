# Microsoft DirectX Shader Compiler

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler)
- Revision: ShaderMake release selection `v1.9.2602`, dated `2026_02_20`
- Governing Terms: University of Illinois Open Source License plus the upstream `ThirdPartyNotices.txt`

## UVSR Relationship

ShaderMake downloads and invokes DXC to turn HLSL into DXIL. UVSR does not
incorporate compiler source into the renderer. Compiled shader output is not
presented as copied DXC code.

## Evidence

- [Root Build Configuration](../../CMakeLists.txt)
- [ShaderMake Build Configuration](../../donut/ShaderMake/CMakeLists.txt) at revision `5daebdbef45088fc2369d441391ecab0eba25e54`

## Commercial Clearance

The compiler's complete license and third-party notices must accompany any
redistributed compiler bundle. They are not currently mirrored in this legal
directory because the compiler is fetched as a build tool.
