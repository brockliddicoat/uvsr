# Sparse Virtual Shadow Maps Technical Report

## Executive Finding

UVSR can add SVSM without changing Bend, Donut, or NVRHI. The lowest-conflict
boundary is a first-party directional visibility producer that resolves to its
own full-resolution linear `R8_UNORM` texture immediately after the G-buffer
and before deferred lighting. A producer-neutral fixed two-slot interface maps
each complete texture to an exact light pointer and multiplies matching factors.
Bend and SVSM do not include, bind, configure, benchmark, or name one another.
They also build as separate static-library targets. With the aggregate
application disabled, each component and its reference test configure, build,
and pass while the other component is disabled. Each target publishes its
public header dependencies and owns an independent shader catalog.

The public reference record is useful but not production-complete. K. T.
Stephano's article describes the intended design in detail, and older
StratusGFX branches contain a public WIP implementation that was not visible on
the current default branch. Later Stratus history explicitly disables caching
because of a major bug and contains an allocation workaround. That code is
therefore evidence for resource layout and algorithms, not a safe source to
copy.

## Source Review

### K. T. Stephano Article and Source

The complete rendered article and source Markdown were reviewed, including code
samples, comments, equations, captions, static diagrams, GIFs, and animation
frames. The design uses directional clipmaps, stable origin-centered sample
addressing, page-aligned render origins, per-pixel camera-depth marking, compact
page metadata, a fixed physical pool, coarser-level fallback, two-dimensional
wraparound, and manual page-safe filtering.

The visual material adds information not carried by the prose alone:

- Camera motion exposes narrow wrapped strips while central physical pages are
  reused.
- Clipmap colors and extents show discrete level transitions.
- Light rotation changes the stable mapping and requires full invalidation.
- Hardware sparse allocation and a software fixed pool are alternative backing
  models.
- Fine pages may fill over a persistent coarse fallback.
- Filter footprints crossing page borders cannot assume physical adjacency.

### Reddit Discussion and Video

Every visible comment and reply was reviewed. The main implementation warning
is animated foliage: its changing bounds can invalidate enough pages to erase
cache benefits. Boundary blending was discussed as a possible improvement but
was not reported as tested. A stochastic per-pixel choice between overlapping
levels was suggested by a commenter; it is not part of the requested reference
path.

The full video was inspected across its duration. It demonstrates persistent
physical-page reuse during fast camera traversal, narrow churn near
disocclusion and clip boundaries, clipmap transitions, and complex static
foliage. It does not demonstrate a moving sun or moving casters, so it cannot
validate those invalidation paths.

### Stratus Repository and Branches

The current default branch contains generic sparse texture support, including a
128-by-128 default virtual page size, sparse texture enablement, and page
commitment calls. It does not contain the full SVSM pipeline.

Older `v0.11-vsm*`, `v0.11`, and `v0.11-gi` branches do contain public WIP
SVSM shaders and renderer integration for depth analysis, page marking,
allocation/freeing, clearing, page rendering, and sampling. Relevant details
include a packed 32-bit page entry, an `R32F` resource viewed as `R32UI`, a
software-managed physical pool, per-pixel marking, stable sample transforms,
page-aligned origins, and a GPU free list.

The WIP path also exposes reasons not to transplant it:

- A later commit disables caching because of a major bug.
- A still-later branch carries a workaround for a page-allocation bug.
- Hardware sparse allocation uses delayed CPU readback.
- Page groups to render are read by the CPU each frame.
- Some values are hard-coded and receiver validity checks are incomplete.
- The reference uses conventional-Z clear one and atomic minimum.

UVSR must instead use reverse-Z clear zero and atomic maximum, keep page
decisions on the GPU, and fail open through coarser levels or white.

All public Stephano repositories and every Stratus branch were rescanned.
No newer complete public SVSM implementation was found outside the historical
Stratus branches and the article sources.

### Timberdoodle Cross-Check

