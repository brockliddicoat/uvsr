# Noise

## Product Contract

The Noise drawer defines one precomputed texture configuration shared by every
effect that exposes stochastic noise sampling. The factory configuration is
**Spatiotemporal Blue**, **128x128**, with **Animate Samples** enabled and
**Accumulate Samples** disabled. Accumulate Samples is the final collapsible
section in the drawer. It starts expanded on each launch; collapsing it can
still hide both its top-level **Enable** toggle and every enabled-only option.
It renders no gray explanatory text. Accumulation is global because it changes
the retained lighting result rather than one effect's sampling texture.

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

**Accumulate Samples** applies to both Lighting Solution modes. Enabling it
exposes three named starting profiles. **Variance Guided** is the factory preset. It first
takes 16 successful samples everywhere, then converts the largest per-channel
relative standard error into a deterministic revisit interval. Its two-percent
target and 1/16 minimum update rate guarantee that even a low-variance pixel is
revisited at least once every 16 scheduling cycles. **Progressive Mean** samples
every eligible pixel and updates an unbounded cumulative scene-linear RGB mean.
**Responsive Mean** also samples every eligible pixel, but uses a 32-sample
exponential history that reacts faster at the cost of a persistent noise floor.

All accumulation controls are exposed whenever accumulation is enabled:
averaging, scheduling, effective history, warmup samples, target error, and
minimum update rate. Editing any field retains the selected profile as its
origin and displays `<Profile> (Custom)`. Reselecting that named profile
reapplies its complete vector. The per-field reset restores the origin
profile's value, while the mode reset restores factory Variance Guided.

**History Preset** provides transparent shortcuts for Effective History:
**Quick Preview** is 8 samples, **Responsive** is 32, **Balanced** is 64,
**Stable** is 256, and **Very Stable** is 1024. Higher values suppress more
noise in Exponential Mean but react more slowly; Cumulative Mean remains an
unbounded mean, so this slider does not change it. Selecting a shortcut changes
only the visible Effective History value and may mark the outer profile
`(Custom)`.

**Adaptive Workload** provides four transparent Variance Guided recipes.
**Full Quality** begins easing after 32 samples, targets one-percent relative
error, and keeps at least one-quarter of pixels active. **Balanced** uses 16,
two percent, and 1/16. **Performance** uses 8, four percent, and 1/32.
**Maximum Savings** uses 4, eight percent, and 1/64. The three sliders remain
editable after selection, and any nonmatching vector displays Custom.

**Warmup Samples** controls when adaptive work may begin to ease. **Target
Error** maps estimated RGB uncertainty to an update rate, and **Minimum Update
Rate** bounds the longest revisit cycle. These controls reduce eligible ray
attempts; required raster, deferred, resolve, and presentation work remains.

The old harmonic `1 / (n + 1)` retry schedule is not used. It produced only
about the square root of one sample per eligible frame and could leave many
pixels visibly incomplete. Variance Guided uses a stable per-pixel hashed phase
and a bounded integer interval instead of independent Bernoulli retries, so its
minimum update rate is a real revisit guarantee rather than an average. Each
success advances the pixel's scheduling congruence by an odd stride. Adaptive
updates therefore cover every phase of UVSR's power-of-two projection-jitter
sequences instead of aliasing to one repeated subpixel location.

Every accepted stationary stochastic sample uses that pixel's
successful-sample count as its sequence phase. A skipped frame does not consume
a phase, and stationary accumulation continues to obtain new samples even when
**Animate Samples** is off. When physical camera motion resets the mean,
**Animate Samples** instead selects the live frame phase so the noise pattern
visibly moves with the camera; disabling it deliberately retains phase zero
during those reset frames. Scheduling never classifies a candidate by
brightness. A finite environment miss or black contribution is successful and
remains part of the mean.

Path Tracing makes the scheduling decision before traversal. Ordinary presets
update the full frame each pass. A one-Gi synthetic work-unit safety budget
introduces a bounded progressive lattice only for extreme combinations of
resolution, fresh samples, lights, candidates, bounces, and usable replay
donors; the Pathing drawer reports the active phase count and estimated work.
Reset frames are charged only for current paths. When an extreme reset still
needs a sparse lattice, a presentation-only bilinear preview replaces the old
nearest-tile expansion without entering estimator history. A skipped pixel
retains its previous scene-linear mean, RGB variance, and count without tracing
a path. Ray Marching
runs a prepare shader before stochastic screen-space visibility,
Heitz shadow, ray-traced flashlight, and ray-traced sky producers. Each guarded
producer consumes the same attempt token and returns early for a rejected
pixel. Deterministic hard-sun and point-flashlight rays also honor the adaptive
work mask even though their phase value is irrelevant. Half- or quarter-scale
screen-space visibility safely forces Every Pixel scheduling for the shared Ray
Marching attempt mask because one reduced-resolution sample cannot represent
divergent full-resolution per-pixel phases.

Ray Marching accumulation resolves the raw scene-linear frame before TAA. While
it is enabled, the accumulator is the sole long-term history owner: TAA's
history, rectification, and temporal blend are bypassed, and Ray Marching
denoisers are bypassed instead of feeding a second temporal estimate into the
mean. Raster TAA camera jitter is also inactive so a reset cannot expose one raw
Halton phase per displayed frame.
This prevents an already clipped, denoised, or nonlinear temporal result from
being averaged a second time. A matching transactional resolve commits only a
finite attempted sample; rejected and non-finite attempts copy the prior mean,
RGB variance, and count exactly. With accumulation disabled, Ray Marching
bypasses its full-resolution history and Path Tracing continuously replaces each
pixel with the cumulative batch selected by **Samples**. Shared Primary Surface
traces a full-resolution receiver/direct baseline before the indirect batch. Its
validated depth and motion let TAA reconstruct non-accumulating final output;
progressive path accumulation remains the sole history owner and only borrows
the selected camera-jitter sequence.

Path Tracing's **Firefly Clamp (Biased)** is part of the estimator rather than
the noise schedule. When enabled, it limits each successful contribution before
the persistent mean is updated, so the retained result is intentionally biased.

All retained samples share the renderer's lighting-history epoch. Camera motion
always discards every mean, variance, count, and stable signal. Path Tracing's
optional **Motion Reuse** may retain only surface-validated direct-light,
fully replayable RESTIR PT, and reconnectable rough diffuse-tail RESTIR GI
proposals across an eligible camera-only change. Donors are reprojected through
the prior camera, then re-evaluated at the current receiver. It never retains
accumulated radiance, intentionally has no effect while stationary, and is
unavailable when the requested work needs a sparse dispatch lattice.
Geometry, dynamic vertices, instance transforms, materials, lights,
environment, output extent, lighting solution, solver, transport, accumulation
policy, noise, scene, or shader changes clear every history family. Noise
animation alone changes ordinary non-accumulating sample presentation;
stationary accumulated sample phases remain owned by each pixel's successful
count. Full path-transport details are in
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
mean/variance/count math, deterministic revisit bounds, per-pixel successful
sample phases, prepare-before-producer attempt-mask gating, raw scene-linear
transactional commit behavior, epoch invalidation coverage, and staged asset
equality.
