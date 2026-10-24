# Downloaded Scene Imports

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/import-downloaded-scenes` at
  `C:\Users\brock\Documents\Codex\uvsr-downloaded-scenes`
- Base commit: `644a521fe0afb867c412bc12f9f1a364636b0e84`
- Started: 2026-07-26
- Last updated: 2026-07-27
- Planned archive:
  `docs/exec-plans/completed/downloaded-obj-scenes.md`
- Agent policy version: `2026-07-22.1`
- UI reference version: `2026-07-21.2`

## Goal and Done Condition

Goal: preserve the four matching 2026-07-26 Downloads archives in the task
worktree, retain Living Room and Sponza as UVSR-loadable glTF 2.0 scenes, stage
those two scenes in builds from the latest recorded Canonical verified
checkpoint, and prove that both are discoverable and render without regressing
anti-aliasing or visibility GI.

Done when:

- [x] `living_room.zip`, `sibenik.zip`, `sponza.zip`, and
      `salle_de_bain.zip` no longer remain in Downloads and are preserved
      recoverably under this task worktree.
- [x] Living Room and Sponza are glTF 2.0 assets with geometry, retained
      normals, materials, and resolved textures. Salle de Bain and Sibenik were
      removed from runtime assets at the user's direction.
- [x] A clean configure/build stages only those two scenes beside the existing
      Intel PBR Sponza scenes and the World Scenes catalog exposes exactly two
      new friendly entries.
- [x] The production and full-AA Release builds and their full CTest suites
      pass.
- [ ] Both retained scenes complete a labeled runtime load from an interior
      spawn without an importer or GPU error, with TAA and denoised GI active.
- [ ] Final documentation, line-count, Title Case, and diff checks pass after
      runtime verification is recorded.

## Scope

In scope:

- The four exact recent archives originally moved from
  `C:\Users\brock\Downloads`; all remain recoverable in ignored task work.
- Living Room and Sponza runtime assets, with Salle de Bain and Sibenik excluded
  from source, staging, metadata, tests, and the scene picker.
- Recoverable archive movement, extraction, OBJ-to-glTF conversion, asset
  validation, runtime staging, scene-catalog naming, documentation, and tests.
- Scene-specific unit conversion only where the source coordinate scale is
  demonstrably inconsistent with the other packages.

Non-goals:

- `fireplace_room.zip`, older Sponza downloads, or any unrelated asset.
- New camera-preset UI, a default-scene change, unrelated renderer-lighting
  changes, asset redistribution, GitHub publication, or modification of the
  active Bend/SVSM/IBL experiment.
- Changes under the pinned `donut/` dependency.

Affected subsystems and paths:

- `assets/scenes/`
- `CMakeLists.txt`
- `tests/scene_catalog_tests.cpp` and focused asset validation
- `README.md`
- `docs/exec-plans/active/downloaded-obj-scenes.md`
- Worktree-local `work/downloaded-scene-archives/` and
  `work/downloaded-scene-import/`

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `README.md`, the active execution plan, binary scene assets,
  the Git index, the build directory, and the UVSR runtime/GPU lease.

## Baseline

- Canonical repository/remote:
  `https://github.com/brockliddicoat/uvsr.git`; live `main` is `4104018`.
- Local versus remote state: task work starts from the verified `644a521`
  checkpoint and is intentionally uncommitted; `origin/main` is one newer,
  separately unverified commit ahead.
- Verified source commit/build:
  `644a521`; production executable SHA-256
  `2302C4A376196F28AA22ADEAD1CF4A408E5B548F1113F045298032BAA83F0320`;
  developer 20/20 tests, production 21/21 tests, 2,211 DXIL tasks, packaged
  shader contract, document and line-count checks, and product acceptance.
- GPU, scene, camera, resolution, and settings preset when relevant:
  available Windows D3D12 adapter; each imported scene at its automatic initial
  framing; default renderer settings; runtime checks are correctness smoke
  checks rather than performance comparisons.
- Known pre-existing failures: none recorded at `644a521`.

## Dependencies and Interfaces

