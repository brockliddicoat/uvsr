# Diagnostic Cascaded Shadow Maps: Ue5-Style Reference Path

## Status

- State: complete; intentionally local and uncommitted
- Coordinator: `/root`
- Project branch and worktree: `codex/bend-screen-space-shadows` in the
  preserved UVSR feature checkout
- Base commit: `ea566bc67f744059e6f62e33c541c5b25bde9bd8`
- Started: 2026-07-22
- Last updated: 2026-07-24
- Planned archive:
  `docs/exec-plans/completed/diagnostic-cascaded-shadow-maps.md`

## Goal and Done Condition

Goal:

Add one diagnostic, UVSR-local cascaded directional-shadow implementation that
tracks UE5's conventional CSM behavior as closely as the pinned Donut and NVRHI
interfaces allow. Full redraw and every cache policy use the same persistent
depth maps, caster path, and full-resolution receiver. The path exists to make
controlled CSM, cached-CSM, and SVSM comparisons; it is not the preferred
renderer path.

Done when:

- [x] One shared core supports one through four UE-style cascades, geometric
  split distribution, transition and distance fades, stable texel-snapped
  projections, opaque and alpha-tested casters, depth and slope bias, and
  comparable filtering.
- [x] Full redraw reuses Donut scene traversal, per-cascade AABB culling,
  material bindings, depth shaders, adjacent-instance batching, and pipeline
  state caches without changing Donut or NVRHI.
- [x] Whole-map reuse, whole-cascade reuse, dirty rectangles, and scrolling are
  independent update-policy toggles with no meaningful disabled work.
- [x] Dirty updates clear every conservative old and new affected rectangle and
  rerender every current caster overlapping any cleared rectangle.
- [x] Scrolling raster-moves only exact integer-texel-compatible overlap through a
  scratch resource, clears every exposed texel, and fully invalidates on any
  light-basis, projection-scale, split, bias, format, or depth-mapping change.
- [x] The seven requested profile choices select the same core and differ only
  in cascade count and update policy where required.
- [x] CSM resolves to a full-resolution linear `R8_UNORM` visibility texture
  before deferred lighting and applies only to its pointer-identical light.
- [x] Matched setup, culling, clear/update, raster, sampling, and total timings
  plus coverage, filtering, caster, cache, texel, and memory stats are visible.
- [x] Full-redraw modes expose an independently toggleable exact cached caster
  draw list; the strict Single Map Reference profile leaves it disabled.
- [x] Deterministic tests, Release build, full CTest, title checks, diff checks,
  guarded runtime smoke validation, and independent source review pass.

## Scope

In scope:

- A first-party `DiagnosticCascadedShadowMapPass` with one persistent
  four-slice conventional depth array and one full-resolution visibility
  receiver.
- One through four cascades using UE5.6's accumulated geometric split weights,
  orientation-independent subfrustum sphere fitting, rounded extents, stable
  light-space texel snapping, cascade overlap fades, and final distance fade.
- Donut's ordinary opaque and alpha-tested mesh/material path, with a narrow
  local correction for constant-opacity alpha-tested materials.
- Full redraw, all-or-nothing whole-map reuse, per-cascade reuse, conservative
  old-plus-new dirty rectangles, and exact-compatible two-dimensional scrolling.
- UE-like 5-by-5 PCF and a matched one, four, eight, or sixteen-tap Poisson mode
  for controlled comparison with SVSM.
- Focused UI, debug output, counters, matched timings, tests, and source report.

Non-goals:

- Virtual pages, sparse resources, static/dynamic depth layers, per-object
  partial shadow caches, per-object shadow maps, or a new CSM-only indirect
  submission system.
- Editing, updating, or forking Donut, NVRHI, or any submodule.
- Replacing SVSM or making CSM the preferred/default renderer path.
- Renderer-wide bindless, meshlet, visibility-buffer, or scene-database work.
- Optimizing the UE reference beyond the source architecture being diagnosed.

Affected subsystems and paths:

- New `src/diagnostic_cascaded_shadow_map*` CPU, shared, HLSL, and shader-config
  files.
- `src/uvsr.cpp` for ownership, ordering, UI, debug presentation, and stats.
- `src/directional_light_visibility.h` and the UVSR PBR deferred pass for a
  third independent exact-light visibility producer.
- `CMakeLists.txt` for an independently buildable CSM component and focused
  test target.
- New `tests/diagnostic_cascaded_shadow_map_tests.cpp` and this plan.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `src/uvsr.cpp`, the PBR deferred CPU/HLSL contract,
  settings/presets, shader bindings, and all new CSM implementation files.

## Baseline

- Canonical repository/remote: `origin` at
  `https://github.com/brockliddicoat/uvsr.git`.
