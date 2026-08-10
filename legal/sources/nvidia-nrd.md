# NVIDIA NRD

## Record

- Relationship: Dependency Integration and Incorporated Upstream Material
- Status: Current, Disabled by Default
- Confidence: Confirmed
- Upstream: [NVIDIA Real-Time Denoisers](https://github.com/NVIDIA-RTX/NRD)
- Revision: `792eff196afdd350fd9c3f862119017ccb438a0e` (version 4.17.3)
- Governing Terms: NVIDIA RTX SDK License Agreement

UVSR can optionally fetch and link NVIDIA Real Time Denoisers version 4.17.3
at pinned upstream commit
`792eff196afdd350fd9c3f862119017ccb438a0e`.

This software contains source code provided by NVIDIA Corporation.

NRD is not covered by the UVSR license. NVIDIA provides it under the NVIDIA
RTX SDK License Agreement in the upstream repository:

- Source: <https://github.com/NVIDIA-RTX/NRD/tree/792eff196afdd350fd9c3f862119017ccb438a0e>
- License: <https://github.com/NVIDIA-RTX/NRD/blob/792eff196afdd350fd9c3f862119017ccb438a0e/LICENSE.txt>

The dependency is disabled by default. Configuring with `UVSR_WITH_NRD=ON`
also requires `UVSR_ACCEPT_NRD_LICENSE=ON`, which confirms that the builder
reviewed and accepted NVIDIA's terms. This build time acknowledgement does not
alter or replace those terms.

An NRD enabled build copies the exact fetched license to
`bin/licenses/NRD-LICENSE.txt` and the consolidated attribution to
`bin/THIRD_PARTY_NOTICES.md`. A distributor must still provide sufficiently
protective terms, distribute NRD only as an incorporated part of an
application with material additional functionality, preserve every required
notice and attribution, and comply with all other applicable terms. Packaged
notices do not replace those obligations.

## UVSR Relationship

An opted-in build fetches and links NRD and uses its ReBLUR implementation.
UVSR's surrounding integration is first-party, but NRD itself remains NVIDIA
material and is never relicensed under UVSR's project license.

## Evidence

- [Build Configuration](../../CMakeLists.txt)
- [Consolidated Notices](../documentation/THIRD-PARTY-NOTICES.md)
- Commit `f892c17e33c007db69ca10f055bd7e59301b37d0`

## Commercial Clearance

Commercial distribution requires a fresh review of the pinned RTX SDK terms.
The opt-in CMake acknowledgement and copied notice are compliance aids, not a
commercial redistribution determination.
