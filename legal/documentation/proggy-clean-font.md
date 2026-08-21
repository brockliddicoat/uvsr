# `ProggyClean` Font

## Record

- Relationship: Incorporated Upstream Material and Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [Proggy Fonts](https://www.proggyfonts.net/)
- Revision: The ProggyClean copy embedded by Dear ImGui `1.92.2b` at
  `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`, through pinned Donut commit
  `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`
- Governing Terms: MIT License; Copyright (c) 2004, 2005 Tristan Grimmer

## UVSR Relationship

The `Ogg (ProggyClean)` interface-font option uses Dear ImGui's existing
embedded ProggyClean data at 13 pixels for stock/Ogg controls and registers the
same Regular face at 16 pixels for Amp body text. Because ProggyClean has no
Bold face, authored Amp headings retain Noto Sans Bold. UVSR does not stage a
second ProggyClean font binary. The vendored reference font is 41,208 bytes with
SHA-256
`527d2a443ce051f93f7e77b855609722b8cb220a9f104b4aa037be5c90b71324`.

Current build output and newly staged renderer packages carry the exact
copyright and MIT terms as `licenses/ProggyClean-MIT.txt`. Historical schema-1
packages did not have that separate notice and remain valid so the launcher can
recover or update them. If a package records the notice, the launcher requires
its exact bytes.

## Evidence

- [Exact Packaged Notice](../../assets/fonts/proggy-clean/ProggyClean-MIT.txt)
- [Vendored Reference Font](../../donut/thirdparty/imgui/misc/fonts/ProggyClean.ttf)
- [Embedded Font Attribution and Data](../../donut/thirdparty/imgui/imgui_draw.cpp)
- [Dear ImGui Font Documentation](../../donut/thirdparty/imgui/docs/FONTS.md)
- [Package Validation](../../launcher/src/UVSR.Installer/PayloadPackager.cs)

## Redistribution Conditions

Preserve Tristan Grimmer's copyright notice and the complete MIT permission and
warranty terms with every distributed package that uses the embedded font.
The MIT terms permit commercial use and redistribution subject to that notice
condition. They do not alter UVSR's own license or the terms of other bundled
materials.
