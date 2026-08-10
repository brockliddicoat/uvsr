# Intel `PresentMon`

## Record

- Relationship: Auxiliary Tool Integration
- Status: Current development and performance tooling; not a shipped UVSR runtime component
- Confidence: Confirmed
- Upstream: [PresentMon](https://github.com/GameTechDev/PresentMon)
- Revision: Version 2.5.1 Windows x64 executable, SHA-256 `9BEC3083069F58F911E6A512F4806DB51A27BD096103087BC1D05EF54C80A191`
- Governing Terms: MIT-style license, Copyright 2017-2024 Intel Corporation, plus the pinned release's third-party notices

## UVSR Relationship

UVSR's performance bootstrap downloads and verifies the Intel-signed PresentMon
executable for frame-presentation measurement in controlled development runs.
It also fetches the exact license and third-party notice files. PresentMon is
not linked into UVSR or copied into the renderer's distribution output.

## Evidence

- [Pinned Download, Hash, Signature, And Notice Logic](../../tools/get_uvsr_performance_tools.ps1)
- [Historical Performance-Tool Plan](../../docs/exec-plans/abandoned/sparse-virtual-shadow-maps.md)
- Tool integration introduced at `ea566bc67f744059e6f62e33c541c5b25bde9bd8`

## Commercial Clearance

The current renderer package does not include PresentMon. Any separate
redistribution of the executable must include Intel's license and the applicable
third-party notices and must not imply Intel endorsement.
