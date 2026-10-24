# Adaptive MSAA and Compact Visibility MSAA

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/adaptive-msaa-visibility` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\adaptive-msaa-visibility`
- Base commit: `578496047c4753077e36f39a68df8c2c323cee58`
- Started: 2026-08-03
- Last updated: 2026-08-04
- Planned archive:
  `docs/exec-plans/completed/adaptive-msaa-visibility.md`

## Goal and Done Condition

Goal:

Implement one UVSR-native adaptive multisample architecture with three
selectable implementations: the unchanged full per-sample reference, adaptive
G-buffer MSAA, and adaptive compact-visibility MSAA. Preserve exact final
surface ownership, reconstruct supported opaque triangles safely, share only
bounded surface-invariant work, and fall back to reference shading whenever
identity, reconstruction, capacity, or shading coherence is uncertain.

Done when:

- [ ] The reference implementation is unchanged, remains the default, and is
  the fail-closed fallback for every unsupported or invalid condition.
- [ ] A single raster/future-DXR `SurfaceKey` and `SurfaceHit` contract has
  lossless generation-aware packing, invalidation metadata, and deterministic
  CPU/GPU tests.
- [ ] Adaptive G-buffer MSAA loads each sample descriptor once, groups by exact
  final key, separates unsafe shading, preserves uncovered background, and is
  correct at 2x, 4x, 8x, and 16x.
- [ ] Compact visibility replaces the full multisampled G-buffer for supported
  opaque triangles, reconstructs the complete PBR surface with correct explicit
  texture gradients and motion, and safely falls back for unsupported content
  and overflow.
- [ ] Diagnostics are default-off, benchmark-excluded, and expose the requested
  views, inspector fields, counters, freeze/reference comparison, and per-pass
  timers without readbacks or dual rendering during accepted measurements.
- [ ] Unit, contract, shader-permutation, resource-lifetime, resize/reload,
  image-diff, animated error, overflow, smoke, independent rendering review,
  and matched Position-1 performance checks pass at every sample count.
- [ ] Neither adaptive implementation becomes the default until the exact
  candidate receives visual acceptance and demonstrates a matched full-frame
  improvement within the documented image-error limits.

## Scope

In scope:

- The Forge `Visibility_Buffer` implementation at
  `a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b` as the primary Apache-2.0
  implementation source, including exact provenance and required notices.
- Supplementary representation and grouping evidence from AMD Capsaicin at
  `914b91596cd119eda85fbc1d3c7ee6ac391b1452`, Burns and Hunt's BSD-style
  reference code, and Epic's public Lumen technical documentation.
- UVSR-owned HLSL/NVRHI resources, passes, scene lookup tables, material access,
  topology and generation invalidation, grouping, shading, resolve,
  diagnostics, tests, documentation, and performance evidence.
- Supported sample counts 2x, 4x, 8x, and 16x on DirectX 12.

Non-goals:

- Editing `donut/`, enabling Vulkan or DirectX 11, changing the default AA
  method, or introducing cross-pixel SRAA-style radiance reconstruction.
- Treating `SV_Coverage`, material, depth, barycentrics, lighting, normals,
  cache coordinates, or sample position as surface identity.
- Assuming one radiance value is valid for every same-triangle sample.
- Building ray tracing or cache systems now; only their shared identity and
  derived-address contract is established.
- Publishing, pushing, opening a pull request, merging, or releasing.

Affected subsystems and paths:

- `src/uvsr.cpp`, UVSR settings/state, render-target and pass ownership.
- Existing first-party PBR G-buffer, deferred lighting, resolve, and shared HLSL.
- New first-party adaptive-MSAA CPU/shared/HLSL modules and focused tests.
- `src/shaders.cfg`, `CMakeLists.txt`, `README.md`, PBR/AA documentation, and
  third-party notice/provenance records.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/shaders.cfg`, `src/uvsr.cpp`, global
  settings, all CPU/HLSL binding contracts, generated registries, provenance,
  execution-plan, and integration documentation.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`;
  live `refs/heads/main` resolved to the requested
  `578496047c4753077e36f39a68df8c2c323cee58` on 2026-08-03.
- Local versus remote state: the original checkout's local `main` is diverged
  (`5e300c91e253441edceecd527cdf0161e2b97820`, ahead 2 and behind 21) and is
  untouched; the feature worktree is a clean branch at the exact live remote
  commit.
- Verified source commit/build: source is the exact requested canonical commit;
  its nearest trustworthy build/verification manifest is still to be located
  or re-established before feature performance comparison.
- GPU, scene, camera, resolution, and settings preset when relevant: Brock's
  documented laptop; Position-1 control; exact adapter, scene, camera,
  resolution, presentation state, and settings will be captured in the raw
  benchmark manifest before measurement.
- Known pre-existing failures: none established yet for this isolated worktree.
- Active-work overlap: PR #10 changes shared screen-space visibility helpers and
  PR #11 changes visibility tests; local Bend, CSM, and SVSM projects touch
  neighboring deferred-lighting/visibility code but are not inputs to this
  exact-base implementation. Integration with any of them is deferred and must
  be semantic and serialized by their established owners.
- Visible Codex tasks: only this Adaptive MSAA task was active in the UVSR
  project at preflight.
- Policy versions: exact-base `AGENTS.md` `2026-08-03.1`; exact-base UI
  reference `2026-08-03.8`. The initial preflight accidentally read
  `AGENTS.md` `2026-07-30.1` and UI reference `2026-07-29.3` from the diverged
  original checkout. Both mismatches were detected before renderer or UI
  implementation, that scope stopped, and both exact-base documents were read
  completely and reconciled here.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Exact UVSR base | `578496047c4753077e36f39a68df8c2c323cee58` | Verified | All work |
| The Forge source audit | Commit `a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b`, license/provenance map | Complete | Architecture and HLSL |
| UVSR baseline map | Existing per-sample resources, bindings, lighting, UI, and tests | Complete | Interface freeze |
| Exact-base technical baseline | Release renderer/PBR build and all registered tests | Complete | Regression oracle |
| Identity contract | Lossless key packing, generation/topology invalidation, raster/DXR parity | Frozen | Both adaptive modes |
| Shared loading/grouping contract | One sample load, deterministic grouping, coherence split, fail-closed reasons | Frozen | Both adaptive modes |
| Adaptive G-buffer correctness | Reference-matched implementation at all sample counts | Source complete; rebuild pending | Compact visibility |
| Compact scene access | Instance/geometry/index/vertex/material/texture lookup and reconstruction | Source complete for the qualified first slice; rebuild pending | Compact visibility |
| Diagnostics and error policy | Default-off controls, counters, views, inspector, spatial/temporal limits | Source complete; ABI rebuild pending | Validation |
| Image-validation runner | Frozen-seed 180-job capture, exact token receipts, schema-2 manifest, and evaluator | Source complete; build/runtime evidence pending | Validation |
| Testing window | Full clean thermal/process preflight and Position-1 bracket | Available for correctness/visual work; performance bracket later | Accepted benchmarks |
| Visual acceptance | Exact artifact and settings accepted after final repair | Deferred | Default decision |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Logical identity is `SurfaceKey = { stableInstanceHandle, geometryIndex,
  geometryLocalPrimitiveIndex }`.
- GPU visibility is `uint2(geometryInstanceHandle,
  geometryLocalPrimitiveIndex)`. `geometryInstanceHandle` losslessly resolves
  stable instance plus geometry and detects slot reuse through a generation or
  topology epoch. No silent truncation or aliasing is allowed; invalid and
  overflow encodings select reference fallback.
- `SurfaceHit = { SurfaceKey, barycentrics, frontFace }`. Raster output and
  future DXR `{ InstanceIndex, GeometryIndex, PrimitiveIndex }` mapping produce
  the same logical identity.
- Topology, LOD, deformation, and material revisions are explicit validation
  metadata, not key fields. Caches, cards, picking, and future integrations
  derive their own addresses from `SurfaceHit`.
- `SameSurface` is exact key equality. `CanShareShading` is a separate,
  conservative depth/position/BRDF coherence decision and may subdivide a
  same-surface group or select complete reference shading.
- All implementations use one sample-descriptor loading, grouping, shading,
  representative, weighting, fallback-reason, and resolve contract. A
  deterministic lowest-sample-index representative is the initial rule unless
  measured quality requires a different deterministic policy.
- Adaptive G-buffer begins with multisampled `RG32_UINT` identity unless a
  smaller encoding proves exact and retains a validated overflow fallback.
- Compact visibility stores multisampled key plus depth and only reconstruction
  payload demonstrated cheaper than recomputation. Unsupported raster/material
  paths route through a complete reference-compatible path rather than
  masquerading as compact visibility.
- Uncovered samples retain the resolved background contribution. Safe group
  weights are `groupSampleCount / totalSampleCount`; sample-dependent shadow,
  screen-space, local-light, sharp-specular, and visibility terms remain
  per-sample unless a validated bound permits sharing.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| SRC-FORGE | `/root/forge_source` | Read-only feature worktree/reference sources | Exact UVSR and Forge commits | None | Exact source URLs | Complete |
