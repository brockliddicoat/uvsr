# `LibreHardwareMonitor`

## Record

- Relationship: Auxiliary Tool Integration
- Status: Current development and thermal-monitoring tooling; not a shipped UVSR runtime component
- Confidence: Confirmed
- Upstream: [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)
- Revision: Version 0.9.6 archive, SHA-256 `086D9F1B5A99E643EDC2CFAAAC16051685B551E4C5AC0B32A57C58C0E529C001`
- Governing Terms: Mozilla Public License 2.0 plus the pinned release's third-party notices

## UVSR Relationship

UVSR's performance bootstrap downloads and verifies LibreHardwareMonitor for
development-time sensor and thermal evidence. It fetches the pinned license and
third-party notices alongside the archive. No LibreHardwareMonitor source or
binary is linked into or distributed with the renderer.

## Evidence

- [Pinned Download, Hash, And Notice Logic](../../tools/get_uvsr_performance_tools.ps1)
- Tool integration introduced at `ea566bc67f744059e6f62e33c541c5b25bde9bd8`

## Commercial Clearance

The current renderer package does not include this tool. Redistributing it
separately requires MPL 2.0 compliance for covered files, access to the covered
source in the required form, preserved notices, and all applicable third-party
terms.
