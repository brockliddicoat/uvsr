# Noise

## Product Contract

The Noise drawer defines one precomputed texture configuration shared by every
effect that exposes stochastic noise sampling. The factory configuration is
**Spatiotemporal Blue**, **128x128**, with **Animate Samples** enabled.

Ambient occlusion and diffuse illumination share one screen-space visibility
dispatch and therefore share one effect override. Ray traced directional
shadows and Ray Traced Sky Visibility each have their own override. Finite
flashlight shadows also consume the global noise directly and maintain a
separate animated sample phase. An effect with **Specify Noise** inherits the
Noise drawer until that option is enabled. Its hidden custom values are retained
while inheritance is active and do not change another effect.

The override tooltip states the isolation rule directly:

> Use custom noise sampling for this effect only. This does not change the
> noise sampling used by any other effect.

## Patterns

- **Spatial White** is a deterministic R8 rank permutation.
- **Spatial Blue** is a deterministic toroidal, spectrally shaped R8 rank
  field with suppressed low spatial frequencies.
- **Spatiotemporal Blue** contains 64 R8 layers. Every XY layer preserves
  spatial blue-noise structure, while a fixed XY address advances through a
  temporally blue 64-sample sequence.

Spatial patterns use one array layer. Spatiotemporal Blue uses 64 layers and
advances one layer after each successful animated dispatch. Each effect and
semantic sample dimension uses a fixed independent stream offset.

## Resolution and Centering

Noise Resolution offers **64x64**, **128x128**, **256x256**, and **512x512**.
All patterns are precomputed at all four sizes. Sampling uses point-loaded
`R8_UNORM` texture arrays without mips.

Tiling is centered in the effect's local dispatch rectangle, not anchored to
the absolute viewport origin. For a power-of-two tile size `N`, each axis uses:

```text
tile = (localPixel - floor(dispatchExtent / 2) + N / 2) mod N
```

This makes clipped rectangles at opposite screen edges use the same centered
mapping. Spatial modes translate the complete tile with a deterministic Weyl
offset when animation is enabled. Spatiotemporal Blue keeps its XY address
fixed and advances only the array layer. R8 values are decoded to bin centers,
so random scalars never equal exactly zero or one.

## Assets and Memory

The 12 checked-in files live under `assets/noise/` and are staged to
`media/uvsr/noise/`. `manifest.json` records dimensions, format, byte length,
algorithm revision, seed, and SHA-256 for every file. The runtime library loads
only an exact requested pattern-resolution pair and shares that texture among
all matching consumers.

The default 128x128x64 texture is exactly 1 MiB. All four Spatiotemporal Blue
volumes total 21.25 MiB; both spatial families total about 0.66 MiB. These are
source assets, not configure-time or runtime generated data.

## Provenance

The feature follows the spatial/temporal objective described in NVIDIA's
[Spatiotemporal Blue Noise paper](https://arxiv.org/abs/2112.09629) and
[rendering guidance](https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-1/).
UVSR does not copy NVIDIA's generator or packaged textures. The checked-in
volumes are produced by UVSR's deterministic first-party
`tools/generate_noise_assets.py` spectral construction from the published
objective. This avoids redistributing files from the
[NVIDIA-RTX/STBN repository](https://github.com/NVIDIA-RTX/STBN), whose bundled
license does not establish a general commercial redistribution grant for the
STBN work.

## Validation

Automated contracts verify settings inheritance, hidden override isolation,
centered odd/even dispatch coordinates, phase wrap, R8 open-bin decoding,
asset dimensions and hashes, spatial low-frequency suppression, temporal
progression, central cache sharing, shader binding invalidation, and staged
asset equality.