| MAP-UVSR | `/root/uvsr_architecture` | Read-only feature worktree | Exact UVSR base | None | Clean worktree | Complete |
| REVIEW-DESIGN | `/root/validation_design` | Read-only feature worktree/reference sources | Exact UVSR base and cited sources | None | User contract | Complete |
| CONTRACT | `/root` | Feature worktree | Exact UVSR base | Plan and shared contracts | Explorer handoffs | Complete |
| IMPL-CORE | `/root/forge_source` | Shared feature worktree | Frozen core contract below | `src/adaptive_msaa.{h,cpp}` and `tests/adaptive_msaa_tests.cpp` only | CONTRACT | Complete; review repaired and closed |
| IMPL-DRAW | `/root/uvsr_architecture` | Shared feature worktree | Frozen draw contract below | New adaptive G-buffer pass, draw-loop, raster-contract, VS, and PS files only | CONTRACT | Complete; review repaired and closed |
| IMPL-SCENE | `/root/validation_design` | Shared feature worktree | Frozen compact-scene contract below | New adaptive compact GPU-scene snapshot and focused tests only | CONTRACT and DESIGN-COMPACT | Complete; focused tests pass |
| IMPL-LIGHT | `/root/uvsr_architecture` | Shared feature worktree | Frozen adaptive-lighting contract below | One new inline adaptive G-buffer lighting shader only | IMPL-CORE and IMPL-DRAW | Complete; all permutations compile |
| IMPL-LIGHT-OPT | `/root/forge_source` | Shared feature worktree | Reviewed inline shader | Adaptive inline lighting shader only | IMPL-LIGHT and PERF-DESIGN | Complete; covered-descriptor loading optimized |
| IMPL-DIAG-CORE | `/root/forge_source` | Shared feature worktree | Frozen diagnostics contract below | New portable diagnostics state and focused tests only | CONTRACT | Complete; focused tests pass |
| IMPL-DIAG-ERROR | `/root/diagnostics_error_gate` | Shared feature worktree | Reviewed four-domain diagnostics and adaptive lighting | Diagnostic core/pass/shader files and focused tests listed below | IMPL-DIAG-CORE and IMPL-DIAG-GPU | In Progress |
| IMPL-RASTER-MAP | `/root/uvsr_architecture` | Shared feature worktree | Current reviewed dirty candidate | New raster scene-map module and focused tests only | IMPL-CORE and IMPL-SCENE | Complete; integrated and focused tests pass |
| IMPL-GBUFFER | `/root` | Feature worktree | Core revision | Adaptive G-buffer resources, shaders, passes, tests | IMPL-RASTER-MAP | In Progress |
| IMPL-COMPACT-RASTER | `/root/validation_design` | Shared feature worktree | Reviewed draw/key contract | New compact visibility raster/draw files only | IMPL-DRAW and IMPL-SCENE | API complete; two-phase prepare/submit refinement in progress |
| IMPL-COMPACT-SHADE | `/root/uvsr_architecture` | Shared feature worktree | Frozen compact scene/key/PBR contracts | New compact reconstruction, closest-surface resolve, and lighting files only | IMPL-CORE and IMPL-SCENE | Assigned |
| IMPL-COMPACT-SHADE-OUTCOMES | `/root/compact_failure_path_audit` | Shared feature worktree | Released prepared compact shade and predicted directional producer contract | Precreated lighting outcome variants and focused tests | IMPL-COMPACT-SHADE and IMPL-SHADOW-PREP | In Progress |
| IMPL-SHADOW-PREP | `/root/directional_visibility_prepare` | Shared feature worktree | Current screen-space directional-shadow pass | Prepared command-free producer token and focused tests | IMPL-COMPACT-SHADE | In Progress |
| IMPL-COMPACT | `/root` | Feature worktree | G-buffer-correct revision | Compact scene access, reconstruction, shading, fallback | IMPL-GBUFFER | Planned |
| IMPL-DIAG-GPU | `/root/forge_source` | Shared feature worktree | Frozen diagnostics/error contracts | New diagnostic GPU pass, shader, and focused tests only | IMPL-DIAG-ERROR and IMPL-CORE | Assigned |
| IMPL-DIAGNOSTICS | `/root` | Feature worktree | Both adaptive modes | UI, views, counters, inspector, comparison/freeze | IMPL-COMPACT | Planned |
| IMPL-COMPACT-TEXTURES | Assigned texture writer | Shared feature worktree | Passing untextured/mixed-fallback checkpoint | Compact scene texture mapping, alpha raster, reconstruction HLSL, and focused/GPU tests only | IMPL-COMPACT and scope audit | In Progress |
| REVIEW-COMPACT-TEXTURES | Independent read-only reviewer | Shared feature worktree | Same checkpoint and user contract | None | Scope audit and writer handoff | In Progress |
| REVIEW-RENDER | Independent reviewer | Read-only final candidate | Final dirty snapshot identity | None | Implementations complete | Planned |
| VERIFY | `/root` | Dedicated feature build/runtime lease | Reviewed candidate | Build/runtime artifacts only | Review repairs | Planned |

## Assignment Contracts

### Src-Forge: Audit Primary and Grouping Sources

- Owner/thread: `/root/forge_source`
- Branch/worktree: read-only feature worktree and separately fetched references.
- Base commit/state: UVSR
  `578496047c4753077e36f39a68df8c2c323cee58`; The Forge
  `a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b`.
- Read scope: cited The Forge visibility-buffer sample, test scripts, license,
  Burns and Hunt reference code, and relevant build wiring.
- Write scope: none.
- No-touch scope: all UVSR files, Git state, build trees, processes, and GPU.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: distinguish exact source behavior from proposed
  UVSR adaptation; enumerate copied/adapted obligations and provenance.
- Deliverable: file/function-level algorithm map, resource transitions,
  simple/complex separation, reconstruction/shading/resolve details, sample
  test coverage, license/notice requirements, and incompatibilities.
- Done when: every requested Forge behavior has an exact source location and a
  recommended UVSR-native adaptation or explicit rejection.
- Required verification: exact commit identity and authoritative license text.
- Allowed Git and external actions: read-only fetch/search/download; no UVSR
  mutation, Git history changes, or publication.
- Stop and report if: a cited commit/source/license cannot be authenticated.
- Handoff revision/artifact: exact-source algorithm, license, and provenance map
  delivered on 2026-08-03; no files changed.
- Handoff acknowledged by/on: `/root`, 2026-08-03.

### Map-UVSR: Map Existing Renderer and Tests

- Owner/thread: `/root/uvsr_architecture`
- Branch/worktree: read-only feature worktree.
- Base commit/state: clean exact UVSR base.
- Read scope: first-party source, shaders, build registration, tests,
  documentation, and ignored verification manifests; `donut/` may be read only
  to understand public interfaces.
- Write scope: none.
- No-touch scope: every file, Git/index/history, builds, processes, and GPU.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: preserve the existing per-sample output and map
  only public Donut/NVRHI interfaces; do not propose dependency edits.
- Deliverable: current MSAA resource/pass/binding/data-flow map, scene identity
  opportunities, UI/Statistics ownership, shader permutation/build/test map,
  known resize/reload paths, and minimum likely first-party change set.
- Done when: coordinator can freeze CPU/HLSL interfaces without guessing.
- Required verification: cite exact files, symbols, and relevant test commands.
- Allowed Git and external actions: local read-only inspection only.
- Stop and report if: the exact base is dirty or a nested policy conflicts.
- Handoff revision/artifact: exact-base renderer, resource, draw-loop,
  GPU-scene, UI, lifecycle, and test map delivered on 2026-08-03; no files
  changed.
- Handoff acknowledged by/on: `/root`, 2026-08-03.

### Review-Design: Validate Identity, Sharing, Diagnostics, and Evidence

- Owner/thread: `/root/validation_design`
- Branch/worktree: read-only feature worktree and cited public sources.
- Base commit/state: clean exact UVSR base.
- Read scope: user contract, AMD Capsaicin exact commit, Epic Lumen technical
  details, UVSR benchmark/test policy, and relevant current renderer code.
- Write scope: none.
- No-touch scope: all files, Git state, builds, processes, and GPU.
- Build directory and runtime/GPU/resource lease: none.
- Dependencies already integrated: none.
- Interface/invariant contract: `SameSurface` never implies constant radiance;
  all proposed sharing tests must fail closed and be testable.
- Deliverable: identity/invalidation review, conservative `CanShareShading`
  policy, compact reconstruction risk list, diagnostics/counter schema,
  spatial/temporal image-error proposal, adversarial scene matrix, and matched
  benchmark acceptance plan.
- Done when: every user-requested validation dimension maps to measurable
  evidence and an automatic reject/fallback condition.
- Required verification: exact supplementary source revisions/URLs and current
  UVSR performance policy.
- Allowed Git and external actions: read-only browsing/inspection only.
- Stop and report if: a source cannot support the claimed design or a requested
  metric cannot be measured without benchmark contamination.
- Handoff revision/artifact: source-backed identity, conservative sharing,
  fallback, diagnostics, error-gate, adversarial-test, and benchmark design
  delivered on 2026-08-03; no files changed.
- Handoff acknowledged by/on: `/root`, 2026-08-03.

### Impl-Core: Implement Surface Identity and Grouping Core

- Owner/thread: `/root/forge_source`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: exact source base plus coordinator-owned README and active
  plan changes; baseline Release build and 32 of 32 CTest tests pass.
- Read scope: frozen contract below, adjacent first-party C++/test style, and
  source-audit handoff.
- Write scope: only new `src/adaptive_msaa.h`, `src/adaptive_msaa.cpp`, and
  `tests/adaptive_msaa_tests.cpp`.
- No-touch scope: every existing file, `CMakeLists.txt`, `src/uvsr.cpp`, all
  shaders and shader registries, documentation, assets, `donut/`, build trees,
  Git/index/history, processes, and GPU.
- Dependencies already integrated: none; the coordinator will wire CMake and
  run all builds/tests after ownership is released.
- Interface/invariant contract: implement the frozen core contract below
  without NVRHI or Donut dependencies. Keep `SameSurface` exact and
  `CanShareShading` separately conservative. Invalid covered samples must select
  an explicit reference fallback; uncovered samples must preserve background.
- Deliverable: production-quality portable C++ core and exhaustive deterministic
  tests for packing, invalid/overflow, generation reuse/exhaustion, epoch reset,
  raster/DXR equivalence and collisions, grouping, representatives, weights,
  background, coherence splitting, and fallback reasons.
- Done when: the three files are self-consistent, comment the invariants and
  exact provenance, and are ready for coordinator CMake wiring and review.
- Required verification: local source inspection only; do not build while the
  coordinator owns the shared build tree.
- Allowed Git and external actions: none.
- Stop and report if: the frozen API cannot represent a required condition or
  an unexpected existing change appears in an owned path.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

### Impl-Scene: Implement Compact GPU-Scene Snapshot

- Owner/thread: `/root/validation_design`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: exact source base plus coordinator documents and disjoint
  IMPL-CORE/IMPL-DRAW files; baseline Release build and 32 of 32 CTest tests
  pass.
- Read scope: DESIGN-COMPACT handoff, public Donut scene/buffer structures,
  public NVRHI D3D12 bindless/event-query APIs, `src/adaptive_msaa.h`, and
  adjacent first-party test style.
- Write scope: only new `src/adaptive_msaa_scene.h`,
  `src/adaptive_msaa_scene.cpp`, and `tests/adaptive_msaa_scene_tests.cpp`.
