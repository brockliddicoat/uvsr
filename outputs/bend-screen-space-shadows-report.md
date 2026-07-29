# Bend Screen-Space Shadows Experiment Report

## Outcome

Runtime validation is in progress. The corrected renderer integration launches
without a D3D12 validation error, the automated checks pass, and the required
debug views plus the directional-light projection cases have completed. The
480/960-pixel quality sweep and live resize matrix remain before the longest
reliable sample-count result is final.

## Candidate Provenance

- Canonical base: `a55e215e4bf0eddb20330283d9a4f8e853bda49f`
- Experiment branch: `codex/bend-screen-space-shadows`
- Build: DirectX 12 Release
- GPU: NVIDIA GeForce RTX 4090 Laptop GPU
- Initial observed render resolution: 1902 x 1069

## Implementation Scope

The experiment adds only the renderer glue needed to consume the existing
single-sample device-depth buffer, run Bend Studio's CPU-generated compute
dispatch list, write a full-resolution `R8_UNORM` visibility texture, and bind
that texture to the existing deferred-lighting shadow-channel input for the
pointer-matched primary directional light.

The first-party adapter owns projection and reverse-Z conversion, resources,
compiled-variant selection, settings, timing, and grayscale debug presentation.
The released Bend implementation remains behind that boundary.

## Upstream Preservation

The two upstream headers are byte-identical to Bend Studio's
`code_final_candidate.zip` release:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `bend_sss_cpu.h` | 12,335 | `23AAE596DBB1B9BDAE23D87AC85079B138426823C3153079F7A7DD36F603D02A` |
| `bend_sss_gpu.h` | 25,289 | `7FBE24BD2040A62536C31DF6CD38A92CB22C172A9A419EED3C0AF7DF1D50A68C` |

CMake rejects either file if its released hash changes. Scoped Git attributes
also prevent line-ending normalization.

## Automated Verification

- Canonical Release baseline: built successfully; 12 of 12 tests passed.
- Candidate Release build: built successfully.
- Candidate test suite: 13 of 13 tests passed.
- Shader build: all 45 combinations of five `SAMPLE_COUNT`, three
  `HARD_SHADOW_SAMPLES`, and three `FADE_OUT_SAMPLES` values compiled.
- Bend reference test: defaults, preset reset, compiled-variant mapping, and
  positive/negative projected-light `w` dispatches passed.
- Documentation checker: self-test passed; 380 repository headings and bold
  lead-ins passed.
- Source preservation: both upstream hashes match the official archive.
- Independent rendering review: no P0-P2 source correctness findings remained
  after the volatile-buffer ordering repair.

## Runtime Validation

### Debug-View Verification

The debug views were checked in the requested order before timing the
composite:

- **Wave:** coherent full-frame lane ramps with continuous dispatch boundaries.
  The default and on-screen light cases converged on the projected light
  endpoint; the off-screen case became parallel diagonal lanes; the
  behind-camera case remained finite and coherent. No holes, stale strips,
  NaNs, or quadrant corruption were visible.
- **Edge:** flat surfaces remained black and thin white contours appeared only
  at architectural, foliage, and depth discontinuities. No wave data leaked
  into the view and no full-surface false edge appeared.
- **Thread:** stable horizontal lane ramps covered the full target without gaps
  or cross-dispatch corruption.

### Directional-Light Cases

- **On-Screen:** `SUN` at approximately 146.0 degrees azimuth and 31.3 degrees
  elevation produced a Wave convergence point near the upper center and stable
  cast/contact shadows. The projection sign matched the visible light
  direction.
- **Off-Screen:** approximately 47.7/31.3 degrees produced parallel Wave lanes
  and a stable composite without a dark screen border or stale visibility
  strip.
- **Behind-Camera:** -50.6/16.1 degrees produced a finite Wave convergence
  pattern across six dispatches and a stable composite without an inverted
  halo, NaN burst, or resource error.

### Reverse-Z and Resolution Changes

