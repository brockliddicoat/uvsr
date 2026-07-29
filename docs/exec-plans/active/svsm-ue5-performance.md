# UVSR Virtual Shadow Map Performance Versus Unreal Engine 5: Paired Caching, Local Invalidation, and GPU Work Reduction

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/bend-screen-space-shadows` at
  `C:\Users\brock\Documents\Codex\2026-07-18\i-want-you-to-take-the\work\uvsr-bend-shadows`
- Base commit: `ea566bc67f744059e6f62e33c541c5b25bde9bd8` plus the preserved
  uncommitted UVSR, Bend, SVSM, CSM, benchmark, test, and documentation changes
- Started: 2026-07-25
- Last updated: 2026-07-25
- Planned archive:
  `docs/exec-plans/completed/svsm-ue5-performance.md`

## Goal and Done Condition

Goal:

Continuously research, implement, profile, visually validate, and harden UVSR's
directional SVSM path using the newly accessible UE5 VSM source as a primary
cross-reference. Improve static, slow-camera, fast-camera, and moving-light
behavior without depending on Nanite, changing Donut or NVRHI, weakening the
white fail-open contract, or losing independently selectable reference paths.

Done when:

- [ ] One virtual address space supports paired static and dynamic depth for
  each virtual region, with static reuse and dynamic updates merged at resolve.
- [ ] Per-object invalidation modes, old-plus-new localized invalidation,
  tighter deforming bounds handling, conservative occlusion-aware rejection,
  page-mask caster culling, moving-light LOD bias and recovery, selective coarse
  policies, and distance-based mip clamping are implemented and validated.
- [ ] Shared six-clipmap caster preparation, cached caster metadata,
  precomposed marking and resolve transforms, opaque raster specialization, and
  reduced alpha-tested bindings are implemented with reference fallbacks.
- [ ] Close-up filtering is materially improved without sampling across
  unrelated physical pages or hiding missing/invalid data.
- [ ] Deterministic tests cover mapping, invalidation, paired depth, movement,
  fallback, raster classification, filtering, and resource recreation.
- [ ] Release build, full CTest, Title Case checker, Bend hash verification,
  diff checks, runtime image review, and independent source review pass.
- [ ] Accepted same-hardware measurements show SVSM at least 30 percent faster
  than the documented projected UE5 VSM and diagnostic UE5-style CSM comparison
  targets in static, slow-camera, and fast-camera scenarios, with equivalent
  coverage, filtering, resolution, scene, and visible correctness.
- [ ] Raw timings remain authoritative. Clock-normalized results are reported
  only as same-adapter advisory estimates under the repository procedure.

## Scope

In scope:

- Paired static and dynamic physical depth layers governed by one page table,
  one virtual mapping, and compact per-page metadata.
- Static/dynamic caster classification and conservative promotion of unchanged
  movable geometry into the reusable cache representation.
- Stable-key caster snapshots and localized page invalidation.
- Existing-visibility-assisted invalidation rejection that cannot suppress
  hidden or off-screen shadow casters without conservative proof.
- Page-mask and optional HZB-assisted caster rejection.
- Moving-light degradation controls and gradual quality recovery.
- Selective coarse-page and caster policies.
- Distance-based receiver and caster mip clamping for traditional geometry.
- Shared six-clipmap packet preparation and metadata caching.
- Precomposed marking and resolve transforms.
- Opaque and alpha-tested shadow-raster specialization.
- Page-safe high-quality filtering and performance instrumentation.

Non-goals:

- Nanite, meshlets, a visibility-buffer rewrite, bindless renderer conversion,
  a new scene database, a new virtual-texture framework, or a CSM rewrite.
- Donut or NVRHI changes, submodule updates, CPU draw calls per virtual page,
  final-path GPU readback, or a second independent static clipmap system.
- Bend coupling, a Bend hit/miss mask, or Bend-controlled page requests.
- Commit, push, pull request, merge, main modification, or published-history
  changes.

Affected subsystems and paths:

- `src/sparse_virtual_shadow_map.cpp`
- `src/sparse_virtual_shadow_map.h`
- `src/sparse_virtual_shadow_map_settings.h`
- `src/sparse_virtual_shadow_map_*.hlsl`
- `src/uvsr.cpp`
- `tests/sparse_virtual_shadow_map_tests.cpp`
- UVSR-owned build and shader manifests only when required by a new shader

Shared hotspots reserved for the coordinator:

- All affected CPU/HLSL contracts and resource layouts
- `src/uvsr.cpp`, `CMakeLists.txt`, `README.md`, and this execution plan
- Build, test, launch, renderer control, and performance measurement

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`
- Local versus remote state: feature branch with extensive preserved
  uncommitted work; no publication action authorized
