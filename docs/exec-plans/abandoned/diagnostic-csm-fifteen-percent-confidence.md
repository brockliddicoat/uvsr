# Diagnostic Cascaded Shadow Maps: Fifteen-Percent Confidence

## Status

- State: superseded by shadow retirement
- Coordinator: `/root`
- Branch/worktree: preserved local `codex/bend-screen-space-shadows` worktree
- Started: 2026-07-24
- External actions: no commit, push, pull request, merge, main change, submodule update, or destructive Git operation

## Goal and Done Condition

Optimize the diagnostic conventional CSM until the evidence supports at least
85 percent confidence that its performance is no more than 15 percent slower
than the matched UE5 conventional CSM implementation, while preserving
artifact-free shadows and every existing change.

Done requires:

- [x] Independently reversible CPU and GPU optimizations with neutral disabled paths.
- [ ] Exact caster-decision parity across organizational toggles.
- [ ] Thermally valid detailed-off headline measurements and detailed-on attribution.
- [ ] Raw and 42.5-TFLOPS-normalized detailed-off headline measurements.
- [ ] Stable normal, cascade-selection, visibility, and cache-action output.
- [ ] Stationary, motion, teleport, invalidation, alpha-tested, boundary, and resize checks.
- [x] Release build, focused tests, full CTest, title-case audit, Bend hashes, and diff check.
- [ ] An evidence-backed confidence assessment that separates UVSR repeatability from uncertainty in the unmatched UE target.

## Baseline and Targets

The accepted optimized four-cascade Position 1 baseline uses four 2048-by-2048
D16 cascades, full redraw, UE-style manual 5-by-5 PCF, one-pass classification,
and direct submission.

| Metric | Accepted UVSR Center | Provisional UE Estimate | Fifteen-Percent Upper Target |
| --- | ---: | ---: | ---: |
| GPU total | 1.278 ms | 1.09 ms | 1.254 ms |
| Clear plus raster | 1.213 ms | 1.00 ms | 1.150 ms |
| Sampling | 0.054 ms | 0.06 ms | 0.069 ms |
| CPU total | 0.574 ms | 0.375 ms | 0.431 ms |
| CPU culling | 0.374 ms | 0.245 ms | 0.282 ms |
| CPU recording | 0.132 ms | 0.085 ms | 0.098 ms |

The UE estimates are not matched measurements. Crossing these targets improves
the probability assessment but cannot by itself remove UE-side benchmark
uncertainty.

The final evidence review found no public capture matching UE 5.6, this
adapter, scene, coverage, resolution, filter, and timing boundary. Under a
deliberately conservative one-sided model, at least 85 percent GPU-only
confidence requires a retained UVSR median at or below 1.05 milliseconds, with
repeated blocks at approximately 1.07 milliseconds or lower. The analogous
CPU-only threshold is 0.381 milliseconds. Treat 0.97 milliseconds GPU plus
0.355 milliseconds CPU as the conservative joint threshold; the provisional
targets above are useful engineering centers but are not sufficient by
themselves for the requested confidence.

## Unofficial Throughput Normalization

Raw GPU timing remains the only official score. For same-adapter,
identical-work trend comparisons, normalize the unsmoothed current-clock FP32
capacity to the 42.5-TFLOPS RTX 4090 Laptop reference:

`estimated ms at 42.5 TFLOPS = raw GPU ms * current-clock TFLOPS / 42.5`

The UI must show the raw time, current-clock capacity, utilization-scaled
throughput, normalized estimate, and grade together. Utilization never enters
the estimate because doing so could erase a real optimization that reduces GPU
occupancy. CPU timings are never GPU-normalized. Heat lowers the estimate grade
instead of automatically deleting a useful trend sample, but unsafe
temperature, device failure, wrong work, contention, or visibly incorrect
shadows still invalidate it.

Historical validation is unusually consistent for the full-redraw CSM: two
receiver-scissor-on runs measured 1.510 ms at 34.7 utilized TFLOPS and 1.602 ms
at 32.7 utilized TFLOPS. They differ by 6.1 percent raw but normalize to 1.23287
and 1.23260 ms, a 0.022-percent difference. These older samples lack separately
recorded clock-capacity telemetry, so their near-saturated utilized TFLOPS is a
documented proxy rather than the new system's preferred denominator.

