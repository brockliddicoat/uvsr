# `HWiNFO`

## Record

- Relationship: Auxiliary Tool Integration
- Status: Optional current development tooling; user-supplied and never downloaded automatically
- Confidence: Confirmed
- Upstream: [HWiNFO Download Page](https://www.hwinfo.com/download/)
- Revision: HWiNFO 8.50-6020 portable archive, SHA-256 `90F4F896B1CEF5D211E3F7721DE8ADA901245159274790840CA7072942271B45`
- Governing Terms: REALiX proprietary terms; UVSR records that commercial use is licensed separately and redistribution requires approval

## UVSR Relationship

The bootstrap can verify and extract an archive that the user has already
downloaded, including REALiX signatures and exact executable hashes. It does
not download or execute HWiNFO. HWiNFO can temporarily load a kernel driver when
the user runs it and is not part of UVSR's runtime package.

## Evidence

- [Optional Verification Logic And Restrictions](../../tools/get_uvsr_performance_tools.ps1)
- Verification support introduced at `ea566bc67f744059e6f62e33c541c5b25bde9bd8`

## Commercial Clearance

Do not redistribute HWiNFO with UVSR and do not use it commercially without the
license required by REALiX. Keep the integration opt-in and user-supplied unless
separate written permission and deployment terms are obtained.
