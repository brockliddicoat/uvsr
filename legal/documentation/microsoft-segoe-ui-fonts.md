# Microsoft Segoe UI Fonts

## Record

- Relationship: Dependency Integration
- Status: Current optional runtime dependency
- Confidence: Confirmed behavior; redistribution permission unconfirmed
- Upstream: Microsoft Windows-installed Segoe UI and Segoe UI Variable fonts
- Revision: The supported files present in `%WINDIR%/Fonts` at runtime
- Governing Terms: Microsoft proprietary font and Windows license terms; no redistribution grant is recorded in UVSR

## UVSR Relationship

Current builds never copy or package Windows-installed UI fonts. Noto Sans is
the deterministic default documented in [Noto Sans Fonts](noto-sans-fonts.md).
When a user selects `Codex (Segoe UI)`, UVSR reads the installed Regular,
SemiBold, and Bold Segoe UI faces at runtime and leaves those files in the
Windows Fonts directory. If the required system faces are unavailable, UVSR
disables that optional choice and keeps Noto Sans available.

## Evidence

- [Current Font Packaging Rules](../../CMakeLists.txt)
- The source tree and current packages contain no Segoe font binary or
  Microsoft font license

## Commercial Clearance

Current packages contain no Segoe files. The optional runtime selector does not
grant or require redistribution rights.
