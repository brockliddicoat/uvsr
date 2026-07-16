# Experimental Visibility Sample Rotation v1

## Scope and Status

Visibility Sample Rotation v1 is a retired, removable, default-off image-quality
experiment. It changes which full-resolution G-buffer pixel represents each
reduced-resolution screen-space AO/GI block. It does not change scene
rasterization, material shading, direct lighting, texture derivatives, or camera
projection. Direct user evaluation found no visible improvement and a worse,
noisier result even after temporal history gathered. The
[postmortem](postmortem/visibility-sample-rotation-v1.md) records the negative
product result and the conditions for any future reconsideration.

The experiment is intentionally narrower than a general temporal upscaler. UVSR
renders its scene and G-buffer at output resolution, then optionally traces
screen-space visibility on a smaller dense rectangular grid. The renderer's
**Half** visibility resolution is one sample per output-space 2x2 block, which
is one quarter of the output-pixel count and matches the prompt's four-phase
case. UVSR's **Quarter** visibility resolution is one sample per 4x4 block, or
one sixteenth of the output-pixel count; that layout is unsupported by this
four-phase experiment and retains its legacy fixed receiver.

In the retained local candidate, enable the experiment with **Visibility >
Reconstruction and Upsampling > Experimental Sample Rotation** after choosing
**Half** sampling resolution. The option starts disabled and is unavailable for
unsupported visibility resolutions. Odd output dimensions also resolve to the
legacy fixed receiver because their partial 2x2 edge blocks cannot share one
exact global phase registration.

## Existing Sample and Jitter Behavior

UVSR currently sets its planar-view pixel offset to zero every frame. It has no
active camera-projection jitter or scene-level temporal antialiasing sequence.
The existing `frameIndex` variation belongs to screen-space visibility ray
scheduling:

- Independent hash noise varies slice, radial, budget, and feedback choices.
- Toroidal blue noise uses a 64-frame cycle with independently optimized
  semantic layers and a hashed cycle offset.
- Filter-adapted spatiotemporal noise uses a 32-frame volume with a
  low-discrepancy cycle offset.

Those sequences vary the directions and budgets traced from a receiver; they do
not vary the receiver's output-pixel location. The rotation experiment uses a
separate rendered-sample clock and therefore preserves the scheduler's complete
32- or 64-frame sequence. A skipped or inactive visibility frame does not skip a
rotation phase, and a history reset restarts the phase cycle at zero.

The legacy reduced-resolution mapping is:

```text
fullPixel = samplingPixel * resolutionScale + resolutionScale / 2
```

At scale two this always chooses output-pixel index `(1, 1)`, the bottom-right
pixel inside each 2x2 block. Disabled and unsupported configurations resolve the
new frame constant to this exact legacy value.

## Rotation Model

The CPU resolves one `VisibilitySampleRotationState` per rendered visibility
frame. The state contains the mode, phase index and count, integer output-pixel
index inside the block, and its centered output-pixel offset. Sampling, temporal
guide lookup, and bilateral-guide lookup consume the same resolved integer
offset. The full-resolution upsampler subtracts the centered offset before
mapping output pixels into the low-resolution lattice, so the current value is
registered at the represented output-pixel center. There is no per-pixel
feature branch.

The low-resolution storage lattice remains block-addressed. Rotation changes
the full-resolution receiver and guide selected for each logical block and the
current phase's reconstruction registration; it does not shift texture
allocation, dispatch coordinates, or low-resolution history addresses. This is
receiver dithering and block-estimate accumulation, not true four-sample
output-space superresolution.

### Coordinate Units

- Integer phase coordinates are output-pixel indices inside a 2x2 block, with
  positive X to the right and positive Y downward.
- Centered phase offsets are measured in output pixels and are always `-0.5` or
  `+0.5` on each axis.
- The same centered displacement is `-0.25` or `+0.25` in scale-two visibility
  sampling-grid pixels.
- No normalized-device-coordinate conversion is performed. The camera pixel
  offset and projection matrices remain unchanged, so the experimental NDC
  offset is exactly zero.

### Exact Four-Phase Sequence

| Phase | Pixel in 2x2 Block | Centered Output-Pixel Offset | Position |
| --- | --- | --- | --- |
| 0 | `(0, 0)` | `(-0.5, -0.5)` | Top-left |
| 1 | `(1, 1)` | `(+0.5, +0.5)` | Bottom-right |
| 2 | `(1, 0)` | `(+0.5, -0.5)` | Top-right |
| 3 | `(0, 1)` | `(-0.5, +0.5)` | Bottom-left |