- No-touch scope: every existing file, all other adaptive paths,
  `CMakeLists.txt`, `src/uvsr.cpp`, shaders and shader registries,
  documentation, assets, `donut/`, build trees, Git/index/history, processes,
  and GPU.
- Dependencies already integrated: none; the coordinator will review the
  interface, wire CMake/application/shaders, and run all builds/tests after
  ownership is released.
- Interface/invariant contract: expose an immutable per-topology snapshot that
  losslessly maps each registered geometry-instance handle to instance,
  geometry, index/vertex, current/previous transform, material, and texture
  access records. Keep strong resource handles alive, use a dedicated immutable
  bindless layout/table with separate texture and raw-buffer spaces, publish
  only at a frame boundary, and retain replaced snapshots until an NVRHI event
  query proves GPU completion. Eligibility is evaluated for the whole opaque
  frame before compact submission and returns one stable fail-closed reason;
  unsupported content never produces a partially compact frame. Do not depend
  on Donut's volatile descriptor-table slot identities or mutate Donut state.
- Deliverable: a narrow UVSR-owned snapshot builder/runtime interface with
  bounds/overflow/stale-epoch/unsupported validation and portable focused tests
  for all logic that does not require a live device. Exact borrowed or adapted
  code carries commit-level provenance and applicable notices.
- Done when: every index and descriptor is bounds checked, retirement cannot
  release in-flight resources, an empty/failed snapshot is explicitly
  ineligible, and the files are ready for coordinator integration.
- Required verification: source inspection only; do not build the coordinator's
  shared tree.
- Allowed Git and external actions: none.
- Stop and report if: the pinned public APIs cannot implement immutable
  descriptors or event-query retirement without a dependency edit, or a
  required material/deformation classification is unavailable before drawing.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Frozen Core Contract

- `SurfaceKey` is exactly logical ownership:
  `{ uint64 stableInstanceHandle, uint32 geometryIndex,
  uint32 geometryLocalPrimitiveIndex }`. `SurfaceHit` adds two stored
  barycentric coordinates and `frontFace`; none of those additions change key
  equality.
- GPU visibility is exactly
  `uint2(geometryInstanceHandle, geometryLocalPrimitiveIndex)`. The first word
  is an opaque registry handle; the second is the geometry-local triangle
  index. `0xffffffff` is reserved for invalid handle and primitive words.
- The 32-bit handle uses 20 slot bits and 12 generation bits. Generation zero
  and all-ones are reserved; valid generations are 1 through `0xffe`. Capacity,
  invalid encodings, primitive-sentinel collision, and generation exhaustion
  fail closed. Generation exhaustion requires a synchronized wider topology
  epoch bump and full registry reset before any slot is reused.
- A registry entry losslessly resolves the full stable instance handle and
  geometry index and records topology epoch plus LOD, deformation, and material
  revisions. Topology or LOD revision changes invalidate primitive meaning and
  therefore the prior handle. Deformation/material revisions remain explicit
  validation metadata and do not silently enter key equality.
- Raster mapping begins with the already resolved geometry-instance handle.
  Future DXR mapping resolves an explicit `{InstanceIndex, GeometryIndex}` pair
  to that same handle before appending `PrimitiveIndex`. Duplicate transient
  mappings to different logical identities are rejected.
- `SameSurface` is valid packed-key equality only. `CanShareShading` additionally
  requires finite values, identical front-face state, no caller-supplied
  sample-dependent blocker, relative linear-depth coherence of at most
  `2^-12`, and world-position span no greater than one eighth of the smaller
  reconstructed pixel footprint. It is pairwise across every member, not only
  representative-to-candidate.
- Grouping loads a caller-supplied descriptor for each of 2, 4, 8, or 16
  samples, visits samples in ascending order, selects the lowest safe sample as
  representative, groups only exact ownership plus `CanShareShading`, and uses
  `groupCount / totalSampleCount` weights. Uncovered samples produce explicit
  background weight. Invalid or unsafe covered samples form deterministic
  reference-fallback singletons. Total background plus group weight must be one
  within a strict floating-point tolerance.
- Stable reason enums must distinguish at least invalid sentinel, slot range,
  generation mismatch, topology/LOD mismatch, capacity/primitive overflow,
  transient mapping collision/failure, unsupported deformation/material,
  unsafe sharing, reconstruction failure, format/sample unsupported, and
  work-list overflow. Values are ABI candidates for later HLSL and diagnostics,
  so tests must lock them.

## Impl-Draw Assignment Contract

- Owner/thread: `/root/uvsr_architecture`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: exact source base plus coordinator documents and disjoint
  IMPL-CORE files; baseline Release build and 32 of 32 CTest tests pass.
- Read scope: DESIGN-DRAW handoff, Donut source named there, existing
  `pbr_gbuffer_ps.hlsl`, and the frozen contract below.
- Write scope: only new `src/adaptive_msaa_gbuffer_pass.h`,
  `src/adaptive_msaa_gbuffer_pass.cpp`,
  `src/adaptive_msaa_geometry_pass.h`,
  `src/adaptive_msaa_geometry_pass.cpp`,
  `src/adaptive_msaa_raster_cb.h`,
  `src/adaptive_msaa_gbuffer_vs.hlsl`, and
  `src/adaptive_msaa_gbuffer_ps.hlsl`.
- No-touch scope: every existing file, IMPL-CORE paths, `CMakeLists.txt`,
  `src/uvsr.cpp`, shader registries, tests, documentation, assets, `donut/`,
  build trees, Git/index/history, processes, and GPU.
- Dependencies already integrated: none; these files may use only current
  Donut/NVRHI public headers and constants. The coordinator will wire the
  registry, build, tests, and application after ownership is released.
- Interface/invariant contract: copy the pinned Donut traversal and buffer-load
  behavior with its complete MIT notices and prominent UVSR modification/exact
  provenance comments. Preserve the current material evaluation and alpha clip.
  Never infer geometry identity from offsets; batch only the exact same mesh,
  geometry pointer/local ordinal, support class, draw ranges, and consecutive
  instances.
- Deliverable: implementation-ready first-party pass, exact draw functions,
  shared 32-byte CPU/HLSL push contract, and raster shaders.
- Done when: all seven files are internally coherent, ResetBindingCache occurs
  on handle-map replacement, local geometry state resets after every push,
  mapping bounds fail to the invalid handle, and no ordinary Donut path changes.
- Required verification: source inspection only; do not build the coordinator's
  shared tree.
- Allowed Git and external actions: none.
- Stop and report if: an owned path unexpectedly exists or a public pinned API
  cannot implement the frozen contract.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Frozen Draw Contract

- The reference mode keeps the existing `PbrGBufferFillPass`,
  `SubmittedTriangleCountingPass`, and Donut `RenderCompositeView` call
  unchanged.
- Adaptive modes use a sibling `AdaptiveMsaaGBufferFillPass` and UVSR-owned
  `RenderAdaptiveMsaaView`/`RenderAdaptiveMsaaCompositeView` copies. The exact
  `DrawItem` supplies the geometry ordinal by pointer equality within
  `MeshInfo::geometries`; an absent ordinal renders the full G-buffer with an
  invalid key rather than dropping the draw.
- The 32-byte push constant is Donut's seven buffer-load words followed by
  `geometryLocalIndex`. The input binding retains instance `t10` and raw vertex
  `t11` in space 1 and adds the global structured handle map at `t12`.
- The vertex shader computes absolute instance index, loads
  `InstanceData.firstGeometryInstanceIndex + geometryLocalIndex`, bounds-checks
  the map, and exports a noninterpolated 32-bit handle. Background clear uses
  handle word zero; valid handles never use generation zero; covered mapping
  failure writes `0xffffffff`.
- The pixel shader reproduces the current PBR material/alpha/normal/motion
  behavior and appends exactly
  `uint2(geometryInstanceHandle, SV_PrimitiveID)`. The key target is target 5
  without motion and target 6 with motion.
- Batching additionally requires identical mesh, geometry, geometry ordinal,
  support class, material/cull/buffers, vertex/index ranges and counts, plus
  exactly consecutive instance indices with checked arithmetic. The stored
  batch identity is flushed before installing the next identity.
- Surface-key render targets are `RG32_UINT`, multisampled, render-target/SRV,
  cleared exactly to zero with `clearTextureUInt`, and appended after AO and
  optional motion. `RG32_UINT` support is checked separately so failure selects
  full per-sample Reference at the otherwise supported sample count.

## Impl-Light Assignment Contract

- Owner/thread: `/root/uvsr_architecture`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: exact source base plus released adaptive core/draw files
  and coordinator-owned integration changes; exact-base baseline remains the
  regression oracle.
- Read scope: unchanged `src/pbr_deferred_lighting_msaa_cs.hlsl`, released
  adaptive raster/key contracts, UVSR PBR helpers, The Forge/Burns source-audit
  handoff, and the frozen contract below.
- Write scope: only new
  `src/pbr_deferred_lighting_adaptive_msaa_cs.hlsl`.
- No-touch scope: every existing file, all other adaptive paths,
  `CMakeLists.txt`, `src/uvsr.cpp`, CPU bindings, shader registries, tests,
  documentation, assets, `donut/`, build trees, Git/index/history, processes,
  and GPU.
- Dependencies already integrated: no CPU binding or registry wiring; the
  coordinator will review, register permutations, bind `t21`, and build after
  ownership is released.
- Interface/invariant contract: retain the reference shader unchanged. Compile
  static 2x/4x/8x/16x and visibility-correction variants. Bind multisampled
  `uint2` ownership at `t21`. Load each sample's key, depth, four G-buffer
  channels, authored AO, coverage state, reconstructed pixel-center world
  position, and all descriptor data exactly once into bounded local arrays.
  Form exact-key groups in ascending sample order with the lowest deterministic
  representative. Keep `SameSurface` separate from a pairwise conservative
  sharing predicate using valid generation/primitive words, finite data,
  matching reconstructed surface channels/orientation, relative linear depth
  at most `2^-12`, and world span no more than one eighth of the smaller pixel
  footprint. Invalid/unsafe samples execute the complete reference-equivalent
  sample path. Never infer identity from coverage or assume same-key radiance.
  Share decoded/material/BRDF invariant work only where exact inputs permit;
  direct/local/shadow/directional/screen-space visibility and view-dependent or
  sharp-specular terms remain per sample unless a cheap explicit bound proves
  complete-radiance sharing. Preserve the pre-resolved uncovered background and
  exact `groupSampleCount / totalSampleCount` weighting. Fail non-finite output
  closed as the reference does.