The current Timberdoodle implementation was reviewed only for the requested
cross-check areas. It confirms practical patterns for:

- wave-level request deduplication;
- a free list followed by eviction of unvisited cached pages;
- reverse mapping from physical pages to virtual owners;
- toroidal page offsets and wrapped-page freeing;
- newly exposed strip invalidation;
- full invalidation when caching is disabled or the sun moves;
- moving-AABB invalidation in page blocks;
- a dirty-bit hierarchy for culling;
- per-page stable view-position storage; and
- a pooled `R32` texture.

Its bindless, meshlet, visibility-buffer, and scene architectures are not
applicable to UVSR and will not be copied. Current source comments also expose
unfinished edge cases around out-of-range invalidation and unallocated
sampling, reinforcing the required fail-open tests.

### Epic Production Guidance

Epic's documentation confirms 128-square pages, depth-driven allocation,
directional clipmaps with doubling radius, cache invalidation from light and
caster changes, optional static/dynamic separation, resolution bias, coarse
page consumers, and extensive visualizations.

The production warnings become UVSR requirements:

- pool exhaustion must never corrupt page ownership;
- moving sun can invalidate the entire cache every frame;
- throttled page rendering can expose page-boundary artifacts;
- animated foliage and world-position-offset geometry need conservative
  invalidation;
- camera-depth marking alone misses offscreen consumers and therefore needs
  coarse fallback; and
- optional static/dynamic separation follows a correct single-cache path.

### Bend and Pinned Resource Interface

Bend's published design confirms that screen-space directional shadows
complement shadow maps with visible contact and fine detail; they do not replace
world-space shadow coverage.

The pinned local NVRHI revision already supports the required normal
`R32_UINT`, `R8_UNORM`, UAV, structured-buffer, indirect-dispatch, and
indirect-draw interfaces. No NVRHI update or fork is needed.

## UVSR Integration Design

### Render Order and Light Identity

The existing render order is:

1. single-sample G-buffer;
2. any enabled independent full-resolution visibility producers;
3. deferred lighting;
4. later screen-space indirect, temporal, and display work.

SVSM belongs between the G-buffer and deferred lighting as a standalone
producer. Its result carries only a texture and its exact directional-light
pointer. The renderer boundary adapts enabled producers into two neutral slots;
deferred lighting applies each texture only to its pointer-identical light and
computes:

`initial visibility = product(saturate(matching producer visibility))`

Every absent, incomplete, incompatible, stale-sized, or unmatched input
contributes white. SVSM clipmap selection and fallback live inside its resolve
pass, not in Donut's four-cascade receiver.

### Reference Resource Layout

The reference layout uses six clipmaps, 8192-square virtual resolution,
128-square pages, and therefore a 64-by-64 page table per clipmap. Each compact
entry stores physical index, resident, required, dirty, and age/frame state.
A reverse-owner buffer permits safe GPU eviction.

The physical depth pool is a normal `R32_UINT` texture pool. The reference
backend encodes non-negative reverse-Z float depth with `asuint`, clears to
zero, and uses `InterlockedMax`. IEEE-754 bit ordering is monotonic for finite
non-negative values, so the nearest reverse-Z caster wins.

Dense mode backs every page and disables caching. Because six full
8192-square `R32_UINT` clipmaps consume approximately 1.5 GiB before auxiliary
resources, it must be explicit and guarded. Sparse mode uses one configurable
fixed physical pool and never exposes invalid ownership under exhaustion.

### Page Marking and Allocation

The marking pass reconstructs world position from UVSR's existing
single-sample camera depth, selects the finest covering clipmap after applying
the configured resolution bias, maps to stable virtual coordinates, wraps
them, and marks the page plus filter-neighbor pages.

Per-pixel mode is the reference. The 8-by-8 and 16-by-16 modes conservatively
cover every page touched by a tile and deduplicate page IDs within GPU waves or
groups. Bend never suppresses requests.