The first transition crosses the full block diagonal. Every position is visited
once, the ordering is deterministic, and the cycle's mean offset is exactly
zero. The rotating sequence restarts at phase zero after history invalidation.
In a Debug build, a deliberately frozen phase remains selected when only its
history is reset so fixed-phase captures stay reproducible.

## Temporal Reconstruction and Motion

UVSR motion vectors remain current-to-previous full-resolution pixel motion.
The experiment does not encode sample phase into object or camera motion.
Current receiver depth, normal, and motion are loaded from the resolved phase,
then the previous full-resolution position is mapped to the existing logical
visibility block. Previous AO/GI, depth, and normal history are all stored at
the visibility sampling resolution.

This block-addressed contract deliberately accumulates the four receiver phases
into one temporal block estimate. A current phase can reject history when its
depth or normal differs from the preceding phase at a silhouette or material
boundary. UVSR cannot retain four independent output-pixel values without a
new output-resolution or per-phase history resource, which this experiment
forbids.

The history-configuration key includes the resolved legacy or rotating
convention and phase count, but excludes the changing phase index. History and
the rendered-sample clock reset when the convention changes. Existing resource
recreation and explicit reset paths cover resolution/scale changes, resize,
scene unload, camera-preset cuts, node-pick camera reframing, White World
changes, shader reload, and entering or leaving an active visibility
configuration.

## Supported Configurations

| UVSR Setting or Layout | Effective Sample Rate | Behavior |
| --- | --- | --- |
| Full visibility resolution | One per output pixel | Legacy behavior; rotation falls back automatically |
| Half visibility resolution, even output dimensions | One per output 2x2 block | Four-phase receiver rotation supported |
| Half visibility resolution, either output dimension odd | Partial 2x2 edge block | Legacy behavior; rotation falls back automatically |
| Quarter visibility resolution | One per output 4x4 block | Legacy behavior; the requested four-phase sequence does not cover this layout |
| Two samples per output 2x2 block | Not present in UVSR | Diagonal checkerboard mode omitted |

UVSR has no packed, anisotropic, programmable-sample-position, or parity-aware
layout that can store two diagonal samples per 2x2 block at the existing cost.
Implementing that mode would require new sampling, filtering, history, and
reprojection contracts. Full-resolution rasterization with pixel discard is not
used.

## Debug Controls

Debug builds expose phase freezing, a phase selector, the current phase index,
and an explicit history-reset button only while rotation is enabled on the
supported Half resolution. Release builds contain none of those controls. The
normal temporal-reconstruction checkbox remains available for comparisons with
and without history; rotation does not add a second temporal-history path or a
debug visualization pass. Resetting history retains a deliberately selected
frozen phase; leaving freeze mode returns to the rotating sequence from phase
zero.

## Resource and Performance Contract

The experiment adds no texture, buffer, render target, history surface, phase
mask, validity mask, pass, dispatch, barrier, synchronization point, or resource
transition. It preserves the existing sampling dimensions, formats, sample
counts, AO/GI dispatch size, and full-resolution upsample dispatch. The existing
constant buffer's unused padding carries one packed integer receiver coordinate
and two centered output-pixel offsets. Disabled and unsupported modes write zero
centered offsets, so its byte size and binding layout do not change.

All shader permutations and compute-pipeline counts remain unchanged. The
disabled path has no feature branch: the CPU writes the legacy
`resolutionScale / 2` offset, and the common mapping consumes it. Generated
shader inspection and runtime telemetry are recorded below. The available
runtime samples are not strong enough to claim that the constant substitution
is within measurement noise.

## Texture Sampling and Mip Behavior

The full-resolution G-buffer has already evaluated material textures before
visibility tracing. Rotating the visibility receiver can change which existing
depth, normal, radiance, and material values represent an AO/GI block, but it
cannot improve scene raster coverage, alpha-test coverage, implicit material
texture derivatives, or explicit LOD/gradient operations. The experiment adds
no mip bias or sharpening control.

## Comparison Procedure

### Fixed Benchmark Setup

1. Build Release and launch with
   `tools\launch_uvsr.ps1 -Experiment "samplerotation" --benchmark-camera`.
2. Use the **PBR Sponza Decorated** scene at the locked Benchmark Position 1,
   1920x1080 output, deferred PBR, AO and GI enabled, Uniform Solid Angle, and a
   fixed sample count.
3. Hold scene, camera, adapter, resolution, estimator, sample scheduler,
   bounce count, filtering, temporal response, and warmup constant.
4. Compare the verified `a7e51b7` baseline, candidate with rotation disabled,
   and candidate with rotation enabled in an interleaved A/B/A/B order.

### Capture Matrix

