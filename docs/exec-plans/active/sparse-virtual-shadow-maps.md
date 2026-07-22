# Sparse Virtual Shadow Maps: Separate Directional Visibility

## Status

- State: active
- Coordinator: `/root`
- Project branch: `codex/bend-screen-space-shadows`
- Base commit: `a55e215e4bf0eddb20330283d9a4f8e853bda49f`
- Started: 2026-07-20
- Last updated: 2026-07-22
- Planned archive:
  `docs/exec-plans/completed/sparse-virtual-shadow-maps.md`

## Goal and Done Condition

Goal:

Add the smallest safe Sparse Virtual Shadow Map implementation that follows
K. T. Stephano's published directional-clipmap design, preserves the existing
uncommitted Bend experiment byte-for-byte, and keeps Bend and SVSM independently
portable behind a producer-neutral deferred-lighting seam. SVSM resolves its
own full-resolution linear `R8_UNORM` visibility before deferred lighting.
Refine the cached implementation until a thermally valid benchmark
at camera position 1 measures at most 0.4 ms total SVSM GPU cost both for a
fully static scene and while the motion benchmark turns exactly 0.1 degrees per
rendered frame, without unstable or visibly broken shadows.

Done when:

- [ ] Dense reference mode renders all virtual pages without caching.
- [ ] Sparse mode uses camera-depth marking, a fixed software-managed physical
  pool, GPU allocation, clearing, rendering, and coarse fallback.
- [ ] Cached mode reuses pages through page-aligned two-dimensional wraparound,
  invalidates all required causes, and renders zero pages after warmup for a
  fixed camera, sun, and static scene.
- [ ] Pointer-identical light application, white fail-open behavior, and generic
  same-light visibility multiplication pass deterministic and runtime checks.
- [ ] All reference, sampling, marking, cache, profile, debug, timing, resize,
  and resource-recreation requirements are represented
  by independently controllable settings and evidence.
- [ ] The Release build, full CTest suite, title-case checker, Bend hash
  verification, diff check, source review, and available runtime matrix pass.
- [ ] Three repeatable, thermally accepted position-1 runs measure no more than
  0.4 ms median total SVSM GPU cost for the static and motion benchmarks, with
  stable shadows, no recurring multi-millisecond page-rendering spikes, and no
  page, clipmap, filter, or cache artifacts. Tail latency is reported with p95,
  p99, worst-frame, and per-stage slow-frame evidence rather than inferred from
  the median.
- [ ] No completion claim is made until the runtime matrix passes.

## Scope

In scope:

- A first-party directional SVSM pass with six logical clipmaps.
- A dense reference backend, then a fixed-pool sparse backend, then caching.
- `R32_UINT` reverse-Z atomic depth storage using clear zero and
  `InterlockedMax`.
- Existing UVSR opaque and alpha-tested geometry drawing through a narrow
  first-party adapter.
- Full-resolution `R8_UNORM` resolve before deferred lighting.
- A producer-neutral two-slot exact-light visibility interface in the UVSR PBR
  deferred pass.
- Independent marking, sampling, caching, filtering, debug, timing, and
  speed-to-quality profile controls.
- Deterministic mapping, allocation, invalidation, filtering, composition, and
  recreation tests.
- Technical design, implementation, validation, and limitation reporting.

Non-goals:

- A general virtual texture system.
- Changes below `donut/`, `nvrhi/`, or any other submodule.
- Native sparse/tiled resources as a requirement.
- Meshlets, bindless rendering, a visibility buffer, a new scene database, or
  a renderer-wide architecture change.
- GPU readback in the cached correctness path or one CPU draw call per page.
- Native `D16` or `D32` pages before the atomic reference path is correct.
- Static and dynamic cache separation before the single-cache path is correct.
- Cross-producer optimization hints, inputs, resources, settings, UI state, or
  benchmark state.

Affected subsystems and paths:

- New `src/sparse_virtual_shadow_map*` CPU, shared, and HLSL files.
- `src/uvsr.cpp` for ownership, ordering, controls, and presentation.
- `src/pbr_deferred_lighting_pass.*` and
  `src/pbr_deferred_lighting_cs.hlsl` for the separate exact-light visibility
  input.
- `src/shaders.cfg` and `CMakeLists.txt` for first-party build registration.
- New focused tests and this plan/report.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `src/shaders.cfg`, `src/uvsr.cpp`, the PBR deferred pass,
  settings/presets, and all CPU/HLSL shared layouts.

## Baseline

- Canonical repository/remote: local target tracks `origin/main`.
- Local versus remote state: branch head equals `origin/main` at `a55e215`;
  the worktree contains the uncommitted Bend experiment and related
  documentation changes.
