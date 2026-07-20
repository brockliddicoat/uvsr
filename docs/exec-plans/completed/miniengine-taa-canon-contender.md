# Miniengine Temporal Anti-Aliasing Canon Contender Integration

This successor corrects the earlier local-contender interpretation by composing
the accepted MiniEngine TAA renderer work with the current live GitHub canonical
line before presenting another Canon Contender.

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/taa-miniengine-canon-contender` in
  `work/uvsr-taa-miniengine-canon-contender`
- Base commit: `9f2e69e832c81c105b059a83e4e72daa25b15783`
- Started: 2026-07-17
- Last updated: 2026-07-17
- Planned archive:
  `docs/exec-plans/completed/miniengine-taa-canon-contender.md`

## Goal and Done Condition

Goal: create the actual Canon Contender by transplanting the accepted
MiniEngine TAA and Piloted-camera renderer changes onto current live
`origin/main`, resolving the tonemapper-sunset overlaps without changing the
TAA algorithm.

Done when:

- [x] The branch contains live `origin/main` `9f2e69e` and is not behind it.
- [x] TAA retains its accepted scene-linear, motion, history, resolve, and
      sharpen contracts without restoring the removed tonemapper.
- [x] Release build, all registered tests, documentation checks, runtime smoke,
      exact artifact identity, and independent integration review pass.
- [x] The result is clearly labeled Canon Contender rather than Canonical
      verified, with publication state reported exactly.

## Scope

In scope:

- Transplant renderer commits `6f84a99` and `f2551e1`.
- Resolve overlaps in `CMakeLists.txt`, `README.md`, and `src/uvsr.cpp` against
  the tonemapper-free canonical renderer.
- Rebuild and reverify the composed artifact.

Non-goals:

- Algorithmic TAA changes, recovery systems, camera-mechanics changes, or
  restoration of the retired tonemapper drawer and LUTs.
- Push, pull request, merge to `main`, or Canonical promotion without separate
  publication and exact-artifact acceptance.

Affected subsystems and paths:

- `CMakeLists.txt`
- `README.md`
- `src/shaders.cfg`
- `src/sponza_camera_preset.cpp`
- `src/taa_miniengine*`
- `src/uvsr.cpp`
- `tests/taa_miniengine_tests.cpp`
- TAA and Sponza camera reference tests

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- `src/shaders.cfg`
- `src/uvsr.cpp`
- CPU/HLSL temporal contracts

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main`
  `9f2e69e832c81c105b059a83e4e72daa25b15783`
- Local versus remote state: integration branch equals live `origin/main`
  before transplant.
- Verified source commit/build: Canonical verified renderer
  `0b4279839f9f24399dcecefca139af67fc70404f`; `9f2e69e` is its
  documentation-only verification record.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU; PBR Sponza Decorated; 1920x1080; normal
  `Freelook` launch; deferred PBR defaults.
- Known pre-existing failures: none recorded at the Canonical verified
  checkpoint.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `origin/main` `9f2e69e` | Integrated as base | Whole contender |
| Accepted TAA implementation | `6f84a99` | Integrated as `d275175` | Renderer |
| Piloted camera label | `f2551e1` | Integrated as `8e3cbad` | Final contender UI |
| Independent TAA review | No blocker on accepted manifest | Complete: no P0-P2 findings after repair | Integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- TAA reads and writes scene-linear HDR before the tonemapper-free display
  conversion.
- Motion remains current-to-previous pixel XY, previous-minus-current device
  depth Z, and explicit validity A.
- TAA is unavailable while visibility Temporal Reconstruction is enabled
  because that history does not yet consume TAA's subpixel jitter delta.
- Camera mechanics and internal `SponzaCameraLocation::Free` identity remain
  unchanged; only its visible descriptor is `Piloted`.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `integrate` | `/root` | `codex/taa-miniengine-canon-contender` | `9f2e69e` | All integration paths | Renderer commits and live canonical | Active |

## Assignment Contracts

### `integrate`: Compose and Verify the Canon Contender

- Owner/thread: `/root`
- Branch/worktree: `codex/taa-miniengine-canon-contender`,
  `work/uvsr-taa-miniengine-canon-contender`
- Base commit/state: clean `9f2e69e`
- Read scope: full repository and completed predecessor plan
- Write scope: affected paths listed above plus this plan
- No-touch scope: `donut/`, archived tonemapper restoration bytes, unrelated
  visibility PR paths, and the preserved predecessor branch
- Build directory and runtime/GPU/resource lease: integration-local `build/`;
  one integration UVSR window
- Dependencies already integrated: live canonical base
- Interface/invariant contract: preserve scene-linear TAA and the accepted
  MiniEngine behavior while adapting only current renderer interfaces
- Deliverable: clean, current-GitHub Canon Contender commit and artifact
- Done when: every plan criterion is mapped to evidence
- Required verification: Release build, full CTest, document validator,
  `git diff --check`, normal-camera runtime smoke, exact hash, and independent
  post-composition review
- Allowed Git and external actions: local branch/worktree integration and
  commits; no push, pull request, or merge to `main`
- Stop and report if: composition requires restoring removed display features
  or changing the TAA algorithm rather than adapting interfaces