Record at least four consecutive frames plus a still image for each applicable
row:

- Legacy fixed receiver at Half resolution.
- Four-phase rotation at Half resolution.
- Four-phase rotation with temporal reconstruction disabled.
- Four-phase rotation with temporal reconstruction enabled.
- Full-resolution native visibility reference.
- Quarter-resolution unsupported fallback.
- Slow camera motion, fast motion/disocclusion, static ivy and curtains, thin
  edges, emissives, alpha-tested materials, and specular highlights.

Use identical crops for stills and difference images. A single static frame is
not convergence evidence. If synchronized capture automation is unavailable,
record the exact settings and use a four-frame screen recording so phase cadence
and history convergence remain inspectable.

### Performance Matrix

After a fixed warmup, record multiple windows of:

- Total frame time and FPS.
- Visibility **All**, **Trace**, **Filter**, and **Other** GPU timers.
- Exact logical **Outputs** and **Working** bytes shown by UVSR.
- Executable/source identity, adapter, scene, camera, output resolution,
  settings, warmup length, sample-window length, mean, spread, and run order.

UVSR's displayed memory-bandwidth number is theoretical adapter peak bandwidth,
not measured traffic. Do not present it as a DRAM-bandwidth measurement. Use PIX,
RenderDoc, or another GPU profiler for actual traffic and invocation counts when
available; otherwise report those metrics as unmeasured and rely only on the
unchanged resource/dispatch contract.

## Known Limitations

- Temporal history stores one value per visibility block, so the four phases
  cannot preserve four independent output-pixel details.
- In the user's test, allowing that mixed block history to gather did not recover
  a visual benefit; the rotation-enabled result remained worse and noisier.
- With temporal reconstruction disabled, phase-to-phase AO/GI variation can
  appear as cadence rather than convergence.
- Silhouettes and depth/normal discontinuities can reject alternating phases,
  reducing the benefit or increasing shimmer.
- Odd output dimensions fall back to the legacy fixed receiver. A single
  frame-global centered phase cannot exactly register both complete blocks and
  a clamped partial edge block without edge-specific reconstruction logic.
- UI Quarter and diagonal half-rate checkerboarding are unsupported.
- Scene geometry, material texture detail, mip selection, transparency, and UI
  rendering are not resampled by this visibility-only experiment.
- UVSR has explicit reset paths for product camera presets and scene/resource
  changes, including node-pick reframing, but no general detector for every
  externally forced arbitrary camera discontinuity.

## Evaluation Results

### Automated and Static Evidence

The task-local candidate is based directly on canonical verified commit
`a7e51b7`. Release and Debug renderer builds pass, including every one of the 53
visibility shader tasks. The complete Release test suite passes 12/12 after the
new sample-rotation reference target is added. The documentation checker
self-test and the complete tracked-Markdown scan pass with zero violations.
The final primary-sampling helper was composed and all 42 of its Release
permutations were rebuilt. The Debug executable and focused Debug test preceded
that last HLSL-only composition; the later Debug runtime used the shared final
shader package, while the final clean renderer rebuild restored Release output.

Representative DXC 1.9.2602.17 compute variants were compiled directly as
Shader Model 6.5 with `ENABLE_AO=1` and `ENABLE_GI=1`; the filter row also uses
`SPATIAL_FILTER=1`. Counts below are LLVM/DXIL assembly instructions, not an
architecture-specific issued-instruction or cycle measurement.

| Variant | Baseline Instructions | Candidate Instructions | Baseline/Candidate Constant Loads | Texture Loads | Texture Stores | Branches |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Primary sampling | 1,392 | 1,395 | 35 / 36 | 17 / 17 | 11 / 11 | 182 / 182 |
| Temporal | 403 | 406 | 5 / 6 | 17 / 17 | 4 / 4 | 24 / 24 |
| Gaussian filter | 618 | 628 | 24 / 26 | 8 / 8 | 6 / 6 | 63 / 63 |

The candidate adds no feature branch, texture operation, binding, resource,
dispatch, or permutation in any representative variant. Its direct DXIL byte
sizes change from 19,192 to 19,316 for primary sampling, 10,880 to 11,000 for
temporal reconstruction, and 11,900 to 12,056 for the Gaussian filter. All 42
primary sampling permutations compile after final composition.

### Runtime Evidence

The controlled rows used PBR Sponza Decorated at locked Benchmark Position 1,
1920x1080, an NVIDIA GeForce RTX 4090 Laptop GPU, Half visibility resolution,
Uniform Solid Angle, 20 fixed samples, Toroidal Blue Noise, AO strength 1, one
GI bounce at intensity 4 with emissive gain 4, temporal response 0.35, and the
Gaussian joint-bilateral filter at radius 4.