- Local versus remote state: feature HEAD equals its upstream at `ea566bc`;
  tracked files are clean. Pre-existing untracked reports, benchmark telemetry,
  handoffs, DRED output, and Python cache remain out of scope and untouched.
- Verified source commit/build: the prior Bend/SVSM candidate was independently
  reviewed and built before this task; combined verification will be rerun.
- GPU, scene, camera, resolution, and settings preset when relevant: runtime
  correctness smoke testing will use the existing Sponza diagnostic scene.
  Performance measurement requires a fresh user-provided testing window and
  the repository thermal/load preflight; no estimates will be called measures.
- Known pre-existing failures: no tracked source/build failure is recorded.

## Dependencies and Interfaces

| Dependency Or Reference | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Local Donut | Pinned submodule, unchanged | Verified | Views, depth pass, materials, scene culling, draw batching |
| Local NVRHI | Pinned submodule, unchanged | Verified | D16-preferred/D32-fallback arrays, scissored depth blits, timers, R8 resolve |
| UE5 directional CSM | Official Epic behavior/docs plus inspected UE5.6 source | Reviewed | Splits, fades, projections, culling, bias, filtering |
| UE5 CSM cache | Official setting plus inspected `ShadowSetup` and shadow-depth source | Reviewed | Compatibility, invalidation, overlap reuse |
| Existing UVSR SVSM | Reuse only neutral seam, reconstruction, timing vocabulary, and Poisson pattern | Verified | Receiver and comparison diagnostics |
| Existing Bend pass | Independent producer; no CSM references or changes | Frozen | Deferred multiplication only |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- CSM result is `{ fullResolutionR8Visibility, exactDirectionalLight,
  optionalDebugTexture }`; incomplete results are neutral white.
- The neutral deferred-lighting seam has three fixed ordinary-SRV slots. Each
  producer is validated independently and multiplied only for its
  pointer-identical light.
- Depth storage is a persistent normal-depth two-dimensional array with four
  slices. D3D12 profiles prefer sampleable `D16`, matching conventional UE;
  unsupported devices fall back to sampleable `D32`. Active profiles use one
  through four slices; inactive slices are neither cleared nor rendered.
- Output resize recreates only full-resolution visibility/debug resources.
  Shadow resolution or depth-format changes recreate the depth array and fully
  invalidate every cascade.
- A single optional one-slice same-format scratch map exists only while
  scrolling is enabled; exact overlap copies are never performed in place.
- Camera depth remains reverse Z for world reconstruction. Conventional CSM
  shadow depth is normal Z, clears to one, and uses less-equal depth testing.
- Cache features change update policy only. They do not select another map,
  caster path, receiver, filter implementation, or submission backend.

## Assignment Summary

| Task ID | Owner | Branch And Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| local-architecture-audit | `/root/csm_local_architecture_audit` | Target worktree, read-only | `ea566bc` | None | Pinned local source | Complete |
| ue5-reference-research | `/root/ue5_csm_reference_research` | Target worktree, read-only | `ea566bc` | None | Epic docs/source | Complete |
| validation-design | `/root/csm_validation_design` | Target worktree, read-only | `ea566bc` | None | Frozen requirements | Complete |
| implementation | `/root` | Target worktree | Preserved baseline | First-party CSM and narrow integration | Audits | Complete |
| verification-review | Independent CSM reviewers | Target worktree, read-only | Final local candidate | None | Implementation and checks | Complete; final recheck clean |

## Assignment Contracts

### Implementation: Add One Shared Csm Core

- Owner/thread: `/root`
- Branch/worktree: preserved target feature checkout
- Base commit/state: `ea566bc` plus untouched untracked diagnostics
- Read scope: first-party renderer, pinned dependency APIs, active plans, and
  cited UE references
- Write scope: new CSM files/tests/plan plus narrow CMake, UVSR, and neutral
  deferred-lighting integration
- No-touch scope: all Bend files, all submodules, main, Git history, remotes,
  unrelated source, and pre-existing untracked files
- Build directory and runtime/GPU/resource lease: existing local Release build;
  one launched renderer only after the testing gate allows it
- Dependencies already integrated: local, UE5, and validation read-only audits
- Interface/invariant contract: same maps/caster/receiver for every policy;
  exact-light R8 output; normal shadow depth; fail-white invalid states; no
  hidden cache-feature work while disabled
- Deliverable: selectable diagnostic CSM path with requested profiles, cache
  policies, diagnostics, and tests
- Done when: source checks and available runtime correctness checks pass
- Required verification: focused tests, Release build, full CTest, title-case
  checks, diff check, shader/resource smoke, resize, profile/cache transitions,
  and independent review
- Allowed Git and external actions: local edits, builds, tests, and guarded
  launch only; no commit, push, PR, merge, main change, or dependency update
- Stop and report if: required correctness would require a Donut/NVRHI edit, a
  forbidden cache architecture, destructive Git action, or uncertain ownership