- Handoff revision/artifact: renderer `3266edf190665c9f9162bad452477bbcdf42c1a8`;
  executable SHA-256
  `3AE1D454E8BFC3FEF9CA71BB2888EA6E33B9D0BA7E6DE6A94C223EE647967C12`
- Handoff acknowledged by/on: exact artifact left running for user review on
  2026-07-17

## Integration Order

1. Initialize the worktree and transplant `6f84a99`.
2. Resolve current-main build, renderer insertion, UI, and documentation
   conflicts without restoring the removed tonemapper.
3. Transplant `f2551e1`.
4. Build, test, review, smoke-test, hash, and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Current GitHub is integrated | Branch base and ahead/behind count | Fetch plus Git ancestry | Pass: `0` behind, `2` ahead before final repair commit |
| TAA composes with tonemapper sunset | Semantic conflict review and Release build | Cherry-pick plus CMake build | Pass: full Release build |
| Reference contracts remain stable | All registered tests | CTest Release | Pass: 12/12 |
| Normal final camera is not locked and the free-location descriptor is `Piloted` | Responsive normal launch plus camera reference tests | Required launcher without benchmark flag and CTest Release | Pass: command line contains no arguments; default is `Freelook`; label test expects `Piloted` |
| Documentation is conforming | Validator self-test and full scan | Repository checker | Pass: self-test and 317 headings/lead-ins |
| High-risk composition is reviewed | Independent read-only review | Post-composition source review | Pass: initial visibility jitter-grid blocker repaired; re-review found no P0-P2 issue |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-17 | Start a new branch directly from live `origin/main`. | A merge into the old-base experiment would preserve unnecessary documentation commits and make the contender harder to audit. A fresh canonical base plus the two renderer commits produces the clearest lineage. | All |
| 2026-07-17 | Preserve the prior branch unchanged. | Its accepted artifacts and review evidence remain useful provenance; rewriting it would invalidate cited SHAs. | All |
| 2026-07-17 | Retain the tonemapper sunset while resolving the `src/uvsr.cpp` cherry-pick conflict. | The TAA UI and pass are required; the retired tonemapper drawer and LUT state are not. | Renderer integration |
| 2026-07-17 | Make TAA mutually exclusive with visibility Temporal Reconstruction. | The current visibility history reprojects de-jittered motion without TAA's jitter delta. A small runtime/UI guard has lower contender risk than adding new cross-temporal shader math. | Renderer and TAA tests |
| 2026-07-17 | Use the physical short build directory `work/btaacanon`. | DirectX-Headers exceeded the Windows path limit in the generated worktree-local build directory. A worktree `build` junction retains the expected local entry point without changing source. | Verification |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-17 | `integrate` `/root` | Active | Base `9f2e69e` | Remote freshness and branch identity confirmed | Transplant renderer commits |
| 2026-07-17 | `integrate` `/root` | Active | Renderer commits `d275175`, `8e3cbad` plus repair diff | Full Release build, 12/12 CTest, document self-test, 317-heading audit, and `git diff --check` pass | Await independent repair review, freeze commit, rebuild exact SHA, smoke-test |
| 2026-07-17 | `integrate` `/root` | Complete | Renderer `3266edf`; executable SHA-256 `3AE1D454E8BFC3FEF9CA71BB2888EA6E33B9D0BA7E6DE6A94C223EE647967C12` | Exact-commit Release rebuild, 12/12 CTest, responsive normal launch `canoncontender-3266edf-0454`, independent review clean | Await product acceptance; do not publish without authority |

## Risks and Escalation Triggers

- The tonemapper sunset changed `src/uvsr.cpp`, `CMakeLists.txt`, and README;
  textual conflict resolution must preserve both display removal and the
  scene-linear TAA insertion.
- Any conflict repair creates a new artifact and invalidates the prior runtime
  acceptance.
- The new worktree requires its own initialized submodule and build tree.
- Visibility Temporal Reconstruction does not yet share TAA's jitter-delta
  contract; the contender explicitly prevents the two temporal modes from
  running together.

Stop and ask the user if:

- integration exposes a material TAA quality/performance choice or requires
  restoring a removed display feature;
- publication authority beyond the local contender is needed.

## Completion

- Final integrated renderer commit:
  `3266edf190665c9f9162bad452477bbcdf42c1a8`
- Verification summary: Release build passed; 12/12 CTest passed; document
  checker self-test passed; 317 tracked headings and bold lead-ins passed;
  `git diff --check` passed; normal launch is responsive with command line
  containing no benchmark-camera argument.
- Independent review: no P0-P2 findings after the visibility-history repair;
  current-main tonemapper sunset remains intact.
- Coming Soon/documentation update: README records the current-main contender
  and the conservative visibility-history compatibility guard.
- Pushed/PR/merged, or intentionally local: intentionally local; no push, pull
  request, or merge was authorized.
- Remaining experiments or follow-ups: product acceptance on this exact
  artifact, then the postmortem's controlled static/motion/resize capture matrix,
  temporal stability evidence, and longer performance soak before Canonical
  publication.
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/miniengine-taa-canon-contender.md`