GPU allocation consumes a compact required-page list, uses free pages first,
then evicts eligible old pages, updates both ownership directions, and records
allocation failure without aliasing. Dirty pages are compacted for clear and
render work.

### Page Rendering

Existing UVSR/Donut opaque and alpha-tested material batches remain the source
of geometry and material state. A narrow first-party draw adapter instances
each existing batch across a GPU dirty-page list, so CPU work scales with
existing batches rather than pages. Per-instance page transforms place
geometry in a physical atlas viewport and clip triangles to the virtual page.
Alpha testing reuses existing material textures and cutout semantics.

Initial culling uses conservative object AABBs per clipmap and rectangular dirty
bounds. A more compact dirty hierarchy is permitted only after the reference
path is correct.

### Caching and Invalidation

Clipmap render origins move in whole-page increments. Virtual-to-physical
ownership wraps in two dimensions without copying page data. Newly exposed
strips become invalid. Moving caster bounds, alpha/material changes, and
incompatible scene bounds dirty overlapping pages. Directional-light rotation
or stable-mapping changes invalidate every page.

Eviction considers age and current-frame visitation but must never discard a
page still required for a valid fallback. A fixed camera, fixed sun, and static
scene must compact zero dirty render pages after warmup.

### Sampling and Filtering

Resolve selects the finest requested clipmap and tests full validity:
resident, not dirty, mapping ownership correct, and filter footprint available.
Failure advances to the next coarser clipmap. White is returned only after no
valid level remains.

The 1, 4, 8, and 16 tap variants translate every virtual tap independently.
The 16-tap pattern follows Donut's current Poisson receiver as closely as the
page-safe interface permits. Hybrid filtering may reuse a translation only
when the complete footprint stays in one valid physical page.

Adaptive filtering starts with multiple page-safe probes. It may return a
common result only when every probe agrees and the complete footprint is valid;
otherwise those probes are reused as part of the selected full filter.

### Producer-Local Optimization Boundary

SVSM optimizations remain producer-local:

- Resolution bias is applied consistently to marking, allocation, resolving,
  fallback, and debug presentation.
- Adaptive filtering follows the page-safe probe rule.
- Fine-caster exclusion remains off until existing bounds and camera-depth
  evidence can conservatively prove every exclusion condition.

## Profile Contract

### Performance

Cached SVSM with adaptive page-safe 8-tap filtering and a plus-one resolution
bias. Static visibility caching, packet culling, batched submission, packet
sorting, empty-work skipping, and other validated no-work paths are enabled.

### Balanced

The same validated cache and submission paths with unbiased adaptive 8-tap
filtering.

### Quality

The same validated cache and submission paths with unbiased full 16-tap
filtering and adaptive filtering disabled.

### Custom

Every validated option is editable independently.

## Validation State

Research, architecture mapping, and the local integrated implementation are
complete enough for active validation. The decoupled Release renderer, all
modified shaders, and the focused PBR/SVSM tests build successfully; the focused
tests pass. Full CTest, title case, frozen Bend hashes, diff checks, and runtime
validation remain pending at this point in the report.
No full runtime-matrix claim has been made.

The first interactive enable attempt exposed two startup failures and one
follow-on workload failure:

- D3D12 rejected an unused 8192-square `R8_UNORM` raster coverage target with
  `E_INVALIDARG`, and sparse initialization consequently reported that the
  fixed physical pool could not be allocated.
- After removing that redundant resource, the original sparse raster adapter
  submitted every scene batch across all 4096 physical pages for each of six
  clipmaps, causing device removal on the first enabled frame.

The atomic depth shaders have no output-merger color or depth output, so the
correct narrow fix is an attachmentless NVRHI framebuffer. Sparse allocation
now also emits a compact scheduled-page list and one GPU render count per
clipmap. Existing opaque and alpha-tested scene draws consume those counts
through indirect instancing. CPU recording remains per existing scene item,
never per page, and the unlimited reference budget remains available.