- Handoff revision/artifact: local uncommitted diff and completed plan evidence
- Handoff acknowledged by/on: local coordinator on 2026-07-22

## Integration Order

1. Add pure settings, split/fade, rectangle, compatibility, and profile helpers
   with deterministic tests.
2. Add persistent depth views/resources, the corrected Donut-derived depth
   adapter, full redraw, and timed R8 receiver.
3. Add whole-map and whole-cascade reuse over the same full-redraw core.
4. Add conservative old-plus-new dirty updates and overlap rerendering.
5. Add exact-compatible scrolling through scratch and exposed-strip updates.
6. Integrate the third neutral visibility slot, UVSR ownership/UI/debug/stats,
   and matched comparison controls.
7. Run source/build/test checks, guarded runtime smoke validation, and an
   independent review; repair only task-introduced defects.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| UE split/fade/profile invariants | Focused deterministic tests | CSM test executable through CTest | Passed after final cache/timer fixes |
| Cache/dirty/scroll correctness | Compatibility, old/new, overlap, copy/exposure tests | CSM test executable through CTest | Passed; includes localized reuse, fail-open overlap, scroll quadrants, and configuration gates |
| Alpha-tested and state-path correctness | Source test plus shader/runtime smoke | Focused test and Sponza foliage inspection | Passed source review and live foliage smoke |
| Build and shader integration | Clean Release application build | `cmake --build build --config Release --target uvsr` | Passed; runtime shader packaging defect found live and repaired |
| Repository regression safety | Full test suite | `ctest --test-dir build -C Release --output-on-failure` | Final rerun passed 15 of 15 |
| Documentation convention | Self and full heading scan | Repository title-case checker | Self-test passed; final scan checked 529 headings with zero violations |
| Patch hygiene | No whitespace errors or dependency changes | `git diff --check` and submodule status | Passed; no staged files and pinned Donut revision unchanged |
| Runtime correctness | Stable shadows across profiles, motion, cache, resize, and debug | Guarded launch via `tools/launch_uvsr.ps1` | Full-redraw, cached single, optimized four-cascade, debug, and camera-rotation smoke passed; deterministic recreation tests passed; live resize was not rerun after active user input was detected |
| Performance data | Thermal/load-valid raw timing samples only | Existing preflight and benchmark procedure | Awaiting future testing window |
| Independent review | Read-only rendering/resource/cache review | Fresh subagent review after checks | Final re-review found no actionable issue |

## Decisions

| Date And Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-22 | Resolve CSM to full-resolution R8 before deferred lighting | Direct Donut light-shadow sampling is fused into deferred lighting, cannot isolate sampling cost, and uses XY map fades instead of UE depth-split fades | Core, diagnostics, deferred seam |
| 2026-07-22 | Reuse Donut depth/material/draw infrastructure through a UVSR-local adapter | It already supplies opaque/alpha scene traversal, per-view culling, material bindings, batching, bias-capable PSOs, and persistent view/framebuffer support; dependency edits are unnecessary | Raster and culling |
| 2026-07-22 | Keep conventional normal-depth CSM maps | Reverse Z remains limited to camera-depth reconstruction. A later authenticated-source audit selected UE-equivalent sampleable D16 with D32 fallback, without changing receiver or cache interfaces. | Resources, raster, receiver |
| 2026-07-22 | Adapt UE caching to one depth layer | UE's static/movable depth-layer composition conflicts with the explicit non-goal. Whole reuse, dirty update, and scroll operate on the same complete map instead | Cache policies |
| 2026-07-22 | Use UE5.6 accumulated geometric split weights | This is the inspected source behavior; Donut's reciprocal split loop and common power-form approximations are not equivalent | Projection and tests |
| 2026-07-22 | Raster-move depth scroll overlap through one-slice scratch | Pinned NVRHI always supplies a source box to `CopyTextureRegion`, which D3D12 forbids for depth resources. Two scissored `SV_Depth` blits preserve exact values for either selected format without a backend escape hatch or dependency edit. | Scrolling and resources |
| 2026-07-22 | Pair delayed timer queries with configuration generations | Profile and diagnostic changes must not publish an old GPU sample beside current cache and SVSM comparison metadata | Timings and stats |
| 2026-07-22 | Report requested and actual maximum light-depth spans | Sphere fitting can safely expand a cascade beyond the requested depth; comparison diagnostics must expose that mismatch rather than call it matched | Projection and comparison stats |

## Progress and Handoffs

