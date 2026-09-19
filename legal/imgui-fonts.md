# Dear ImGui default fonts

## source and relationship

the repository retains two standalone font assets under `assets/fonts`:

- **ProggyClean**, from Dear ImGui v1.92.9b, commit
  `f1cc2ae15e53a861a874c3034aae6798fde194ab`. this is the bitmap-style font
  exposed by `AddFontDefaultBitmap()`.
- **ProggyForever Regular**, from `ocornut/proggyforever` commit
  `f8868ba43bb42c4d88454d03ad81cbd139789f72`. this is the full upstream font.
  ImGui's `AddFontDefaultVector()` embeds an optimized 18,556-byte subset,
  `ProggyForever-Regular-minimal.ttf`, rather than this 20,020-byte file.

v1.92.9b was the latest ImGui release checked on 2026-09-19. the font binaries
are unmodified. ProggyForever's upstream license is copied byte for byte with
only its filename changed. the complete ProggyClean MIT notice is retained
beside its font. the framework does not yet load or package these assets.

## exact files

paths below are relative to `assets/fonts`.

| file | bytes | SHA-256 |
| --- | ---: | --- |
| `proggy-clean/ProggyClean.ttf` | 41208 | `527d2a443ce051f93f7e77b855609722b8cb220a9f104b4aa037be5c90b71324` |
| `proggy-clean/ProggyClean-MIT.txt` | 1082 | `8b802d79f256d29b45ad253323d212fa14ca952a20dcd227cfbcdb3d140bfe7c` |
| `proggy-forever/ProggyForever-Regular.ttf` | 20020 | `56864f00513974a481a9db1a996dc100657fca0faeef76e60aa59c6a0cf5b1b6` |
| `proggy-forever/ProggyForever-MIT.txt` | 1118 | `47d329848d81b5ad777bf5eca73509a4d1b730d6e2ca917f03e72010b2f59576` |

## terms and attribution

ProggyClean is copyright (c) 2004, 2005 Tristan Grimmer. ProggyForever is
copyright (c) 2026 Disco Hello and copyright (c) 2019,2023 Tristan Grimmer.
both use the MIT License. retain the applicable copyright notices and the
complete permission and warranty text with copies or substantial portions.
these terms permit commercial use subject to the notice condition and do not
replace the project's own license or clear other retained assets.

## evidence

- [ProggyClean font](../assets/fonts/proggy-clean/ProggyClean.ttf) and
  [complete notice](../assets/fonts/proggy-clean/ProggyClean-MIT.txt)
- [ProggyForever font](../assets/fonts/proggy-forever/ProggyForever-Regular.ttf) and
  [complete notice](../assets/fonts/proggy-forever/ProggyForever-MIT.txt)
- [pinned upstream ProggyClean](https://github.com/ocornut/imgui/blob/f1cc2ae15e53a861a874c3034aae6798fde194ab/misc/fonts/ProggyClean.ttf)
- [pinned upstream ProggyForever](https://github.com/ocornut/proggyforever/blob/f8868ba43bb42c4d88454d03ad81cbd139789f72/ProggyForever-Regular.ttf)
- [pinned ProggyForever license](https://github.com/ocornut/proggyforever/blob/f8868ba43bb42c4d88454d03ad81cbd139789f72/LICENSE.txt)
- [ImGui font documentation](https://github.com/ocornut/imgui/blob/f1cc2ae15e53a861a874c3034aae6798fde194ab/docs/FONTS.md)
- [upstream subset script](https://github.com/ocornut/proggyforever/blob/f8868ba43bb42c4d88454d03ad81cbd139789f72/build/BuildMinimal-FontForgeScript.txt)
