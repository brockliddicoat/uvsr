# Blender Classroom Scene Cleanup

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: canonical `main` at
  `C:/Users/brock/Documents/Codex/uvsr-canonical`
- Base commit: `bed36f951407e23d9e6b4a1a9462a96abc059e8a`
- Local state: continues the uncommitted five-scene/Classroom candidate already
  accepted by the user; unrelated local asset work must be preserved
- Started: 2026-08-02 01:20 America/Chicago
- Completed: 2026-08-02 02:10 America/Chicago
- Archived as:
  `docs/exec-plans/completed/blender-classroom-scene-cleanup.md`

## Goal and Done Condition

Remove the wire trash can and crumpled-paper contents beside the spawn door,
and replace every paper using Blender's generated UV-test checker with the
existing blank-paper appearance.

Done when:

- [x] Only the photographed spawn-corner trash cluster is omitted from export.
- [x] No Classroom paper binds or packages the generated checker image.
- [x] The affected sheets use the same appearance as existing blank paper.
- [x] Geometry/material/image counts and provenance describe the new package.
- [x] Targeted asset checks, the Release renderer build, and the complete
  Release test suite pass.

## Scope

In scope:

- `tools/export_blender_classroom.py`
- `assets/scenes/blender_classroom/` and its staged build copy
- Classroom asset/provenance contracts and scene documentation

Non-goals:

- Moving other props or changing the indoor camera
- Reauthoring source-authored drawing textures
- Renderer, lighting, or UI changes
- Commit, push, PR, merge, release, or deployment actions

## Baseline

- The current package contains one Blender-generated 2048-by-2048 UV-test
  `checker.png`, intentionally bound by source material `drawing`.
- The photographed corner contains a wire bin and crumpled-paper geometry next
  to the open spawn door.
- The previously accepted PBR normal fix and restored source textures remain
  part of the working candidate and must not be regressed.

## Assignment Summary

| Task ID | Owner | Write Scope | Status |
| --- | --- | --- | --- |
| Trash-cluster identification | `/root/classroom_trash_probe` | Read-only | Complete |
| Checker-material diagnosis | `/root/classroom_checker_probe` | Read-only | Complete |
| Regression-contract design | `/root/classroom_cleanup_tests` | Read-only | Complete |
| Asset-contract implementation | `/root/classroom_contract_patch` | `tests/scene_asset_contract_tests.cpp` | Complete |
| Documentation | `/root/classroom_docs_patch` | Classroom and scene overview docs | Complete |
| Independent final review | `/root/classroom_final_review` | Read-only | Complete |
| Integration, assets, tests, and docs | `/root` | Task-owned paths above | Complete |

## Verification Plan

| Acceptance Criterion | Evidence Required |
| --- | --- |
| Exact prop removal | Export report and glTF contracts name/count the omitted cluster |
| Blank papers | No checker image/material binding; affected nodes use the audited blank-paper material |
| Package integrity | Repack report hashes/counts match every packaged file and GitHub limits |
| Runtime integrity | Release renderer and shader bundle build successfully |
| Repository integrity | Targeted and complete CTest, title-case, README count, and diff checks pass |

## Decisions

| Date/time | Decision | Reasoning |
| --- | --- | --- |
| 2026-08-02 01:20 | Make both changes in the deterministic Blender exporter and regenerate the package. | Source-object and material rules remain reproducible and testable; hand-editing the emitted glTF would create an unaudited fork. |
| 2026-08-02 01:35 | Keep `drawing__UVSR_PBR` as a distinct audited identity while matching `drawing.004` exactly. | The source diagnostic assignment remains traceable without packaging or displaying the generated checker. |
| 2026-08-02 01:42 | Omit the trash only through the exact `dustBin` owner hierarchy. | Shared mesh-name filtering could silently remove a different instance; owner identity confines the cleanup to the photographed cluster. |
| 2026-08-02 02:05 | Accept the user's live verification as the product check. | The user personally verified the final scene and explicitly requested no further renderer interaction. |

## Completion

- Final integrated commit: none; no commit authorized
- Export and packaging: pinned Blender 5.1.2 export passed; 545,830 retained
  triangles, 58,320 owner-scoped omitted triangles, 66 opaque materials, 19
  archive-backed images, and one 14,211,272-byte external buffer; lossless
  repack self-test passed
- Asset verification: source and staged packages match byte-for-byte at 28
  files; the focused scene contract and provenance tests passed
- Build and tests: the Release `uvsr` target built successfully and the complete
  Release CTest suite passed 35/35 before the final documentation/comment-only
  audit; affected asset contracts passed again afterward
- Product verification: the exact `classroomclean-bed36f9-0159` candidate loaded
  Blender Classroom, and the user personally verified the scene
- Documentation verification: Title Case self-test and full scan passed; README
  line-count self-test and current-count check passed; patch whitespace passed
- Independent review: no product/package blockers; one inaccurate hypothetical
  second-bin comment was corrected and its provenance hash revalidated
- Publication: intentionally local; no commit, push, PR, merge, or release
- Active ownership released: yes
- Archived plan:
  `docs/exec-plans/completed/blender-classroom-scene-cleanup.md`
