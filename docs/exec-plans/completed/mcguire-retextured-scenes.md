# Mcguire Retextured Scenes

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `main` in
  `C:/Users/brock/Documents/Codex/uvsr-canonical`, as explicitly requested by
  the user
- Base commit: `bed36f951407e23d9e6b4a1a9462a96abc059e8a`
- Started: 2026-08-01 19:10 America/Chicago
- Last updated: 2026-08-01 21:11 America/Chicago
- Planned archive:
  `docs/exec-plans/completed/mcguire-retextured-scenes.md`

## Goal and Done Condition

Goal: Add the retextured Bistro interior and retextured San Miguel packages
downloaded from Morgan McGuire's archive as complete UVSR runtime scenes, with
indoor starting cameras and repository-safe component files.

Done when:

- [x] Each scene appears once in the World Scenes catalog through a friendly
  descriptor and loads every intended geometry/material component.
- [x] Every tracked runtime file is below the decimal 100,000,000-byte GitHub
  file limit. Each scene component is standard glTF 2.0 whose buffer views are
  losslessly distributed across ordinary external `.bin` buffers, with
  deterministic provenance and no opaque runtime byte concatenation.
- [x] Each descriptor supplies a validated initial camera whose position is
  inside the scene, clear of collision geometry, above a reachable floor, and
  facing reachable scene geometry.
- [x] CMake stages the two scenes alongside Intel PBR Sponza without exposing
  component GLBs or reintroducing unrelated Donut sample scenes.
- [x] Targeted contracts, the complete Release test suite, document checks, and
  live scene smoke checks pass for the exact local candidate.

## Scope

In scope:

- Reproducible import/buffer-repacking tools and source/output provenance.
- The authored Bistro Interior Wine GLB already present in the main checkout.
- The full `san-miguel.obj` scene from `San_Miguel.zip`; the archive's separate
  low-poly variant is source material but not a second runtime picker entry.
- Generic descriptor-owned initial camera metadata, scene staging, contracts,
  attribution, and user-facing documentation.

Non-goals:

- Bistro exterior, San Miguel low-poly as a separate scene, geometry
  decimation, Draco/mesh compression, renderer lighting redesign, or GitHub
  publication.
- Deleting or rewriting the user's original untracked Bistro GLBs.

Affected subsystems and paths:

- `assets/scenes/bistro_interior_retextured/`
- `assets/scenes/san_miguel_retextured/`
- `assets/scenes/README.md` and provenance manifests
- `tools/` scene import/cutting utilities
- `src/scene_catalog.*`, `src/uvsr.cpp`, scene contracts, and `CMakeLists.txt`
- `README.md`

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `README.md`, `src/uvsr.cpp`, `src/scene_catalog.*`, tests,
  tracked binary scene assets, and this plan.

## Baseline

- Canonical repository/remote: `main` and `origin/main` both at `bed36f9` after
  a safe fast-forward; relationship is equal.
- Local versus remote state: no tracked changes at start. User-owned untracked
  `assets/scenes/nvidia_bistro/` and `tools/__pycache__/` are preserved.
- Verified source commit/build: newest documented Canonical verified ancestor
  is retained in first-parent history; the exact `bed36f9` base has no
  task-local build record yet, so a clean source baseline check is required
  before first-party code integration.
- GPU, scene, camera, resolution, and settings preset when relevant: runtime
  smoke checks only, not performance claims; both new descriptors at their
  authored indoor camera, factory renderer settings, and the candidate window
  size.
- Known pre-existing failures: none established. Unrelated reference-renderer
  CMake/MSBuild work was active outside this checkout at preflight, so UVSR
  builds must not contend with it.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| McGuire source packages | Bistro GLB `47C71CF...FDBC6A`; San Miguel ZIP `858740...F7D4A` | Ready | Asset import |
| Scene component contract | Valid glTF 2.0, external buffers `< 100,000,000` bytes, descriptor composition | Stable | CMake/catalog/runtime |
| Descriptor camera contract | Finite position, normalized non-collinear direction/up, FOV in `(1, 179)` | Stable | Catalog/runtime/tests |
| Build/GPU lease | No conflicting UVSR build or runtime process | Complete | Build and smoke checks |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Optional descriptor field `initialCamera` contains `position`, `direction`,
  `up`, and `verticalFovDegrees`. Invalid metadata is ignored safely; valid
  metadata initializes every UVSR camera controller after a scene load.
- A friendly `.scene.json` descriptor owns one component `.gltf` through
  `models` and `graph`; the owned model remains hidden from the picker.
- Runtime components are directly loadable glTF 2.0 files whose buffer views
  reference standard external `.bin` buffers capped at 90,000,000 bytes. The
  original view bytes are preserved exactly; no custom runtime reconstruction
  is permitted.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| A1 Architecture | `/root/scene_architecture` | Read-only shared filesystem | `bed36f9` | None | None | Done |