- Verified source commit/build: source identity recorded; existing candidate
  reports a passing Release build and CTest in its Bend execution plan, but
  this task will rerun combined verification.
- GPU, scene, camera, resolution, and settings preset: Benchmark Position 1 at
  1920 by 1080 is the required control; the complete cold baseline is pending a
  verified CPU cooldown. Exactly one renderer and at most one task-owned frame
  capture may run.
- Thermal gate on the current laptop: CPU at or below 75 C and GPU at or below
  55 C for 30 quiet seconds, CPU performance-limit flags zero, CPU performance
  limit 100 percent, no live GPU thermal limiter, and a five-sample adjusted
  external CPU average at or below 20 percent after subtracting the renderer and
  live ChatGPT/Codex process trees. Cumulative NVIDIA thermal counters are
  diagnostic only on driver 610.47 because they advance at idle.
- User-occupancy gate: unexplained CPU or GPU activity means the laptop is in
  use. No build, UVSR launch, benchmark, or opportunistic test may start until
  the user explicitly provides a testing window and the complete quiet
  preflight passes for its full duration. Source-only review continues while
  the machine is occupied; agents do not poll repeatedly for a quiet dip.
- Monitoring package: pinned LibreHardwareMonitor 0.9.6 and PresentMon 2.5.1
  are downloaded under ignored `work/` storage with release hashes, complete
  extraction manifests, licenses, and available signatures verified. HWiNFO
  Portable 8.50 is also downloaded and signature-verified but has not been run;
  it remains optional because it temporarily loads a driver and has separate
  license constraints. The current firmware exposes no live CPU temperature to
  LibreHardwareMonitor, so runs use the documented reduced-evidence exception
  while retaining every other thermal, load, process, power, and bracket gate.
- Known pre-existing failures: none recorded. The existing Bend runtime matrix
  still contains pending rows and must not be misreported as complete.
- Preservation rule: every pre-existing modification and untracked file is
  retained. Bend source hashes were captured before SVSM edits.

## Dependencies and Interfaces

| Dependency Or Reference | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Local NVRHI | Pinned `8e8c36e37558acec333204619b95d9d2fcdc4a79`; no update | Verified | Resource, UAV, indirect APIs |
| Local Donut | Pinned tree; no edits | Verified | Scene, materials, draw batches, views |
| Stephano article and source | Full article, media, code, and limitations | Reviewed | Reference architecture |
| StratusGFX branches | Treat the WIP implementation and disabled-cache history as evidence only | Reviewed | Packing and failure cross-check |
| Timberdoodle | Cross-check allocation, wrap, invalidation, dirty tracking, and sampling only | Reviewed | Sparse/cache design |
| Epic VSM documentation | Production invalidation and pool-exhaustion lessons | Reviewed | Safety and validation |
| Bend release and adapter | Byte-for-byte preservation and separate light identity | Frozen | Deferred composition |

Public interface, shader binding, resource layout, and settings contracts:

- SVSM result: `{ visibilityTexture, exactDirectionalLight }`; both are present
  or both are absent.
- Deferred lighting binds two neutral full-resolution visibility SRVs and
  compares each stored light pointer independently. Bend and SVSM know nothing
  about one another.
- Missing, invalid, disabled, unsupported, over-budget, exhausted, or
  out-of-range SVSM samples resolve through coarser valid clipmaps, then white.
- The page table has one logical 64-by-64 slice per clipmap. Page metadata
  includes physical index, resident, required, dirty, and a frame-age marker.
- Physical depth is a normal fixed `R32_UINT` pool. Reverse-Z depth is cleared
  to zero and updated with `InterlockedMax`.
- Virtual resolution is 8192 square, page size is 128 square, clipmap render
  origins move only in page increments, and coverage doubles per level.
- Receiver code never assumes neighboring virtual pages are physically
  adjacent.
- Every optimization is an independent setting; reference settings stay
  available.

## Assignment Summary

| Task ID | Owner | Branch And Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| research | `/root` | Target worktree, read-only | `a55e215` plus dirty Bend state | Plan and report only | Published and local sources | Complete |
| dense-reference | `/root` | Target worktree | Preserved dirty baseline | First-party SVSM and integration files | Research | Implemented; runtime validation pending |
| sparse-pool | `/root` | Target worktree | Dense implementation | SVSM files and focused integration | Dense reference | Implemented; comparison matrix pending |
| cache | `/root` | Target worktree | Sparse implementation | SVSM files and tests | Sparse pool | Implemented; invalidation matrix pending |
| producer-decoupling | `/root` | Target worktree | Cached implementation | SVSM, neutral deferred seam, UI, tests, and docs | Cache | Source implemented; runtime validation pending |
| validation | `/root` | Target worktree | Frozen local candidate | Evidence artifacts and task-defect repairs | All implementation phases | Active; thermal gate blocks runtime only |

