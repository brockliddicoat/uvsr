# Settings UI Reference Canon Contender

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/imgui-reference-canon-contender` in
  `work/uvsr-imgui-reference`
- Base commit: `89708388c7c7af6efccd24ae1ed49131f3f45ddc`
- Started: 2026-07-18
- Last updated: 2026-07-18
- Archived to:
  `docs/exec-plans/completed/imgui-reference-canon-contender.md`

## Goal and Done Condition

Goal: preserve the user-accepted compact translucent settings UI, loading
telemetry, and related runtime polish as a clean local Canon Contender based on
the live canonical line.

Done when:

- [x] The branch is based directly on current `origin/main`.
- [x] Donut and its nested ImGui checkout are clean and remain pinned.
- [x] The complete contender is committed locally.
- [x] An exact-commit Release build, all registered tests, documentation checks,
      runtime smoke, and artifact hashing pass.

## Scope

In scope:

- The accepted settings-panel visual system, layout, animations, toggles,
  tooltips, loading UI, stat presentation, material-editor behavior, and window
  startup placement.
- First-party build-time patches for the required Donut loading telemetry and
  ImGui rendering behavior.
- Supporting shader, font, GPU-stat, documentation, and reference-test changes.

Non-goals:

- Push, pull request, merge to `main`, release, or Canonical promotion.
- Further visible restyling after the accepted compensated toggle-knob color.
- Restoration of retired touchpad input experiments.

Affected subsystems and paths:

- `CMakeLists.txt`
- `README.md`
- `assets/fonts/`
- `overrides/`
- `src/backdrop_blur_ps.hlsl`
- `src/gpu_performance_monitor.*`
- `src/shaders.cfg`
- `src/uvsr.cpp`
- Related design and reference-test documents

Shared hotspots reserved for the coordinator:

- Root build configuration
- Renderer UI and runtime state
- Pinned dependency override patches
- Root product documentation

## Baseline

- Canonical repository/remote: live `origin/main`
  `89708388c7c7af6efccd24ae1ed49131f3f45ddc`
- Local versus remote state before integration: equal, `0` behind and `0` ahead
- Verified source commit/build: Canonical verified `8970838`; accepted
  experiment built from the same commit plus the complete task-owned diff
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU; PBR Sponza Decorated; deferred PBR defaults;
  Settings, Visibility, and nested visibility drawers visible
- Known pre-existing failures: none recorded

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `origin/main` `8970838` | Integrated as base | Whole contender |
| Donut | Pinned root submodule revision | Clean | Renderer |
| Dear ImGui | Pinned nested submodule revision plus UVSR build-time patch | Clean and integrated | Settings UI |
| User visual acceptance | Accepted compensated knob and 50% white active track | Complete for predecessor artifact | Product review |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Donut and Dear ImGui source trees remain unmodified; UVSR patches selected
  source copies in the build tree and replaces only the affected target inputs.
- The loading UI consumes explicit importer work counters without changing
  serialized scene data.
- The settings UI remains session-only and preserves existing renderer defaults.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `integrate` | `/root` | `codex/imgui-reference-canon-contender` | `8970838` | All listed contender paths | Accepted experiment and live canonical | Complete |

## Assignment Contracts

### Integrate: Compose and Verify the Canon Contender

- Owner/thread: `/root`
- Branch/worktree: `codex/imgui-reference-canon-contender`,
  `work/uvsr-imgui-reference`
- Base commit/state: clean canonical commit plus the accepted task-owned diff
- Read scope: full repository and prior Canon Contender records
- Write scope: affected paths listed above and this plan
- No-touch scope: pinned dependency histories, canonical branch, remote refs,
  and unrelated archived work
- Build directory and runtime/GPU/resource lease: worktree-local `build/` and
  one labeled UVSR contender process
- Dependencies already integrated: current `origin/main`
- Interface/invariant contract: reproduce the accepted UI while leaving pinned
  dependencies clean
- Deliverable: clean local Canon Contender commit and labeled Release artifact
- Done when: every done condition and verification row has evidence
- Required verification: Release build, full CTest, document checker self-test
  and scan, `git diff --check`, exact artifact hash, and responsive labeled
  launch
- Allowed Git and external actions: local branch and commits only; no push,
  pull request, merge, release, or deployment
- Stop and report if: clean dependency staging changes visible accepted behavior
  or the live canonical line diverges
- Handoff revision/artifact: renderer commit
  `df097d74a609b34979f5d5e462525e3e7f9ae7f9`; `build/bin/uvsr.exe`
  SHA-256 `66E274925485A5C320664FB6CD7533365F7B35996E254DFF693222AFC4E27890`
- Handoff acknowledged by/on: user verified the exact running contender
  `canoncontender-df097d7-0618` on 2026-07-18

## Integration Order

1. Refresh the live canonical target and create the contender branch.
2. Capture dependency edits as first-party build-time patches and clean both
   pinned repositories.
3. Build and test the clean override composition.
4. Update documentation, commit the contender, and rebuild the exact commit.
5. Run the final checks, hash the artifact, and launch it with a labeled title.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Current GitHub is integrated | Branch ancestry and ahead/behind count | Fetch and Git ancestry | Pass: `0` behind, `0` ahead before contender commit |
| Pinned dependencies are clean | Nested repository status | Root and nested `git status` | Pass |
| Build-time overrides reproduce the source | Clean dependency Release build | CMake Release build | Pass before contender commit |
| Reference contracts remain stable | All registered tests | CTest Release | Pass before contender commit: 12/12 |
| Documentation conforms | Validator self-test and full scan | Repository checker | Pass: 28-case self-test and 337-heading scan |
| Exact contender is reproducible | Clean exact-commit rebuild and hash | CMake build and SHA-256 | Pass: `df097d7`; `66E274925485A5C320664FB6CD7533365F7B35996E254DFF693222AFC4E27890` |
| Runtime starts normally | Responsive labeled launch | Required experiment launcher | Pass: responsive PID 44736, `UVSR Renderer D3D12 (canoncontender-df097d7-0618)` |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-18 | Start the contender directly from live `origin/main`. | The experiment was already based on the current canonical commit, so no transplant or merge was required. | Whole contender |
| 2026-07-18 | Convert dependency edits into build-time patch overrides. | Committing dirty or advanced submodules would violate UVSR's pinned-dependency contract and make the contender hard to reproduce. | Build, loading telemetry, and UI |
| 2026-07-18 | Keep the 50% white active-toggle track and use a compensated solid knob color. | The accepted screenshot showed that nominal linear header RGB rendered too brightly over the new track; the compensated source matches the rendered header appearance. | Toggle rendering |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-18 | `integrate` `/root` | Complete | Renderer `df097d7`; executable SHA-256 `66E274925485A5C320664FB6CD7533365F7B35996E254DFF693222AFC4E27890` | Exact Release rebuild; 12/12 CTest; document self-test and 337-heading scan; responsive labeled launch | User confirmation of the exact contender before any Canonical promotion |

## Risks and Escalation Triggers

- The predecessor dirty artifact and the clean contender use byte-equivalent
  patched source content. The user verified the exact clean contender after it
  received its final renderer identity.
- Fixed-size text tooltips intentionally trade unused space on short help text
  for consistent dimensions at every nesting depth.

Stop and ask the user if:

- publication beyond the local contender is requested without a clear target;
- clean dependency staging changes the accepted UI or loading behavior.

## Completion

- Final integrated renderer commit:
  `df097d74a609b34979f5d5e462525e3e7f9ae7f9`
- Verification summary: exact Release rebuild passed; 12/12 registered tests
  passed; title-case checker passed its 28-case self-test and scanned 337
  headings with zero violations; `git diff --check` passed; the exact
  executable was hashed and launched responsively with the required contender
  identity.
- Independent review: no separate reviewer was assigned; the coordinator
  completed source, staged-diff, clean-dependency, exact-build, and runtime
  audits for this local contender.
- Coming Soon/documentation update: README updated with the accepted UI and
  loading behavior
- Pushed/PR/merged, or intentionally local: pushed to
  `origin/codex/imgui-reference-canon-contender` after this acceptance record;
  no pull request or merge
- Remaining experiments or follow-ups: Canonical promotion only if separately
  requested
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/imgui-reference-canon-contender.md`
