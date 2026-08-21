# Noto Sans Fonts

## Record

- Relationship: Incorporated Upstream Material and Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic)
- Revision: Noto Sans v2.015 release, upstream commit
  `c4a321e123e4d4ff315f57f4e0adf294fe3a95be`
- Release Archive SHA-256: `0C34DF072A3FA7EFBB7CBF34950E1F971A4447CFFE365D3A359E2D4089B958F5`
- Governing Terms: SIL Open Font License 1.1; copyright 2022 The Noto Project Authors

## UVSR Relationship

UVSR checks in the unmodified hinted TrueType Regular, SemiBold, and Bold faces
from Noto Sans v2.015. CMake verifies every source file's exact size and SHA-256
before building, stages the three faces under `media/fonts/NotoSans/`, and
installs the complete OFL text as `licenses/Noto-Sans-OFL-1.1.txt`.

UVSR Launcher embeds the same exact Regular and Bold files for its visible
controls, validates their size, SHA-256, family, subfamily, and OS/2 weight
metadata before opening the interface, and exposes the complete OFL text through
**Notices**. Existing Regular and Bold roles retain their prior sizes and
emphasis; no launcher control silently falls back to a Windows system font.

During the launcher transition, the build also stages byte-identical copies of
those three Noto faces under the historical `media/fonts/System/CodexUI*.ttf`
paths required by UVSR Launcher sequence 9. These filenames are compatibility
aliases, not Segoe UI or a separate `CodexUI` family, and the renderer never
loads them. The current launcher verifies the exact dual inventory and removes
only those aliases before creating its package manifest; a package produced by
sequence 9 may retain both sets and remains a valid schema-1 transition package.

With `Noto Sans` selected, the renderer uses Regular at 13 px for stock/Ogg
controls, SemiBold at 16 px for the Amp body, and Bold at 16 px for Amp headers.
When `Ogg (ProggyClean)` supplies the body, authored Amp headings still use Noto
Sans Bold because ProggyClean has no Bold face. Current builds do not copy
Windows-installed UI fonts and do not substitute another face when a required
Noto Sans asset is missing or altered.

| Source File | Size | OS/2 Weight | SHA-256 |
| --- | ---: | ---: | --- |
| `NotoSans-Regular.ttf` | 621572 | 400 | `478C558EA716033CD60C03438F628DFA75694DCF6B5F6D505A2F05FD2B4F3823` |
| `NotoSans-SemiBold.ttf` | 625052 | 600 | `A4E91FD530AC2B4EF5367240144FF37D7D65D66CF76F2E9A2187B93C676F92D0` |
| `NotoSans-Bold.ttf` | 631484 | 700 | `1DF075A380FC7CB898ACF64C1F7B3B4DD780DE3CAA860178BF929DE35817A913` |
| `OFL.txt` | 4396 | N/A | `CEE9892F9F0CC8FE882C9E9537EE6A89621D86EE7CEAF70B02E2B2B1C25C061A` |

## Evidence

- [Bundled Font Assets And License](../../assets/fonts/noto-sans)
- [Build-Time Font Validation And Packaging](../../CMakeLists.txt)
- [Renderer Font Roles](../../src/uvsr.cpp)

## Redistribution Conditions

The OFL permits unmodified Noto Sans to be bundled and redistributed with UVSR,
including in commercial software, provided the font is not sold by itself and
every copy retains the copyright notice and complete OFL text. The font remains
under the OFL; UVSR's source license does not replace it. Do not use the Noto
Project Authors' names to imply endorsement.
