# Blender Classroom Scene

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `main` in
  `C:/Users/brock/Documents/Codex/uvsr-canonical`, as explicitly requested by
  the user
- Base commit: `bed36f951407e23d9e6b4a1a9462a96abc059e8a`
- Started: 2026-08-01 21:45 America/Chicago
- Last updated: 2026-08-01 23:17 America/Chicago
- Planned archive:
  `docs/exec-plans/completed/blender-classroom-scene.md`

## Superseding Source-Texture Restoration

The initial generated-normal material treatment recorded by this historical
plan was superseded on 2026-08-02 after visual review. The current Classroom
package restores 22 byte-identical archive images plus Blender's generated UV
checker, binds no custom normal maps, and uses audited source-texture material
fallbacks. The follow-up work is recorded in
`docs/exec-plans/completed/blender-classroom-source-textures.md`.

## Goal and Done Condition

Goal: Add Christophe Seux's Blender Classroom archive as a complete UVSR
runtime scene, preserving its indoor framing and converting its Blender 2.79
materials into renderer-compatible glTF PBR materials.

Done when:

- [x] Classroom appears once in the World Scenes catalog and stages with the
  existing Bistro, San Miguel, and Intel Sponza scenes.
- [x] The complete visible `_mainScene` geometry and textures are represented
  by valid glTF 2.0 resources, with every tracked runtime file below the
  decimal 100,000,000-byte GitHub limit.
- [x] Legacy Cycles materials have deterministic PBR equivalents and every
  visible primitive uses a material domain UVSR actually submits.
- [x] The descriptor camera is demonstrably indoors, clear of scene geometry,
  and faces reachable classroom geometry.
- [x] Source identity, archive hash, CC0 license, author credit, conversion
  decisions, and output hashes are durable and reproducible.
- [x] Targeted contracts, the complete Release test suite, documentation
  checks, and a live Classroom smoke check pass for the exact local candidate.

## Scope

In scope:

- The archived `classroom.zip` snapshot supplied by the user, whose main
  Blender file contains `_mainScene`, `dustParticules`, and `volumeLight`.
- Visible geometry from `_mainScene`, linked asset libraries, source textures,
  the authored `renderCam`, deterministic material conversion, scene cutting,
  staging, catalog registration, tests, documentation, and provenance.
- Reproducible local conversion with pinned Blender. An external conversion
  service is considered only if source PBR intent cannot be faithfully
  represented from the available shader graphs and texture inputs.

Non-goals:

- Reproducing Blender compositing, the separate dust/volume helper scenes,
  Cycles lighting, or renderer lighting redesign.
- Uploading source assets to an external service without a concrete technical
  need and an auditable result.
- Publishing, committing, pushing, opening a PR, merging, or deleting any of
  the user's downloaded or prior scene files.

Affected subsystems and paths:

- `assets/scenes/blender_classroom/`
- `assets/scenes/README.md` and a Classroom provenance manifest
- `tools/` Classroom import and audit tooling
- `CMakeLists.txt`, `src/scene_catalog.*`, tests, `README.md`, and
  `docs/advanced-settings.md`

Shared hotspots reserved for the coordinator:

- All tracked files and runtime assets. Read-only agents may inspect them but
  must not mutate the checkout, Git state, builds, or running renderer.

## Baseline

- Canonical repository: `main` at `bed36f9`; no commit or publication action is
  authorized.
- Existing task-owned dirty candidate: the verified Bistro Interior and San
  Miguel additions, including descriptor camera support, scene contracts, and
  their staged runtime packages.
- User-owned state to preserve: `assets/scenes/nvidia_bistro/` and
  `tools/__pycache__/`.
- Exact existing verified build:
  `C:/Users/brock/Documents/Codex/uvsr-canonical/build/bin/uvsr.exe`; Classroom
  changes require rebuilding and reverifying it.
- Source archive: Wayback snapshot at 2024-09-26 14:26:51 UTC of
  `https://download.blender.org/demo/test/classroom.zip`, 70,279,690 bytes,
  SHA-256 `0F0ECC58D45F12B2F4272A53E2D8E69135518E16E956B118F10C20D8DCFDF5E6`.
