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

`uvsr-launcher.exe` embeds the unmodified Noto Sans v2.015 Regular and Bold
faces. it validates their size, SHA-256, family, subfamily, and OS/2 weight before
opening the interface, and exposes the complete OFL text through **Notices**.
the renderer uses Windows-installed Segoe UI and does not package Noto Sans.
the retired SemiBold face is preserved with the legacy skins in
`docs/postmortem/archive/legacy-ui-skins.zip`.

| Source File | Size | OS/2 Weight | SHA-256 |
| --- | ---: | ---: | --- |
| `NotoSans-Regular.ttf` | 621572 | 400 | `478C558EA716033CD60C03438F628DFA75694DCF6B5F6D505A2F05FD2B4F3823` |
| `NotoSans-SemiBold.ttf` | 625052 | 600 | `A4E91FD530AC2B4EF5367240144FF37D7D65D66CF76F2E9A2187B93C676F92D0` |
| `NotoSans-Bold.ttf` | 631484 | 700 | `1DF075A380FC7CB898ACF64C1F7B3B4DD780DE3CAA860178BF929DE35817A913` |
| `OFL.txt` | 4396 | N/A | `CEE9892F9F0CC8FE882C9E9537EE6A89621D86EE7CEAF70B02E2B2B1C25C061A` |

## Evidence

- [Bundled Font Assets And License](../../assets/fonts/noto-sans)
- [Launcher Font Validation](../../launcher/native/ui.cpp)
- [Embedded Fonts And Notice](../../launcher/native/launcher.rc)

## Redistribution Conditions

The OFL permits unmodified Noto Sans to be bundled and redistributed with UVSR,
including in commercial software, provided the font is not sold by itself and
every copy retains the copyright notice and complete OFL text. The font remains
under the OFL; UVSR's source license does not replace it. Do not use the Noto
Project Authors' names to imply endorsement.