## Assignment Contracts

### Research: Establish the Source-Grounded Design

- Owner/thread: `/root`
- Branch/worktree: target worktree, read-only research
- Base commit/state: `a55e215` plus preserved Bend changes
- Read scope: all user references, all Stephano repositories/branches, pinned
  local dependencies, UVSR render paths, and existing Bend experiment
- Write scope: this plan and technical report
- No-touch scope: dependency files, Bend files, Git history, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: distinguish published design, public WIP code,
  cross-check code, and UVSR-specific inference
- Deliverable: design decisions and proof obligations
- Done when: the reference sweep and UVSR mapping are complete
- Required verification: article/source/media/comment/code cross-check
- Allowed Git and external actions: read-only web and repository inspection
- Stop and report if: a source contradicts a mandatory user requirement
- Handoff revision/artifact:
  `outputs/sparse-virtual-shadow-maps-technical-report.md`
- Handoff acknowledged by/on: coordinator, 2026-07-20

### Dense Reference: Implement the Fail-Open Baseline

- Owner/thread: `/root`
- Branch/worktree: target worktree
- Base commit/state: preserved dirty baseline
- Read scope: full first-party renderer and pinned APIs
- Write scope: new SVSM files, narrow PBR/UVSR integration, build registration,
  tests, plan, and report
- No-touch scope: every `bend_screen_space_shadows*` file, dependencies, main,
  Git history, remote state, and unrelated changes
- Build directory and runtime/GPU/resource lease: existing local build; one
  UVSR runtime at a time
- Dependencies already integrated: research contract
- Interface/invariant contract: all pages backed, no caching, no Bend
  optimization, separate exact-light visibility, white on any invalid state
- Deliverable: selectable dense reference mode and deterministic CPU tests
- Done when: build/tests pass and dense debug/resolve behavior is observable
- Required verification: mapping, reverse-Z, presets, composition, resources,
  build, CTest, and runtime inspection
- Allowed Git and external actions: local file edits/build/tests/run only
- Stop and report if: implementation requires a dependency or Bend edit, a
  destructive operation, or a per-page CPU draw loop
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Sparse Pool: Add GPU-Managed Residency

- Owner/thread: `/root`
- Branch/worktree: target worktree
- Base commit/state: verified dense reference
- Read scope: dense implementation and source-grounded allocation references
- Write scope: SVSM allocation, marking, clearing, rendering, resolve, tests,
  plan, and report
- No-touch scope: dense availability, Bend, dependencies, unrelated renderer
- Build directory and runtime/GPU/resource lease: existing local build/runtime
- Dependencies already integrated: dense reference
- Interface/invariant contract: fixed pool, no correctness readback, conservative
  requests, coarse fallback, white if all levels fail
- Deliverable: per-pixel and conservative tiled marking plus sparse rendering
- Done when: dense/sparse comparison and pool exhaustion tests pass
- Required verification: allocation, clearing, rendering, fallback, boundaries,
  odd sizes, resize, and exhaustion
- Allowed Git and external actions: local file edits/build/tests/run only
- Stop and report if: fixed-pool correctness requires CPU page decisions
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Cache: Add Stable Reuse and Invalidation

- Owner/thread: `/root`
- Branch/worktree: target worktree
- Base commit/state: verified sparse pool
- Read scope: sparse implementation and invalidation references
- Write scope: SVSM cache/invalidation code, tests, plan, and report
- No-touch scope: uncached reference modes, Bend, dependencies
- Build directory and runtime/GPU/resource lease: existing local build/runtime
- Dependencies already integrated: sparse pool
- Interface/invariant contract: page-aligned origins, toroidal reuse, newly
  exposed strip invalidation, conservative caster invalidation, full light or
  mapping invalidation, GPU-managed allocation
- Deliverable: cache with observable residency, dirtiness, and reuse
- Done when: fixed static view reaches zero page renders after warmup
- Required verification: motion, teleport, moving sun/geometry, age, eviction,
  fallback, and recreation
- Allowed Git and external actions: local file edits/build/tests/run only
- Stop and report if: cache reuse cannot be proven without driving correctness
  from readback
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### Producer Decoupling: Keep Visibility Projects Independent

- Owner/thread: `/root`
- Branch/worktree: target worktree
- Base commit/state: cached SVSM with the earlier cross-producer prototype
- Read scope: SVSM, deferred lighting, renderer orchestration, UI, benchmark,
  tests, and current documentation