## Optimization Order

1. Precompute receiver-hull axes and projection intervals once per cascade.
2. Share caster light-space preparation across cascades using the same directional-light basis.
3. Evaluate a conservative receiver-footprint raster scissor for uncached full redraws.
4. Measure detailed profiling overhead and use detailed-off totals for headline performance.
5. Batch contiguous all-full-redraw depth-array clears without changing mixed or cached updates.
6. Normalize the exact depth axis once per cascade in a separate vertex-shader permutation.
7. Consume the CPU-normalized receiver light directly in a separate resolve permutation.
8. Match UE's squared caster-radius comparison without redundant square roots.
9. Skip shared caster projection after the same radius test has already rejected
   the caster for every cascade.
10. Precompose the camera-to-shadow receiver transform in an independently
    reversible shader permutation.
11. Evaluate a conservative exact-result saturated-slope branch in an
    independently reversible depth-shader permutation.
12. Replace the remaining finite unsaturated slope calculation with the
    algebraically equivalent perpendicular-to-parallel normal ratio in an
    independently reversible permutation.
13. Push exact identity-linear single-instance caster translations directly
    while retaining Donut's instance-buffer path for every other transform.
14. Consider further work only after accepted A/B evidence identifies a
    remaining hot stage.

## Measurement Protocol

- Require a cold preflight, inactive thermal limiters, High renderer priority,
  one renderer instance, external CPU at or below 20 percent excluding
  ChatGPT/Codex, and at least 30 live TFLOPS for an official raw score.
- Retain an unofficial normalized trend sample when the exact workload identity
  and output remain valid but heat lowers throughput. Record its raw value and
  grade; do not spend an extended code-work window waiting for Grade A data.
- Stop runtime work for unsafe temperature or headroom, an active limiter that
  threatens hardware stability, a device error, external GPU contention, or
  visibly incorrect output. Normalization does not override those gates.
- Settle for at least five seconds after each toggle.
- Use mirrored on/off/off/on blocks, rejecting intervals whose bracketing
  optimized GPU medians drift by more than three percent or CPU medians by more
  than ten percent.
- Measure headline totals with detailed timing disabled. Use detailed timing
  only for stage attribution and quantify its overhead separately.
- Preserve identical coarse, radius-rejected, hull-rejected, candidate, and
  rendered caster counts across organizational optimization toggles.

## Assignments

| Task | Owner | Scope | Status |
| --- | --- | --- | --- |
| Receiver and caster SAT precomputation | `/root/ue_csm_cpu_gap` | CSM-local source, settings, UI, stats, and tests | Complete |
| Lazy caster SAT and light-XY rejection | `/root/csm_independent_review` | CSM-local source and differential tests | Complete |
| Depth-raster audit | `/root/ue_csm_gpu_gap` | UE/Donut/NVRHI comparison and reversible receiver scissor | Complete |
| Full-redraw clear batching | `/root/csm_batched_clear` | Contiguous array clear, settings, UI, stats, and tests | Complete |
| Depth-axis normalization | `/root/csm_depth_axis_design` | Exact CPU inverse length and legacy/optimized shader permutations | Complete |
| Final UE Hot-Path Audit | `/root/ue_csm_remaining_gap` | UE/Donut/NVRHI source review and receiver-light permutation | Complete |
| TAA Receiver Review | `/root/jitter_envelope_review` | Jitter coverage, scissor conservativeness, and counter review | Complete |
| Confidence and measurement protocol | `/root/benchmark_analysis` | Read-only statistical design | Complete |
| Final raster hot-path audit | `/root/csm_gpu_raster_gap` | UE depth-path review and conservative saturated-slope permutation | Complete |
| Final receiver hot-path audit | `/root/csm_projection_gap` | UE projection review and precomposed clip-to-shadow permutation | Complete |
| Final compact-sort audit | `/root/runtime_benchmark_audit` | UE visible-command comparison and CPU contingency design | Complete |
| Integration, build, runtime, and reporting | `/root` | Shared worktree | Active |

