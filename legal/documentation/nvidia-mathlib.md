# NVIDIA MathLib

## Record

- Relationship: Direct Dependency Integration and Incorporated Header Material
- Status: Current, Required by NRD
- Confidence: Confirmed
- Upstream: [NVIDIA MathLib](https://github.com/NVIDIA-RTX/MathLib)
- Revision: commit `974e1387ba936740c7cdc494792d2641bc127e86`
  (upstream tag `v11`)
- Commit-Archive SHA-256:
  `8250a1a903cb9d69234029a226349abf05d23a61a6cb2f1caf8fded8e5bcdea5`
- Governing Terms: MIT License

## UVSR Relationship

UVSR fetches the exact commit archive directly. The commit plus archive hash is
the immutable trust boundary; `v11` is descriptive. Retained NRD C++ and HLSL
include MathLib headers; UVSR does not maintain an alternate or transitive
MathLib owner. The fetched `LICENSE.txt` is 1,085 bytes with SHA-256
`7c5e43e9fe07dd6f50cc6e000df7980640706d6278b4c6e4d3126a31e9ff1c93`.
The production package copies that exact file to
`bin/licenses/NVIDIA-MathLib-MIT.txt`.

- Source: <https://github.com/NVIDIA-RTX/MathLib/tree/974e1387ba936740c7cdc494792d2641bc127e86>
- License: <https://github.com/NVIDIA-RTX/MathLib/blob/974e1387ba936740c7cdc494792d2641bc127e86/LICENSE.txt>

## Evidence

- [Direct NRD and MathLib Fetch](../../cmake/DirectNRD.cmake)
- [Root License Package Mapping](../../CMakeLists.txt)
- [Consolidated Notices](third-party-notices.md)

## Commercial Clearance

The MIT license permits use, modification, and distribution subject to
preserving its copyright and permission notice in all copies or substantial
portions. The package mapping preserves the complete upstream text; it does not
change those terms.