One persisted canonical `a7e51b7` Release observation reported 2.367 ms,
422.6 FPS, and visibility timers of 0.52 ms All, 0.27 ms Trace, 0.13 ms Filter,
and 0.13 ms Other. Five candidate Release observations with rotation disabled
reported a median frame interval of 2.422 ms with a 2.416-2.493 ms range and a
median 412.9 FPS with a 401.1-413.9 FPS range. Their Trace median was 0.20 ms
with a 0.20-0.29 ms range, Filter was 0.13 ms with a 0.13-0.14 ms range, and
Other was 0.13 ms in every sample. The cursor obscured the candidate All value,
so no exact candidate All summary is claimed.

Every baseline and candidate-disabled observation reported the same logical
texture footprint: 24.7 MiB Outputs, 18.0 MiB Working, 0.0 MiB Mask Cache,
0.0 MiB Avoided, and 2.0 MiB Shared. This supports the unchanged-allocation
contract but is not an API-residency measurement.

These short UI observations are smoke telemetry, not a statistically controlled
performance result. The baseline has only one persisted sample, the rows were
sequential rather than interleaved, the timers are instantaneous UI query
results, and the pre-existing user-owned `main-a7e51b7-2243` window remained
running and imposed variable shared-GPU load. The raw frame-interval difference
therefore must not be characterized as either a regression or equivalence. A
controlled rotation-enabled Release row was interrupted by a user-driven
adapter restart and was excluded rather than mixed into the NVIDIA results.

Debug runtime smoke checks rendered all four frozen phases, preserved phase 3
across an explicit history reset, resumed the rotating clock, and exercised
rotation Off-On, temporal Off-On, Half-Full-Half, and Half-Quarter-Half
transitions without visible stale-history flashes or corruption at the locked
pose. Full and Quarter kept the checked preference but disabled the control and
used the legacy fallback. A Half/rotation/temporal state capture and four fixed-
phase UI captures were retained with the task handoff. The final clean Release
binary was also checked to contain none of the Debug control strings.

### User Image-Quality Evaluation

In the user's direct evaluation of the current candidate, enabling sample
rotation produced no visible improvement over the legacy fixed receiver and
appeared worse and noisier even after temporal reconstruction was allowed to
gather. This is qualitative product feedback rather than a synchronized
convergence measurement, but it is sufficient to reject production promotion of
this implementation. The current one-value-per-block history has not
demonstrated convergence across the four receiver phases; any convergence
advantage remains speculative.

No available tool measured CPU frame-setup time, separate scene-rendering GPU
time, actual DRAM or render-target/depth traffic, shader invocation counts,
texture-sampling cost, register pressure, occupancy, or API residency and VRAM.
PIX, RenderDoc, and PresentMon were unavailable. Source and generated-DXIL
inspection establish unchanged passes, dispatches, bindings, texture
operations, branches, and allocations, but cannot substitute for those runtime
metrics or for synchronized motion and convergence capture.

## Modified Files

- `AGENTS.md`
- `CMakeLists.txt`
- `README.md`
- `src/screen_space_visibility.cpp`
- `src/screen_space_visibility.h`
- `src/screen_space_visibility_cb.h`
- `src/screen_space_visibility_cs.hlsl`
- `src/screen_space_visibility_filter_cs.hlsl`
- `src/screen_space_visibility_temporal_cs.hlsl`
- `src/uvsr.cpp`
- `src/visibility_sample_rotation.h`
- `tests/visibility_sample_rotation_tests.cpp`
- `docs/visibility-sample-rotation-v1.md`
- `docs/exec-plans/completed/visibility-sample-rotation-v1.md`
- `docs/postmortem/README.md`
- `docs/postmortem/native-resolution-analytical-reconstructive-temporal-anti-aliasing-v1.md`
- `docs/postmortem/visibility-sample-rotation-v1.md`
- `tools/check_document_title_case.py`

## Recommendation

Do not promote this implementation. The user's qualitative comparison found no
visible benefit and a worse, noisier result even after temporal history gathered.
Keep it only as a removable, default-off local research reference if further
investigation is desired; otherwise remove it. Any convergence advantage is
speculative and must not be cited as a demonstrated benefit.

Reconsider only when a redesigned history model or another architecture trigger
listed in the
[postmortem](postmortem/visibility-sample-rotation-v1.md#revival-triggers) can
preserve sample identity, synchronized captures demonstrate a repeatable
improvement without cadence or added noise, and controlled timing shows
acceptable cost. A true output-space superresolution design would require a
separate, broader architecture.