| Dependency or Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical verified base | `644a521` | Ready | Whole task |
| Four source archives | Exact 2026-07-26 ZIP hashes retained in ignored task work | Ready | Recovery and provenance |
| Blender portable | Existing Blender 5.1.2 executable | Ready | OBJ import and glTF export |
| Pinned Donut | `bc1ea24` | Initialized with exact nested pins | Configure, build, and runtime |
| Active shadow/IBL work | Separate `codex/bend-screen-space-shadows` worktree | No source overlap; its user-controlled renderer still owns the runtime lease | Final smoke checks |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Donut discovers only `.scene.json`, `.gltf`, and `.glb`; imported scenes must
  therefore be valid glTF 2.0 and may not rely on direct OBJ loading.
- The existing World Scenes dropdown remains the only selector. It receives
  two additional Title Case display names without changing its ID, ordering
  rules, deferred scene-load transaction, default, reset behavior, or layout.
- The existing Intel PBR Sponza descriptor remains UVSR's default.
- Build staging must depend on every source scene asset so additions, updates,
  and removals synchronize deterministically into
  `media/glTF-Sample-Assets/Models`.
- Source archives stay recoverable in ignored task work and are not runtime
  assets or redistribution candidates.

## UI Intake Record

- Owning presentation: the existing **World Scenes** disruptive destination
  dropdown in the General content path.
- Control classification: existing nonstructural dropdown using
  `BeginRoundedCombo`, `DrawDeferredDropdownOption`, and the shared deferred
  commit barrier.
- Default and reset: unchanged; Intel PBR Sponza Decorated remains default and
  the destination selector has no reset icon.
- Direct consumers: scene catalog, current-scene path, scene loader, and loading
  screen display name.
- Structural consumers: none; only option count and popup scrolling can change.
- Animation and scroll owner: existing combo popup and `##SettingsBody`;
  no new animation owner.
- Renderer cost: scene file I/O, decoding, GPU upload, and scene replacement
  only after a committed selection.
- New labels: Living Room and Sponza, both conventional English Title Case.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Source discovery | `/root/find_download_scenes` | Read-only | Downloads snapshot | None | None | Completed |
| Base discovery | `/root/find_verified_canon` | Read-only | Local and live history | None | None | Completed |
| Integration discovery | `/root/inspect_scene_integration` | Read-only | `644a521` repository shape | None | None | Completed |
| Convert and integrate | `/root` | `codex/import-downloaded-scenes` | `644a521` | All task-owned paths above | Discovery handoffs | Runtime verification pending |

## Assignment Contracts

### Convert and Integrate: Produce Working UVSR Scenes

- Owner/thread: `/root`
- Branch/worktree:
  `codex/import-downloaded-scenes` at
  `C:\Users\brock\Documents\Codex\uvsr-downloaded-scenes`
- Base commit/state: clean `644a521`
- Read scope: exact source archives, asset packages, scene catalog, CMake
  staging, renderer load path, repository documents, and test contracts.
- Write scope: task work folders plus the affected paths listed above.
- No-touch scope: Downloads files other than the four resolved archives, live
  `main`, other worktrees, `donut/` sources, existing Intel PBR Sponza bytes,
  remote refs, and the active shadow/IBL branch.
- Build directory and runtime/GPU/resource lease: worktree-local `build`; the
  final labeled renderer process only after coordination with the active task.
- Dependencies already integrated: none beyond `644a521`.
- Interface/invariant contract: preserve the existing scene selector and
  default; keep portable glTF 2.0 with resolved resources; retain authored
  material names; never silently omit a referenced texture.
- Deliverable: two locally staged working scenes, source/import provenance,
  focused regression coverage, Release executable, and verification evidence.
- Done when: every acceptance criterion maps to evidence.
- Required verification: archive/hash audit, glTF structural validation, scene
  catalog test, focused UI source contract, developer and production Release
  builds, both full CTest matrices, Title Case checker, line-count checks when
  counted source changes, `git diff --check`, stale-reference and staged-package
  audits, and labeled load smokes for Living Room and Sponza with TAA/GI.
- Allowed Git and external actions: local branch/worktree, scoped moves and
  edits, submodule initialization at the pinned revision, builds, tests, and
  labeled runtime. No commit, push, PR, merge, release, or remote mutation.
- Stop and report if: any archive identity changes, a referenced source texture
  is missing, a scene cannot load without a renderer tradeoff, an output exceeds
  a practical packaging limit that requires destructive simplification, or
  redistribution rights would need to be asserted.
