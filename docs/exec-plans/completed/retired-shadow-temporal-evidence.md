# Retired Shadow and Temporal Evidence

## Purpose

This record preserves the technically relevant evidence from five superseded
shadow and temporal execution-plan/report artifacts consolidated on 2026-07-30.
Their implementation-lineage branding and stale active-task instructions were
removed when the current first-party shadow replacement and generically named
temporal integration superseded them.

This is historical evidence only. It does not validate the replacement shader,
its source snapshot, or its performance.

## Prior Screen-Space Shadow Experiment

- Canonical base:
  `a55e215e4bf0eddb20330283d9a4f8e853bda49f`.
- The accepted UI tree at that base was byte-identical to contender
  `87eb377`.
- The canonical Release baseline built and passed 12 of 12 tests.
- The experiment Release candidate built and passed 13 of 13 tests.
- All 45 combinations of five trace lengths, three hard-sample counts, and
  three fade-sample counts compiled.
- The reference test covered defaults, preset reset, variant mapping, and
  positive and negative projected-light `w`.
- The documentation checker self-test and all 380 then-tracked headings and
  bold lead-ins passed.
- Independent rendering re-review found no remaining P0-P2 issue after the
  volatile-buffer ordering repair.

The retired external Apache-2.0 source snapshot was preserved at the time by
these exact identities:

| Historical Component | Bytes | SHA-256 |
| --- | ---: | --- |
| CPU planner snapshot | 12,335 | `23AAE596DBB1B9BDAE23D87AC85079B138426823C3153079F7A7DD36F603D02A` |
| GPU tracer snapshot | 25,289 | `7FBE24BD2040A62536C31DF6CD38A92CB22C172A9A419EED3C0AF7DF1D50A68C` |

### Runtime Evidence

- Wave diagnostics produced coherent full-frame lane ramps with continuous
  dispatch boundaries in on-screen, off-screen, and behind-camera light cases.
- Edge diagnostics stayed dark on flat surfaces and produced thin contours at
  architecture, foliage, and depth discontinuities.
- Thread diagnostics covered the target with stable horizontal lane ramps and
  no gaps.
- On-screen, off-screen, and behind-camera directional-light cases remained
  finite and stable after two integration defects were repaired.
- The adapter used reverse-Z near/far values of 1/0.
- At grazing incidence, the 60- and 120-pixel variants remained stable at
  screen borders and on long masonry ledges. The 240-pixel variant exposed
  increasingly visible screen-space silhouette limitations.
- Odd-resolution, maximize/restore, 480/960-pixel, and complete resize-resource
  validation remained pending.

### Historical Timing

The historical timing window used a 1902x1069 Benchmark Position 1 camera on
an NVIDIA GeForce RTX 4090 Laptop GPU. Each range was the minimum and maximum
of at least three warmed UI samples. These figures belong only to the retired
implementation and are not a baseline for the replacement.

| Length | Trace GPU Time | Frame-Time Delta | Notes |
| --- | ---: | ---: | --- |
| 60 | 0.098-0.106 ms | Pending | Four hard and eight fade samples |
| 120 | 0.183-0.203 ms | Pending | Compiled validation variant |
| 240 | 0.347-0.362 ms | Pending | Long profile |
| 480 | Pending | Pending | Compiled validation variant |
| 960 | Pending | Pending | Maximum validation profile |

The first enabled launch found and repaired two D3D12 adapter defects: an
invalid optimized clear value on a non-render-target `R8_UNORM` UAV, and a
volatile constant buffer bound before its first write. The corrected build
completed the recorded diagnostic and directional-light matrix without another
validation error.

## Prior Temporal Canon Contender

- Live canonical base:
  `9f2e69e832c81c105b059a83e4e72daa25b15783`.
- Accepted renderer:
  `3266edf190665c9f9162bad452477bbcdf42c1a8`.
- Accepted renderer tree:
  `2aea4ecc7adf674f9ab8a2635866bc13edf77acb`.
- Canonical publication record:
  `3a4be88d3f410a572b3363427244ee89056b54b9`.
- Accepted executable SHA-256:
  `3AE1D454E8BFC3FEF9CA71BB2888EA6E33B9D0BA7E6DE6A94C223EE647967C12`.
- Accepted executable size: 2,088,448 bytes.
- Exact-commit Release rebuild: passed.
- Registered CTest suite: 12 of 12 passed before publication and again during
  publication preflight.
- Documentation checker: self-test and all 317 then-tracked headings and bold
  lead-ins passed.
- `git diff --check`: passed on the clean publication worktree.
- Independent integration re-review found no remaining P0-P2 issue after the
  visibility-history compatibility guard.
- The normal launch stayed interactive in `Freelook`, with free camera
  locations labeled `Piloted`.
- The accepted contender runtime and the post-publication canonical runtime
  were responsive using the exact accepted executable bytes.
- The user explicitly accepted the exact contender artifact and separately
  authorized its publication to `main` on 2026-07-17.

The publication was a clean fast-forward. No pull request, merge commit,
conflict repair, rebase, force update, or history rewrite changed the accepted
renderer snapshot during publication.

## Prior Anti-Aliasing Options Experiment

- The developer Release renderer and shader bundle rebuilt successfully.
- All 20 developer tests passed.
- The production configuration rebuilt 2,211 shader tasks, linked the Release
  renderer, and passed all 21 production tests.
- Temporal, CMAA2, and every MSAA quality were exercised without a crash.
- The experiment confirmed safe 16x MSAA fallback, deferred popup selection,
  and a centered zoom-only presentation crosshair.
- The README line-count check and documentation Title Case self-test/full scan
  passed before commit.
- Direct interactive review covered animation, dropdowns, scrolling,
  visibility, statistics, magnification, camera, and anti-aliasing behavior.
  The user reported no remaining defect before separately authorizing
  canonical publication on 2026-07-22.

## Supersession

The first-party directional-shadow tracer and current temporal cost profiles
must be verified from
[`taa-shadow-cleanroom.md`](taa-shadow-cleanroom.md) after closeout. No
historical timing or product acceptance in this record transfers to the new
artifact.
