# Tonemapper Drawer Sunset

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/tonemapper-sunset` in
  `C:\Users\brock\Documents\Codex\2026-07-17\ive\work\uvsr-tonemapper-sunset`
- Base commit: `5f43205ecfe00e31fd64af34cad0f031472a224c`
- Started: `2026-07-17 01:46:52 -05:00`
- Last updated: `2026-07-17 02:05:32 -05:00`
- Planned archive:
  `docs/exec-plans/completed/tonemapper-drawer-sunset.md`

## Goal and Done Condition

Goal:

Sunset the optional Tonemapper drawer, grading presets, `.cube` LUT discovery,
bundled film-look LUTs, and premature local-tone-mapping roadmap work while
retaining UVSR's required fixed neutral AgX display conversion. Preserve the
exact pre-sunset feature as a mechanically restorable repository archive and
record why the feature was strategically deferred rather than judged a failure.

Done when:

- [x] The active renderer has no Tonemapper drawer, grading state, LUT loader,
      LUT GPU resource, LUT runtime packaging, or bundled LUT asset.
- [x] Fixed neutral AgX remains the normal HDR-to-display path and the explicit
      forward tonemapperless comparison mode remains available.
- [x] A postmortem, restoration manifest, and checked restoration patch let a
      future agent act on “bring back the tonemapper drawer” without
      reconstructing the feature from history.
- [x] The revival trigger requires the bilateral-grid local tonemapper to be
      implemented in the same candidate and rejects a drawer-only restoration.
- [x] README, PBR documentation, postmortem index, and agent guidance describe
      the sunset accurately with Title Case headings.
- [x] Release renderer and PBR tests build, tests pass, documentation checks
      pass, the restoration patch passes `git apply --check`, and a labeled
      runtime smoke check exercises the affected display path.

## Scope

In scope:

- Remove the user-facing grade controls, presets, LUT selection, parsing,
  upload, discovery, assets, and packaging.
- Simplify the AgX shader and pass to their fixed neutral production contract.
- Remove the stale bilateral-grid local-tone-mapping roadmap entry.
- Add a durable postmortem and self-contained restoration bundle.

Non-goals:

- Removing required HDR display conversion.
- Retuning lighting, sky, exposure, materials, visibility, or camera behavior.
- Removing the explicit `ForwardTonemapperless` comparison mode.
- Publishing, opening a pull request, merging, or committing without later
  user authorization.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/agx_tonemapping_ps.hlsl`
- `CMakeLists.txt`
- `assets/luts/kodak/`
- `README.md`
- `AGENTS.md`
- `docs/pbr-foundation.md`
- `docs/postmortem/`

Shared hotspots reserved for the coordinator:

- `README.md`, `AGENTS.md`, `CMakeLists.txt`, `src/shaders.cfg`, the AgX
  CPU/HLSL binding contract, and LUT assets.

## Baseline

- Canonical repository/remote:
  `https://github.com/brockliddicoat/uvsr.git`
- Local versus remote state: local `main` and live `origin/main` are equal at
  `5f43205ecfe00e31fd64af34cad0f031472a224c`.
- Verified source commit/build: the task base is a clean integrated main
  checkpoint; this plan does not assume a reusable build directory.
- GPU, scene, camera, resolution, and settings preset when relevant: use a
  task-local Release build and the launcher's default standardized Sponza scene
  for the smoke check.
- Known pre-existing failures: none recorded for this isolated worktree.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| AO performance optimization | Remains isolated in its existing worktree; do not consume its uncommitted benchmark evidence | Disjoint | Future integration |
| PR #10 shared shader helpers | No AgX, UI, asset, or documentation overlap | Disjoint | Future integration |
| PR #11 visibility tests | No AgX, UI, asset, or documentation overlap | Disjoint | Future integration |
| Bilateral-grid local tone mapping | No live local or remote branch was found; remove its stale Coming Soon entry as part of the sunset | Retired from roadmap | README |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `agx_tonemapping_ps.hlsl` remains the packaged fixed display shader.
- Its active binding contract shrinks to the scene-color texture only.
- The default visual contract remains neutral AgX plus output conversion and
  dithering.
- No serialized settings contract exists; renderer settings already reset on
  each launch.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| sunset | `/root` | `codex/tonemapper-sunset` | `5f43205` | All in-scope paths | None | Complete |

## Assignment Contracts

### Sunset: Preserve and Remove the Optional Feature

- Owner/thread: `/root`
- Branch/worktree: `codex/tonemapper-sunset`
- Base commit/state: clean `5f43205`
- Read scope: complete UVSR repository and feature history
- Write scope: only the in-scope paths listed above
- No-touch scope: `donut/`, scene assets, visibility implementation, AO
  performance worktree, and unrelated branches
- Build directory and runtime/GPU/resource lease: task-local `build/`; one
  launcher-owned process named with experiment `tonesunset`
- Dependencies already integrated: main through `5f43205`
- Interface/invariant contract: keep fixed neutral AgX output and the
  tonemapperless comparison mode; remove all optional grading/LUT state
- Deliverable: local verified sunset candidate with restoration archive
- Done when: every goal checkbox and verification row is satisfied or an
  explicit limitation is recorded