- Deliverable: an inline grouped compute implementation plus exact provenance
  comments and compile-time guards; do not add the separated work-list variant
  yet.
- Done when: all local arrays are statically bounded at sixteen, every covered
  sample is routed exactly once, invalid keys cannot group, representative and
  weight selection are deterministic, background and visibility correction
  match Reference, and sample-dependent radiance is never broadened by key
  equality alone.
- Required verification: source inspection only; do not build the coordinator's
  shared tree.
- Allowed Git and external actions: none.
- Stop and report if: exact reference parity requires a new CPU/HLSL contract
  beyond `t21`, or any requested sharing rule would trade quality for speed.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl-Diag-Core Assignment Contract

- Owner/thread: `/root/forge_source`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: exact source base plus released adaptive core/draw/lighting
  files and coordinator-owned integration changes; exact-base baseline remains
  the regression oracle.
- Read scope: the frozen diagnostics/error policy, `src/adaptive_msaa.h`, and
  adjacent portable test style.
- Write scope: only new `src/adaptive_msaa_diagnostics.h`,
  `src/adaptive_msaa_diagnostics.cpp`, and
  `tests/adaptive_msaa_diagnostics_tests.cpp`.
- No-touch scope: every existing file, all other adaptive paths, NVRHI/Donut
  renderer code, shaders and registries, CMake, UI, documentation, build trees,
  Git/index/history, processes, and GPU.
- Dependencies already integrated: none; the coordinator will wire the model
  into renderer passes, UI, Statistics, commands, and CMake after review.
- Interface/invariant contract: define default-off diagnostics settings for
  comparison mode, debug view, freeze, and explicit selected-pixel capture;
  define bounded per-frame counters for the 0..16 group histogram, lighting
  evaluations, fallback reasons, invalid/stale/mapping failures, work-list
  occupancy/overflow, and pass timings; define one selected-pixel record with
  at most sixteen sample records containing key, depth, barycentrics, group,
  representative, weight, fallback, and radiance. Accumulation must saturate
  instead of wrapping, reject non-finite error input, keep spatial and temporal
  error separate, compute deterministic percentile summaries from caller-owned
  samples, and expose an explicit predicate proving accepted-benchmark safety
  only when diagnostics, comparison, capture, readback, counters, and dual
  rendering are all structurally disabled. No renderer readbacks or GPU work
  belong in this portable layer.
- Deliverable: production-quality portable C++ model and exhaustive tests for
  defaults, sanitization, saturation, histogram/fallback accounting, percentile
  and maximum error, selected-pixel bounds, reset, and benchmark gating.
- Done when: invalid enum/count/error inputs fail closed, sample-count support
  is exactly 2/4/8/16 where required, and future DXR/cache consumers can reuse
  the records without inventing another identity.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: the portable contract would need NVRHI, renderer-global
  state, or a readback to remain correct.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Raster Map Assignment Contract

- Owner/thread: `/root/uvsr_architecture` after its read-only scene review is
  acknowledged.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: exact source base plus the current coordinator-integrated
  adaptive core, draw, scene, lighting, and diagnostics files. The latest
  focused adaptive/UI/renderer suite passes, and the Release renderer builds.
- Read scope: `src/adaptive_msaa.{h,cpp}`,
  `src/adaptive_msaa_scene.{h,cpp}`, the public Donut Scene/SceneGraph APIs,
  the adaptive raster VS map ABI, and adjacent focused-test style.
- Write scope: only new `src/adaptive_msaa_raster_scene.h`,
  `src/adaptive_msaa_raster_scene.cpp`, and
  `tests/adaptive_msaa_raster_scene_tests.cpp`.
- No-touch scope: every existing file, including CMake, `src/uvsr.cpp`, all
  existing adaptive modules and shaders, documentation, `donut/`, build trees,
  Git/index/history, processes, and GPU.
- Dependencies already integrated: the 20-bit-slot/12-bit-generation core,
  stable-instance allocator, compact handle registry, and raster map binding
  contract. The coordinator will register and integrate the new module.
- Interface/invariant contract: build one transactional, immutable scene
  ownership snapshot whose structured `uint32_t` buffer is indexed by Donut's
  exact flattened `firstGeometryInstanceIndex + geometryIndex`. Resolve stable
  instances by shared-owner control-block identity, reconcile every supported
  triangle geometry against the supplied nonzero topology epoch and explicit
  topology/LOD/deformation/material revisions, and retain invalid sentinels for
  entries that cannot produce an exact raster key. Mapping collisions,
  malformed signed indices, count/arithmetic overflow, invalid generations,
  allocation failure, epoch rollback, or registry exhaustion fail the entire
  new snapshot without mutating the last successful registry state. Material
  and deformation support do not gate ownership in the full G-buffer path;
  they remain separate compact-eligibility metadata. The output buffer must be
  a nonempty four-byte structured SRV in the Graphics queue lifecycle and keep
  strong ownership until its last submitted use retires. Expose CPU handles
  and exact stable-instance/geometry assignments for diagnostics and future
  raster/DXR equivalence checks. Do not allocate compact material, descriptor,
  or reconstruction resources.
- Deliverable: production-quality new module and deterministic tests for
  flattened indexing, stable owner reuse, topology/LOD generation change,
  material/deformation metadata stability, invalid geometry sentinels,
  collision, overflow, stale epoch, transactional failure, and snapshot
  version ordering. Document UVSR-native provenance and the Graphics-queue
  lease explicitly.
- Done when: a valid static/animated triangle scene can supply the exact map
  consumed by `AdaptiveMsaaGBufferFillPass`, unsupported entries cannot forge
  a valid key, and no failure can replace or corrupt the prior successful
  registry state.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: the public scene API cannot prove the flattened mapping,
  the requested lifecycle would require editing Donut, or a shared existing
  file must change.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Light Optimization Assignment Contract

- Owner/thread: `/root/forge_source`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: current coordinator-integrated shader after the
  per-sample finite-HDR accumulation repair; all eight permutations compile.
- Read scope: the adaptive shader, unchanged Reference shader, core grouping
  contract, and PERF-DESIGN handoff.
- Write scope: only
  `src/pbr_deferred_lighting_adaptive_msaa_cs.hlsl`.
- No-touch scope: every other file, CMake, shader registry, tests,
  documentation, `donut/`, build trees, Git/index/history, processes, and GPU.
- Interface/invariant contract: size sample/group storage by the compile-time
  `PBR_DEFERRED_MSAA_SAMPLES`; replace redundant boolean/index bookkeeping
  arrays with bounded bitmasks where behavior stays exact; load channel 2 for
  every sample exactly once to determine raster coverage, then return through
  an exact all-uncovered final-store path before loading other descriptors;
  load every remaining descriptor exactly once for each covered sample.
  Coverage must never derive from key validity. Preserve ascending grouping,
  deterministic representatives, exact raw-channel comparison, pairwise
  sharing checks, singleton Reference fallbacks, visibility/debug precedence,
  per-sample preweighted HDR accumulation, and finite/clamped output. Do not
  add the separated work-list topology in this assignment.
- Deliverable: one reviewable shader reduction with a handoff mapping every
  removed array/load to its exact replacement and identifying any expected
  output-order change (none is preferred).
- Done when: static capacity is exact at 2/4/8/16, uncovered pixels cannot load
  unneeded descriptors or directional visibility, and all covered pixels
  follow the prior grouped shading path without a broadened sharing decision.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: a reduction changes output order, fallback routing,
  descriptor-load count for covered samples, or visibility/debug semantics.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Compact Raster Assignment Contract

- Owner/thread: `/root/validation_design`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: current reviewed dirty candidate with frozen key/map/raster
  ABI and the unchanged Reference path.
- Read scope: existing adaptive G-buffer pass/draw/shaders, compact scene GPU
  records, public Donut geometry/material APIs, pinned Forge visibility raster,
  and current PBR alpha-test source.
- Write scope: only new files prefixed
  `src/adaptive_msaa_compact_raster`, new
  `src/adaptive_msaa_compact_visibility_vs.hlsl`, new
  `src/adaptive_msaa_compact_visibility_ps.hlsl`, and one new focused test
  `tests/adaptive_msaa_compact_raster_tests.cpp`. This may include a new
  compact-only draw-loop `.h/.cpp` under the same
  `adaptive_msaa_compact_raster` prefix.
- No-touch scope: every existing file, CMake, shader registry, `src/uvsr.cpp`,
  all existing shaders/modules/tests, documentation, `donut/`, build trees,
  Git/index/history, processes, and GPU.
- Dependencies already integrated: RG32_UINT key ABI, stable flattened handle
  map, generation encoding, adaptive draw validation, and whole-scene compact
  snapshot. The coordinator will register, bind, and compose the new files.
- Interface/invariant contract: implement a UVSR-owned visibility-only
  graphics pass whose framebuffer contract is exactly multisampled RG32_UINT
  target 0 plus the existing multisampled depth target—never full G-buffer
  MRTs. The VS must resolve the exact flattened geometry-instance handle,
  carry the geometry-local ordinal supplied by UVSR push constants, and emit
  only position plus reconstruction data needed for exact alpha coverage. The
  PS writes `uint2(handle, SV_PrimitiveID)` and must match the current PBR
  alpha-test decision exactly for opaque/alpha-tested triangles, including
  texture transform, sampler, cutoff, and material domain. `SV_Coverage` is
  not identity. A CPU preflight over the exact visible opaque draw list must
  validate every draw/map index, triangle topology, stream/index bounds,
  material/deformation support, and map generation before any compact draw is
  submitted; a failure returns one explicit reason so the coordinator can run
  the complete Reference frame instead. Preserve deterministic triangle
  accounting and do not mutate Donut.
- Deliverable: production-quality compact raster pass/draw loop/shaders and
  device-independent preflight/ABI tests, with exact Donut/Forge provenance
  and required notices in source comments.
- Done when: the pass can rasterize a fully supported opaque view into only
  key+depth, alpha cutouts match the existing PBR G-buffer body, no raw
  `SV_PrimitiveID` can collide across draws, and malformed/unsupported input
  is rejected before compact submission.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: exact alpha parity needs a new shared contract, the
  public draw strategy cannot be replayed for fail-closed preflight, or any
  existing/shared file must change.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Compact Shade Assignment Contract

