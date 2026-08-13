# Noise

## Product Contract

The Noise drawer defines one precomputed texture configuration shared by every
effect that exposes stochastic noise sampling. The factory configuration is
**Spatiotemporal Blue**, **128x128**, with **Animate Samples** enabled and
**Accumulate Samples** disabled. Accumulation is global because it changes the
retained lighting result rather than one effect's sampling texture.

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

## Progressive Accumulation

**Accumulate Samples** applies to both Lighting Solution modes. A pixel with no
successful result is always retried. After `n` successful results, its retry
probability is `1 / (n + 1)`. The decision uses the pixel, prior count, and an
unconditional per-frame scheduling serial. That serial is independent of
**Animate Samples**, so disabling authored noise animation cannot permanently
starve a skipped pixel. Scheduling never examines whether the candidate is
bright, dark, or zero. A finite environment miss or black contribution is
successful and remains part of the mean.

Path Tracing makes the retry decision before traversal. A skipped pixel retains
its previous scene-linear mean and count without tracing a new path. Ray
Marching runs a prepare shader before its stochastic screen-space visibility,
Heitz shadow, ray-traced flashlight, and ray-traced sky producers. Each guarded
producer consumes the same attempt mask and returns early for a rejected pixel.
Required raster, deferred, anti-aliasing, and presentation passes still run, so
the mask skips stochastic producer work rather than the entire frame.

After production and anti-aliasing, a matching transactional resolve consumes
the actual scene-linear presentation source. Rejected pixels copy the previous
mean and count exactly. A non-finite attempted candidate is unsuccessful and
also preserves history. Only a valid matching prepare/resolve transaction
advances the epoch and history write index. With accumulation disabled, Ray
Marching bypasses its full-resolution accumulation history, while Path Tracing
attempts every pixel and replaces its history with count one.
That is a continuously refreshed one-sample estimate, so high-variance
environment lighting can look sparse or heavily speckled. Enable accumulation
to converge while the camera, geometry, materials, lights, and environment are
stationary.

Path Tracing's **Firefly Clamp (Biased)** is part of the estimator rather than
the noise schedule. When enabled, it limits each successful contribution before
the persistent mean is updated, so the retained result is intentionally biased.

All retained samples share the renderer's lighting-history epoch. Camera,
geometry, dynamic vertices, instance transforms, materials, lights,
environment, output extent, lighting solution, solver, transport, noise, scene,
or shader changes advance the epoch and discard every accumulated value. Noise
animation alone advances sampling phase without invalidating compatible
history. Full details are in
[Path Tracing Transport](path-tracing-transport.md#progressive-accumulation).

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
progression, central cache sharing, shader binding invalidation, accumulation
retry/count math, prepare-before-producer attempt-mask gating, transactional
commit behavior, epoch invalidation coverage, and staged asset equality.
