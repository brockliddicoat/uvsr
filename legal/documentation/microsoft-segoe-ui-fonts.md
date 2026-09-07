# Microsoft Segoe UI Fonts

## Record

- Relationship: Dependency Integration
- Status: Current runtime dependency
- Confidence: Confirmed behavior; redistribution permission unconfirmed
- Upstream: Microsoft Windows-installed Segoe UI and Segoe UI Variable fonts
- Revision: The supported files present in `%WINDIR%/Fonts` at runtime
- Governing Terms: Microsoft proprietary font and Windows license terms; no redistribution grant is recorded in UVSR

## UVSR Relationship

UVSR reads Windows-installed `seguisb.ttf` and `segoeuib.ttf` for its single
interface, at 16 pixels before DPI scaling. it never copies or packages those
files. missing or invalid fonts stop startup with a repair message. the launcher
separately embeds Noto Sans.

## Evidence

- [Current Font Packaging Rules](../../CMakeLists.txt)
- The source tree and current packages contain no Segoe font binary or
  Microsoft font license

## Commercial Clearance

current packages contain no Segoe files. using installed fonts does not grant
redistribution rights.