- Owner/thread: `/root/uvsr_architecture`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: current reviewed dirty candidate with the unchanged
  Reference shader, exact key ABI, compact GPU-scene snapshot, and adaptive
  G-buffer grouping/lighting shader available as the correctness oracle.
- Read scope: `src/adaptive_msaa_scene.{h,cpp}`, compact GPU record layouts,
  `src/pbr_deferred_lighting_adaptive_msaa_cs.hlsl`, current PBR G-buffer and
  lighting helpers, public Donut bindless/material/view records, and pinned
  Forge visibility reconstruction/shading source.
- Write scope: only new files prefixed `src/adaptive_msaa_compact_shade`, one
  optional new HLSL include with that prefix, and one new focused test
  `tests/adaptive_msaa_compact_shade_tests.cpp`.
- No-touch scope: every existing file, every compact-raster-owned file, CMake,
  shader registry, `src/uvsr.cpp`, documentation, `donut/`, build trees,
  Git/index/history, processes, and GPU.
- Dependencies already integrated: `RG32_UINT` multisample key plus depth,
  generation/epoch/revision-aware compact scene records, bindless raw
  index/vertex/instance/material buffers and textures, exact 2x/4x/8x/16x
  adaptive grouping, and the unchanged per-sample Reference oracle.
- Interface/invariant contract: implement a UVSR-owned compute path that takes
  only multisampled key plus depth and the immutable compact snapshot for
  surface reconstruction; it must not require or allocate a multisampled full
  G-buffer. Validate generation, epoch, topology/LOD/deformation/material
  revisions, all record/descriptor/index/primitive/vertex bounds, and finite
  reconstructed data before indirection. Reconstruct perspective-correct
  barycentrics at the exact DX12 sample positions, current/previous object and
  world positions, normals/tangents/front-face handling, UV, material, motion,
  and analytic explicit texture gradients. Match PBR material evaluation and
  alpha-tested coverage semantics. Provide one closest-surface resolve that
  can populate UVSR's existing single-sample visibility/TAA G-buffer outputs,
  plus one multisample compact-lighting path that preserves the adaptive
  shader's sample loading, exact-key grouping, deterministic representative,
  conservative sharing, per-sample lighting, Reference background weighting,
  finite/clamp order, and all four sample counts. Any invalid reconstruction
  must expose an explicit failure result and never invent a surface or mix
  samples; whole-frame CPU preflight remains the primary fail-closed route.
- Deliverable: production-quality new C++/HLSL pass files and device-independent
  ABI/reconstruction tests covering sample positions, perspective
  barycentrics, gradients, transforms/motion, front face/double sided, record
  bounds, epoch/generation/revision mismatch, primitive overflow, and output
  resource compatibility.
- Done when: supported opaque/alpha-tested triangles can be reconstructed and
  shaded from compact visibility without full multisampled G-buffer inputs,
  closest-surface consumers receive reference-compatible single-sample data,
  and unsupported or stale data fails before unsafe GPU access.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: exact DX12 sample positions cannot be represented, the
  current compact scene records omit a required lossless field, alpha parity
  needs an existing shared-file edit, or the path would silently substitute a
  G-buffer allocation.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Diagnostics Error Assignment Contract

- Owner/thread: `/root/forge_source`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: current portable diagnostics core and its passing focused
  tests; renderer diagnostics UI integration is coordinator-owned.
- Read scope: `src/adaptive_msaa_diagnostics.{h,cpp}`, its focused tests,
  `src/agx_tonemapping_ps.hlsl`, and the frozen error policy below.
- Write scope: only `src/adaptive_msaa_diagnostics.h`,
  `src/adaptive_msaa_diagnostics.cpp`, and
  `tests/adaptive_msaa_diagnostics_tests.cpp`.
- No-touch scope: every other file, CMake, renderer/UI/shaders, documentation,
  build trees, Git/index/history, processes, and GPU.
- Dependencies already integrated: deterministic nearest-rank summaries,
  non-finite rejection, separate spatial/temporal state, and selected-pixel and
  counter models.
- Interface/invariant contract: extend the portable model to retain all four
  summaries `hdr.{spatial, temporal}` and `display.{spatial, temporal}`. Define
  scene-linear HDR spatial and signed-residual temporal error with denominator
  floor `2^-10`; define display spatial/temporal error as continuous 8-bit code
  differences supplied after UVSR's exact AgX plus linear-to-sRGB transform
  without dithering. Freeze hard limits: HDR p99 `2^-10`, HDR maximum `2^-7`,
  display p99 `0.5` code, and display maximum `2.0` codes for both spatial and
  temporal domains. Preserve per-frame and pooled gating; empty active masks,
  non-finite input, and invalid domains fail. Add a dedicated error-budget
  rejection counter/reason and a pure deterministic decision that rejects a
  whole group when any member exceeds HDR `2^-10`, display `0.5` code, or is
  non-finite. Post-fallback equality and zero rejections in supported scenes
  remain external hard correctness gates; this layer performs no GPU work.
- Deliverable: reviewed portable API/implementation and exhaustive tests for
  formulas, four independent summaries, p99/max boundaries, active masks,
  signed temporal residuals, single-pixel maximum failure, greater-than-one
  percent p99 failure, empty/non-finite invalidation, and group rejection.
- Done when: the numerical limits are compile-time constants, cannot be tuned
  per scene or mode, and every boundary has deterministic pass/fail coverage.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: representing all four domains requires renderer/HLSL
  changes or if backward compatibility would silently conflate domains.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Diagnostics GPU Assignment Contract

- Owner/thread: `/root/forge_source`.
- Branch/worktree: shared feature worktree; no branch or Git operations.
- Base commit/state: current reviewed portable diagnostics/key contracts and
  root-integrated adaptive G-buffer resources; compact shading remains a
  separate writer and is not an input contract.
- Read scope: portable adaptive-MSAA/diagnostics modules, the existing adaptive
  G-buffer lighting shader, NVRHI readback/event-query examples, and current
  renderer resource formats.
- Write scope: only new `src/adaptive_msaa_diagnostics_pass.h`,
  `src/adaptive_msaa_diagnostics_pass.cpp`,
  `src/adaptive_msaa_diagnostics_cs.hlsl`, and
  `tests/adaptive_msaa_diagnostics_pass_tests.cpp`.
- No-touch scope: every existing file, compact shade/raster files, CMake,
  shader registry, renderer/UI, documentation, build trees, Git/index/history,
  processes, and GPU.
- Interface/invariant contract: allocate and execute nothing unless explicitly
  enabled. Accept exact key/depth, optional reference/adaptive resolved HDR,
  optional reference/reconstructed single-surface attributes, sample count,
  selected pixel, and requested view. Recompute deterministic exact-key groups
  without making coverage identity; produce ownership/group/representative,
  fallback, reconstruction-error, and final-lighting-error visualizations.
  Maintain a fixed GPU counter ABI for group histogram 0..16, evaluations
  saved, fallback reasons, invalid/stale/mapping failures, work-list
  occupancy/overflow, error-budget rejections, and selected-pixel sample
  records. Selected records include key, depth, supplied/reconstructed
  barycentrics when available, group, representative, weight, reason, and
  radiance when supplied; unavailable fields stay explicitly invalid. Use a
  bounded asynchronous readback ring with event queries and never wait for the
  GPU. Invalid dimensions/formats/sample counts/capacities fail before dispatch.
  A disabled or benchmark-safe call path must not allocate, dispatch, copy,
  query, map, or retain dual-render targets.
- Deliverable: production-quality new NVRHI/HLSL diagnostics pass and pure ABI,
  compatibility, sizing, counter, selected-record, disabled-gate, and
  readback-state tests. The coordinator will supply compact reconstruction and
  per-sample radiance inputs after the separate shade API lands.
- Done when: every requested generic view/counter has an executable output or
  explicit-invalid field, readback is nonblocking and bounded, 2/4/8/16 are
  supported, overflow fails closed, and diagnostics cannot contaminate a
  disabled measurement.
- Required verification: source inspection only; do not build the shared tree.
- Allowed Git and external actions: none.
- Stop and report if: correctness requires editing an existing shader/pass,
  waiting for a GPU query, or assuming a compact-shade ABI still owned by
  another writer.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Diagnostics Automatic Group Gate Assignment Contract

- Owner/thread: `/root/diagnostics_error_gate`.
- Branch/worktree: shared isolated feature worktree; no branch or Git
  operations.
- Base commit/state: exact requested UVSR base plus the current reviewed
  adaptive-lighting and diagnostics implementation; compact raster/shade files
  remain separately owned and are not inputs.
- Read scope: the frozen four-domain error policy, current adaptive G-buffer
  lighting shader, diagnostics producer/GPU pass, AgX display transform, and
  focused diagnostics tests.
- Write scope: only `src/adaptive_msaa_diagnostics.h`,
  `src/adaptive_msaa_diagnostics.cpp`,
  `src/adaptive_msaa_diagnostics_pass.h`,
  `src/adaptive_msaa_diagnostics_pass.cpp`,
  `src/adaptive_msaa_diagnostics_cs.hlsl`,
  `src/adaptive_msaa_diagnostics_producer.hlsli`,
  `src/pbr_deferred_lighting_adaptive_msaa_cs.hlsl`,
  `tests/adaptive_msaa_diagnostics_tests.cpp`, and
  `tests/adaptive_msaa_diagnostics_pass_tests.cpp`.
- No-touch scope: `src/uvsr.cpp`, CMake, shader registry, compact raster/shade,
  every other source or test, documentation beyond this coordinator-owned
  plan, build trees, Git/index/history, processes, and GPU.
- Dependencies already integrated: diagnostics-only shader permutations are
  compile-time isolated from accepted benchmarks; exact-key grouping and
  conservative complete-radiance sharing already expose deterministic groups
  and representatives; portable four-domain limits and counters exist.
- Interface/invariant contract: make diagnostics automatically measure every
  candidate shared group against complete per-sample Reference lighting in the
  diagnostics-only permutation. Reject the entire group to per-sample
  Reference shading when any member is non-finite or exceeds the immutable HDR
  spatial group threshold, publish an exact error-budget reason per member,
  and count rejected groups/samples. Preserve zero additional instructions,
  resources, or state when diagnostics are disabled. Keep the existing pooled
  HDR/display spatial/temporal p99 and maximum limits independent and fail
  closed. Do not use resolved cross-pixel reconstruction or infer identity from
  radiance.