| A2 Asset Inventory | `/root/asset_inventory` | Read-only shared filesystem | Downloads plus `bed36f9` | None | None | Done |
| A3 Split Convention | `/root/split_conventions` | Read-only shared filesystem | `bed36f9` | None | None | Done |
| I1 Integration | `/root` | Main UVSR worktree | `bed36f9` | All task paths | A1-A3 | Done |
| R1 Independent Review | `/root/final_review` | Read-only shared filesystem | Final dirty candidate | None | I1 | Done |

## Assignment Contracts

### A1: Inspect Scene Architecture

- Owner/thread: `/root/scene_architecture`
- Branch/worktree: read-only main checkout
- Base commit/state: `bed36f9`
- Read scope: scene discovery, catalog, camera load behavior, CMake, tests
- Write scope: none
- No-touch scope: all files, Git metadata, builds, and runtime
- Deliverable: exact integration points and checks
- Done when: runtime registration, staging, camera, and validation paths are
  identified
- Required verification: source inspection only
- Allowed Git and external actions: read-only
- Stop and report if: an overlapping scene project or unexpected mutation is
  found
- Handoff revision/artifact: coordinator message received 2026-08-01
- Handoff acknowledged by/on: `/root`, 2026-08-01

### A2: Inventory Downloaded Assets

- Owner/thread: `/root/asset_inventory`
- Branch/worktree: read-only Downloads and main checkout
- Base commit/state: downloaded archives plus `bed36f9`
- Read scope: archive inventories, hashes, licenses, GLB metadata, file sizes
- Write scope: none
- No-touch scope: archive extraction, tracked files, Git metadata
- Deliverable: exact source identity, license, format, camera, and size report
- Done when: both requested packages and all oversized inputs are identified
- Required verification: hashes, ZIP listings, and GLB header/JSON inspection
- Allowed Git and external actions: read-only
- Stop and report if: licensing or source identity is ambiguous
- Handoff revision/artifact: coordinator message received 2026-08-01
- Handoff acknowledged by/on: `/root`, 2026-08-01

### A3: Determine GitHub Cutting Convention

- Owner/thread: `/root/split_conventions`
- Branch/worktree: read-only main checkout and historical reference worktree
- Base commit/state: `bed36f9`
- Read scope: Intel Sponza assets, import tools, descriptors, staging, tests
- Write scope: none
- No-touch scope: every file and Git resource
- Deliverable: repository-compatible cutting/staging/camera recommendation
- Done when: size threshold and runtime composition approach are explicit
- Required verification: source and asset inspection only
- Allowed Git and external actions: read-only
- Stop and report if: convention conflicts with the user's GitHub constraint
- Handoff revision/artifact: coordinator message received 2026-08-01
- Handoff acknowledged by/on: `/root`, 2026-08-01

### I1: Integrate Both Scenes

- Owner/thread: `/root`
- Branch/worktree: main UVSR checkout
- Base commit/state: `bed36f9` plus preserved user-owned untracked files
- Read scope: repository, downloaded source archives, historical read-only
  conversion references
- Write scope: all task-owned paths listed above
- No-touch scope: `donut/`, unrelated active plans, original downloaded ZIPs,
  unrequested Bistro exterior, user-owned `tools/__pycache__/`
- Build directory and runtime/GPU/resource lease: canonical `build/`, only after
  external build activity ends; sole controller of task-owned UVSR runtime
- Dependencies already integrated: A1-A3 findings
- Interface/invariant contract: component and camera contracts above
- Deliverable: local dirty candidate with runtime assets, code, tests, docs,
  verification evidence, and completed plan
- Done when: every goal checkbox and verification row passes
- Required verification: import audits, size/hash/glTF contracts, targeted and
  full Release tests, documentation checks, `git diff --check`, and two live
  scene smoke checks
- Allowed Git and external actions: local edits/builds/tests only; no commit,
  push, PR, merge, or release
- Stop and report if: license terms cannot be satisfied, a cut cannot remain
  below the limit without quality loss, another writer changes a task path, or
  the user's originals would need deletion
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

### R1: Independently Review the Final Candidate

- Owner/thread: `/root/final_review`
- Branch/worktree: read-only main checkout
- Base commit/state: final dirty candidate
- Read scope: complete task diff, asset manifests/contracts, staging and camera
  implementation
- Write scope: none
- No-touch scope: all files, Git metadata, builds, and runtime
- Deliverable: prioritized correctness, packaging, licensing, and camera risks
- Done when: high-risk binary packaging and scene-load path receive an
  independent review
- Required verification: diff/source/manifest inspection; no build lease
- Allowed Git and external actions: read-only
- Stop and report if: candidate files are still changing
- Handoff revision/artifact: final dirty candidate after material-domain repair
- Handoff acknowledged by/on: `/root`, 2026-08-01

## Integration Order

1. Generate and audit cut GLBs plus provenance in ignored work storage.
2. Establish the exact-base source/build baseline before tracked code edits.
3. Add descriptor camera support and scene staging.
4. Copy audited runtime assets, descriptors, licenses, manifests, tests, and
   documentation into tracked paths.
