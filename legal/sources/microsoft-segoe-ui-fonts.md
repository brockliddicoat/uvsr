# Microsoft Segoe UI Fonts

## Record

- Relationship: Dependency Integration
- Status: Current Windows builds
- Confidence: Confirmed behavior; redistribution permission unconfirmed
- Upstream: Microsoft Windows-installed Segoe UI Variable, Segoe UI, Segoe UI Semibold, or Segoe UI Bold fonts
- Revision: Whichever supported files are installed in `%WINDIR%/Fonts` at build time
- Governing Terms: Microsoft proprietary font and Windows license terms; no redistribution grant is recorded in UVSR

## UVSR Relationship

On Windows, CMake copies installed Segoe font files into build media under the
names `CodexUI.ttf` and `CodexUI-Semibold.ttf`. These files are not checked into
the repository. Renaming or copying an installed font does not create a new
license or establish redistribution rights.

## Evidence

- [Windows Font Packaging Rules](../../CMakeLists.txt)
- The source tree contains no Segoe font binary or Microsoft font license

## Commercial Clearance

Do not redistribute a packaged Windows build containing these copied font files
until the applicable Microsoft terms are reviewed and permission is confirmed.
Prefer a clearly redistributable bundled font if that permission cannot be
established.
