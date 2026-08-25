# `ProggyClean` Font

## Record

- Relationship: Incorporated Upstream Material and Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Proggy Fonts](https://github.com/bluescan/proggyfonts)
- Revision: The ProggyClean copy embedded by direct Dear ImGui commit
  `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`
- Governing Terms: MIT License; Copyright (c) 2004, 2005 Tristan Grimmer

## UVSR Relationship

The `Ogg (ProggyClean)` interface-font option uses Dear ImGui's existing
embedded ProggyClean data at 13 pixels for stock/Ogg controls and registers the
same Regular face at 16 pixels for Amp body text. Because ProggyClean has no
Bold face, authored Amp headings retain Noto Sans Bold. UVSR does not stage a
standalone ProggyClean font. The pinned upstream TTF represented by the embedded
payload is 41,208 bytes with SHA-256
`527d2a443ce051f93f7e77b855609722b8cb220a9f104b4aa037be5c90b71324`;
it is a provenance reference, not a vendored or packaged file.

The package input is the complete copyright and MIT notice at
`assets/fonts/proggy-clean/ProggyClean-MIT.txt`. Strict renderer packages install
those exact bytes as `bin/licenses/ProggyClean-MIT.txt`, and package validation
requires them.

## Evidence

- [Exact Packaged Notice](../../assets/fonts/proggy-clean/ProggyClean-MIT.txt)
- [Embedded Font Attribution and Data](../../third_party/imgui/imgui_draw.cpp)
- [Pinned Upstream Reference TTF](https://github.com/ocornut/imgui/blob/45acd5e0e82f4c954432533ae9985ff0e1aad6d5/misc/fonts/ProggyClean.ttf)
- [Package Validation](../../launcher/src/UVSR.Installer/PayloadPackager.cs)

## Redistribution Conditions

Preserve Tristan Grimmer's copyright notice and the complete MIT permission and
warranty terms with every distributed package that uses the embedded font.
The MIT terms permit commercial use and redistribution subject to that notice
condition. They do not alter UVSR's own license or the terms of other bundled
materials.
