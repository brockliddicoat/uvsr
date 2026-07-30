# Bend Studio Screen-Space Shadows

## Source

These two upstream headers are copied byte-for-byte from Bend Studio's official
`code_final_candidate.zip` release:

- Article: <https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/>
- Archive:
  <https://www.bendstudio.com/assets/cms/downloads/code_final_candidate.zip>
- Retrieved: 2026-07-18
- Archive SHA-256:
  `75707A8E287D485C0F71D04FB0EDE245BB9A7E9569F1492B1C4D1F6AB943DE83`

## Upstream File Hashes

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `upstream/bend_sss_cpu.h` | 12,335 | `23AAE596DBB1B9BDAE23D87AC85079B138426823C3153079F7A7DD36F603D02A` |
| `upstream/bend_sss_gpu.h` | 25,289 | `7FBE24BD2040A62536C31DF6CD38A92CB22C172A9A419EED3C0AF7DF1D50A68C` |

## Preservation

Do not edit, format, or normalize the two files under `upstream/`. UVSR's
renderer and shader adapters live outside this directory. CMake verifies both
hashes during configuration, and scoped `.gitattributes` rules disable line
ending conversion.

## License

The upstream headers are Copyright 2023 Sony Interactive Entertainment and are
distributed under the Apache License 2.0. The full license is included in
`LICENSE.txt`.
