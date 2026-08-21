# Microsoft Segoe UI Fonts

## Record

- Relationship: Dependency Integration
- Status: Current optional runtime dependency and historical packaged dependency
- Confidence: Confirmed behavior; redistribution permission unconfirmed
- Upstream: Microsoft Windows-installed Segoe UI and Segoe UI Variable fonts
- Revision: The supported files present in `%WINDIR%/Fonts` at runtime
- Governing Terms: Microsoft proprietary font and Windows license terms; no redistribution grant is recorded in UVSR

## UVSR Relationship

Earlier Windows builds copied installed Segoe UI faces into renderer media under
the internal `CodexUI` names. Those files were not checked into the repository,
and renaming or copying an installed font did not establish redistribution
rights.

Current builds never copy or package Windows-installed UI fonts. Noto Sans is
the deterministic default documented in [Noto Sans Fonts](noto-sans-fonts.md).
When a user selects `Codex (Segoe UI)`, UVSR reads the installed Regular,
SemiBold, and Bold Segoe UI faces at runtime and leaves those files in the
Windows Fonts directory. If the required system faces are unavailable, UVSR
disables that optional choice and keeps Noto Sans available. This record also
remains relevant to previously built packages, which the launcher continues to
validate without altering their schema-1 manifests.

The current transition build temporarily emits files under the historical
`media/fonts/System/CodexUI*.ttf` paths for Launcher sequence 9 compatibility.
Those aliases are byte-identical copies of the checked-in Noto Sans faces; they
contain no Segoe UI or other Windows-installed font data. The current launcher
removes the aliases from its newly staged packages.

## Evidence

- Repository history before the Noto Sans v2.015 font migration
- [Current Font Packaging Rules](../../CMakeLists.txt)
- The source tree, transition aliases, and new packages contain no Segoe font
  binary or Microsoft font license

## Commercial Clearance

Do not redistribute a historical package containing copied Windows font files
until the applicable Microsoft terms are reviewed and permission is confirmed.
Current packages do not contain those files; the optional runtime selector does
not grant or require redistribution rights.
