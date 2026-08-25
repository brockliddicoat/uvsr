# NVIDIA NRD

## Record

- Relationship: Dependency Integration and Incorporated Upstream Material
- Status: Current, Required Renderer Dependency
- Confidence: Confirmed
- Upstream: [NVIDIA Real-Time Denoisers](https://github.com/NVIDIA-RTX/NRD)
- Revision: commit `792eff196afdd350fd9c3f862119017ccb438a0e`
  (4.17.3)
- Commit-Archive SHA-256:
  `ad148d3653e7e4a149af0d1608ec662eeb522144cf34f6a29f9dfd333933baa8`
- Governing Terms: NVIDIA RTX SDK License Agreement

UVSR fetches the exact commit archive above and builds one narrow static NRD
target. The commit plus archive hash is the immutable trust boundary; the
4.17.3 label is descriptive.
The retained methods are REBLUR_DIFFUSE, RELAX_DIFFUSE, and SIGMA_SHADOW; the
build compiles their 34 retained shader tasks with UVSR's direct pinned DXC
pipeline. NVIDIA MathLib v11 is a direct required dependency and has a separate
[source record](nvidia-mathlib.md).

This software contains source code provided by NVIDIA Corporation.

NRD is not covered by UVSR's project license. NVIDIA provides it under the
license at the pinned source:

- Source: <https://github.com/NVIDIA-RTX/NRD/tree/792eff196afdd350fd9c3f862119017ccb438a0e>
- License: <https://github.com/NVIDIA-RTX/NRD/blob/792eff196afdd350fd9c3f862119017ccb438a0e/LICENSE.txt>

The production package copies that exact fetched license to
`bin/licenses/NRD-LICENSE.txt` and installs the consolidated attribution as
`bin/licenses/third-party-notices.md`. Those files do not replace a
distributor's duty to satisfy the RTX SDK agreement, including applicable
application, protective-term, distribution, notice, and attribution rules.

## UVSR Relationship

UVSR owns the selection, build integration, configuration, shader catalog,
blob loading, and renderer adapters around the pinned upstream code. NRD and
its license remain NVIDIA material; direct ownership and first-party patches do
not relicense it under UVSR's terms.

## Evidence

- [Direct NRD Build](../../cmake/DirectNRD.cmake)
- [Root Package Mapping](../../CMakeLists.txt)
- [Consolidated Notices](third-party-notices.md)
- Original integration commit `f892c17e33c007db69ca10f055bd7e59301b37d0`

## Commercial Clearance

Commercial distribution requires a fresh review of the exact pinned RTX SDK
terms. A successful build and packaged notices are compliance aids, not a
commercial redistribution determination.
