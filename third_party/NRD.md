# NVIDIA NRD

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