- Verified source commit/build: base
  `ea566bc67f744059e6f62e33c541c5b25bde9bd8`; the current dirty candidate has
  prior Release and focused-test evidence recorded in the predecessor SVSM plan
- UE5 reference: private `EpicGames/UnrealEngine`, `release` branch; exact file
  blob identities are recorded in research notes and source-review evidence
- GPU, scene, camera, resolution, and settings preset when relevant: RTX 4090
  Laptop GPU, existing Position-1/Sponza benchmark controls, exact workload
  identity recorded per run
- Existing numeric comparison evidence is not matched closely enough for a
  final claim:
  - cached diagnostic CSM is 0.062--0.067 ms, making its provisional
    thirty-percent-faster band 0.043--0.047 ms;
  - full-redraw diagnostic CSM is 1.248--1.305 ms, making its provisional
    thirty-percent-faster band 0.874--0.914 ms;
  - historical SVSM light motion is 0.243 ms median at 0.1 degree per rendered
    frame, but it belongs to an older source/settings identity;
  - no public or repository-local matched UE5 VSM timing exists for this
    adapter and scene, so generic Epic performance budgets are not a measured
    UE5 VSM baseline.
- Known pre-existing failures: the prior optimized cold-motion scatter
  configuration caused NVIDIA `0x141` resets and remains bounded and
  independently disabled; the complete runtime matrix is not yet accepted

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| UE5 VSM source audit | `EpicGames/UnrealEngine` `release` symbols and invariants | Active | All designs |
| Current SVSM CPU/cache audit | Dirty feature worktree | Complete; Hardening In Progress | Shared builder and invalidation |
| Current SVSM GPU/shader audit | Dirty feature worktree | Complete; Filter And Hierarchy Design In Progress | Marking, raster, filtering, culling |
| Paired-depth contract | One virtual mapping, two compatible physical-depth slices | Implemented; Static Runtime Path Validated | Allocation, raster, resolve |
| Caster snapshot contract | Stable identity plus conservative fail-open | Hardened; Runtime Validation Pending | Local invalidation |
| Static-depth HZB contract | Complete paired-static pages, reverse-Z minimum reduction, owner/epoch validation, conservative fail-open | Phase 1 Implemented; Motion Runtime Validation Pending | Dynamic caster/page culling |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Existing page-table and receiver semantics remain valid for the reference
  path.
- Static and dynamic depth for one virtual region share virtual coordinates,
  clipmap selection, wraparound, and invalidation ownership.
- Missing, invalid, unsupported, over-budget, or out-of-range data resolves to
  white when no valid coarser representation exists.
- Static and dynamic depth merge by the reverse-Z nearest-caster rule before
  shadow comparison.
- Optimization toggles add no meaningful disabled-path work.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| UE5 VSM architecture audit | `/root` and delegated read-only reviewers | Shared, read-only | Dirty feature state | None | UE5 access | Complete; Continuing Per Phase |
| SVSM CPU/cache audit | `/root/adaptive_cache_review` | Shared, read-only | Dirty feature state | None | Current source | Complete |
| SVSM hierarchy audit | `/root/hierarchical_page_mask_design` | Shared, read-only | Dirty feature state | None | Current source and UE5 access | Complete |
| SVSM filter audit | `/root/svsm_filter_research` | Shared, read-only | Dirty feature state | None | Current source and UE5 access | Complete; Further Filtering Work Planned |
| Local cache correctness hardening | `/root/localized_cache_hardening` | Shared feature worktree | Dirty feature state | Scoped SVSM CPU/cache files and tests | CPU/cache audit | Complete; Runtime Validation Pending |
| Moving-light cache policy | `/root/moving_light_cache_impl` | Shared feature worktree | Dirty feature state | Scoped SVSM CPU/cache files, shaders, UI, and tests | Paired-depth contract | Complete; Runtime Validation Pending |
| Per-object invalidation and binding safety | `/root/per_object_invalidation_audit` and `/root/svsm_per_object_invalidation` | Shared feature worktree | Dirty feature state | Scoped SVSM CPU/cache files, public resolver API, UI adapter, and tests | Local invalidation hardening | Phase B Complete; Runtime Validation Pending |
| Persistent shared-caster metadata | `/root/persistent_packet_builder_design` and `/root` | Shared feature worktree | Dirty feature state | Scoped SVSM CPU/cache files and tests | Shared packet builder | Implemented; Runtime Validation Pending |
| Distance and coarse policy | `/root/distance_coarse_policy_design` and `/root` | Shared feature worktree | Dirty feature state | Scoped marking, resolve, settings, UI, and tests | Moving-light recovery | Receiver Policy Implemented; Caster And Transition Work Pending |
| Balanced page-safe filtering | `/root/svsm_balanced_filter_phase` | Shared feature worktree | Dirty feature state | Scoped SVSM resolve, configuration, runtime, and tests | Filter audit | Complete; Further Kernel Work Pending |
| Paired-cache UE cross-check | `/root/svsm_paired_cache_ue_audit` | Shared, read-only | Dirty feature state | None | UE5 source and current paired cache | Complete; Findings Queued |
| Integration and implementation | `/root` | Feature worktree | Dirty feature state | Scoped SVSM files and tests | Audits | Active |

