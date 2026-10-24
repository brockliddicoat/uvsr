# Image-Based Lighting Environments

UVSR ships exactly six imported sky-only Radiance HDR sources from
[Poly Haven](https://polyhaven.com/). Poly Haven publishes these assets under
[CC0](https://polyhaven.com/license), permitting use, modification, and
redistribution without attribution. Attribution and source hashes are retained
here for reproducibility.

The pure-sky variants avoid foreground geometry and hard duplicate sunlight.
UVSR derives diffuse SH9 `E / pi`, a GGX-prefiltered specular cube, the
optional visible background, and therefore both IBL lighting lobes from the
same selected scene-linear radiance field. A source-independent split-sum
environment BRDF LUT completes the specular receiver. The separate directional
light remains the only source of hard, shadowed sunlight.

## Included Sources

| UVSR Choice | Default Exposure | Source | File | Bytes | Published MD5 |
|---|---:|---|---|---:|---|
| Day - Kloppenheim 03 | `-2.75 EV` | [Kloppenheim 03 (Pure Sky)](https://polyhaven.com/a/kloppenheim_03_puresky) | `kloppenheim_03_puresky/kloppenheim_03_puresky_2k.hdr` | 5,324,445 | `06abf490739e537e9339d619a2a3c941` |
| Bright Overcast - Snow Field 2 | `-2.50 EV` | [Snow Field 2 (Pure Sky)](https://polyhaven.com/a/snow_field_2_puresky) | `snow_field_2_puresky/snow_field_2_puresky_2k.hdr` | 4,045,437 | `b89ecaa1ac90078090531b5b8b1dec33` |
| Soft Day - Farm Field | `-3.25 EV` | [Farm Field (Pure Sky)](https://polyhaven.com/a/farm_field_puresky) | `farm_field_puresky/farm_field_puresky_2k.hdr` | 5,033,248 | `bf9103944928c6ae1596e523ce658558` |
| Night - Kloppenheim 07 | `-5.00 EV` | [Kloppenheim 07 (Pure Sky)](https://polyhaven.com/a/kloppenheim_07_puresky) | `kloppenheim_07_puresky/kloppenheim_07_puresky_2k.hdr` | 5,003,360 | `154e49aa2e8b0e62191beb216c88832e` |
| Starry Night - Qwantani | `-6.50 EV` | [Qwantani Night (Pure Sky)](https://polyhaven.com/a/qwantani_night_puresky) | `qwantani_night_puresky/qwantani_night_puresky_2k.hdr` | 5,461,210 | `a2ef5b92f49f77b5bad15add5dad4feb` |
| Legacy - Quadrangle Cloudy | `-3.00 EV` | [Quadrangle Cloudy](https://polyhaven.com/a/quadrangle_cloudy) | `quadrangle_cloudy/quadrangle_cloudy_1k.hdr` | 1,671,356 | `705339eebceee57b9f32e2d44a05f1c7` |

## Source Roles

Kloppenheim 03 is the default balanced day source. Snow Field 2 supplies the
brightest low-contrast overcast option, while Farm Field provides a softer
neutral-warm day. Kloppenheim 07 is the dedicated overcast-night option with
soft moonlit cloud breaks. Qwantani is a distinct clear, high-contrast
starry-night choice with its own lower operating exposure.

The legacy Quadrangle Cloudy source remains available for before-and-after
comparison. UVSR has no procedural environment source and does not normalize
one imported source against another. The **Exposure** control applies one
common multiplier to diffuse IBL, specular IBL, and the matched background.

The calibrated EV values are renderer-relative starting points for UVSR's
current scene-linear lighting scale, not absolute camera-exposure metadata from
Poly Haven. Selecting a source restores its value. The source images remain
unchanged, and users can adjust exposure without regenerating convolution or
prefilter resources.

## Runtime Contract

The **Sky** drawer owns environment selection, common exposure, the diffuse and
specular lobe toggles, independent **Diffuse Strength** and **Specular
Strength**, and **Show Environment Background**. One selection owns the
complete global IBL state:

- The source radiance becomes a persistent 512-by-512 cube.
- A normalized SH9 Lambert projection becomes a 16-by-16 cube storing
  unit-albedo outgoing response `E / pi`.
- A 256-by-256, nine-mip cube stores the matching GGX-prefiltered radiance.
- One persistent 64-by-64 split-sum environment BRDF LUT completes specular
  evaluation.
- **Show Environment Background** samples the same source cube, orientation,
  and exposure behind scene geometry.

**Diffuse IBL** and **Specular IBL** are independent. Disabling a lobe or
setting its strength to `0.00` makes it exactly zero. Each strength ranges from
`0.00` to `2.00` and defaults to the `1.00` radiometric reference. Diffuse
strength also scales the same environment-diffuse contribution supplied as an
SSGI source. Specular strength affects only specular IBL.

Exposure remains one common source multiplier shared by diffuse IBL, specular
IBL, and the background. Lobe strengths are applied after that common scale and
do not change the background. The background toggle does not change lighting.
With a fixed valid selection, imported sources perform no upload or prefilter
work after warmup; exposure, lobe, and strength changes update only scalar
state.

A missing or invalid HDR asset deactivates the selected environment to zero:
there is no diffuse probe, specular probe, visible background, procedural
fallback, or stale previously loaded source. UVSR latches the failed request
instead of retrying synchronous disk I/O and logging the same failure every
frame. Direct lighting and actual SSGI remain independent and available.

The two night selections do not modify the separate directional scene light.
Matching sun or moon direction, color, and intensity remains an explicit scene
lighting decision.

## No-Hidden-Ambient Invariant

Before IBL, UVSR always added a hidden two-color hemispherical ambient term:
`lerp(bottom, top, normal.y * 0.5 + 0.5)`. It illuminated every surface without
any visibility test, masking fully shadowed regions and making indirect light
appear present where none had been computed.

IBL integration removed that term. With both IBL lobes disabled or at zero
strength, the renderer now shows shadowed direct lighting plus actual SSGI;
regions with neither can reach deep black. The direct BSDF and fixed neutral
AgX tonemapper were unchanged. Future UVSR-derived projects must preserve this
invariant: do not restore a constant, hemispherical, procedural, or
missing-asset ambient fallback to make dark regions look filled.

## Current Limits

UVSR currently supports one fixed-orientation infinite environment. It does not
capture local scene probes, blend probe volumes, correct parallax, rotate
imported sources, or stream source changes asynchronously. The checked-in
asset/projection tests validate the six-source catalog, source identity,
orientation, radiance decoding, and scale contracts, while runtime debug views
remain necessary to inspect generated GPU prefilter and BRDF-LUT output.