- Deliverable: production HLSL/CPU ABI changes plus focused deterministic tests
  proving pass/fail boundaries, whole-group fallback, counter accounting,
  non-finite rejection, and compile-time isolation from accepted permutations.
- Done when: diagnostics-only automatic rejection cannot publish shared
  radiance above the frozen per-group limit and the renderer can consume the
  existing producer outputs without a new accepted-path binding.
- Required verification: source inspection and focused source/shader contract
  reasoning only; do not build while other writers are active.
- Allowed Git and external actions: none.
- Stop and report if: exact complete Reference shading cannot be called from the
  existing diagnostics permutation, a new renderer binding is unavoidable, or
  post-tonemap per-group rejection would alter accepted rendering.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Directional Visibility Preparation Assignment Contract

- Owner/thread: `/root/directional_visibility_prepare`.
- Branch/worktree: shared isolated feature worktree; no branch or Git
  operations.
- Base commit/state: exact requested UVSR base plus current first-party
  screen-space directional-shadow implementation and the released compact
  prepared-shade API.
- Read scope: `src/screen_space_directional_shadows.{h,cpp}`, its focused
  tests, `src/directional_light_visibility.h`, and the prepared compact shade
  contract. Donut and vendored Bend sources are read-only.
- Write scope: only `src/screen_space_directional_shadows.h`,
  `src/screen_space_directional_shadows.cpp`, and
  `tests/screen_space_directional_shadows_tests.cpp`.
- No-touch scope: every other file including `src/uvsr.cpp`, compact files,
  build files, shader registry, documentation beyond this coordinator-owned
  plan, `donut/`, vendored sources, build trees, Git/index/history, processes,
  and GPU.
- Dependencies already integrated: the current pass has bounded dispatch
  planning, stable R8 visibility output, PSO/resource caching, and focused
  settings/math tests; compact shading now requires its exact final
  directional-visibility tuple before command-list open.
- Interface/invariant contract: add a move-only prepared submission which
  performs every recoverable validation, dispatch-list construction, constant
  capture, pipeline/resource/binding creation, and exact view/depth/light
  identity capture command-free. Provide an immediate command-free
  revalidation and a recording method that performs only captured clears,
  buffer writes, binds, dispatches, markers, and timing. Its predicted
  `DirectionalLightVisibility` must be exactly the tuple returned after
  recording; disabled or unsupported input is a validated neutral no-op.
  Preserve the legacy `Render` behavior for non-compact callers.
- Deliverable: production prepared API/implementation and focused tests for
  move-only ownership, disabled neutral behavior, identity drift rejection,
  exact output prediction, finite-light/view rejection, and dispatch capture.
- Done when: compact renderer integration can prepare its lighting token using
  an exact guaranteed directional tuple before opening the command list, and
  accepted recording cannot allocate or discover mutable render state.
- Required verification: source inspection and focused portable tests only;
  do not build while other writers are active.
- Allowed Git and external actions: none.
- Stop and report if: the existing timer API necessarily allocates during
  recording, NVRHI resource creation cannot remain command-free, or preserving
  legacy behavior requires a shared-file edit.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Impl Compact Shade Outcome Variants Assignment Contract

- Owner/thread: `/root/compact_failure_path_audit`.
- Branch/worktree: shared isolated feature worktree; no branch or Git
  operations.
- Base commit/state: released prepared compact-shade transaction, released
  compact-raster ViewProof contract, and the frozen predicted directional
  visibility producer interface.
- Read scope: current compact shade files/tests, compact raster public proof,
  `src/directional_light_visibility.h`, and the directional prepared-interface
  proposal. The diagnostics producer ABI is read-only while separately owned.
- Write scope: only `src/adaptive_msaa_compact_shade.h`,
  `src/adaptive_msaa_compact_shade.cpp`, and
  `tests/adaptive_msaa_compact_shade_tests.cpp`.
- No-touch scope: every other file including `src/uvsr.cpp`, raster/common
  proof files, diagnostics files, directional-shadow files, build/registry
  files, documentation beyond this coordinator-owned plan, build trees,
  Git/index/history, processes, and GPU.
- Dependencies already integrated: `PrepareFrame` captures an immutable guide
  and one lighting path; screen-space visibility can finish success or neutral
  failure after guide reconstruction; the prepared directional producer can
  guarantee either its predicted tuple or neutral output.
- Interface/invariant contract: before command-list open, precreate every
  required lighting outcome for requested screen-space visibility on/off and
  predicted directional visibility present/neutral. Capture distinct constant
  payloads, pipelines, bindings, strong resource handles, and diagnostics
  bindings. Extend prepared lighting submission to accept the actual guaranteed
  directional tuple and final visibility-success boolean, select only an exact
  precreated outcome, and perform no allocation, scene/view/resource discovery,
  or binding creation. Neutral and active variants must match Reference
  semantics; an unpredicted tuple fails closed and is proved unreachable by
  renderer prevalidation. Preserve legacy overload behavior.
- Deliverable: production API/implementation plus focused tests for all four
  outcome combinations, exact tuple selection/rejection, move-only phase
  behavior, diagnostics publication, and no prepared-recording creation calls.
- Done when: guide, directional shadow, and screen-space visibility may record
  after preparation while compact lighting still selects an exact immutable
  binding/constant variant without stale state.
- Required verification: source inspection and focused portable tests only;
  do not build while other writers are active.
- Allowed Git and external actions: none.
- Stop and report if: selecting the outcome requires a new renderer resource,
  diagnostics ABI mutation, shader permutation, or command-time allocation.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Integration Order

1. Authenticate sources, baseline, and existing data flow; freeze the shared
   identity, invalidation, grouping, shading, fallback, and binding contracts.
2. Add pure CPU/shared key mapping, generation reuse, grouping, weighting,
   overflow, and raster/DXR parity tests.
3. Implement and validate adaptive G-buffer MSAA against the unchanged
   per-sample reference at all sample counts.
4. Compare inline versus separated simple/complex execution and retain only the
   fastest correct full-frame topology.
5. Add compact GPU-scene/material access and compact visibility reconstruction,
   initially for supported opaque triangles with explicit fallbacks.
6. Add default-off diagnostics, reference/error modes, freeze, inspector,
   counters, timers, and benchmark gating.
7. Complete automated, resize/reload, permutation, overflow, image/animation,
   resource-state, and smoke validation.
8. Freeze writes; obtain independent rendering/resource-lifetime review; repair
   findings and invalidate stale evidence as necessary.
9. Run thermally and procedurally matched Position-1 controls and candidate
   measurements, report full-frame/pass/memory/group/fallback/error data, and
   present the exact build for visual acceptance.
10. Recheck policy/UI versions, documentation, title case, line counts, final
    diff/status, and archive the execution plan only after every done condition
    has evidence.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact source lineage | Clean branch/worktree at requested SHA | `git status`, `git rev-parse`, live remote query | Passed at preflight |
| Primary-source fidelity | Commit-authenticated algorithm/provenance map | Source audit and hashes | Passed; Forge tree `e300153c60c611a05d878d22035c703a81724fb6`, Burns archive SHA-256 `6C9A2410ED37270B6CB5F8FED34CE5A330A229ECD426C16E573646FBBE0C76E2` |
| Exact-base technical baseline | Requested source compiles and existing tests pass before renderer edits | Release build plus full CTest | Passed; `uvsr.exe` SHA-256 `35F9106C4951A4825A7DA2D4FB9D0F9EE70B9094A9C67127F3A5D59C79E1CC01`, 32 of 32 CTest |
| Key packing and reuse | Deterministic lossless/invalid/overflow/generation tests | Focused CPU tests | Pending |
| Raster/DXR identity | Equivalent mapping for synthetic raster and DXR tuples | Focused CPU/shared tests | Pending |
| Grouping and weighting | Exhaustive 2/4/8/16 patterns, background and fallback | Focused CPU/GPU tests | Pending |
| G-buffer reference match | Spatial and temporal image errors within frozen limits | Automated reference image suite | Pending |
| Compact reconstruction | Per-attribute and final-lighting comparison to G-buffer | Debug compare plus image suite | Pending |
| Capture evidence integrity | All 180 jobs, 360 BMPs, 360 receipts, and 120 manifest comparisons authenticate the exact candidate | Unattended renderer runner plus schema-2 evaluator | Source complete; runtime pending |
| Sample-dependent safety | Adversarial shadows/lights/grazing/specular/normal-map tests | Animated reference-error suite | Pending |
| Resource lifecycle | Resize, sample switches, reload, topology/LOD invalidation | Contract tests and live smoke | Pending |
| Capacity safety | Occupancy and forced overflow selects correct fallback | Focused and GPU stress tests | Pending |
| Shader coverage | All 2/4/8/16 permutations package and bind correctly | Shader and Release builds | Pending |
| UI contract | Intake, deferred topology selector, diagnostics benchmark gate | Focused UI tests and live stress | Pending |
| Full technical suite | Renderer/PBR/UI/tests pass from exact candidate | Release builds and full CTest | Pending |
| Independent review | No unresolved high-risk rendering/lifetime findings | Read-only review handoff | Pending |
| Runtime correctness | Exact-build launch, all modes/counts, scene and resize smoke | `tools/launch_uvsr.ps1 -BuildDirectory "<isolated-build>"` and automatic commit-bearing window title | Pending |
| Performance | Matched valid Position-1 brackets and measurements | AGENTS performance procedure | Pending |
| Product acceptance | User accepts exact executable, scene, and settings | Shown final candidate | Pending |

For performance work, record:

- baseline and candidate commit/diff identities plus executable SHA-256;
- exact adapter and physical-GPU profile, driver, OS/HAGS, power/thermal state,
  displays/presentation state, renderer priority, scene, Position-1 camera,
  resolution, and complete settings;
- warmup, three baseline repeats, at least 1,000 frames or 15 seconds per run,
  continuous measurement monitor with a unique readiness marker that reaches
  matching `ready` and terminal `complete` states, Position-1 before/after
  bracket, and raw telemetry/timing artifacts;
- total GPU/frame time and every adaptive/classify/load/group/shade/resolve pass;
- memory allocation, avoided/stored bandwidth, group histogram, evaluations
  saved, fallback/stale/mapping/overflow rates, work-list occupancy, and error
  percentiles/maxima;