## Verification Plan

- Add deterministic CPU models for stable identity, page coverage, old/new
  invalidation, dirty-region merging, fallback, promotion, moving-light
  recovery, distance clamp, and paired reverse-Z depth merge.
- Compile every affected shader permutation.
- Build `uvsr` and `uvsr_pbr_tests` in Release.
- Run full CTest with failure output.
- Run the Title Case checker self-test and repository scan after documentation
  changes.
- Verify all frozen Bend hashes.
- Run `git diff --check` and inspect the complete scoped diff without staging.
- Launch only through `tools/launch_uvsr.ps1` with a lowercase experiment token.
- Inspect static, slow-camera, fast-camera, moving-sun, deformation, foliage,
  teleport, page-boundary, clipmap-boundary, pool-exhaustion, and resize cases.
- Serialize all runtime work and apply the documented thermal, process,
  Position-1, and same-adapter clock-normalization procedure.
- Use matched coverage, virtual resolution, filter footprint, scene, camera,
  update state, and output resolution for CSM/VSM comparisons.

## Decision Log

| Date | Decision | Reason | Affected Work |
| --- | --- | --- | --- |
| 2026-07-25 | Use paired depth within one virtual mapping, not two clipmap systems. | Duplicate clipmaps would repeat marking, allocation, metadata, culling, wraparound, and fallback work and would not match the requested UE5-style cache separation. | Resource and cache architecture |
| 2026-07-25 | Preserve the current R32 reverse-Z atomic backend as the reference. | Paired caching and culling can be validated without changing the proven nearest-caster rule; raster backend work remains separable. | Depth storage and tests |
| 2026-07-25 | Treat occlusion-aware invalidation only as a conservative rejection aid. | Camera visibility cannot prove that off-screen or hidden geometry casts no world-space shadow. | Invalidation correctness |
| 2026-07-25 | Keep raw performance measurements authoritative. | Same-GPU clock normalization is useful trend evidence but cannot replace matched, thermally valid measurements. | Performance reporting |
| 2026-07-25 | Treat continuously moving directional lights as uncached content while preserving compatible physical allocation ownership. | UE invalidates exact direction changes and uses an uncached path; UVSR can safely avoid releasing fixed-pool mappings when every retained page is marked unresolved and rerendered before sampling. | Moving-light update policy |
| 2026-07-25 | Keep exact light mapping as the production default and reject shadow-only angular hysteresis as a default optimization. | Holding only the shadow basis creates unbounded caster-depth-dependent disagreement with deferred lighting. Exact motion should instead reuse persistent caster source state and apply receiver/coarse policies; an optional day/night controller may quantize the actual shared sun angle for lighting, sky, and shadows together. | Moving-light quality and performance |
| 2026-07-25 | Retain the last successful light-depth origin while the complete projected caster interval remains inside an inclusive 90-percent guard band. | UE separates the cached directional clipmap depth center from small scene-center motion. UVSR additionally includes the camera anchor and exact light, basis, depth-range, cache, and mode gates so retention can only preserve a compatible mapping; invalid bounds retain the legacy rebase path. | Stable depth mapping |
| 2026-07-25 | Fail full for deforming casters until bounds are proven conservative. | Donut's prototype or bind-pose bounds do not prove the current animated silhouette, so localized invalidation cannot safely infer old and new page coverage. | Deformation invalidation |
| 2026-07-25 | Split persistent caster source state from camera-dependent packet projection. | The first shared builder traverses once per rebuild, but camera movement still invalidates matrices and repeats traversal, transforms, bounds, and sorting. UE's cached mesh-command architecture keeps scene draw state independent from view-specific page selection. | Shared packet preparation |
| 2026-07-25 | Treat topology, binding-resource identity, and deforming bounds as correctness state rather than optional optimization policy. | Topology cannot be suppressed by per-object material/transform policies; replaced alpha or geometry resources must not reuse stale Donut binding sets; bind-pose hierarchy bounds cannot exclude unbounded animation. | Per-object invalidation and raster safety |
| 2026-07-25 | Do not remove a traditional caster from a fine receiver-selected clipmap based only on camera distance. | The current receiver selects one clipmap and fallback is page-wide, not per caster. A caster-only clamp could therefore erase an off-screen or hidden shadow instead of finding the same caster in a coarser level. Distance and detail classification may safely restrict only additional coarse-only work until a per-caster fallback representation exists. | Distance and coarse policy |
| 2026-07-25 | Keep manual integer depth filtering as the portable reference and fallback path. | The pinned compiler accepts integer gathers, but legacy R32-uint format-support queries do not establish a portable contract. A capability-gated Advanced Texture Operations path may accelerate supported adapters, while a float mirror or native-depth backend remains a separately measurable future option. | Filtering |
| 2026-07-25 | Permit a separate SM 6.7 `GatherRaw` filtering path only when D3D12 Advanced Texture Operations are reported. | A direct typed-R32-uint runtime probe returned exact bits on the RTX 4090 Laptop while the Intel adapter reports no Advanced Texture Operations support. The SM 6.5 manual-load path remains the portable and failure fallback path, with no resource-format change. | Filtering |
| 2026-07-25 | Do not turn generic Unreal shadow budgets into a claimed UE5 VSM measurement. | No matched public or local UE5 VSM timing exists for this adapter, scene, geometry path, coverage, pool, and filter. Claim-grade comparison requires a matched UE non-Nanite capture or must be explicitly labeled a planning projection. | Benchmark acceptance |
| 2026-07-25 | Use four explicit performance scenarios and two filter-comparison lanes. | Static, 0.1-degree slow yaw, repeated 1.0-degree fast yaw, and continuously moving 0.1-degree sun exercise different cache states. A work-equivalence lane compares matched point/manual filtering, while a quality lane compares accepted output against UE SMRT without pretending unlike tap counts are equal. | Benchmark matrix |
| 2026-07-25 | Keep receiver-subpage masking separate from static-depth HZB culling. | A cached page rendered only for the current receiver mask is not semantically complete for a later camera. Subpage suppression requires persistent request/content masks, subset-triggered dirtiness, transactional publication, and per-tap resolve validity; the first HZB phase will cull only dynamic casters proven behind complete cached static depth. | Page-mask and HZB culling |
| 2026-07-25 | Treat static/dynamic classification and per-object policy transitions as authoritative before policy suppression. | Either transition changes the depth layer or future invalidation contract. Suppressing a coincident transform, material, or deformation event can publish a new classification without refreshing old and new coverage. | Per-object invalidation |
| 2026-07-25 | Track every policy-suppressed historical coverage state until cache debt is resolved. | Live packets can opportunistically raster an intermediate suppressed state into newly dirty pages, so one published snapshot plus the latest observed snapshot cannot prove which state every cached page contains. | Localized invalidation transaction |
| 2026-07-25 | Invoke the exact render-packet cache comparison on every frame that uses packet submission. | A partial outer gate could hide packet-sorting or builder-toggle changes and rebuilt the adaptive classification map before an exact cache hit. The exact cache now owns compatibility, while pending snapshot and packet-mode transitions only disable reuse permission. | Packet preparation |
| 2026-07-25 | Treat every shader-manifest entry used at runtime as an explicit staging dependency. | The compiled `invalidatePages` and scheduled-page-hierarchy binaries existed in the shader staging directory but were absent from `build/bin`, causing startup to fail before runtime validation. | Build and runtime staging |
| 2026-07-25 | Split the sparse resolve family into explicit reference and translation-cache blobs. | A runtime launch proved that the staged shared blob contained only the sixteen reference permutations even though the renderer requested thirty-two combinations. Two complete sixteen-entry blobs keep both independently selectable paths explicit and prevent a partial family from reaching runtime. | Shader compilation and staging |
| 2026-07-25 | Invalidate indirect templates whenever the render-packet cache rebuilds. | Packet preparation may rebuild while GPU-gated submission is disabled. Retaining the old initialized bit could make a later re-enable submit stale arguments for the previous packet set. | Packet-cache correctness |
| 2026-07-25 | Build the first static-depth hierarchy only from complete paired-static pages and use reverse-Z minimum reduction. | A dynamic caster/page pair is safely rejected only when every covered hierarchy cell contains valid static depth nearer than the caster; empty cells, stale owner tags, static-dirty pages, scatter work, or uncertain depth must fail open. | Page-mask and HZB culling |
| 2026-07-25 | Count resident dirty pages rather than every page-table entry carrying initialization dirty bits. | Nonresident entries deliberately remain fully dirty so future allocations cannot expose uninitialized depth. Counting them as pending raster work made a clean 729-page cache appear to contain 23,847 dirty pages. | Runtime diagnostics |
| 2026-07-25 | Use balanced progressive Poisson subsets for named optimized profiles while retaining the exact legacy stride ordering. | The original four- and eight-tap subsets were spatially biased. A fixed progressive order provides more even early subsets, preserves the same sixteen-sample kernel, and adds no runtime selection cost because each ordering is an explicit shader family. | Filtering |
| 2026-07-25 | Implement deferred static-depth merging as an independent raster permutation, not as an HZB-dependent feature. | UE writes static depth to one slice and merges it after raster. UVSR can fuse `max(merged, static)` into the existing post-raster hierarchy scan, but paired-depth correctness must remain available when HZB is disabled or unavailable and the legacy dual-atomic path must remain exact. | Paired depth and raster |
| 2026-07-25 | Latch any lazy failure of the allocated deferred raster permutation to the exact dual-atomic reference until the feature is toggled off. | Shader creation alone cannot prove that every lazy graphics pipeline will instantiate. A failed optional permutation must return white for the failed transaction, rebuild once with the reference pass, and avoid an indefinite per-frame retry loop, including effective-unpaired moving-light frames. | Optional raster fallback |
| 2026-07-25 | Apply receiver-distance LOD only to receiver selection, with the coarsest level retained as an independent complete fallback. | A camera-distance caster rejection can remove a distant or hidden caster that shadows a near receiver. Identical mark-and-resolve receiver thresholds reduce fine-page demand without weakening world-space caster coverage. | Receiver distance and moving-light recovery |
| 2026-07-25 | Never receiver-mask paired static casters, and track dynamic page coverage before reusing any partially rendered dynamic slice. | UE disables receiver masks for cached static geometry because static depth must remain complete. UVSR can preserve zero-work dynamic reuse only when the current mask is a subset of transactionally published rasterized coverage; expansion or feature disablement must dirty the dynamic slice. | Receiver-subpage masks |
| 2026-07-25 | Treat raster submission and page publication as one transaction. | A failed material, binding, pipeline, packet, or argument setup after a page clear must not reach hierarchy publication, finalization, visibility resolve, or cache-state commit. Sparse failure returns white and latches a full resource clear; dense failure returns white and redraws from clear depth on the next frame. | Raster correctness |
| 2026-07-25 | Keep spatial filter rotation cache-stable until stochastic visibility has an explicit cache identity. | A frame-varying filter phase conflicts with static request reuse and the eight cached full-resolution visibility slots. Fixed per-pixel rotation can decorrelate the kernel without breaking zero-work reuse; temporal blue noise remains a later explicit policy. | Filtering and static reuse |
| 2026-07-25 | Keep Quality on exact receiver clipmap selection and zero moving-light bias until a transition band is implemented. | A global one-level bias degraded every near receiver during exact light motion, while enabling a hard distance threshold in the quality path would introduce a possible clipmap seam. Balanced and Performance may exercise continuous receiver-distance coarsening while the exact zero-bias path remains available. | Receiver distance and moving-light quality |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-25 | Project preflight `/root` | Active | This plan | Dirty worktree, branch, base, constraints, UE5 private-source access, active plans, worktrees, and roadmap inspected | Finish source audits and freeze the first CPU/GPU contract before editing implementation files |
| 2026-07-25 | Paired static/dynamic depth `/root` | Implemented; Runtime Validation Pending | One page table and physical owner with merged slice 0 and persistent static slice 1 | Focused deterministic tests and Release `uvsr` build passed | Validate restore, static/full clear, alpha-tested casters, and visual equivalence at runtime |
| 2026-07-25 | Shared six-clipmap packet builder and precomposed receiver transforms `/root` | Implemented; Runtime Validation Pending | Shared caster traversal/projection plus independently toggleable world-space transform reference | Focused deterministic tests and Release `uvsr` build passed | Retain fail-open legacy builder and randomized transform parity coverage |
| 2026-07-25 | Localized caster invalidation and adaptive cache classification `/root` | Hardened; Runtime Validation Pending | Strong object/geometry snapshots, exact old-plus-new class-aware dirty pages, latched invalidation, receiver-depth union extension, deforming fail-open, O(1) promotion deadlines, and finite-drain packet reuse | Independent focused Release executable, focused CTest, full Release `uvsr` build, and scoped diff check passed | Validate rigid motion, deforming fail-open, removal, alpha changes, disabled/re-enabled SVSM, and finite-budget drain at runtime |
| 2026-07-25 | Sparse opaque raster specialization `/root/svsm_opaque_specialization` | Implemented; Runtime Validation Pending | Position-only opaque shader and material-free pipeline with independent legacy fallback | Focused Release tests and full Release `uvsr` build passed, including shader compilation | Validate specialized and alpha-tested PSOs and output parity on the GPU |
| 2026-07-25 | Scheduled-page hierarchy `/root/hierarchical_page_mask_design` | Implemented; Runtime Validation Pending | Per-clipmap 8-by-8 any/static scheduled-tile masks as a conservative negative front end to the exact packet scan | All affected permutations compiled; focused executable, focused CTest, full Release `uvsr` build, exhaustive/random CPU models, and independent source review passed | Measure true rejects versus coarse false positives |
| 2026-07-25 | Page-safe bilinear convention `/root` | Corrected; Runtime Validation Pending | Manual page-safe bilinear filtering now uses the hardware texel-center convention and ignores exactly zero-weight corners before virtual-page translation | All 32 resolve permutations compiled; deterministic center, quarter-texel, and page-boundary tests plus Release build passed | Add an explicit zero-weight adjacent-page regression model and inspect close-up output before selecting further kernel changes |
| 2026-07-25 | Moving-light uncached and recovery policy `/root/moving_light_cache_impl` | Implemented; Runtime Validation Pending | Exact pointer-plus-light-basis key, allocation-preserving merged-only moving path, paired rebuild on the first stable frame, and ten-successful-frame recovery state | Independent Release `uvsr` and focused-test build, executable, focused CTest, DXIL compilation, source audit, and diff check passed | Profile continuously moving sun, stop/start transitions, resource failure retry, and visible recovery |
| 2026-07-25 | Persistent shared-caster source cache `/root/persistent_packet_builder_design` and `/root` | Implemented; Runtime Validation Pending | Transactional scene-source records cache stable identity, transforms, bounds, geometry, material, alpha state, classification, and shadow signatures independently from light-space and camera-dependent projection | Focused deterministic tests and Release `uvsr` build passed; exact outer packet-cache hits retain the zero-traversal path | Validate first miss, light-only rebuild reuse, camera movement reuse, removal, topology replacement, deformation fail-open, and failed-frame transactionality |
| 2026-07-25 | Moving-light cache policy `/root/moving_light_cache_impl` | Implemented; Runtime Validation Pending | Exact committed light-pointer-and-basis state machine, allocation-preserving content invalidation, effective unpaired moving raster, paired first-stable rebuild, and ten-successful-frame LOD recovery | Deterministic transition/retention matrix, focused Release executable and CTest, full Release `uvsr` and DXIL build, and scoped source audit passed | Validate continuous sun motion, first- and second-stable frames, pool pressure, toggles, and bias recovery at runtime; feed the continuous recovery factor into the receiver-distance clamp |
| 2026-07-25 | Per-object invalidation Phase B `/root/svsm_per_object_invalidation` | Implemented; Runtime Validation Pending | Public stable-identity resolver, all four object modes, authoritative mode and classification transitions, suppression-debt coverage history, transactional generation, localized structural edits, and conservative material/content fallback | Release focused target and `uvsr`, focused executable, focused CTest, policy/configuration/failure matrices, stable-identity renumber/removal/re-add tests, and independent source review passed | Runtime-test mixed policies, localized motion and removal, material/content full fallback, and failure recovery |
| 2026-07-25 | Lean alpha-tested raster specialization `/root/svsm_lean_alpha_bindings` | Implemented; Runtime Validation Pending | Independent sparse shadow-only material layout with constants, base/diffuse, and opacity only; explicit-opacity precedence; scalar-opacity invalidation; full GBuffer reference fallback | Release focused target and `uvsr`, focused executable, focused CTest, DXIL resource reflection, and scoped diff check passed | Runtime-test alpha foliage output, live layout toggling, and frame-time benefit |
| 2026-07-25 | Static-depth HZB and receiver-mask architecture `/root/svsm_hzb_page_mask_design` | Phase 1 Implemented; Dynamic Runtime Workload Pending | Compact 86-word-per-physical-page hierarchy over complete paired-static reverse-Z depth; receiver-subpage masking remains deferred behind request/content validity | All four fill permutations and hierarchy stage compiled; exhaustive 8-by-8 rectangular-query parity, tag/bootstrap, reverse-Z, fail-open, toggle, Release executable, focused CTest, full `uvsr`, blob enumeration, and diff checks passed; runtime startup and static warm visual review passed | Exercise mixed static/dynamic casters and camera motion with HZB off/on; inspect queries, rejects, fail-opens, image parity, and cost before enabling a later invalidation-rejection phase |
| 2026-07-25 | Comparison-target audit `/root/distance_coarse_policy_design` | Complete | Matched-workload audit of local CSM/SVSM evidence, authenticated UE5 VSM pass ownership, and proposed static/slow/fast/moving-sun protocol | Repository timing artifacts, active plans, authenticated UE5 source, and official Epic behavior documentation cross-checked | Existing measurements cannot prove the headline because resolution, coverage, depth, filtering, build identity, and motion differ; collect the new matched matrix before any final performance claim |
| 2026-07-25 | Integer-filter capability audit `/root/svsm_filter_gather_design` | Complete; Integration Pending | Portable SM 6.5 manual loads plus optional D3D12 SM 6.7 `GatherRaw`, balanced progressive Poisson subsets, zero-weight boundary fix, and cache-key correction | Pinned DXC compilation, D3D12 capability queries on both adapters, and an RTX typed-R32-uint 2-by-2 sentinel runtime probe | Integrate after cache correctness work; Intel and every capability or pipeline failure retain the exact manual path |
| 2026-07-25 | Exact packet-cache ownership and classifier deferral `/root` | Implemented; Runtime Validation Pending | Every enabled packet frame performs the complete O(1) cache-key comparison; adaptive static classification is rebuilt transactionally only after a miss and carries a committed generation; packet rebuilds now invalidate stale indirect templates across GPU-gating transitions | Release focused target and `uvsr`, focused executable, focused CTest, exhaustive reuse-permission and generation-wrap tests, scoped diff check, and independent source review passed; the review's one stale-template defect is fixed with a deterministic transition test | Rebuild the integrated HZB candidate, then measure packet CPU median and tail on rotation, page-aligned translation, and GPU-gating off/on |
| 2026-07-25 | Sparse shader runtime staging `/root` | Corrected And Runtime Verified | Added missing compute binaries, explicit HZB hierarchy staging, and independently staged reference and translation-cache resolve blobs | All required blobs are nonempty; the fill blob contains all four scheduled-mask/HZB combinations; both resolve blobs contain all sixteen intended permutations; Release relink and runtime startup passed without a missing-shader modal | Preserve explicit staging for every new filtering or moving-light shader family |
| 2026-07-25 | HZB diagnostics and static runtime review `/root` | Complete For Static Workload | Resident-dirty counter semantics, split HZB status/counters, and static Sponza Position-1 review | Release `uvsr`, focused executable, focused CTest, and diff check passed; runtime showed normal continuous shadows, 729 resident pages, zero resident dirty/rendered pages, zero-work warm GPU stages, 1.3 MiB hierarchy storage, and no unsupported state | Static Sponza has no dynamic caster/page pairs, so zero HZB queries are expected; collect motion and mixed-caster evidence separately |
| 2026-07-25 | Balanced page-safe filtering `/root/svsm_balanced_filter_phase` and `/root` | Complete; Further Kernel Work Pending | Explicit legacy and balanced resolve families, exact page-footprint marking, progressive adaptive probes, and independently staged translation-cache variants | All 64 runtime permutations compiled and staged; focused Release executable, focused CTest, full Release `uvsr`, and diff check passed; a low-elevation shadow-rich Sponza review showed continuous output and no page seams at eight taps, while both orderings converged visually at sixteen taps | One tap remains visibly hard and the current 16-tap footprint is still sharper than the requested close-up target; research wider and higher-speed page-safe kernels without removing the exact path |
| 2026-07-25 | Paired-cache UE source cross-check `/root/svsm_paired_cache_ue_audit` | Complete | Exact comparison against UE5 release `7deeb413d3dc1fc034f48d1aacc0861301829d32` | Allocation, restore, wraparound, eviction, transition, and ownership logic found fundamentally sound; review identified transactional submission publication, light-depth origin churn, loose snapshot bounds, alpha-static eligibility, invalidation HZB, and duplicate static atomics as the highest remaining gaps | Repair raster submission publication first, then add committed depth-origin guardband and OBB/depth-local invalidation before broader policy work |
| 2026-07-25 | Committed light-depth-origin guard band `/root/svsm_depth_origin_guardband` | Implemented; Runtime Validation Pending | Independently toggleable inclusive 0.90 guard using the projected root AABB interval plus camera anchor and the last successful sparse mapping | Exhaustive interval, boundary, invalid-input, compatibility, and failed-frame commit tests; focused Release target and executable, focused CTest, full Release `uvsr`, and diff check passed | Runtime-test stationary-camera scene-bound jitter, page-aligned camera motion, invalid bounds, toggle changes, and first out-of-guard rebase |
| 2026-07-25 | Deferred static-depth merge `/root/svsm_deferred_static_depth_merge`, `/root/svsm_deferred_merge_independent_review`, and `/root` | Implemented And Runtime Verified | Static casters write only the persistent slice; scheduled static-dirty pages merge with reverse-Z maximum before finalization; exact dual-atomic reference remains independently selectable | Release focused and full builds, deterministic randomized equivalence, focused executable and CTest, independent UE5 source review, canonical shader-key staging repair, lazy-pipeline fallback latch, low-sun visual parity, deferred off/on parity, HZB off/on merge-only validation, and warm static zero-work status passed | Add a dedicated UE-shaped merge-only kernel and static-dirty compact list after measuring nonzero static and dynamic update workloads; keep the current fused HZB path |
| 2026-07-25 | UE5 culling and invalidation cross-check `/root/ue_vsm_culling_invalidation_audit` | Complete; Integration Pending | Tight OBB/depth invalidation packets, static-first then dynamic-HZB invalidation ordering, receiver-subpage completeness rules, receiver-driven distance LOD, and selective fallback-only coarse policies | Authenticated UE5 release source and current UVSR snapshot, hierarchy, packet, and page-marking paths cross-checked, including the critical static-cache receiver-mask prohibition | Implement tight OBB/depth candidates first, then dynamic-only invalidation rejection, persistent dynamic coverage masks, and fallback-only coarse policy; every missing tag, bound, owner, or deformation envelope fails open |
| 2026-07-25 | Receiver-distance and continuous moving-light LOD audit `/root/svsm_receiver_distance_policy_design` | Design Complete; Integration Pending | Strict threshold-loop receiver selection using camera-view distance, conservative tile intervals, continuous ten-frame recovery, exact cache keys, and level-five fallback retention | UE mobility and clipmap selection, UVSR mark/resolve/precomposed paths, constant-buffer space, cache identity, tiled conservatism, and edge tests cross-checked | Implement with the exact path off by default, then validate per-pixel and tiled containment, recovery, camera motion, and continuous sun before selecting profile defaults |
| 2026-07-25 | Raster submission transaction `/root/svsm_submission_transaction_fix` | Complete; Runtime Failure Injection Deferred | Dense and sparse failure propagation, packet-state prevalidation, malformed-item abort, partial-timer retirement, and exact packet-cache comparison | Focused Release target and executable, focused CTest, full Release `uvsr`, diff check, pure failure/action/accounting tests, and independent clean source review passed | Exercise ordinary runtime startup now; retain white/full-rebuild behavior for any naturally occurring resource failure without adding a shipping injection seam |
| 2026-07-25 | UE5 filtering cross-check `/root/svsm_filter_ue_research` | Complete; Integration Pending | Current fixed-radius Poisson receiver compared with UE directional SMRT, receiver-plane bias, blue-noise sampling, output dither, and mapped fallback | Page-safety, load counts, cache identity, marking halos, advanced-texture capability, and deterministic/runtime test requirements audited | First candidate is independently toggleable radius 4.5--6 plus cache-stable spatial rotation and receiver-plane bias; clipmap crossfade and capability-gated `GatherRaw` follow before a bounded SMRT experiment |
| 2026-07-25 | Moving-sun hysteresis and quantization audit `/root/svsm_moving_sun_hysteresis_design` | Complete; Exact-Path Work Pending | UE exact-key behavior, four-phase experimental held-basis model, continuous receiver bias, and shared application-level sun quantization candidates | Exact UE cache key, ten-frame mobility recovery, all-six-clipmap mapping invariants, angular error by caster depth, insertion sites, tests, and runtime matrix cross-checked | Implement persistent source reuse and continuous receiver bias first; later add exact shared-sun steps of 0.05 degrees for Quality and 0.10 degrees for Performance in a day/night controller, retaining 0 degrees as exact |
| 2026-07-25 | Receiver-distance clamp and continuous moving-light recovery `/root` | Implemented; Runtime Validation Pending | Per-pixel and conservative tiled camera-view-distance thresholds are identical in marking and resolve, participate in page-request and visibility cache identity, retain level five as a complete fallback, and shift spatial thresholds continuously during recovery | All mark and resolve shader permutations compiled; focused Release executable, focused CTest, randomized and boundary policy tests, full Release `uvsr`, and diff check passed | Inspect 30/60-meter-style transition boundaries, compare exact/continuous policies under SunSlow, add a transition band before considering Quality enablement, and measure page/raster reduction |

