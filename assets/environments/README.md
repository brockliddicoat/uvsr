# image based lighting environments

UVSR packages exactly six unchanged Radiance HDR files from
[Poly Haven](https://polyhaven.com/). Poly Haven publishes them under
[CC0](https://polyhaven.com/license). attribution is voluntary, but the source,
creator, and file identities remain here for reproducibility. the production
package copies this document as `bin/licenses/Poly-Haven-Environments.md`.

## source inventory

the MD5 values are Poly Haven's published file values. the SHA-256 values were
computed from the tracked UVSR files.

| choice | default exposure | source and creators | tracked file | bytes | published MD5 | SHA-256 |
| --- | ---: | --- | --- | ---: | --- | --- |
| day | `-2.75 EV` | [Kloppenheim 03 (Pure Sky)](https://polyhaven.com/a/kloppenheim_03_puresky), Greg Zaal (original), Jarod Guest (sky edits) | `kloppenheim_03_puresky/kloppenheim_03_puresky_2k.hdr` | 5,324,445 | `06abf490739e537e9339d619a2a3c941` | `cf28b37889826b2c9af5059b67110d11fff159d6cef31226ba117034e27c7287` |
| bright Overcast | `-2.50 EV` | [Snow Field 2 (Pure Sky)](https://polyhaven.com/a/snow_field_2_puresky), Sergej Majboroda (original), Jarod Guest (sky edits) | `snow_field_2_puresky/snow_field_2_puresky_2k.hdr` | 4,045,437 | `b89ecaa1ac90078090531b5b8b1dec33` | `9abccc5b1ab71effb5f28e4239eb7bd8066cad8da871b28baa622c74deda858d` |
| soft Day | `-3.25 EV` | [Farm Field (Pure Sky)](https://polyhaven.com/a/farm_field_puresky), Dimitrios Savva (photography), Jarod Guest (processing and sky edits) | `farm_field_puresky/farm_field_puresky_2k.hdr` | 5,033,248 | `bf9103944928c6ae1596e523ce658558` | `51c732607a26aec353a418c4e2a2772257234a055be99b3716aff34a189c88d2` |
| night | `-5.00 EV` | [Kloppenheim 07 (Pure Sky)](https://polyhaven.com/a/kloppenheim_07_puresky), Greg Zaal (original), Jarod Guest (sky edits) | `kloppenheim_07_puresky/kloppenheim_07_puresky_2k.hdr` | 5,003,360 | `154e49aa2e8b0e62191beb216c88832e` | `e06e7f63d48dbe86a5f5a7f987920fa87fd1bfc9de017ce74ce91617b33161c1` |
| starry Night | `-6.50 EV` | [Qwantani Night (Pure Sky)](https://polyhaven.com/a/qwantani_night_puresky), Greg Zaal (photography), Jarod Guest (processing) | `qwantani_night_puresky/qwantani_night_puresky_2k.hdr` | 5,461,210 | `a2ef5b92f49f77b5bad15add5dad4feb` | `d458fe7f20969d89eedbb7ae12d346abf68c86d844235696e7d70baad3e04cf0` |
| cloudy | `-3.00 EV` | [Quadrangle Cloudy](https://polyhaven.com/a/quadrangle_cloudy), Savva Zakharov | `quadrangle_cloudy/quadrangle_cloudy_1k.hdr` | 1,671,356 | `705339eebceee57b9f32e2d44a05f1c7` | `6b1f8153e612ebc6059fa28ec652f29b60df9ff6e7db4a401c653a09fd74c527` |

the exposure values are starting points for UVSR's scene linear scale, not
camera metadata supplied by Poly Haven. the tracked pixels remain unchanged.

## runtime boundary

one selection supplies diffuse IBL, specular IBL, and the optional background.
exposure scales that common source. diffuse and specular enable and strength
controls remain independent. a missing or invalid file produces zero
environment contribution, never a procedural fallback or a stale prior source.
night choices do not alter the separate directional light.

the exact runtime choices and paths live in
[`src/image_based_lighting_sources.h`](../../src/image_based_lighting_sources.h).
the package allowlist is
[`cmake/runtime-asset-map.def`](../../cmake/runtime-asset-map.def).