## Risks

- A view-dependent raster scissor cannot seed cached reuse unless every later
  receiver texel is conservatively regenerated; cached profiles must therefore
  gate it off.
- Precomputed SAT data must fail open for malformed, non-finite, degenerate, or
  transform-incompatible inputs.
- Detailed counters and nested timer queries may inflate the CPU path; their
  cost must not be confused with renderer work.
- No claim of 85 percent confidence in symmetric UE parity is defensible
  without some matched UE-side timing evidence. The final report must state
  whether the confidence refers to not being slower by more than 15 percent or
  to a symmetric plus-or-minus band.

## Current Evidence

- Authenticated read-only access to the exact UE 5.6.1 source is available at
  commit `6978b63c8951e57d97048d8424a0bebd637dde1d`.
- UE 5.6.1 implements an independently optional
  `r.Shadow.CSMScissorOptim` path in `ShadowSetup.cpp` and applies its
  conservative rectangle in `ShadowDepthRendering.cpp`; it defaults off.
- UE traverses a primitive octree and rejects whole node bounds against shadow
  caster volumes before processing individual primitives. UVSR's one-pass
  scene walker is structurally similar but still has an opportunity to apply
  its accurate receiver-hull test at hierarchy nodes. Donut's internal-node
  bounds are not reliable for all skinned and morphing descendants, so this
  optimization was rejected rather than risk missing casters.
- The laptop has no Unreal Editor, source build, or matched project installed.
  Public measurements do not match the hardware, scene, coverage, filtering,
  caster population, and timing boundaries closely enough to narrow the UE
  target uncertainty to 15 percent.
- The first post-build thermal snapshot was idle and limiter-free, but still
  above the accepted cold baseline at 50 degrees Celsius core and 59 degrees
  Celsius hotspot. Runtime sampling used the live TFLOPS gate and stopped as
  the reported throughput approached its lower accepted bound.
- The final source audit found no remaining material local hot-path defect.
  The optimized depth permutation removes one reciprocal square root, one dot
  product, and four multiplies per submitted shadow vertex. The full-redraw
  path now issues one contiguous array clear, and the optimized resolve avoids
  a redundant per-receiver directional-light normalization.
- The combined Release candidate
  `2E9F8C901FBD9A447B5DE9437C0A9C1E758A773FEE275C5ED1C049BEF7E28A58`
  builds and passes the focused CSM test and all 15 Release CTest cases. The
  title-case checker passed all 555 headings and lead-ins, all eight frozen Bend
  file hashes match, and both tracked and untracked CSM whitespace checks pass.
  A later preflight was deliberately rejected at 63 degrees Celsius GPU core
  and 72.4 degrees Celsius hotspot; external CPU load was acceptable and no
  thermal limiter was active.
- A stricter preflight using the repository's 55-degree hottest-sensor gate
  timed out with the GPU unexpectedly resident in P0 at 1,815 megahertz and
  35.86 watts despite zero reported utilization. Its final sample was 67
  degrees core and 80 degrees memory junction with no active limiter; process
  and adjusted-CPU gates passed. No renderer was launched.
- A second bounded strict preflight also timed out with no renderer, build,
  capture worker, or process-hygiene blocker. Its final sample was 60 degrees
  core and 74 degrees memory junction with only 27 degrees of headroom while
  the GPU remained in P0 at 1,815 megahertz. The sole NVIDIA context owner was
  an isolated ChatGPT Chromium `on_device_model` utility child, which was
  verified by executable, parent, and command line and then closed without
  closing the assistant host. A new cold preflight remains required.
- The first post-build preflight began at 42 degrees core and 54 degrees on
  the hottest sensor, but one external-CPU rolling window reached 20.19
  percent and reset the stability timer. Continued polling held the otherwise
  idle adapter in P0 near 1,815 megahertz and roughly 36 watts until the
  hotspot reached 62.2 degrees. The run was rejected. The next single
  preflight will retain every limit and request a five-second sample cadence.
  P8 near 210 megahertz remains the preferred cold-idle evidence; persistent
  P0 is a recorded warning that requires the user's decisive loaded controls
  of at least 30 TFLOPS, adequate headroom, no limiter, and stable brackets.