- Write scope: first-party SVSM, neutral deferred seam, UI, benchmark, tests,
  and documentation
- No-touch scope: all Bend-owned files and behavior
- Build directory and runtime/GPU/resource lease: existing local build/runtime
- Dependencies already integrated: cached SVSM
- Interface/invariant contract: SVSM and Bend never include, bind, sample,
  configure, benchmark, or name one another; the renderer adapts their results
  into neutral exact-light factors; every incomplete factor is white
- Deliverable: independent producers, no cross-producer early reject, and
  Performance, Balanced, Quality, and Custom speed-to-quality profiles
- Done when: source searches are clean, frozen Bend hashes match, generic
  composition passes, and the runtime matrix confirms stable output
- Required verification: focused composition/profile tests, Release build,
  frozen hashes, independent review, and runtime checks
- Allowed Git and external actions: local file edits/build/tests/run only
- Stop and report if: fine-caster exclusion cannot be proven conservative from
  existing data
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

## Integration Order

1. Freeze and later recheck pre-existing Bend file hashes and worktree changes.
2. Add deterministic layout, mapping, reverse-Z, profile, and composition tests.
3. Add the separate SVSM result and deferred-light identity binding, with a
   white default.
4. Implement and validate dense reference resources, rendering, and resolve.
5. Implement per-pixel marking and fixed-pool GPU allocation, clear, render,
   and fallback.
6. Add conservative 8-by-8 and 16-by-16 tile marking.
7. Add page-aligned origin movement, toroidal reuse, invalidation, eviction, and
   cache counters.
8. Add 1, 4, 8, and 16 tap page-safe filters and optional hybrid filtering.
9. Remove the earlier cross-producer prototype and expose speed-to-quality
   profiles over producer-local SVSM settings.
10. Complete debug/timing surfaces, resize/recreation handling, source review,
    automated checks, and runtime matrix.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Existing work preserved | Pre/post hashes and diff ownership audit | SHA-256 and Git diff inspection | All six Bend source hashes match the frozen baseline; final audit pending |
| Page packing and mapping | Deterministic expected values | Focused unit tests | Passed, including compact render-page owner/physical packing |
| Reverse-Z atomic depth | Clear zero and nearest-wins ordering | Deterministic CPU model plus shader inspection | Passed focused test; runtime comparison pending |
| Dense versus sparse | Matching visibility under full residency | Fixed scene image comparison | Pending |
| Cache off versus on | Matching output and zero static rerenders after warmup | Fixed scene counter/capture | Pending |
| Exact light identity | Only pointer-identical directional light is affected | Focused test and multi-light runtime | Pending |
| White fail-open | Missing, invalid, exhausted, and out-of-range states are white | Focused tests and debug runtime | Pending |
| Generic factor composition | Matching exact-light factors multiply; unmatched and incomplete factors are white | Focused PBR test and runtime multi-light view | Focused test passed; runtime pending |
| Page-safe filtering | No physical-neighbor assumption across boundaries | Focused synthetic test | Passed focused boundary test; runtime image comparison pending |
| Marking modes | Conservative, deduplicated requests | Per-pixel/8-by-8/16-by-16 comparison | Pending |
| Cache invalidation | Every requested motion/change case dirties required pages | Deterministic tests and runtime matrix | Pending |
| Resource recreation | Resize and live setting changes remain valid | Odd-resolution/live-resize loop | Recreation-key test passed; runtime loop pending |
| Debug and counters | Every requested state is inspectable | Debug view/counter audit | Pending |
| Timings | Each requested pass and total cost measured separately | GPU timer query audit | Detailed diagnostic mode and total-only benchmark mode pass source and focused-test review; runtime comparison pending |
| Automated verification | Release build, full tests, title scan, hashes, diff | Repository commands | Latest Release app, packed scatter shader permutations, and focused SVSM test pass; full CTest, title scan, hashes, and final diff audit remain pending |
| Runtime matrix | Both requested GPUs when available; no estimates | Controlled runtime matrix | RTX 4090 Laptop enable regression passed; full matrix and iGPU pending |
| Position-1 control | Cold TFLOPS, clocks, temperatures, frame time, memory, and repeatability | Three runs of at least 1,000 frames or 15 seconds | Pending verified CPU cooldown |
| Static performance | Stable visibility, zero page rerenders after warmup, at most 0.4 ms median | Thermally bracketed position-1 static benchmark | Pending |
| Motion performance | Stable visibility at exactly 0.1 degrees per rendered frame, at most 0.4 ms median | Thermally bracketed motion benchmark | Pending |