- diagnostic views and dual rendering disabled during accepted timing windows.

## UI New-Element Intake

- Recorded versions: exact-base UI reference `2026-08-03.8`; exact-base
  `AGENTS.md` `2026-08-03.1`. The stale policy and UI reads are recorded above
  and have been superseded before implementation.
- State owners and defaults: the existing Multisample Adaptive settings remain
  the authority for enablement, quality, and one resolved sample count. A new
  implementation value defaults to the unchanged per-sample reference. A new
  diagnostics state defaults disabled with comparison, freeze, debug output,
  inspector readback, and counters off.
- UI ownership: the user's requested `Adaptive MSAA Diagnostics` group belongs
  in the existing Debug drawer because the exact-base reference requires
  effect-specific diagnostics there. Its implementation and sample selectors
  edit the same authoritative renderer values used by Aliasing; they do not
  create a second sample-count state. The existing Aliasing Quality/Advanced
  presentation remains visible and synchronized unless final source review
  proves a smaller nonduplicated composition is required.
- Labels, IDs, commands, and reset: unique visible labels, hidden IDs, animated
  body ID, ranges, accepted command values, tooltips, and a coherent group reset
  will follow the exact existing Debug/Aliasing conventions after the source map
  freezes symbol names. Every retained selectable state receives command-catalog
  coverage; selected-pixel capture remains an explicit action.
- Classification and renderer cost: diagnostics enablement owns one animated
  toggle region. Implementation and sample count are resource-topology
  dropdowns applied after composition through the shared deferred-action
  barrier; their dependent row set stays fixed unless a structural phase
  machine is proven necessary. Comparison and debug-view dropdowns change pass
  execution or constants. Freeze is immediate state; selected-pixel capture is
  an explicit conditional GPU/readback action. No topology or readback work is
  performed during UI composition.
- Consumer inventory: synchronized Aliasing and Debug selectors, diagnostics
  body, Statistics counters/timers/placeholders, commands, render-target
  allocation, pass creation, shader permutations, binding caches, history and
  topology invalidation, benchmark readiness, pixel inspector, resolve
  ownership, scene loading, and resize/sample-count refresh. Telemetry remains
  `--` until completed queries exist, and unavailable paths state the missing
  condition rather than accepting inert input.
- Scene/loading behavior: scene preparation rejects renderer mutations, cancels
  pending diagnostic captures, and invalidates scene/topology mappings before
  interaction resumes. Diagnostics never expose partially prepared state.
- Animation, scroll, popup, and focus: one animated owner controls the nested
  diagnostic body; Settings remains the only scroll owner; native-arrow popup
  roll, deferred mutation, hit testing, and ImGui scopes stay balanced for Amp
  and OG. Diagnostics input never leaks to camera controls.
- Runtime gate: exercise every choice and reset in Amp and OG, cold/warm and
  forward/backward, with Debug, Aliasing, and Statistics open/closed; scroll and
  clip the owner; cover unavailable/loading states, commands, resize, zoom,
  material inspection, shortcuts, and scene cancellation; then run focused
  UI/renderer contracts, the Release build, full CTest, and tagged live smoke.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-03 | Root the isolated branch at the exact live `origin/main` SHA. | The original checkout is diverged; touching or updating it could lose unrelated work. | All |
