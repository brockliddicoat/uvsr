# Noise Assets and Sampling

## Retained Assets

UVSR retains all 12 binary files under `assets/noise` byte-for-byte. The
authoritative `assets/noise/manifest.json` records each file's pattern,
dimensions, layer count, `R8_UNORM` format, byte size, algorithm revision, seed,
and SHA-256. Packaging consumes these checked-in bytes directly; no generator is
part of the active build or toolchain.

Spatial White and Spatial Blue each have one layer at 64x64, 128x128, 256x256,
and 512x512. Spatiotemporal Blue has 64 layers at each resolution. Array slices
are row-major. The default 128x128x64 spatiotemporal volume is exactly 1 MiB.

## Sampling Contract

The Noise drawer selects pattern, resolution, and animation. Spatial patterns
use one layer. Spatiotemporal Blue advances through 64 layers. Sampling uses
point-loaded `R8_UNORM` values decoded to open-bin scalar values; an effect and
semantic sample dimension receive stable independent stream offsets.

Tiling is centered in the active dispatch rectangle. For power-of-two size
`N`, each axis uses:

```text
tile = (localPixel - floor(dispatchExtent / 2) + N / 2) mod N
```

Animated spatial patterns translate the tile deterministically. Animated
spatiotemporal sampling keeps XY fixed and advances the layer. A disabled
effect-specific override inherits the global values while retaining its hidden
preference; it must not change another effect.

## Accumulation

Ray Marching exposes one **Accumulate Samples** toggle. When enabled, every
eligible pixel is attempted and one finite accepted scene-linear sample advances
one cumulative mean and that pixel's GPU successful-sample count. There is no
exponential averaging, adaptive schedule, workload preset, history preset, or
internal-policy control. The implementation may ping-pong mean/count textures
transactionally; those copies are one logical history, not selectable
alternatives.

Path Tracing always advances its equivalent fixed cumulative mean/count after a
valid fresh sample. Its history is not controlled by an additional path setting.
The GPU count is per pixel and advances only for a valid sample. The displayed
path-tracing count is an asynchronous readback of the center pixel's accepted
GPU history count. Camera, scene, resolution, geometry, material, lighting,
environment, solution, noise, and other image-defining changes reset both the
applicable history and that displayed count.

For both accumulators, `UINT32_MAX` is terminal overflow safety, not a
selectable history cap; mean/count stop advancing there. An invalid candidate
leaves a valid prior history unchanged. A non-finite stored history is repaired
to empty history and published even when the new candidate is rejected.

TAA cannot own a second long-term history while the accumulator owns the
presented result.

## Provenance and Validation

`assets/noise/README.md` and the manifest preserve construction and attribution.
The bytes are a first-party deterministic spectral construction informed by the
published spatiotemporal blue-noise objective; no NVIDIA texture or generator
source is packaged. Historical one-time generation code is recoverable from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`, but restoring it requires a new
need and byte-for-byte review.

Validate every manifest path, format, dimension, layer count, byte size, hash,
and package copy. Test centered wrapping, all resolutions, layer periodicity,
independent streams, deterministic replay, per-pixel successful-sample counts,
the asynchronously displayed center-pixel accepted GPU history count,
transactional failure, and every reset input. Render representative static, motion,
disocclusion, thin-geometry, AO/GI, sky, flashlight, and path-tracing cases in
Bistro and San Miguel. Bind captures to exact source, settings hash/version,
executable SHA-256, scene, camera, resolution, warmup, and sample window.
Compilation or matching dimensions do not prove spectrum, decorrelation,
convergence, or image quality.