Performance evidence records the exact source state, GPU, scene, camera,
resolution, preset, warmup, sample count, pass timings, total shadow time,
correctness guardrails, and raw artifact. Unavailable hardware is reported as
unmeasured.

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-22 | Fully decouple Bend and SVSM behind a neutral two-slot deferred-lighting interface. | The Bend-assisted resolve read could only avoid final filtering, could not reduce dominant culling or rendering work, and disabled static full-resolution visibility reuse. A separate composition dispatch and a general bindless visibility system were rejected as extra cost and architecture. Both producers now expose only independent full-resolution factors and exact light pointers; deferred lighting performs the optional same-light product. | Integration, SVSM resolve/cache, UI, profiles, and motion benchmark |
| 2026-07-22 | Replace implementation-named SVSM presets with Performance, Balanced, Quality, and Custom profiles. | Working reference modes remain available through the backend and Custom controls. User profiles now express the actual speed-to-quality trade: 8 taps with plus-one bias, unbiased adaptive 8 taps, or unbiased full 16 taps. All use validated no-work cache and submission paths. | Settings, UI, tests, and documentation |
| 2026-07-20 | Use a separate SVSM visibility texture and exact-light pointer. | It satisfies the requested producer boundary and avoids Donut's four-cascade receiver. Encoding SVSM into Bend channels or `IShadowMap` was rejected. | Integration and resolve |
| 2026-07-20 | Use normal NVRHI `R32_UINT` resources and a software-managed fixed pool. | This matches the required reference backend without dependency changes. Native sparse resources and an NVRHI fork were rejected. | Dense and sparse backends |
| 2026-07-20 | Translate Stephano's conventional-Z atomic minimum to reverse-Z clear zero plus atomic maximum. | Non-negative IEEE-754 `[0,1]` values preserve unsigned bit ordering. | Depth backend and tests |
| 2026-07-20 | Keep all allocation and eviction decisions on the GPU. | Readback may expose delayed diagnostics, but cannot drive the final cached path. | Sparse pool and cache |
| 2026-07-20 | Reuse existing material batches with page-list instancing. | One draw per existing geometry/material batch avoids one draw per page while preserving opaque and alpha-tested paths. Meshlets and bindless rewrites were rejected. | Page rendering |
| 2026-07-20 | Use an attachmentless framebuffer for atomic raster depth. | The SVSM pixel shaders write only the `R32_UINT` UAV. Removing the redundant 8192-square `R8_UNORM` render target fixes D3D12 `E_INVALIDARG` without changing NVRHI. | Dense and sparse raster resources |
| 2026-07-20 | Compact scheduled render pages per clipmap and drive raster instance counts from GPU counters. | Expanding every scene batch across all 4096 physical pages for each clipmap caused a first-frame device removal. GPU-counted indirect draws preserve unlimited reference budget and avoid CPU work per page. | Sparse page rendering |
| 2026-07-20 | Treat public Stratus VSM branches as WIP evidence. | Later Stratus history explicitly disables caching for a major bug and carries an allocation workaround. Direct transplantation was rejected. | Cache design |
| 2026-07-20 | Fail open under pool exhaustion. | White or a valid coarser page preserves correctness and avoids Epic's documented corruption failure. | Allocation and resolve |
| 2026-07-20 | Defer optional native depth and split caches. | Both are optimizations after the atomic single-cache reference path is correct. | Scope control |
| 2026-07-21 | Gate every performance claim on a cold position-1 control. | Low stat-line TFLOPS and cross-program degradation can indicate thermal throttling, while per-process memory growth can indicate a leak. Bracketing tests and excluding duplicate renderer instances separates these causes. | Validation and reporting |
| 2026-07-21 | Keep HWiNFO optional and never run it implicitly. | Its portable build is cross-vendor on Windows but temporarily loads a kernel driver, automated logging is licensed separately, and it must not be redistributed with UVSR. | Thermal telemetry |
| 2026-07-21 | Add packet-to-dirty-page compaction behind an independent default-off toggle. | Static packet caches still submit every dirty page to every caster. Conservative per-packet page lists can remove that product term while all invalid, over-limit, allocation-failure, and overflow states retain the full-page reference path. | Cached sparse rendering |
| 2026-07-21 | Treat the eight TAA jitter requests as an exact union rather than a page halo. | Preserving only the exact previously observed requests eliminates large conservative overmarking while numeric jitter matching handles signed zero. Extending the union invalidates every visibility slice because allocation or eviction can change fallback. | Static cache and marking |
| 2026-07-21 | Disable static visibility reuse when Bend early reject consumes live Bend output. | Historical prototype decision, superseded on 2026-07-22 by complete producer decoupling and removal of the early reject. | Historical Bend optimization and static visibility |
| 2026-07-21 | Require a full-duration, thermally clean measurement window. | A clean suffix cannot redeem earlier contamination. Preflight may wait for cooldown, while measurement latches any failed sample and covers an explicit duration. | Thermal telemetry and benchmarks |
| 2026-07-21 | Treat unexplained utilization as user occupancy. | High load commonly means the user is working in another program. Agents must not compete, stop unrelated work, or launch on a brief utilization dip. | All builds and runtime validation |
| 2026-07-21 | Treat a render budget at least as large as the physical pool as effectively unlimited for static request reuse. | At most one dirty owner per physical page can be scheduled in a frame, so a pool-sized budget drains every resident dirty page before reuse. Requiring literal `UINT_MAX` needlessly forced marking, allocation, and culling forever. | Static cached performance |
| 2026-07-21 | Keep Bend Hybrid on per-pixel page marking. | Historical prototype decision, superseded on 2026-07-22 by producer-local speed-to-quality profiles. Per-pixel marking remains the safe production default. | Historical profile and motion benchmark |
| 2026-07-21 | Stable-sort batched packets only behind an independent toggle. | Opaque packets can share buffer and raster state without material identity, while alpha-tested and nonbatchable packets retain exact material state and singleton fallbacks. | Motion packet submission |
| 2026-07-21 | Gate empty batched clipmaps with a per-level GPU count word. | A zero dirty-page count now parses zero indirect commands, while any nonzero count publishes `UINT_MAX` so D3D12 and pinned NVRHI clamp to the complete draw group. Using the dirty-page count directly was rejected because it can truncate groups whose packet count is larger. | Sparse page rendering |
| 2026-07-21 | Keep motion refinements independent of named presets. | The automatic motion harness opts into packet-state sorting, empty-level skipping, packet-page culling, small-rectangle direct page-table scans, recent-page eviction grace, and finite-budget allocation-saturation probing. Named presets retain their requested behavior and keep every refinement independently disableable for comparison. | Motion benchmark and reference availability |
| 2026-07-21 | Raster each accepted caster packet once into virtual clipmap space behind an independent scatter toggle. | The old path repeats complete packet geometry for every dirty page, which made cold-page rendering dominate p99 and worst frames. The new path clips one packet draw to its conservative scheduled-page rectangle, translates each surviving pixel through the page table, validates exact render ownership, and atomically scatters reverse-Z depth into the fixed physical pool. The original per-page instanced path remains the default reference path. | Sparse page rendering and spike reduction |
| 2026-07-21 | Pack scatter culling at 64 packets per compute group and omit exact page-list storage. | Scatter needs one conservative packet/global-rectangle intersection and never consumes the exact per-packet page list. Packing removes 63 idle lanes per packet; bounds-only metadata plus a one-word bound resource removes the 16-million-entry false limit. Exact-list mode and its cache key remain intact for the reference path. | Sparse culling and spike reduction |
| 2026-07-21 | Use one outer GPU query for accepted motion benchmarks and keep detailed stage queries as an independent diagnostic mode. | Pinned NVRHI resolves every ended query immediately, so six inner query resolves inside the measured interval can perturb a sub-millisecond tail. Total-only mode preserves exact total evidence without that workload; detailed mode remains selectable for attribution and reference validation. | Timing and spike diagnosis |
| 2026-07-21 | Require both the 0.4 ms median target and a 0.7 ms worst-frame ceiling. | Median-only acceptance previously labeled a 1.188 ms worst-frame run successful. The benchmark now also requires its requested packet/scatter path to be active and reports counts above 0.4, 0.7, and 1.0 ms. | Motion benchmark evidence |
| 2026-07-21 | Probe an already-saturated finite allocation budget before reserving behind an independent toggle. | The render-reservation counter is monotonic within one allocation frame. A relaxed stale-low read falls through to the unchanged atomic and post-check, while a saturated read safely removes redundant contention. Coarsest fallback, unlimited budgets, allocation ownership, age updates, and the default reference path are unchanged. | Sparse allocation tail latency |
| 2026-07-21 | Use a 20 percent adjusted external CPU gate and run UVSR at Windows High priority. | ChatGPT/Codex process trees are measurement infrastructure and are subtracted, while builds and captures remain hard blockers. High—not Realtime—priority is requested fail-soft at startup and becomes part of benchmark identity. | Thermal telemetry and benchmark repeatability |
| 2026-07-21 | Hard-bound dirty scatter with one independently budgeted all-level reservation. | Two automatic motion launches produced NVIDIA `0x141` engine timeouts during cold scene setup. Source review found no counter, indirect-range, or barrier defect, but the coarsest exemption and fail-open virtual rectangle could bypass the existing ratio guard. The reference preset keeps the published coarsest exemption. Scatter now activates only when its guard is enabled, a nonzero budget of at most four pages includes coarsest, and budget times amplification is at most 16 pages; guarded invalid metadata uses those compact pages instead of a full virtual clipmap. | Motion safety and spike reduction |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-22 | Bend/SVSM decoupling `/root` | Source And Automated Validation Complete; Runtime Matrix Pending | Local uncommitted worktree | Bidirectional source scans are clean; separate Bend-only and SVSM-only static-library configurations build and pass their reference tests; all six frozen Bend hashes match; generic exact-light, fail-white, same-light multiplication, packing, profile, and Custom-retention tests pass; Release renderer and modified DXIL permutations build; full CTest is 14/14; title-case and diff checks pass; bounded launch showed a normal frame and separate drawers without an error dialog | Run Bend-only, SVSM-only, combined image checks and the thermal performance matrix when the UI is not under active human control |
| 2026-07-20 | Research `/root` | Complete | Technical report | Full source/media/comment/repository sweep | Implement dense foundation |
| 2026-07-20 | Integrated implementation `/root` | Validation Active | Local uncommitted worktree | Release build, focused tests, and full CTest pass | Complete runtime matrix and source audit |
| 2026-07-20 | SVSM enable regression `/root` | Passed on Available Discrete GPU | `svsmfix-a55e215-0728` | RTX 4090 Laptop, PBR Sponza Decorated, Stephano Reference, 4096 pages, unlimited budget, moving piloted camera; responsive beyond prior device-removal point | Continue dense/sparse, cache, boundary, resize, toggle, and iGPU matrix |
| 2026-07-21 | Thermal and benchmark hygiene `/root` | Source And Tooling Complete; Runtime Gate Pending | `AGENTS.md`, thermal/tool scripts, ignored monitoring binaries | Scripts parse; pinned assets, full extractions, release digests, signatures, smoke checks, and independent safety review pass; exact-adapter, hottest-sensor, process, memory-series, and full-window gates added | Trusted live CPU temperature is still unavailable; do not build or benchmark |
| 2026-07-21 | Packet page compaction `/root` | Source Implemented; Build Pending | Local uncommitted SVSM files | Conservative deforming fail-open, scheduled-clipmap cache keys, 2D fallback dispatch, timing repairs, direct page-table scanning for small packet rectangles, and recent-page eviction grace were added behind independent toggles; the automatic motion harness opts in | Build, focused tests, runtime image checks, and timing remain pending a user-provided quiet window plus thermal clearance |
| 2026-07-21 | Static cache correctness `/root` | Source Implemented; Build Pending | Local uncommitted SVSM files | Exact eight-jitter union, visibility invalidation on union extension, Bend-dependent reuse guard, material-dirty revision, bounds hash, and packet readiness transition added | Build and cache-on/off image validation remain pending thermal clearance |
| 2026-07-21 | Static and dense source audit `/root` | Source Implemented; Build Pending | Local uncommitted SVSM files | Pool-sized budget reuse, debug-only atomic removal, tap-aware marking, dense Bend gating, dense debug-store gating, distributed adaptive probes, and biased dense draw skipping independently reviewed; unsafe pinned-NVRHI partial array clear rejected | Build, shader compilation, and runtime comparisons remain pending a user-provided quiet window |
| 2026-07-21 | Packet-state sorting `/root` | Source Implemented; Build Pending | Local uncommitted SVSM files | Stable state ordering, global argument reindexing, metadata attachment, cache-key participation, independent default-off control, and deterministic models added | Verify D3D12 support/active status and measure only after the occupancy and thermal gates pass |
| 2026-07-21 | Per-level empty-work skip `/root` | Release Build And Focused Tests Pass; Runtime Validation Pending | Local uncommitted SVSM files | Allocation now publishes a same-frame 0-or-1 culling-dispatch gate and packet fill promotes it to the existing 0-or-`UINT_MAX` draw-count gate. Empty finite-budget levels skip both GPU packet scanning and draws without readback; D3D12 barriers, stale arguments, budget draining, and reference-path availability passed independent review. | Measure static finite-budget GPU culling after a clean preflight; CPU command recording remains a separate bounded-drain opportunity |
| 2026-07-21 | Dirty-page scatter raster and total-only timing `/root` | Release And Debug Focused Tests Pass; Runtime Validation Pending | Local uncommitted SVSM files | Scatter culling uses one lane per packet, omits exact page-list storage, and retains the default-off exact reference. Motion timing now issues only the outer total query, rejects detailed-query leakage and inactive optimized paths, and enforces the 0.4 ms median plus 0.7 ms worst ceiling. | Two bounded 20 percent preflights were rejected: the first found stale MSBuild workers; after cleanup, the second ended instantaneously green at 52 C and 17.4 percent adjusted CPU average but never held all gates for 30 continuous seconds. Continue source review without polling, then verify High priority, `scatter active`, images, and off/on tail latency in the next clean window |
| 2026-07-21 | Allocation-budget saturation early-out `/root/allocation_saturation_implementation` | Release App, Shader, And Focused Test Pass; Runtime Validation Pending | Local uncommitted SVSM files | Independent default-off Custom toggle, finite-budget-only shader flag, explicitly branch-gated relaxed pre-atomic saturation probe, backend-correct motion metadata, and deterministic helper, flag, preset, and equality coverage added; the combined Release app and shader build plus focused CTest pass with MSBuild node reuse disabled. | Measure off/on allocation tails only in a thermally clean window |
| 2026-07-21 | Finite-budget drain and scatter tail guards `/root` | Release Build And Focused Tests Pass; Runtime Validation Pending | Local uncommitted SVSM files | Finite budgets now drain conservatively to the zero-work static path; zero-to-nonzero TAA jitter transitions reset the eight-slot request cache exactly once; allocation saturation is fully branch-gated in legacy HLSL; sparse dirty rectangles independently fall back to compact per-page instances above a configurable amplification ratio; and alpha-tested virtual scatter can reject invalid page ownership before explicit-gradient texture sampling. The automatic motion harness uses a four-page fine budget and opts into each independent guard. | Compare every guard off and on with stable alpha foliage and page-boundary images, then measure static and motion tails only in a thermally clean window |
| 2026-07-21 | Cold-scatter TDR containment `/root` | Release Build And Focused Test Pass; Independent Review Active | Local uncommitted SVSM files | Two fresh optimized motion launches caused `nvlddmkm` `0x141` engine resets with `MISSING_MACRO_DATA`. The automatic path now shares its four-page budget with coarsest, unsafe scatter configurations select exact packet-page lists, and every guarded invalid-bounds path uses compact scheduled-page instances without requiring trustworthy packet bounds. Stephano Reference remains unlimited with the coarsest exemption and every scatter optimization off. | Complete independent source review, full tests, thermal preflight, then permit one controlled diagnostic motion retry |