| 2026-08-03 | Keep reference as default/oracle and make every adaptive failure fail closed. | This is required by the user and prevents partial compact reconstruction from silently changing final radiance. | All implementation |
| 2026-08-03 | Use one exact surface identity and a separate sharing predicate. | Ownership equality is necessary but insufficient for sample-dependent lighting. | Contract and shaders |
| 2026-08-03 | Begin with `RG32_UINT` key storage and prove any reduction. | Exact identity is higher priority than speculative bandwidth savings; overflow must be detectable. | G-buffer and compact visibility |
| 2026-08-03 | Keep all initial workers read-only until interfaces are frozen. | CPU/HLSL bindings, scene handles, and resource layouts are shared high-risk contracts. | Coordination |
| 2026-08-03 | Defer integration with PR #10/#11 and local shadow branches. | The user named an exact base; neighboring projects have separate owners and are not authorized inputs. | Future integration |
| 2026-08-03 | Replace stale UI reference `2026-07-29.3` with exact-base `2026-08-03.8` before code changes. | The original checkout was diverged; the exact worktree reference makes diagnostics a Debug-owned effect group, retains four independent AA controls, and uses the current compact deferred-control contract. | UI plan and verification |
| 2026-08-03 | Replace stale policy `2026-07-30.1` with exact-base `2026-08-03.1` before code changes. | The newer policy adds exact default-view and continuous measurement-marker requirements; the full 767-line worktree policy was reread. | All implementation and verification |
| 2026-08-03 | Use a UVSR-owned adaptive draw loop and retain Donut's loop for Reference. | Donut's callback omits exact `DrawItem` geometry identity and its batching predicate can alias distinct geometry records with matching offsets. | Adaptive raster identity |
| 2026-08-03 | Preflight compact eligibility for the whole opaque snapshot and select Reference before submission if any draw is unsupported. | Same-frame rescue after discarding the full MSAA G-buffer is impossible for an invalid key; whole-frame fail-closed routing keeps eligible compact frames genuinely compact. | Compact visibility and fallback |
| 2026-08-03 | Keep pixel-center XY in adaptive lighting where it is compared with the unchanged reference. | The reference reconstructs position at pixel center plus per-sample depth; true sample-position attribute reconstruction is retained for compact barycentrics without silently redefining reference parity. | Shading and image error |
| 2026-08-03 | Collect detailed grouping/error evidence only in diagnostic sessions. | Dual rendering, atomics, histograms, and inspector readback would contaminate accepted timing; performance mode structurally bypasses them. | Diagnostics and benchmarks |
| 2026-08-03 | Query runtime sample positions and reproduce the Reference raster interpolation rules explicitly. | `Texture2DMS::GetSamplePosition` follows the bound quality pattern. The Direct3D 11.3 functional specification defines ordinary varyings at pixel center and centroid varyings at pixel center for full coverage or the first covered sample by increasing index otherwise; Donut marks only normal and tangent centroid. This preserves the requested true per-sample `SurfaceHit` while reconstructing the mixed center/centroid attributes that the Reference G-buffer actually stores. Forcing a new standard sample-quality pattern would change the established Reference topology. | Compact reconstruction and image error |
| 2026-08-04 | Treat untextured-only compact reconstruction as a checkpoint, not completion. | Exact local full-G-buffer fallback is safe, but the user explicitly requires production texture access, explicit gradients, normal/emissive reconstruction, and exact alpha coverage. Descriptor planning, GPU texture evidence, and the accepted gradient strategy must be completed before final verification. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Reserve generation zero and use exact `{0,0}` as the physical `RG32_UINT` ownership clear; retain `0xffffffff` as logical invalid/stale. | NVRHI clears RTV-only integer targets through D3D12 float clear values. The Direct3D data-conversion rules do not preserve low bits of 32-bit integer values above `2^24`, so `0xffffffff` is not an exact physical clear. Zero converts exactly, cannot encode a valid generation, and is accepted as background only with coherent class and positive-zero reverse-Z depth. | Compact raster, adaptive G-buffer, diagnostics, and GPU tests |
| 2026-08-04 | Keep production texture and alpha compact eligibility closed until qualification and snapshot-coherence proofs are portable and exact. | The selected adapter can validate the bounded gradient matrix, but Direct3D coarse derivative quad formation remains implementation-dependent. Widening additionally needs an adapter/driver/shader/sampler/sample-count certificate, live material/texture fingerprints, final submission revalidation, exact semantic-format allowlists, and normal/TBN bounds. Unsupported draws retain the local full-G-buffer fallback. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Use a lossless canonical material fingerprint rather than a hash. | The compact snapshot stores the exact 52 shader-visible `MaterialConstants` words after neutralizing only snapshot-local texture indices and padding, retains material and texture ownership, and revalidates each reconstruct record before submission. Hash collisions, raw-pointer reuse, and cooperative `dirty`-bit omissions are therefore not accepted as identity. | IMPL-COMPACT-TEXTURES and compact submission |
| 2026-08-04 | Qualify analytic gradients with a local exact-target certificate; do not add a gradient attachment. | An `RGBA16_FLOAT` gradient target is lossy and `RGBA32_FLOAT` erases the no-motion compact bandwidth advantage. The certificate will fail closed and bind the exact adapter, driver, runtime, renderer, shaders, compiler, sampler, feature/format contract, sample count, and all four real viewport-origin parities. It qualifies only 2x/4x/8x; missing or stale evidence retains local full-G-buffer fallback without runtime readback or dual rendering. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Limit the first target-qualified texture slice to opaque, single-sided, undeformed standard metal-rough materials. | Base color, metal/roughness, emissive, and occlusion use explicit semantic-format allowlists. Alpha/opacity, normal mapping, transmission, subsurface, specular-gloss, double-sided textured surfaces, and textured 16x remain closed until their independent raster, TBN, coverage, and target proofs pass. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Bind texture qualification to a process-monotonic immutable-content lease and validate the active certificate separately for each view. | Texture content revision never resets or aliases after scene reuse, while sample count and viewport-origin parity may change without rewriting texels. One lease therefore authorizes the complete exact certificate at a nonzero content revision; pure per-view validation selects only certified 2x/4x/8x requests and all four origin parities. Scene snapshots, material fingerprints, preparation, and final submission retain and recheck the exact token/revision pair. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Revalidate the complete qualification artifact identity after staged render-pass shader loading and keep shader reload conservative. | Initial acquisition follows the final material publication barrier, then the same live-identity capture hashes the package again after every pass has loaded its shader. Any mismatch revokes the lease and invalidates only textured compact snapshots. Explicit shader reload also revokes and leaves a textured scene on Reference until scene reload creates a new immutable snapshot and lease; UVSR never silently recertifies changed shader bytes. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Derive certificate evidence from the exact rows that passed and keep topology identities process-global. | The issuer may write only its canonical executable-sibling path and copies per-count observed masks. Required G-row evidence is intersected, viewport parity is accumulated one successful row at a time, and alpha/format/validation bits come from their proving checks. A process authority serializes topology epoch/revision advancement and permanently latches Reference on exhaustion, preventing reuse across scene-viewer recreation. | IMPL-COMPACT-TEXTURES and VERIFY |
| 2026-08-04 | Make the renderer produce the complete validation manifest only after exact token-bound receipt proof. | A hand-authored image manifest cannot prove that a requested adaptive path, sample count, source snapshot, shader bundle, or GPU diagnostic work actually produced each BMP. The unattended runner keeps one frozen seed across 180 jobs, rejects downgrade or stale evidence, and atomically publishes schema 2 only after all 720 evidence files pass validation. | VERIFY |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-03 | Preflight `/root` | Complete | Exact base/worktree and live remote evidence | Status, worktrees, branches, plans, PRs, tasks, policies inspected | Source and architecture audits next |
| 2026-08-03 | UI version reconciliation `/root` | Complete | Exact-base UI reference `2026-08-03.8` | Full 298-line reference reread before renderer/UI implementation | Freeze exact control symbols after source map |
| 2026-08-03 | Policy reconciliation `/root` | Complete | Exact-base `AGENTS.md` `2026-08-03.1` | Full 767-line worktree policy reread and plan corrected | Apply current gates throughout |
| 2026-08-03 | Exact-source and architecture audits | Complete | SRC-FORGE, MAP-UVSR, REVIEW-DESIGN, DESIGN-DRAW, and DESIGN-COMPACT handoffs | Exact commits, tree/archive hashes, APIs, licenses, risks, and evidence mapped; no files changed | Implement frozen contracts |
| 2026-08-03 | Exact-base technical baseline `/root` | Complete | `build/bin/uvsr.exe`, SHA-256 `35F9106C4951A4825A7DA2D4FB9D0F9EE70B9094A9C67127F3A5D59C79E1CC01` | Release UVSR and PBR targets built; all 32 registered CTest tests passed | Preserve as regression oracle |
| 2026-08-03 | IMPL-CORE and IMPL-DRAW | In Progress | Disjoint new-file ownership in the isolated worktree | Writers prohibited from build/Git/shared files; coordinator owns integration | Review both handoffs, then wire and build |
| 2026-08-04 | Untextured compact checkpoint `/root` | Complete checkpoint; not final | Dirty exact-base candidate | Release build passed; 44/45 full CTest plus repaired scoped contract retry passed; D3D12/NVRHI production/diagnostic/overflow test passed in 579.05 seconds | Texture/alpha audit found a completion-blocking production-wiring gap; reopen compact scope and invalidate final evidence after edits |
| 2026-08-04 | Integer-clear and compact-capability repair `/root` with three independent read-only reviews | Source complete; build/GPU evidence stale | Dirty exact-base candidate | Prior 2x/4x/8x texture matrix had zero coverage/key/depth/class mismatches and passed every frozen guide threshold; all failures were uncovered samples affected by the non-exact `0xffffffff` RTV clear. Production now uses exact zero; diagnostics validate key/class/depth coherence; D32 DSV and R32 SRV capability queries are split correctly; CPU and source contracts plus per-supported-count GPU injections cover exact clear, malformed class, reconstruct/fallback zero depth, and classless behavior. Reviews report no remaining P0/P1 in this scope. | Focused CPU/DXIL build remains pending. The bounded follow-up preflight had clean process/CPU evidence but failed closed because the monitor did not map the requested GPU identity, so no GPU work was launched. Correct adapter mapping before the next GPU window. |
| 2026-08-04 | Material fingerprint and texture-qualification architecture `/root` plus independent read-only audits | Source implementation in progress; build evidence stale | Dirty exact-base candidate | Added lossless 52-word material canonicalization, retained object/resource identities, snapshot count/descriptor contracts, prepare and final-submission validation, render-thread affinity, and focused/source tests. The independent review found no P0/P1. Added real viewport-origin parity rows for 2x/4x/8x and a narrow dormant semantic-format allowlist while the production texture gate remains closed. | Complete the fixed-schema certificate core, integrate exact runtime/issuer identities and immutable texture-content revision, then build before any GPU run. |
| 2026-08-04 | Fixed-schema texture qualification core `/root/texture_qualification_core`, reviewed by `/root/qualification_core_review` | Source complete; build evidence stale | `adaptive_msaa_texture_qualification.{h,cpp}` and focused CPU tests | Fixed 564-byte little-endian `TQC1` wire format, trailing SHA-256, exact live-identity comparison, ordered 2x/4x/8x records, full four-parity requirement, and explicit 16x/16-to-8x rejection. Independent review found no P0/P1; its digest-set and mutation-test P2s were repaired. | Finish the Windows live-identity/file-I/O/lease authority and issuer wiring, then build the CPU targets. |
| 2026-08-04 | Diagnostics ABI v6 review and repair `/root` with `/root/diagnostics_abi_review` | Source complete; build/DXIL evidence stale | Counter block 264 bytes; readback 1352 bytes | Independent review found two P1s: reasons 22/23 were omitted from GPU/CPU stale reconciliation, and CPU-authored ABI tags let stale DXIL masquerade as v6. HLSL and CPU now agree on both stale bins; CPU initializes impossible sentinels and zero sizes; current HLSL writes exact v6/264 and v6/1088 cookies before completion flags. Focused and source-contract regressions isolate both headers, values, stores, and ordering. Re-review found no remaining P0/P1. | Compile every diagnostics permutation and run CPU/GPU readback tests in the next accepted build/testing window. |
| 2026-08-04 | Adapter and viewport qualification hardening `/root` | Source complete; build/GPU evidence stale | GPU micro-test and thermal preflight source | GPU tests now use UVSR's renderer-identical Donut adapter enumeration and maximum-dedicated-VRAM policy, validate the created D3D12 device LUID, and exercise real viewport origins `00`, `10`, `01`, and `11` with odd shifted extents at 2x/4x/8x. Thermal preflight accepts an exact NVIDIA identity when the generic sensor mapper is unavailable; its PowerShell parser check passed. | Do not run the repaired preflight or any GPU work until a fresh user-available cool testing window. |
| 2026-08-04 | Qualification runtime, issuer, and production lease integration `/root` with `/root/texture_certificate_design` and `/root/qualification_integration_audit` | Source complete; build/GPU evidence stale | Runtime authority, renderer integration, explicit GPU-test issuer, and fail-safe API defaults | Added exact live artifact/runtime/adapter/sampler/format identity, atomic read/write, process-monotonic content leases, immutable snapshot and material-fingerprint propagation, per-view and final-submission validation, explicit full-matrix-only issuance, post-shader-load revalidation, and conservative shader-reload revocation. Independent integration audit found no P0/P1; its permissive-default and shader-byte timing P2s were repaired. | Run focused CPU compile/tests, all relevant DXIL permutations, full Release build/CTest, then the ordinary and explicit issuer GPU paths in the next accepted testing window. |
| 2026-08-04 | Qualification and invalidation hardening `/root` with `/root/qualification_postload_review` | Source complete; build/GPU evidence stale | Requested/resolved sample provenance, Q00, process topology authority, canonical issuer path, and row-derived evidence | Repaired two reviewed P1s: textured raster preparation now retains the snapshot lease, and a requested 16x mode resolved to 8x cannot inherit 8x qualification. Added direct request and authority overflow tests, process-global non-reused topology identities, canonical-path enforcement, and per-row/per-parity certificate evidence. The read-only review reports no remaining P0/P1 in this scope. | Compile CPU targets and GPU-test source, run source contracts, then execute the complete ordinary and issuer GPU matrices in the next accepted testing window. |
| 2026-08-04 | Exact image-evidence pipeline `/root` with `/root/texture_certificate_design`, `/root/validation_manifest_writer`, and `/root/validation_runner_runtime_review` | Source complete; build/runtime evidence stale | 64-field receipts, 180-job frozen-seed runner, 720-file manifest writer, schema-2 evaluator, and runtime-shader-bound build manifest | Python complete-matrix self-test passed; receipt field audit is 64 of 64; read-only runner review hazards were incorporated. Added sticky post-frame-B proof failure and forced pending-readback drain. | Finish independent line-pinned review and source contracts, then compile and run only in the next accepted testing window. |
| 2026-08-04 | User-opened correctness and visual testing window `/root` | Preflight passed | `outputs/adaptive-msaa-preflight-user-window.{txt,csv}` | 35 stable seconds; exact NVIDIA adapter mapped; 48 C, 39 C thermal headroom, zero GPU utilization, inactive live limiter, AC power, clean renderer/build/capture process identity, and accepted external CPU window. P0/high-clock state is recorded but does not block correctness testing. | Finish the source freeze, build all targets, run CPU/DXIL/GPU suites, then execute the unattended visual matrix. Defer matched performance optimization and scoring. |

## Risks and Escalation Triggers

- A Donut public interface may not expose stable instance/geometry identity,
  requiring UVSR-owned draw metadata and lossless lookup tables.
- 16x MSAA may exceed fixed wave/tile assumptions or make groupshared payloads
  unprofitable; capacity must be explicit and overflow safe.
- Alpha testing, double-sided normals, skinned/deformed vertices, explicit
  gradients, motion, or material indirection may require reference fallback
  until exact compact reconstruction is proven.
- Dual reference rendering, selected-pixel readback, freeze, counters, and error
  views can contaminate performance unless they are structurally bypassed.
- Current open visibility PRs and local shadow projects may later conflict with
  shared shaders or deferred-lighting bindings; no integration is attempted in
  this exact-base task.
- Matched benchmark evidence depends on a clean user-available testing window,
  thermal/process gates, and stable Position-1 controls.

Stop and ask the user if:

- Exact alpha/deformation/material support would require editing `donut/` or
  changing visible quality rather than safely falling back.
- A smaller key encoding, stored barycentrics, or broader sharing requires a
  material quality-versus-performance tradeoff not settled by correctness and
  full-frame measurements.
- The final benchmark or visual-acceptance window is unavailable after all
  source-only, automated, and build work is complete.
- Publication, integration, deletion of uncertain assets, or an incompatible
  accepted product outcome becomes necessary.

## Completion

- Final integrated commit: local dirty candidate unless the user later
  authorizes a focused commit.
- Verification summary: pending.
- Independent review: pending.
- Coming Soon/documentation update: active entry added; durable documentation
  pending implementation.
- Pushed/PR/merged, or intentionally local: intentionally local; no publication
  authority granted.
- Remaining experiments or follow-ups: pending.
- Active ownership released: no.
- Archived to completed/abandoned path: no.