- Handoff revision/artifact: pending.
- Handoff acknowledged by/on: pending.

## Integration Order

1. Validate and move the four exact archives into recoverable task work.
2. Extract and convert each package with scene-specific scale and normal rules.
3. Retain only Living Room and Sponza under `assets/scenes`, per the user's
   revised scope.
4. Generalize runtime staging and extend focused catalog/asset coverage.
5. Update user-facing asset/build documentation.
6. Configure and test developer and production builds, then run both retained
   scenes serially with TAA/GI enabled.
7. Review the complete diff, archive this plan, and release ownership.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command or Experiment | Result or Artifact |
| --- | --- | --- | --- |
| Exact sources moved | Original hashes match and Downloads paths are absent | SHA-256 plus source/destination path audit | Passed; all four destination hashes match and the exact Downloads paths are absent |
| Valid converted assets | glTF 2.0 structure, buffers, normals, materials, images, and bounds | Blender round-trip plus structural validator | Passed for both retained GLBs; embedded textures, required normals, opaque or masked materials, declared lengths, and interior camera geometry are valid |
| Deterministic staging | Source and staged inventories/hashes match | Clean configure/build and manifest comparison | Passed through `uvsr_scene_asset_contract_tests` |
| Catalog exposure | Exactly two friendly entries plus existing Sponza entries | `uvsr_scene_catalog_tests` and runtime dropdown inspection | Passed; static contracts and the live full-AA picker show exactly Living Room, PBR Sponza Decorated, PBR Sponza Plain, and Sponza |
| Build correctness | Production and full-AA Release renderers and all registered tests pass | CMake build and CTest | Passed after the GI correction: production 24/24 and full-AA 23/23; one final rerun remains after adversarial test hardening |
| Runtime correctness | Both scenes reach responsive loaded frames without importer/GPU errors and retain TAA/GI | Two labeled `tools/launch_uvsr.ps1` runs | Living Room passed from its interior spawn with High Gaussian radius 4 and valid TAA history; downloaded Sponza remains pending because Windows foreground recovery failed |
| Documentation quality | Title Case, links, line counts, and whitespace pass | Repository checkers and `git diff --check` | Passed before final plan archival; final rerun required after archival |

## Decisions

| Date or Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-26 | Use `644a521`, not newer live `4104018` | `644a521` is the newest checkpoint with complete recorded developer, production, shader, publication, and product-acceptance evidence; the hot-patch head has no replacement production verification record | Whole task |
| 2026-07-26 | Convert rather than add OBJ support to Donut | UVSR and its pinned dependency intentionally discover glTF/GLB only; a renderer/importer extension would be broad, risky, and unnecessary | Conversion |
| 2026-07-26 | Preserve ZIPs in ignored task work | The user explicitly requested a move; retaining the archives is recoverable while avoiding runtime staging and any implied redistribution | Source handling |
| 2026-07-26 | Keep Intel PBR Sponza as the default | The request adds scenes but does not change startup behavior or standardized benchmark semantics | UI and runtime |
| 2026-07-26 | Export one merged, embedded-texture GLB per scene | The pinned runtime needs portable glTF 2.0 without Draco; merging preserves all primitives and materials while avoiding excessive scene-node overhead | Conversion and runtime |
| 2026-07-26 | Use scales 1 and 0.01 for retained Living Room and Sponza | Source bounds show Sponza is centimeter-scaled while Living Room already has room-scale units | Conversion |
| 2026-07-26 | Keep all runtime materials opaque except Sponza texture cutouts | UVSR currently instantiates only its opaque draw strategy; preserving imported transmissive or scalar-alpha materials would make intended surfaces disappear | Conversion and renderer compatibility |
| 2026-07-26 | Omit legacy height maps from glTF normal slots | The MTL bump files are scalar height fields, not tangent-space normal maps; labeling them as glTF normals would produce invalid shading | Conversion |
| 2026-07-26 | Export through a pending GLB and replace only after `FINISHED` | A failed rerun must preserve the prior known-good conversion without ever mistaking it for fresh output | Reproducible conversion |
| 2026-07-26 | Verify the provenance manifest and complete staged inventory at CTest time | Descriptor-only checks would not catch stale hashes or omitted notices; exact SHA-256, byte counts, paths, and source/staged bytes now fail closed | Packaging and regression coverage |
| 2026-07-26 | Remove Salle de Bain and Sibenik from the product but preserve their ignored archives | The user narrowed the branch before merge; removing source assets, staging entries, metadata, tests, and documentation prevents stale product exposure while ignored archives keep the operation recoverable | Product scope and verification |
| 2026-07-27 | Restore production TAA cache identity and retain the full-AA acceptance build | Production Performance, Balanced, and Quality/Ultra now occupy three distinct algorithm slots, with Quality and Ultra sharing only because their shader options match; the accepted renderer is explicitly built with `UVSR_AA_DEVELOPER_OVERRIDES=ON` | Anti-aliasing and packaging |
| 2026-07-27 | Denoise High and Ultra before renderer TAA | Live Living Room A/B showed raw stochastic visibility remained noisy with TAA; radius-4 Gaussian joint-bilateral reconstruction removed the speckle without enabling a second temporal history, while Low and Medium retain their compact bilateral path | Visibility GI and runtime quality |