| Date And Time | Task Or Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-22 | Local architecture audit | Complete | Read-only handoff | Donut/NVRHI interfaces cross-checked | Implement local scissor-enabled depth adapter and constant-alpha correction |
| 2026-07-22 | UE5 reference research | Complete | Read-only handoff | Splits, fades, projection, cache, bias, filter sources cross-checked | Preserve source behavior within explicit single-layer constraint |
| 2026-07-22 | Validation design | Complete | Read-only handoff | Twelve deterministic test groups specified | Implement pure helpers before GPU integration |
| 2026-07-22 | Coordinator | Complete | Local uncommitted CSM candidate | Standalone CSM-only configure/build/test passed; final Release application build, 15-test CTest, title checks, and diff check passed | Handoff local candidate without publishing |
| 2026-07-22 | Runtime Smoke | Passed Available Matrix | Sponza, D3D12, RTX 4090 Laptop GPU | Full-redraw shadows coherent; cached single reported one reused/zero redrawn; optimized cached CSM reported four reused/zero redrawn; clear and raster were known zero; sampling was about 0.10-0.12 ms; debug views and camera rotation remained stable | No performance benchmark claim; resize deferred when active user input was detected |
| 2026-07-22 | Independent Reviews | Complete | Read-only CSM, D3D12 scroll, requirements, and final post-fix reviews | Illegal D32 copies, stale timing pairing, false depth match, unaffected-cascade redraw, empty clipped updates, unreported rectangle culling, unreliable bounds, stale bindings, missing reason masks, and component-build documentation were repaired; final re-review found no actionable issue | None |

## Risks and Escalation Triggers

- NVRHI has no rectangular depth-clear command. The local clear draw and caster
  PSO must both enable scissoring; a viewport scissor alone is insufficient.
- Donut's stock depth pass rejects alpha-tested materials that use constant
  opacity without a texture. The local adapter must bind and clip them.
- Dirty updates are incorrect if they render only changed casters. Every caster
  overlapping any cleared old/new region must be submitted.
- Scrolling is incorrect if XY shift is fractional, depth mapping changes, or
  copies overlap in place. Any uncertain compatibility fails to full redraw.
- Skinned, morphed, non-finite, or otherwise unreliable bounds fail open to a
  full affected-cascade redraw when scene content changed, or are submitted to
  every exposed scroll rectangle when the cached scene is otherwise unchanged.
- Cached frames must report known-zero update/cull/raster timings rather than
  retaining stale nonzero values; sampling remains active and timed.

Stop and ask the user if:

- Correct implementation requires changing a pinned dependency, introducing a
  forbidden architecture, publishing state, or discarding existing work.
- Runtime performance measurement is requested without a valid testing window
  or the thermal/load gate cannot establish usable evidence.

## Production Freeze Addendum

The final cached shadow draw-list implementation is deliberately narrower than
the depth-map update cache. It applies only to full-redraw CSM modes, stores up
to the eight exact TAA phase keys, and reuses the culled caster vectors without
copying them. Its compatibility key includes scene and draw ownership,
pointer-identical light identity, reliable scene revision, the complete
normalized CSM configuration, jittered and unjittered camera state, viewport,
and every cascade transform. Any uncertainty rebuilds the list. Full
invalidation, unreliable scene revisions, or an ineligible cache policy clears
the retained entries and releases their scene ownership.

Final runtime validation covered cache on/off, camera motion and re-warm, live
maximize/restore resource recreation, and both D16 and D32 shadow-depth paths.
The cached path retained the same 483 final caster pairs while reducing
static-scene CPU culling from approximately 0.403 ms to
0.011-0.036 ms. Images remained coherent in every checked state.

The final aggregate Release build passes and all 15 CTest cases pass. Three
independent read-only audits found no remaining source-level production
blocker after hardening cache invalidation and ownership, deforming and
malformed bounds, scaled or sheared light parents, translation-only state,
timer generations, resource recreation, reverse-Z reconstruction,
alpha-tested casters, and CPU/HLSL layouts. The final executable SHA-256 is
`ACD62CC88F2387E68CD3F0623E61E2466EF173803C05945017A223CCDB2FFA38`.
The CSM source is frozen unless a reproducible defect, a new platform failure,
or an explicit feature request justifies reopening it.

## Completion

- Final integrated commit: intentionally none
- Verification summary: standalone CSM-only Release build and focused test pass;
  final aggregate Release build and all 15 CTest tests pass; title checker
  self-test and final full heading scan pass; `git diff --check` passes; no
  files are staged; the Donut submodule revision is unchanged. Guarded Sponza
  runtime smoke passed full redraw, cached draw lists, camera motion and
  re-warm, live resize, and D16/D32 depth recreation.
- Independent review: three final read-only reviews found no production blocker
- Coming Soon/documentation update: README component-only build instructions
  include complete Bend, SVSM, and diagnostic-CSM configurations
- Pushed/PR/merged, or intentionally local: intentionally local and uncommitted
- Remaining experiments or follow-ups: controlled equal-coverage CSM/SVSM
  performance capture is measurement work and does not keep the CSM source open
- Active ownership released: yes
- Archived to completed/abandoned path: yes