- The source now removes two redundant square roots from every enabled
  caster-radius test and matches UE's squared-radius versus squared-distance
  comparison directly. It also skips the shared light-space shape construction
  when that same radius test has already rejected a caster for every cascade.
  These source-only changes supersede the candidate hash above and have not yet
  been rebuilt because the thermal gate failed.
- The final GPU source audits found no material clear, array-layout, transition,
  depth-state, alpha-material, or receiver-filtering defect. The remaining
  source-backed GPU opportunities are deliberately reversible: a precomposed
  receiver transform is expected to save only a few thousandths of a
  millisecond, while a conservative saturated-slope branch is the only
  remaining candidate with a credible several-percent raster benefit.
- The precomposed receiver permutation now operates on homogeneous camera clip
  coordinates, composes `clip-to-world` with each cascade's
  `world-to-shadow` matrix once on the CPU, and derives view depth directly
  through `clip-to-view`. Disabling it selects the prior world-space shader
  permutation. Deterministic tests cover permutation mapping, projective
  equivalence, direct view-depth equivalence, profiles, Custom retention, and
  timing identity.
- The conservative saturated-slope permutation now returns UE's same clamped
  maximum slope when squared normalized NoL is at or below one representable
  float step beneath the exact 0.5 saturation boundary. Invalid, degenerate,
  non-finite, and overflowing inputs retain the prior calculation; disabling
  the setting selects the unchanged shader block. Deterministic tests cover its
  boundary, both signs, scaled normals, reference-result equivalence, and
  exceptional inputs.
- The exact Release candidate including the algebraic slow-slope permutation
  is `9CFFA9CCEF9E816B38B1575893DA72180A5829FEBD76204AA676F37232B89BFD`.
  All 22 CSM shader tasks compile, the focused CSM test passes, all 15 Release
  CTest cases pass, the title checker passes all 555 headings and lead-ins,
  all eight frozen Bend hashes match, the submodule remains pinned, no change
  is staged, and whitespace checks pass.
- DXC inspection confirms that the conservative slope permutation emits a real
  control-flow edge around the reciprocal square root, square root, and divide.
  The combined precomposed and pre-normalized receiver permutation is 140 bytes
  smaller than the fully legacy receiver blob and removes six static fused
  multiply-adds, two divides, two dot products, and two reciprocal square roots.
  The disabled precomputed-axis and legacy receiver DXIL binaries are
  byte-for-byte identical to their prior candidate binaries, proving that both
  new toggles retain neutral reference paths rather than merely similar source.
- UE's compact visible-command sorting remains a measured-only CPU
  contingency. It is expected to save roughly 0.01 to 0.04 milliseconds in
  Position 1, but the last clean CPU center was already at the conservative
  threshold and the change cannot improve the remaining GPU gap.
- The next low-frequency preflight was rejected without launching a renderer.
  Process, power, limiter, adjusted-CPU, and adapter-identity gates passed, but
  the GPU hotspot reached 55.75 degrees Celsius, 0.75 degrees above the fixed
  cold ceiling, while the otherwise idle adapter again remained in P0. The
  machine returned to unmonitored cooldown immediately.
- The final UE 5.6 source audit found no unconditional full-redraw bug. Its
  highest-value remaining GPU contingency is now implemented source-only as
  an independent algebraic slow-slope permutation. For finite inputs it
  evaluates the same clamped tangent as the ratio of the normal component
  perpendicular to the light-depth axis over its parallel component, removing
  one reciprocal square root. Invalid or overflowing inputs retain the exact
  preceding calculation, and disabling the new toggle selects the prior
  shader block.