The repaired path was exercised on the available NVIDIA GeForce RTX 4090
Laptop GPU in PBR Sponza Decorated at 1902 by 1069 with the Stephano Reference
preset, 4096 physical pages, unlimited page budget, per-pixel marking, 16 taps,
manual page-safe filtering, and a moving piloted camera. It remained responsive
beyond both original allocation dialogs and the later device-removal point.
This is a regression result, not a substitute for the remaining runtime matrix
or a performance measurement.

The decisive validation gates are deterministic mapping/allocation/cache tests,
the combined Release build and CTest suite, Bend hash preservation, dense versus
sparse and cached versus uncached image comparisons, stationary warmup,
movement/teleport/sun/caster/boundary/exhaustion/resize cases, all requested
filter and marking combinations, measured per-stage GPU time, and source review.
RTX 4090 Laptop GPU and Core Ultra 9 185H integrated-GPU results will be reported
only when those devices are actually available and measured.

## Thermal-Controlled Cache and Culling Refinement

The performance data collected before the current thermal protocol is retained
only as diagnostic evidence. A motion run reported approximately 1.17 ms total
SVSM at 1920 by 1080, but it was not bracketed by a trusted cold Position-1
control and must not be presented as a measurement. The current goal remains at
most 0.4 ms total SVSM for both a fixed scene and the exact 0.1-degree-per-frame
motion benchmark.

Unexplained CPU or GPU utilization is now a hard user-occupancy signal, not an
opportunity to use spare capacity. Builds, UVSR launches, benchmarks, and
opportunistic tests remain paused until the user explicitly provides a testing
window and the complete quiet preflight passes. Source inspection and local
source edits continue without running the renderer.

`AGENTS.md` now defines a reusable benchmark procedure centered on Position 1.
It rejects duplicate UVSR instances, mismatched process paths, known captures,
builds, overlays, stress tools, low TFLOPS or clock versus the identical cold
control, slow pre/post controls, and monotonic process or system-memory growth.
It requires three baseline repeats and retains every raw run. Measurement mode
covers an explicit duration and latches the first contaminated sample, so a
later clean suffix cannot redeem an invalid capture.

The ignored local monitoring bundle contains pinned LibreHardwareMonitor 0.9.6
and PresentMon 2.5.1 assets downloaded from their official releases. Archive,
file-count, size, SHA-256, license, notice, and available Authenticode checks
pass. The bootstrap roots relative paths to this worktree, verifies downloads
and extraction through unique temporary paths, preserves invalid prior files in
quarantine, and fails explicitly outside its supported Windows x64 host.
HWiNFO Portable 8.50 has been downloaded and verified separately but has never
been executed because it loads a temporary driver and requires explicit user
authorization.

The thermal checker now requires the exact UVSR adapter name, the hottest live
sensor on that adapter, adequate headroom, AC-state evidence, a live CPU
temperature, no CPU performance constraint, no more than five percent external
CPU or preflight GPU load, and a live limiter state when available. A
non-NVIDIA adapter may use an explicit reduced-evidence path only with the
lowest applicable official sensor limit, a live clock, and cold Position-1
clock, TFLOPS, and frame-time controls. Manual temperatures and one-sample
diagnostics can never set `thermalGateReady`. Its CSV records temperatures,
loads, clocks, limiter state, process identity, private and working memory,
dedicated and shared GPU memory, system commit, available memory, and kernel
pools over time.

Current LibreHardwareMonitor data maps the RTX 4090 Laptop GPU exactly and shows
its core near 40 C, hottest sensor near 49 C, roughly 46 C of NVIDIA-reported
T.Limit headroom, P8 idle state, and no live thermal limiter. The Core Ultra 9
185H temperature sensors are enumerated but return no values on this firmware.
Windows also reports no ACPI thermal-zone instance or thermal-zone performance
counter. Consequently the gate correctly remains closed even when a manually
observed CPU temperature is below the limit. No runtime benchmark has been
accepted under that uncertainty; the later source-only Release build is
reported below.