- Official Blender demo-files metadata identifies Classroom by Christophe Seux
  as CC0; the same statement is present in the archived demo page.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Classroom archive | Exact Wayback snapshot and SHA-256 above | Ready | Import |
| License | Official Classroom listing says CC0 | Ready | Provenance/package |
| Blender converter | Pinned portable Blender 5.1.2, build `ec6e62d40fa9` | Ready | Import |
| Material policy | Source-image preservation with no generated normal maps plus deterministic Cycles-to-glTF translation | Superseded treatment replaced | Runtime assets |
| Scene contract | Standard glTF external buffers, resources `< 100,000,000` bytes | Stable | Import/tests |
| Camera contract | Finite indoor pose with geometric enclosure evidence | Stable | Descriptor/tests |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- No new renderer ABI or shader binding is planned. Classroom uses the existing
  descriptor-owned `initialCamera` interface and component hiding behavior.
- Runtime components must be directly loadable glTF 2.0 files. Oversized
  geometry is divided only at buffer-view boundaries into ordinary external
  `.bin` buffers; no opaque fragments or runtime concatenation are permitted.
- Materials must use opaque or alpha-tested metallic-roughness data supported
  by UVSR. Unsupported blend/transmission helpers are removed or flattened by
  an explicit allowlist recorded in the import report.

## Assignment Summary

| Task ID | Owner | Write scope | Status |
| --- | --- | --- | --- |
| A1 Provenance And License | `/root/asset_inventory` | None | Completed |
| A2 Pipeline And PBR Review | `/root/final_review` | None | Completed |
| A3 Asset Audit | `/root/final_review/classroom_asset_audit` | None | Completed |
| I1 Integration And Verification | `/root` | All task paths | Completed |

## Integration Order

1. Freeze the exact source identity, license, scene inventory, material graph
   inventory, authored camera, and source file hashes.
2. Prove the smallest deterministic PBR conversion that preserves visible
   material intent; document why an external upload is or is not necessary.
3. Export `_mainScene`, audit geometry/material/image parity, and divide only
   resources that exceed the GitHub limit.
4. Add the descriptor, provenance, licenses, staging, catalog contracts, asset
   contracts, and user documentation.
5. Freeze writes, independently review the candidate, repair findings, then run
   the complete Release build/test/document/live-smoke verification set.

## Verification Plan

| Acceptance criterion | Evidence required | Planned check |
| --- | --- | --- |
| Source fidelity | Hashes plus source/export geometry, material, and image inventory | Blender import report and glTF audit |
| PBR compatibility | No legacy-only graphs in output; no skipped material domain | glTF material contract and renderer source contract |
| GitHub-safe package | Every tracked file below 100,000,000 bytes | Filesystem and staged parity contract |
| Indoor camera | Bounds, clearance, floor, and forward-ray intersection | Scene asset contract |
| Catalog/staging | One friendly scene; owned components hidden; staged hashes equal source | Catalog and provenance tests |
| Renderer integrity | Release build and complete deterministic suite | CMake build and CTest |
| Visible scene loading | Responsive D3D12 window at Classroom descriptor camera | `tools/launch_uvsr.ps1 -Experiment classroom` |
| Documentation integrity | Title Case, line counts, and clean patch whitespace | Repository document/count tools and `git diff --check` |

## Decisions

| Date/time | Decision | Reasoning | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-01 21:45 | Continue in the canonical main UVSR checkout. | The user explicitly established this checkout for the chat; Classroom extends the existing verified scene candidate. | All |
| 2026-08-01 22:00 | Export only `_mainScene`. | The other two scenes are Blender-internal dust and volume-light passes, not independent classroom geometry requested for UVSR. | I1 |
| 2026-08-01 22:09 | Treat PBR conversion as shader translation before texture invention. | The source contains legacy Cycles diffuse, glossy, bump, glass, and emission graphs with complete file-backed images. Those graphs encode PBR intent even though they predate Principled BSDF/glTF; conversion must first preserve that authored data. | A2, I1 |
| 2026-08-01 22:37 | Generate missing normal maps with NormalMap-Online in its browser-local client. | This initial treatment was later rejected during visual review and superseded by the source-texture restoration recorded above. | A2, I1 |
| 2026-08-01 22:49 | Keep the geometry in one external glTF buffer. | The complete 14,512,184-byte geometry buffer is already far below GitHub's 100,000,000-byte per-file limit, so further cutting would add complexity without satisfying any constraint. | I1 |
| 2026-08-01 23:03 | Use the verified indoor descriptor camera. | Geometry queries prove wall enclosure, floor clearance, camera clearance, and a forward hit; the live D3D12 smoke check visibly confirmed a furnished classroom view facing the blackboard. | I1 |
| 2026-08-01 23:12 | Mark hash-audited text artifacts as byte-preserved Git content. | Export reports, the glTF, license/source records, and exporter are hashed by provenance tests; disabling text and diff normalization keeps those bytes stable after checkout. | A3, I1 |

