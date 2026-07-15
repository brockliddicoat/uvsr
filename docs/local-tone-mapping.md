# Bilateral-Grid Local Tone Mapping

UVSR's optional local tone mapper adjusts exposure by image region while
reducing bleed across strong brightness edges. It is a first-party D3D12 GPU
pass. The feature is disabled by default, leaves manual AgX Exposure as the
authoritative global exposure, and applies one scalar EV correction equally to
all RGB channels.

## Pipeline Contract

The pass analyzes the same scene-linear Rec.709 HDR texture that is selected for
display:

```text
scene and visibility composition
    -> procedural sky
    -> NRA-RTAA resolve, when active
    -> bilateral-grid analysis
    -> fused NRA-RTAA sharpening in AgX
    -> camera white balance
    -> manual global EV + local EV
    -> existing AgX, grade, LUT, output transfer, and dither
```

Without NRA-RTAA, the input is `HdrColor`; with NRA-RTAA, it is the resolve
output. Analysis never modifies either texture. Locally exposed color is not
written into scene color, visibility inputs, or NRA-RTAA history.

The existing NRA-RTAA diagnostic and sharpening-contribution branches retain
priority over local debug output. Local analysis is bypassed for Forward
Tonemapperless, non-final PBR and visibility diagnostics, and non-final NRA-RTAA
diagnostics. Forward with normal AgX is supported.

## Bilateral Grid

Every valid sampled pixel is clamped to non-negative RGB and converted to
Rec.709 luminance. Non-finite samples are rejected. Log luminance is clamped to
the analysis-only range `[-16, +16]` EV; this does not change AgX's encoding
range.

The grid axes are screen X, screen Y, and log luminance. A nearest-cell splat is
followed by separable X, Y, and range-axis Gaussian blur. The stable final 3D
texture is sliced trilinearly in the AgX pixel shader using unsharpened scene
color. Radius changes the spatial sigma, and Edge Preservation changes only the
range-axis sigma:

```text
rangeSigmaBins = lerp(6.0, 0.65, EdgePreservation)
```

The mapping is monotonic: larger Edge Preservation values reduce cross-edge
blur. Neither control changes allocation dimensions.

### GPU Stages and Formats

| Stage | Input/output | Format |
| --- | --- | --- |
| Splat | translated fixed-point log-luminance sum | `R32_UINT` 3D UAV |
| Splat | sample count | `R32_UINT` 3D UAV |
| Reference | ping-pong scalar reference and validity | two `RG32_FLOAT` 1x1 textures |
| Blur X/Y/range | numerator and weight ping-pong | two `RG32_FLOAT` 3D UAVs |
| Finalize | base local EV and confidence | stable `RG32_FLOAT` 3D texture |
| Inactive fallback | zero EV and zero confidence | `RG32_FLOAT` 1x1x1 texture |

Splat accumulation uses `InterlockedAdd` rather than float atomics. Log
luminance is translated from `[-16, +16]` to `[0, 32]` and encoded with 12
fractional bits (`4096` units per EV). The worst supported cell contains at most
81 sampled pixels after resolution scaling. Its maximum sum is `10,616,832`,
well below `UINT32_MAX`; the reference test exhaustively checks supported
viewport heights and every quality tier.

The scalar scene reference is the sampled clamped log average. A 256-lane GPU
reduction computes it without CPU readback. Only this 1x1 reference is smoothed:

```text
currentWeight = 1 - exp(-deltaSeconds / responseSeconds)
```

A zero response uses the current value immediately. There is deliberately no
motion-reprojected or temporally accumulated spatial local-exposure field.

## Quality and Memory

Cell size and sampling stride scale with viewport height relative to 1080p.
This keeps the grid cost approximately stable at native 720p, 1080p, 1440p,
and 4K rendering.

| Quality | Cell at 1080p | Sampling stride at 1080p | Range bins | 1920x1080 grid | Active resources |
| --- | ---: | ---: | ---: | ---: | ---: |
| Performance | 32 px | 8 px | 32 | 60x34x32 | 1.99 MiB |
| Balanced | 24 px | 4 px | 48 | 80x45x48 | 5.27 MiB |
| Maximum Quality | 16 px | 4 px | 64 | 120x68x64 | 15.94 MiB |

The estimates include both integer accumulation grids, both floating blur
grids, the final grid, and both scalar histories. They exclude the tiny neutral
fallback, constant buffer, and timer queries. The Tonemapper drawer reports the
active dimensions, estimate, and non-blocking GPU timings for Splat, Reference,
Blur, Finalize, and the complete local pass.

## Controls

The nested **Local Tone Mapping** group follows the existing AgX controls.
Changing these controls does not select the AgX Custom preset.

| Control | Default | Range or values | Effect |
| --- | ---: | --- | --- |
| Enabled | Off | Off/On | Allocates and runs analysis only when presentation is eligible. |
| Strength | 0.35 | 0.0-1.0 | Scales departure from global Exposure. Zero is an exact bypass. |
| Radius | 64 px at 1080p | 16-192 px | Sets the spatial blur scale. |
| Edge Preservation | 0.70 | 0.0-1.0 | Narrows range blur as the value increases. |
| Shadow Recovery | 0.70 | 0.0-1.0 | Scales positive EV corrections in locally dark regions. |
| Highlight Compression | 0.70 | 0.0-1.0 | Scales negative EV corrections in locally bright regions. |
| Maximum Correction | 1.25 EV | 0.0-2.5 EV | Clamps both correction signs. |
| Reference Response | 200 ms | 0-1000 ms | Smooths only the scene-wide scalar reference. |
| Quality | Balanced | Performance/Balanced/Maximum Quality | Selects grid and sampling cost. |
| Debug Mode | Off | Input Log Luminance/Base Local EV/Final Local EV/Grid Confidence | Bypasses display grading and visualizes analysis. |

Settings are sanitized on the render path as well as constrained in the UI.
**Reset Settings** restores the table defaults and resets scalar history.
History also resets on enable transitions, pass recreation, resize, quality or
scene changes, renderer or White World changes, shader reload, and camera cuts
or camera-mode switches.

## Visibility Contribution Pruning

Local shadow recovery can make a dark higher-bounce contribution more visible
after exposure. When the higher-bounce contribution gate can prune work, UVSR
therefore extends its exposed-energy bound by the conservative maximum positive
local correction:

```text
maxPositiveLocalEV = min(
    MaximumCorrection,
    32 EV * ShadowRecovery * Strength)

exposedEnergyScale = exp2(globalExposureEV + maxPositiveLocalEV)
```

The bound is zero when local tone mapping is disabled, ineligible, at zero
Strength or Shadow Recovery, or showing a local debug view. The grid is never
passed into visibility. Enabling shadow recovery can reduce higher-bounce
pruning because the final exposed-energy bound must remain conservative.

## Limitations

- Output remains SDR through the existing AgX and sRGB path; there is no HDR10
  or scRGB output.
- There is no auto exposure, histogram, pre-exposure, or bloom integration.
- The grid stores one luminance-derived scalar correction, not per-channel
  exposure.
- Only the scalar reference has temporal state. Spatial analysis can retain
  input noise when NRA-RTAA is unavailable, especially at Performance quality.
- A nearest-cell splat keeps atomic cost low; separable blur and trilinear
  slicing provide reconstruction rather than an eight-cell atomic splat.
- The analysis is screen-space and cannot reason about off-screen illumination
  or semantic scene regions.
- Settings intentionally are not serialized between launches.