5. Freeze writes, run independent review, repair findings, then run final
   complete verification and live smoke checks.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Complete source conversion | Input/output SHA-256, geometry/material/image counts, no missing textures | Import/repack audit tools | Passed: 1,316,791 Bistro and 9,963,191 San Miguel triangles; all 269 San Miguel images retained |
| GitHub-safe components | Every glTF resource valid and `< 100,000,000` bytes | Scene asset contract plus filesystem audit | Passed: largest file 89,997,080 bytes; five external buffers per scene |
| Catalog registration | Exactly two new friendly entries; owned components hidden | `uvsr_scene_catalog_reference` and scene asset contract | Passed |
| Indoor cameras | Bounds, collision clearance, downward/horizontal/forward ray enclosure | Scene asset contract | Passed for both descriptor cameras |
| Runtime staging | Source and staged inventory/hash equality | CMake provenance contract | Passed |
| Renderer integrity | Release build and complete deterministic suite | CMake build and CTest | Passed: Release build and 35/35 tests |
| Visible scene loading | Both descriptors load with indoor framing and render without fatal errors | `tools/launch_uvsr.ps1 -Experiment mcguire` | Passed: responsive D3D12 windows; 3.21 GB Bistro and 1.43 GB San Miguel dedicated GPU use |
| Documentation integrity | Title Case and README line-count checks | Repository document/count tools | Passed after plan archival |
| Patch hygiene | No unrelated tracked changes; no oversized tracked files | Git status/diff/size audit | Passed; preserved user-owned originals and cache |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-01 19:05 | Work directly in the main UVSR checkout. | The user explicitly corrected the workspace target; no separate worktree or feature branch was created. | All |
| 2026-08-01 19:08 | Use valid standard glTF multi-buffer slices, not byte fragments. | Donut natively loads multiple glTF buffers. Repacking at buffer-view boundaries preserves every source byte, avoids duplicating Bistro's embedded textures, keeps every resource directly addressable, and needs no runtime reconstruction. | I1 |
| 2026-08-01 19:09 | Import full San Miguel and only Bistro Interior Wine. | These match the two requested scenes; exterior and low-poly are unrequested alternatives. | I1 |
| 2026-08-01 20:55 | Flatten only material domains UVSR cannot submit. | Bistro's five BLEND materials and San Miguel's three transmission materials would otherwise hide 173,510 triangles. Exact allowlists keep geometry visible as opaque PBR while preserving buffer and texture bytes; reports disclose that transparency/transmission is not preserved. | I1, R1 |
| 2026-08-01 21:00 | Treat the Bistro GLB and supporting ZIP as separate source identities. | The GLB is a user-supplied Blender container with UVSR ORM repair metadata and is not a member of `Bistro_v5_2.zip`; provenance now records the ZIP, GLB, archive entry, and upstream page without implying containment. | I1, R1 |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-01 19:10 | A1-A3/read-only agents | Done | Findings in coordinator mailbox | Source, archive, hash, license, and convention inspection | Begin audited import/cutting in ignored work storage |
| 2026-08-01 20:40 | I1/`/root` | Done | Two staged descriptor packages and Release renderer | Audited repacks, targeted contracts, 35/35 tests, initial live smokes | Independent review |
| 2026-08-01 20:55 | R1/`/root/final_review` | Findings repaired | Binary filter, material-domain, and Bistro provenance findings | Regenerated both packages; targeted contracts passed | Final full verification and rereview |
| 2026-08-01 21:10 | I1/`/root` | Done | Exact local dirty candidate | Release build, 35/35 tests, both live smokes, document and patch checks | Archive plan and hand off |

## Risks and Escalation Triggers

- San Miguel is large enough that conversion may exhaust memory or expose an
  individual material primitive above the file target; fall back to
  deterministic geometry subdivision without decimation.
- Texture reuse across cuts can duplicate data; partition by resource locality
  and keep every final component below 100,000,000 bytes.
- The original Bistro GLBs are user-owned untracked state and must remain
  preserved even though only cut derivatives are bundled.
- San Miguel permits research and educational use with attribution. UVSR is a
  research renderer; retain the supplied notice verbatim and clearly document
  the conversion. Stop if publication scope changes beyond that use.

Stop and ask the user if:

- Satisfying the file limit would require visible geometry or texture quality
  loss rather than additional lossless cuts.
- The requested San Miguel variant cannot be resolved mechanically from the
  archive, or the source license would not cover the intended repository use.
- Progress requires deleting or replacing the user's original downloaded data.

## Completion

- Final integrated commit: none; no commit authorized
- Verification summary: audited imports and repacks, strict file-size and staged
  parity contracts, Release build, 35/35 CTest, two responsive D3D12 live
  launches, and repository documentation/patch checks passed
- Independent review: completed; all blocking findings repaired and rereviewed
- Coming Soon/documentation update: no Coming Soon entry planned for direct
  local main work; durable README and asset documentation required
- Pushed/PR/merged, or intentionally local: intentionally local unless the user
  later authorizes publication
- Remaining experiments or follow-ups: none required
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/mcguire-retextured-scenes.md`
