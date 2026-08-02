# Blender Classroom Source Textures

## Status

- State: complete
- Coordinator: `/root`
- Worktree: canonical `main` at
  `C:/Users/brock/Documents/Codex/uvsr-canonical`
- Base commit: `bed36f951407e23d9e6b4a1a9462a96abc059e8a`
- Started: 2026-08-02 00:09 America/Chicago
- Last updated: 2026-08-02 01:17 America/Chicago
- Planned archive:
  `docs/exec-plans/completed/blender-classroom-source-textures.md`

## Goal and Done Condition

Restore Blender Classroom's source-authored texture appearance after user
feedback that the custom generated PBR treatment looks bad.

Done when:

- [x] Every packaged base-color image remains byte-identical to the source
  archive and textured materials do not tint those images with unused legacy
  socket defaults.
- [x] Generated normal maps and their glTF bindings are removed from the
  runtime package, exporter, reports, provenance, tests, and documentation.
- [x] Geometry, the indoor camera, supported material domains, and GitHub-safe
  buffer packaging remain unchanged.
- [x] Mirrored double-sided Classroom instances retain their textures under
  screen-space Visibility in both production PBR paths.
- [x] The complete Release suite and a live D3D12 Classroom comparison pass.

## Scope

In scope:

- `tools/export_blender_classroom.py`
- `assets/scenes/blender_classroom/`
- Classroom-specific asset contracts, provenance checks, and documentation
- Double-sided normal orientation in the shared, deferred, and forward PBR
  paths, plus its regression contracts and foundation documentation
- Regenerated staged media and exact Release renderer verification

Out of scope:

- Geometry, camera, scene-catalog, or broad renderer architecture changes
- General single-sided negative-determinant instance support
- Re-encoding or replacing source-authored image files
- Commit, push, PR, merge, or release actions

## Baseline Finding

The 26 packaged non-normal images already match the archive byte-for-byte.
The visible regression comes from 17 generated normal PNGs, 26 exported
normal-texture bindings, and non-white base-color factors applied to all 47
textured material translations. The source shader socket defaults are inactive
when an image is connected, so multiplying the glTF texture by those defaults
does not preserve the original graph.

The later Visibility-only black door and paper report was independent of those
textures. The affected objects are among 26 valid negative-determinant glTF
instances. Their materials and geometry buffers match the working copies, but
the shared raster front-face state reports the reflected visible side as a back
face. UVSR then inverted an already view-facing transformed normal, causing the
PBR transport gates to reject its lighting. Double-sided materials now orient
their shading and geometric normals from the view hemisphere; single-sided
materials retain the existing raster-facing rule.

## Verification Plan

| Criterion | Evidence |
| --- | --- |
| Original image bytes | Source/package SHA-256 parity for every packaged base image |
| No custom normals | No generated-normal files, glTF normal textures, or generated-normal report section |
| Material compatibility | OPAQUE/MASK-only glTF with source images and white factors for textured materials |
| Stable scene | Unchanged triangles, one buffer, indoor camera contract, and catalog entry |
| Runtime result | User accepted `classroomparity-bed36f9-0112` with Visibility enabled; no further renderer control requested |
| Repository integrity | Full CTest, title-case, README count, file-size, and patch-whitespace checks |

## Decisions

| Date/time | Decision | Reasoning |
| --- | --- | --- |
| 2026-08-02 00:09 | Remove generated normals and use white factors for image-backed base color. | This retains the archive's exact image bytes and stops both custom surface relief and unintended texture tinting. |
| 2026-08-02 01:12 | Orient double-sided PBR normals from the view hemisphere in both production paths. | The Classroom's mirrored objects are valid glTF instances. Fixing the renderer preserves their source transforms and materials, while baking or replacing the assets would hide the shared double-sided lighting defect. |

## Completion

- Final integrated commit: none; no commit authorized
- Verification summary: the Release renderer compiled all 823 production
  shader variants; targeted PBR and scene-asset checks passed; the complete
  Release CTest suite passed 35/35; the user personally verified the exact
  indoor Classroom candidate and requested no further live check.
- Independent review: no Classroom blocker found. The reviewer confirmed the
  perspective and orthographic view-vector signs, forward/deferred parity, and
  HLSL contract. General single-sided reflected-instance culling remains a
  broader follow-up and does not affect this all-double-sided Classroom export.
- Publication: intentionally local
- Archived plan:
  `docs/exec-plans/completed/blender-classroom-source-textures.md`

## Superseding Classroom Presentation Cleanup

The unchanged-geometry and generated-checker statements above describe this
historical candidate. A 2026-08-02 follow-up omits the exact `dustBin` owner
hierarchy (one wire bin and 13 crumpled-paper children), accounting for 58,320
removed triangles, and replaces the diagnostic checker on eight `drawing`
sheets with the blank `drawing.004` appearance. The current package contains
545,830 triangles and 19 archive-backed images with no generated checker.
