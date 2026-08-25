# Bend Studio Screen-Space Shadows

## Record

- Relationship: Incorporated Upstream Material and Adapted Implementation
- Status: Historical; removed from the current renderer
- Confidence: Confirmed
- Upstream: [Bend Studio Article](https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/) and [Official Code Archive](https://www.bendstudio.com/assets/cms/downloads/code_final_candidate.zip)
- Revision: Archive retrieved July 18, 2026, SHA-256 `75707A8E287D485C0F71D04FB0EDE245BB9A7E9569F1492B1C4D1F6AB943DE83`
- Governing Terms: Apache License 2.0; Copyright 2023 Sony Interactive Entertainment

## UVSR Relationship

UVSR historically vendored Bend's two released headers byte-for-byte and kept
its renderer adapter outside the frozen upstream directory. The integration was
substantial executable code reuse, not an independent reimplementation. No Bend
source remains in the current tree.

## Evidence

- Added at `ea566bc67f744059e6f62e33c541c5b25bde9bd8`; inspect the former files with `git show ea566bc:third_party/bend_sss/README.md`
- Temporarily removed at `aed8cdac1a6d3abd6b2ff1951dd6ca7bb6dec83a`, restored at `18236ffb48d475ec85a518b326f3481c838ad7c3`, and finally removed at `f892c17e33c007db69ca10f055bd7e59301b37d0`

## Commercial Clearance

No current distribution obligation arises from removed files. Any restoration
must bring back the full Apache 2.0 notice and license, preserve modification
notices, and revalidate the official archive hashes.