The adapter supplies reverse-Z near/far values of 1/0 and uses a point
clamp-to-border sampler whose border equals the far-depth value. Runtime
odd-resolution, maximize/restore, and active-resource recreation checks are
pending.

### Screen Edges and Grazing Surfaces

At -50.6 degrees azimuth and 5.3 degrees elevation, the 60- and 120-pixel
variants remained stable at the screen borders and on the long masonry ledges.
The 240-pixel variant exposed the expected screen-space limitation more
clearly: long silhouettes become conspicuous on flat walls and around
foreground depth discontinuities, although the image remained finite and
stable. The 480/960-pixel comparison and `Ignore Edge Pixels` tradeoff remain
pending.

## Performance

The reported pass metric will be **Trace GPU Time (White Clear + Bend
Dispatches)**. The composite is one existing PBR shadow-channel texture
read/multiply rather than a standalone pass, so its cost will be reported as a
fixed-condition PBR A/B frame delta instead of an invented exact timer.

| Preset or Length | Trace GPU Time | Frame-Time Delta | Notes |
| --- | ---: | ---: | --- |
| Bend Exact, 60 | 0.098-0.106 ms | Pending | 4 hard, 8 fade |
| 120 | 0.183-0.203 ms | Pending | Compiled validation variant |
| Long, 240 | 0.347-0.362 ms | Pending | Bend defaults except length |
| 480 | Pending | Pending | Compiled validation variant |
| Maximum Validation, 960 | Pending | Pending | Bend defaults except length |

The current timing window is a fixed 1902 x 1069 Benchmark Position 1 camera on
an NVIDIA GeForce RTX 4090 Laptop GPU, with `SUN` at -50.6 degrees azimuth and
5.3 degrees elevation. Each range is the minimum and maximum of three or more
warmed UI samples. The Bend metric is the explicit white clear plus released
CPU dispatch list; the existing PBR composite has no separate timer.

## Artifacts and Reliability

The first enabled launch exposed two D3D12 integration errors, both outside the
untouched Bend implementation:

1. `R8_UNORM` UAV creation rejected an optimized clear value on a texture that
   was not a render target. The adapter now uses its existing explicit white UAV
   clear without an optimized clear value.
2. NVRHI rejected binding the volatile Bend constant buffer before its first
   write. Each released CPU dispatch now writes its constants before rebinding
   compute state, matching Donut's multi-dispatch volatile-buffer pattern.

The replacement build then completed the required Wave/Edge/Thread checks and
the on-screen, off-screen, and behind-camera light matrix without another
validation error. At grazing incidence, increasing trace length increases
visible screen-space silhouette exaggeration even though execution remains
stable. No final maximum-length claim is made before the remaining long-variant
and resize matrix completes.

## Longest Reliable Sample Count

Pending runtime validation. Compilation alone does not establish visual
reliability.

## Known Limitations

- Only the primary directional light is traced and composited.
- Only visible first-layer screen-space depth can occlude.
- Off-screen geometry, hidden geometry, and shadow-map information are absent.
- The R8 visibility texture is full resolution while enabled.
- No stochastic sampling, temporal filtering, Hi-Z, hierarchical far tracing,
  thickness texture, or unrelated renderer changes are included.

## Deferred Follow-Up

The validated Bend pass is a standalone near-visibility producer. A future Hi-Z
or hierarchical far tracer can remain a separate producer and combine at the
renderer boundary, without editing either released Bend header.

## Sources

- [Bend Studio: Inside Bend Screen Space Shadows](https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/)
- [Bend Studio Released CPU and GPU Code](https://www.bendstudio.com/assets/cms/downloads/code_final_candidate.zip)
- [Efficient GPU Screen-Space Ray Tracing](https://jcgt.org/published/0003/04/04/)
- [An Adaptive Acceleration Structure for Screen-Space Ray Tracing](https://research.nvidia.com/sites/default/files/pubs/2015-08_An-Adaptive-Acceleration/AcceleratedSSRT_HPG15.pdf)