## Risks and Escalation Triggers

- Dense six-by-8192-square `R32_UINT` backing is approximately 1.5 GiB before
  auxiliary resources. Dense mode must be explicit, guarded, and never the
  startup default.
- Page-instanced rasterization must preserve alpha testing without copying
  Donut's renderer architecture or creating a CPU draw per page.
- Moving or procedurally deformed geometry may lack a reliable first-party
  change signal. The conservative fallback is invalidation, never silent reuse.
- Fine-caster exclusion remains off unless current UVSR bounds and camera-depth
  evidence can prove every exclusion condition.
- The available machine may not expose both requested GPUs. Missing hardware is
  a validation blocker, not permission to report estimates.
- The requested runtime matrix is much larger than a build-only check. Passing
  compilation and unit tests is not completion.
- The two optimized cold motion launches on 2026-07-21 triggered genuine NVIDIA
  engine timeouts. Do not relaunch the superseded unbounded configuration. The
  bounded replacement still needs one controlled diagnostic run before it can
  be considered runtime-safe.
- A cool GPU alone does not certify this laptop. Windows currently exposes CPU
  performance constraints but not package temperature, so runtime work pauses
  whenever a trusted CPU temperature is unavailable.

Stop and ask the user if:

- a required implementation would modify a Bend file, Donut, NVRHI, another
  submodule, main, Git history, or remote state;
- no existing scene-draw seam can satisfy opaque and alpha-tested rendering
  without a renderer-wide rewrite;
- a product-quality choice cannot preserve the requested white fail-open
  contract or reference path.

## Completion

- Final integrated commit: intentionally none
- Verification summary: the current bounded-scatter Release application and all
  affected SVSM shader permutations build, and the focused SVSM reference test
  passes. The previous
  full 14-test CTest suite, Title Case checker, Bend hashes, diff/whitespace
  checks, and RTX 4090 Laptop SVSM-enable regression passed. The current tail
  guards are not yet runtime-tested; the thermally controlled static and motion
  matrices remain pending.
- Independent review: source and monitoring reviews plus targeted post-fix
  rechecks found no blockers
- Coming Soon/documentation update: pending
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: all implementation phases active
- Active ownership released: no
- Archived to completed/abandoned path: pending