Source review found and repaired several issues without changing frozen Bend or
dependencies:

- static camera-depth requests now preserve the exact union of the eight UVSR
  TAA jitter phases instead of a full page halo;
- extending that union invalidates all cached visibility slices because page
  allocation or eviction can change fallback results;
- the former cross-producer early reject, Bend texture read, static-cache guard,
  counter, debug paths, settings, and benchmark stages were removed; static
  visibility reuse is now wholly SVSM-owned;
- material-dirty state advances the SVSM scene revision before Donut clears it,
  and geometry bounds plus deformation classification participate in the scene
  hash;
- deforming packets and every invalid, overflow, allocation-failed, or
  unsupported packet-culling case retain the full dirty-page fail-open path;
- packet metadata is regenerated after GPU-gated submission is re-enabled;
- fallback indirect filling uses safe two-dimensional dispatch dimensions; and
- the command-line motion benchmark explicitly enables packet-state sorting,
  empty-level skipping, packet-page culling, small-rectangle direct page-table
  scanning, and recent-page eviction grace without reading or changing any
  screen-space-shadow state;
- a finite page budget equal to or larger than the physical pool is now treated
  as effectively unlimited, allowing static request reuse after every possible
  resident dirty page has been drained;
- performance mode no longer performs unused global allocation atomics or
  debug-only rendered/failure atomics;
- one-tap marking requests only the center page, while every current multi-tap
  Poisson variant retains the conservative three-texel neighbor footprint;
- dense resolve now gates Bend rejection on an actual exact-light Bend texture,
  skips debug UAV writes when debug is off, distributes adaptive probes like
  sparse resolve, and omits biased fine clipmaps from geometry drawing;
- the pinned D3D12 backend was verified to ignore array-slice bounds for integer
  UAV clears, so dense depth safely retains a whole-resource clear rather than
  using an invalid partial transition;
- cached packets can be stable-sorted by compatible buffer and raster state
  behind an independent default-off toggle, while alpha-tested and
  nonbatchable packets retain exact state and metadata;
- static page-request reuse skips the complete marker, allocator, clear,
  packet-culling, caster-raster, and finalize sequence, and the independent
  full-resolution SVSM visibility texture may also be reused when its own
  inputs remain unchanged;
- an independent default-off per-level gate publishes zero for an empty
  clipmap and `UINT_MAX` for a nonempty one, allowing indirect-count draws to
  skip every empty batched group without truncating a nonempty group;
- an independent finite-budget drain preserves a stable request union for a
  conservative `ceil(pool pages / fine-page budget)` maintenance window, then
  reaches the same zero-work static path without CPU readback;
- switching between no jitter and UVSR's eight nonzero MiniEngine TAA offsets
  resets the request and visibility jitter slots exactly once, preventing the
  ninth `(0,0)` offset from restarting finite draining indefinitely;
- the finite-budget allocation saturation probe is explicitly nested behind
  its shader flag, so legacy HLSL cannot issue the relaxed counter load when
  the reference toggle is disabled;
- an independent dirty-scatter amplification guard compares each packet's
  conservative intersection area with the scheduled compact-page count and
  falls back to existing per-page instances only when the strict configured
  ratio is exceeded; guarded invalid or inconsistent packet bounds also use the
  compact scheduled-page instances rather than overriding the guard;
- an independent alpha-tested scatter guard captures derivatives before any
  page-dependent return, rejects unscheduled holes, then samples only the base
  and opacity textures with explicit gradients; the disabled and per-page
  paths retain the original implicit-gradient alpha-test order;
- the fill-to-draw transition avoids combining UAV and indirect-argument states
  in pinned NVRHI's pending-state tracker, while packet-list UAV barriers remain
  on their distinct buffers; and