- Individual DXC recompilation with the same shader model, optimization, and
  16-bit-type flags proves that all three algebraic-disabled depth variants
  remain byte-for-byte identical to the previous candidate: legacy depth-axis
  hash `52010CB34BDE75065FE6671F09687803CC4F86F713F85ACADA628A5A05FE3C48`,
  precomputed-axis hash
  `DDA495834E3C52FBF03EDEDFFF45A4FCD8CA68E41678F23C7B5CFF02F14D897B`,
  and saturated-slope hash
  `FF47C1AC28A412308020518CC9BE19DAAF0DCAE3F6DE5BB41745D4C6AB6799EB`.
- The diagnostics review found that the 256-byte generic stat formatter
  truncated CSM optimization state before shared projection, receiver
  scissor, radius culling, and cache gating. Its local stack buffer is now 512
  bytes, which preserves the complete line without changing renderer work.
- The next interactive runtime attempt is rejected evidence. Although its live
  stat line stayed near 33 to 37 TFLOPS, the renderer remained active while the
  UI was configured and NVIDIA telemetry subsequently reported 86 degrees
  Celsius GPU core, 106 degrees Celsius memory junction, zero thermal
  headroom, and active software thermal slowdown. No timing from that interval
  is accepted.
- The current candidate adds an independently reversible translation-only
  caster transform permutation. Exact finite identity-linear, single-instance
  casters push only their world translation; rotated, scaled, sheared,
  reflected, deforming, merged, stale, or unindexed casters retain the prior
  instance-buffer path. The registry is pre-sized from the current Donut scene
  instance list and rejects its signed negative unindexed sentinel before any
  conversion or indexing.
- A narrow command-line benchmark harness now applies and validates the exact
  four-cascade state before the first rendered frame. It rejects simultaneous
  SVSM, debug-runtime, validation-layer, or DRED use; locks the standardized
  scene, camera, light, and renderer configuration; and preserves an exact
  1920-by-1080 client area instead of the earlier work-area-fitted 1902 by
  1069. A separate flag selects the translation-only baseline, and detailed
  timing can be requested only for a non-headline coverage run.
- Release candidate
  `C8F26C873C971A47C1F92D26B29CDEBD5954D7A9B942D1BF722A428292004559`
  builds successfully, its focused deterministic CSM test passes, and all 15
  Release CTest cases pass. Compiled DXIL inspection confirms that the three
  structured instance-buffer row loads remain control-dependent in legacy
  mode and are skipped by translation-only mode.
- The harness arms only after initial scene dirtiness drains, rejects later
  caster or alpha-material changes before rendering them, refreshes the scene
  graph before validating the directional-light transform, and verifies the
  exact Position 1 camera pose rather than only its Static mode and field of
  view.
- The post-build cold preflight timed out without launching a renderer. Its
  final sample reported a 60-degree GPU core and 68.406-degree hotspot, so the
  strict 55-degree cold gate correctly rejected the machine. External CPU also
  averaged 21.218 percent over the final rolling window, just above the
  20-percent limit. Runtime evidence for this candidate remains pending a
  restart or genuinely cold interval.
- The historical 1.122-to-1.149-millisecond candidate was a genuine
  four-cascade, 2048-resolution, D16, full-redraw, manual-5-by-5 run at roughly
  36.8 to 37.2 TFLOPS; it was not a cached or single-cascade preset accident.
  It is not a valid performance target for the corrected implementation:
  it submitted only 407 candidate casters versus 483 in the later accepted
  coherent run, and subsequent fixes changed projection bounds, snap
  displacement, near-plane caster handling, bias/fade semantics, cache
  provenance, and stale resource bindings. Its timing-times-throughput work
  proxy is about 23 percent below the accepted corrected run.

## Current Measurements

The first runtime block used Release binary
`E9EA5210C4399E988A3C4726E0610AA7E936AB44E4F45A66B3FDF8567FA900AE`,
Benchmark Position 1, 1902 by 1069 output, four 2048 D16 cascades, full redraw,
manual UE-style 5 by 5 PCF, detailed timing off, and High process priority.
Every retained sample stayed above the user's 30 TFLOPS gate.