## Risks and Escalation Triggers

- Blender's exporter does not directly map most 2.79 Cycles graphs to glTF.
  The conversion must be explicit and audited rather than accepting default
  gray materials.
- UVSR does not submit blended or transmission materials in its opaque draw
  strategy. Helper planes and glass require an allowlisted conversion that
  keeps intended classroom geometry visible.
- Linked collections may be instanced many times. Export must preserve every
  evaluated instance without losing or silently multiplying geometry.
- Online texture services generally operate on individual bitmaps and cannot
  infer the semantics of a complete Blender shader graph. Uploading the full
  archive is inappropriate unless a specific service provides a verifiable
  capability missing from the pinned local conversion.

Stop and ask the user if:

- A faithful result requires uploading the source scene to a third-party
  service under terms that cannot be audited, incurs a fee, or needs account
  access.
- Satisfying the GitHub limit requires visible quality loss instead of more
  lossless cuts.
- The exact CC0 source identity becomes ambiguous or the imported camera fails
  the indoor geometry contract and no mechanically safe replacement exists.

## Progress and Handoffs

| Date/time | Task/owner | Status | Evidence | Next action |
| --- | --- | --- | --- | --- |
| 2026-08-01 22:05 | Source audit/`/root` | Completed | Archive verified; all linked libraries and file textures resolve; authored camera and scene bounds recorded | Complete |
| 2026-08-01 22:09 | License audit/`/root` | Completed | Archived official Blender demo page identifies Classroom by Christophe Seux as CC0; the package includes the CC0 legal code and source URLs | Complete |
| 2026-08-01 22:49 | Export and package/`/root` | Completed | 604,150 triangles, 70 supported materials, 43 images, 17 packaged generated normals, and one 14,512,184-byte geometry buffer; every scene file is below 100,000,000 bytes | Complete |
| 2026-08-01 23:03 | Runtime smoke/`/root` | Completed | Release D3D12 renderer loaded the exact Classroom descriptor and showed the furnished indoor view; geometric audit measured 0.345255 m clearance, 1.09728 m floor clearance, three enclosure hits, and a 7.73301 m forward hit | Complete |
| 2026-08-01 23:17 | Final verification/`/root` | Completed | Release build succeeded; 35/35 CTest tests passed; asset hashes and staged parity passed; title-case, line-count, and patch-whitespace checks passed; independent audit's byte-preservation finding was repaired and reverified | Archive plan |

## Completion

- Final integrated commit: none; no commit authorized
- Verification summary: Release build succeeded; 35/35 CTest tests passed;
  live D3D12 Classroom smoke, indoor-camera geometry audit, asset-size and
  staged-parity contracts, provenance hashes, document Title Case, README line
  counts, and patch whitespace all passed
- Independent review: completed with one byte-preservation finding; repaired
  with `.gitattributes` rules and manifest-backed hash/size tests, then the
  focused asset tests and complete suite both passed
- Pushed/PR/merged, or intentionally local: intentionally local unless the
  user later authorizes publication
- Active ownership released: yes
- Archived plan: `docs/exec-plans/completed/blender-classroom-scene.md`

## Superseding Classroom Presentation Cleanup

The 604,150-triangle and 22-archive-image-plus-checker package recorded above
remains evidence for this historical candidate. A 2026-08-02 follow-up omits
the exact `dustBin` owner hierarchy (one wire bin and 13 crumpled-paper
children), removes 58,320 triangles, and suppresses the generated diagnostic UV
checker. The current 545,830-triangle package retains 19 archive images, and
its eight affected `drawing` sheets match the blank `drawing.004` appearance.