- Required verification: focused search, patch apply check, Release builds,
  CTest, documentation checks, diff checks, and runtime smoke
- Allowed Git and external actions: create and use this local branch/worktree;
  no commit, push, PR, merge, release, or deployment
- Stop and report if: the required fixed display transform cannot be separated
  from the optional feature without an unrequested lighting change, restoration
  cannot be made self-contained, or overlapping user work appears
- Handoff revision/artifact: final uncommitted task diff and archive manifest
- Handoff acknowledged by/on: coordinator self-handoff on
  `2026-07-17 02:05 -05:00`

## Integration Order

1. Preserve the exact pre-sunset source identity and write the postmortem.
2. Remove optional CPU, GPU, UI, asset, packaging, and roadmap surfaces.
3. Generate and validate the restoration patch from the completed removal.
4. Verify documentation, build, tests, runtime, and final diff.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Optional feature is absent | Focused repository search and diff review | `rg` plus `git diff` | Pass: no optional CPU, HLSL, UI, loader, packaging, or runtime LUT reference remains |
| Fixed AgX path compiles | Release renderer build | `cmake --build build --config Release --target uvsr` | Pass |
| Rendering contracts remain coherent | PBR test build and CTest | Build all registered test targets; `ctest` | Pass: 11/11 |
| Runtime display path starts | Labeled launcher smoke | `tools/launch_uvsr.ps1 -Experiment "tonesunset"` | Pass: `tonesunset-5f43205-0202` loaded PBR Sponza Decorated; fixed display output rendered and no Tonemapper drawer was visible |
| Archive is restorable | Patch integrity, apply check, and full round trip | SHA-256, `git apply --check`, apply, blob verification, reverse apply | Pass: all eight pre-sunset and three post-sunset blobs matched; SHA-256 `e5e85b69d5cf8a5f6ab5cf3933f63b98d765e113df68d4606b4f72f774e5e93a` |
| Documentation is valid | Self-test and repository-wide Title Case audit | `tools/check_document_title_case.cmd --self-test`; checker | Pass: self-test and 277 headings/lead-ins with zero violations |
| Diff is mechanically clean | Whitespace and repository status review | `git diff --check`; `git status` | Pass; task remains intentionally uncommitted and local |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-17 01:46 -05:00 | Retain fixed neutral AgX while sunsetting its optional drawer and LUT layer | Tone mapping is required to display scene-linear HDR correctly; reverting to the older Donut transform would retune the visible renderer, while merely hiding controls would leave dormant complexity | sunset |
| 2026-07-17 01:46 -05:00 | Preserve the feature with a reverse patch, manifest, immutable base SHA, and agent trigger | A base commit alone would force a future agent to reconstruct intertwined hunks; copying the entire old `uvsr.cpp` would overwrite future unrelated work | sunset |
| 2026-07-17 01:52 -05:00 | Make global drawer restoration and bilateral-grid local tone mapping one atomic revival | The user explicitly requires the local tonemapper to be implemented when the drawer returns; a patch-only drawer restoration is therefore incomplete | sunset |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-17 01:46 -05:00 | sunset `/root` | Active | Base `5f43205` | Repository, history, roadmap, worktrees, branches, and open PRs inspected | Patch implementation and archive |
| 2026-07-17 02:05 -05:00 | sunset `/root` | Complete | Local task diff plus restoration bundle | Release renderer and all test targets built; CTest 11/11; labeled runtime smoke; archive round trip; documentation and diff audits | Independent review and product acceptance remain required before any future integration |

## Risks and Escalation Triggers

- Shader and CPU binding layouts must change together.
- Removing neutral grade arithmetic could produce tiny numerical differences;
  smoke validation must confirm no gross display regression.
- A future branch may have independently started the stale local-tone-mapping
  work; integration must re-check live branches and plans.
- High-risk rendering removal needs independent review before any later
  integration.

Stop and ask the user if:

- Preserving fixed neutral AgX proves incompatible with the requested sunset.
- New evidence shows the LUT assets or drawer are required by another accepted
  active product task.
- Publication or integration becomes necessary to meet a newly requested
  outcome.

## Completion

- Final integrated commit: none; the candidate is an uncommitted local diff on
  `codex/tonemapper-sunset`
- Verification summary: Release renderer and all registered test executables
  built; CTest passed 11/11; `tonesunset-5f43205-0202` rendered PBR Sponza
  Decorated without the Tonemapper drawer; archive checksum, apply check, and
  full restore/reverse-restore blob identity passed; documentation self-test,
  277-heading audit, local-link audit, active-reference sweep, and
  `git diff --check` passed
- Independent review: not performed for this local candidate; required before
  any later integration because the task removes rendering and resource code
- Coming Soon/documentation update: stale local-tone-mapping entry removed;
  renderer baseline, PBR foundation, postmortem index, postmortem, archive
  manifest, and agent revival contract updated
- Pushed/PR/merged, or intentionally local: intentionally local; no commit,
  push, pull request, merge, release, or deployment
- Remaining experiments or follow-ups: fresh product acceptance and independent
  review before integration; any future drawer revival must also implement the
  bilateral-grid local tonemapper
- Active ownership released: yes, on final handoff
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/tonemapper-drawer-sunset.md`