- controls whose runtime prerequisites are absent are disabled with retained
  values, batching support and active state are reported, and the unimplemented
  fine-caster exclusion control is explicitly unavailable instead of acting as
  a silent no-op.

The motion benchmark rotates the camera, not the sun; it aborts if the
directional light changes. The old thermally unqualified trace remains only
diagnostic, but its approximately 1.19 ms first-turn median and approximately
0.33 ms return median show that already visited pages were reused. The remaining
first-turn spike therefore tracks newly revealed requests and page rendering,
not a per-frame full light invalidation. The earlier Bend Hybrid prototype
retained the requested per-pixel marker rather than overriding it with a
16-by-16 min/max-depth volume,
which could conservatively request large page rectangles at Sponza depth
discontinuities. Its inoperative Bend-dependent static visibility-cache default
was also removed; static page reuse remains available and resolve stays live.

The automatic motion harness uses one four-page budget across every
clipmap, including coarsest, so finite-budget draining and saturation are
genuinely active and cold work has a strict scheduling bound. It also opts into
the two independent scatter guards. The current Performance-derived benchmark
configuration is SVSM-only and is serialized as Custom after those explicit
benchmark overrides.

## GPU Timeout Investigation and Safety Bound

Two automatic motion launches of the earlier configuration caused NVIDIA GPU
engine resets while the PBR Sponza scene was entering its cold optimized path.
Windows recorded LiveKernelEvent `0x141`, `nvlddmkm` Event 13/153,
`MISSING_MACRO_DATA`, and the `Ada_UserOC` bucket. These are diagnostic engine
timeouts, not accepted performance samples. The stale prior benchmark result
was rejected by timestamp and was not attributed to either launch.

Independent source reviews found no proven indirect-argument overflow, counter
overshoot, missing UAV-to-indirect transition, unbounded loop, or SM 6.8 start
instance error. The strongest application-side hazard was bounded but excessive
cold work: the published coarsest-level budget exemption could schedule a large
coarse set, a sparse row and column could produce a large flat dirty rectangle,
and invalid packet metadata could draw once across the full 8192-square virtual
clipmap even while the rectangle amplification guard was enabled.

The replacement preserves the published reference behavior through the
independent backend and Custom controls and adds a separate **Include Coarsest
in Page Budget** control. Virtual
scatter activates only when its amplification guard is enabled and a nonzero
all-clipmap budget is at most four pages, with budget times the configured
amplification no greater than 16 virtual pages. Every guarded invalid-bounds
branch sets the compact per-page fallback bit, so its draw count is at most that
same four-page reservation and does not require trustworthy packet bounds. If
any safety prerequisite is absent, packet-page culling retains the exact compact
per-page raster path. The automatic motion configuration therefore has a
source-proven workload bound; explicit reference configurations remain
intentionally unlimited. This is not yet proof of driver stability, so one
thermally controlled diagnostic motion retry remains required.

The current Release application and every sparse shader permutation compile.
All 14 Release CTest targets pass, including the deterministic SVSM, frozen
Bend, and generic PBR exact-light composition suites. The document title-case
checker and working-tree whitespace check also pass. Separate component-only
configurations build and pass the SVSM reference test with Bend disabled and
the Bend reference test with SVSM disabled. A bounded launch of the current
Release build showed a normal Sponza frame, separate SVSM and Screen-Space
Shadows drawers, and no error dialog; active human input prevented automated
toggle clicks, so this is only a launch smoke test. The next valid runtime
sequence remains a trusted 30-second preflight, one exact renderer instance,
three Position-1 controls, independent Bend-only and SVSM-only image checks,
static and motion debug/image validation, and post-run Position-1 controls.
Rows without that evidence remain pending rather than estimated.

Independent post-fix reviews found no remaining monitoring-correctness or
monitoring-safety blocker by inspection. Runtime evidence for the newest source
state remains pending the thermal and occupancy gate.