## Risks and Escalation Triggers

- Donut does not expose every UE5 primitive mobility, deformation, occlusion,
  or material revision signal. Missing evidence must invalidate conservatively.
- A static/dynamic merge that requires two complete marking or allocation
  systems is rejected; it must share page requests and addressing.
- Receiver-depth visibility cannot independently prove that a caster is
  irrelevant to world-space shadows.
- Light rotation changes directional projection compatibility. Moving-light LOD
  bias can reduce update pressure but cannot reuse incompatible depth.
- Traditional geometry may remain raster-bound without Nanite. Distance clamps
  must remain conservative enough to preserve silhouettes and important
  casters.
- Filtering must never assume neighboring virtual pages are physically
  adjacent.
- Runtime benchmarking pauses when the user is actively using the machine or
  the documented safety gate fails; source analysis and deterministic testing
  continue during those periods.

Stop and ask the user only if:

- a required implementation would modify Bend, Donut, NVRHI, a submodule,
  `main`, Git history, or remote state;
- correctness requires a visible quality tradeoff that cannot retain a
  reference mode;
- stable caster identity or conservative bounds cannot be obtained without a
  renderer-wide scene-database change.

## Completion

- Final integrated commit: intentionally none
- Verification summary: pending
- Independent review: pending
- Coming Soon and documentation update: pending
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: all implementation phases active
- Active ownership released: no
- Archived to completed or abandoned path: pending