## Progress and Handoffs

| Date or Time | Task or Owner | Status | Revision or Artifact | Checks | Risks or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-26 | Source discovery | Completed | Four exact ZIP paths and hashes | ZIP structure, texture references, mesh counts, and bounds audited | Historical conversion concerns were resolved; Sibenik was later removed from product scope |
| 2026-07-26 | Base discovery | Completed | `644a521` and production artifact hash | First-parent, publication, test, shader, and acceptance records audited | Preserve this verified base while developing; before merge, integrate current `origin/main` at `4104018` and repeat the complete gate |
| 2026-07-26 | Integration discovery | Completed | Scene staging, catalog, renderer, packaging, and licensing contract | Repository and source archives inspected | Contract integrated |
| 2026-07-26 | Coordinator | Converted | Four source archives under `work/downloaded-scene-archives/`; Living Room and Sponza GLBs and descriptors under `assets/scenes/` | Final hash, structural, source/staging identity, provenance, and enclosed-camera geometry checks pass for the retained two-scene product | Removed scene archives remain ignored recovery inputs only |
| 2026-07-27 | Coordinator | Built and tested | `build-scenes/bin/uvsr.exe` and `build-scenes-aa/bin/uvsr.exe` | Both final links succeeded after the GI correction; production passed 24/24 CTest and full-AA passed 23/23; Title Case, line counts, JSON, Python syntax, and diff checks passed | Rerun after the final adversarial test hardening, then finish Sponza and motion smokes |
| 2026-07-27 | Independent diff review | Completed | Current text, asset metadata, staging, camera, TAA, GI, and tests | No product-code blocker; two test-hardening gaps are being repaired | Commit and current-main integration remain procedural merge gates |
| 2026-07-27 | Runtime coordination | In Progress | Rebuilt full-AA `build-scenes-aa/bin/uvsr.exe` | Exact four-entry picker, Living Room interior spawn, High Gaussian radius 4, smooth stationary output, and valid accumulating TAA history verified | Windows foreground recovery failed after the Living Room smoke; resume downloaded Sponza, Intel plain, and standardized motion checks when the overlay clears |

## Risks and Escalation Triggers

- Classic Sponza uses a coordinate scale roughly 100 times larger than the other
  packages and needs an explicit, documented unit conversion.
- Living Room includes no license file in its archive. The retained attribution
  record cites the source archive's CC BY 3.0 terms, but any public publication
  should retain authoritative license evidence beside the asset.
- The active shadow/IBL task may own the GPU/runtime window; runtime checks must
  wait or coordinate rather than contend.

Stop and ask the user if:

- A working result requires visible geometry or texture simplification that
  materially changes a source scene.
- The user wants these locally imported assets committed or distributed without
  established redistribution terms.
- The only way to complete runtime verification would interrupt user-controlled
  renderer interaction.

## Completion

- Final integrated commit: pending; no commit requested.
- Verification summary: pending.
- Independent review: pending.
- Coming Soon/documentation update: no roadmap entry planned; durable asset and
  build behavior will be documented in the renderer baseline.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: pending.
- Active ownership released: no.
- Archived to completed/abandoned path: pending.