| State | Live TFLOPS | GPU Total | CPU Total | CPU Culling | CPU Recording |
| --- | ---: | ---: | ---: | ---: | ---: |
| Receiver scissor on | 34.7 | 1.510 ms | 0.372 ms | 0.206 ms | 0.118 ms |
| Receiver scissor off | 33.9 | 1.579 ms | 0.391 ms | 0.222 ms | 0.124 ms |
| Receiver scissor off repeat | 33.9 | 1.579 ms | 0.382 ms | 0.228 ms | 0.117 ms |
| Receiver scissor on repeat | 32.7 | 1.602 ms | 0.262 ms | 0.149 ms | 0.082 ms |

Raw GPU time is not comparable across the drifting clock state. The paired
GPU-work proxy of milliseconds multiplied by live TFLOPS was approximately
53.53 with the scissor off and 52.39 to 52.40 with it on, indicating a
repeatable improvement of about 2.1 percent. This is useful but does not by
itself close the provisional GPU target.

The new receiver and caster SAT preparation reduced the stable detailed-off
CPU center from the earlier 0.574 ms total and 0.374 ms culling to roughly
0.38 ms total and 0.22 ms culling. Both are inside the provisional
fifteen-percent upper bands. Normal, cascade-selection, and visibility views
were coherent at Position 1 with the scissor enabled; no block cuts, missing
geometry, seams, or alpha-tested foliage loss were observed.

## Production Freeze Boundary

The CSM implementation is now source- and runtime-ready to freeze. This does
not close the separate matched-UE performance-confidence experiment above; it
means further CSM source changes require a reproducible correctness defect, a
new platform failure, or an explicit new feature request rather than another
speculative optimization pass.

The final addition is an independent **Cached Shadow Draw Lists** control.
Full-redraw CSM modes can retain the exact culled caster list across the eight
TAA jitter phases. The key includes the scene root, pointer-identical
directional light, draw strategy, reliable scene revision, complete normalized
CSM configuration, jittered and unjittered view state, viewport, and every
cascade transform. Full scene invalidation, unreliable revision data, an
ineligible cache policy, or any key mismatch fails closed to a fresh gather.
Leaving the eligible mode releases retained scene ownership and list memory.
The Single Map Reference profile keeps this optimization disabled as the
strict uncached baseline.

SVSM exposes the same user-facing control independently of page caching. When
disabled, the packet list is rebuilt transiently only when GPU packet gating
requires it; it is never silently coupled to page reuse.

Runtime checks on the final Release candidate found:

- CSM cached-list warmup reduced static CPU culling from approximately
  0.403 ms to 0.011-0.036 ms while preserving all 483 final caster pairs and
  an unchanged image.
- With SVSM page caching disabled, enabling the draw-list cache reused 2,430
  packets; disabling it reported zero retained packets and exercised the
  transient rebuild path without changing the image.
- D16 and D32 depth paths both rendered coherent shadows. The reported depth
  allocation changed from 32 MiB to 64 MiB as expected.
- Live resize from 1902 by 1069 to 1920 by 1111 and back, followed by camera
  motion and cache warmup, preserved continuous shadows without a device or
  resource error.

Final executable SHA-256:
`ACD62CC88F2387E68CD3F0623E61E2466EF173803C05945017A223CCDB2FFA38`.
The aggregate Release build and all 15 CTest cases pass. Three independent
read-only audits found no remaining CSM source-level production blocker after
the cache-key, invalidation, bounds, light-frame, lifetime, timer, and
translation-only hardening. The remaining assumptions are UVSR's existing
valid Donut mesh/buffer and scene-revision contracts, modern shader Gather
support for the UE nine-Gather filter, one CSM render per command-list
recording for the current volatile-buffer capacity, and Donut's existing
engine-wide treatment of mirrored one-sided instances.

## Archival Resolution

Superseded on 2026-08-03 when diagnostic cascaded shadows left the product.
Its parity, thermal benchmark, normalization, runtime-matrix, and confidence
criteria remain unfinished. Recovery source is preserved locally on
`codex/svsm-csm-preserved` at `f7c0c87d8cba6880428fbc34400eb2882fb5182e`.

The full historical evidence is preserved above. This archive does not claim
completion of any unchecked runtime, visual, performance, thermal, parity, or
product-acceptance criterion.
