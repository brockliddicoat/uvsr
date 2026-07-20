# Anti-Aliasing/UI Merged Experiment

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/aa-ui-merged-experiment` in
  `C:/Users/brock/Documents/Codex/2026-07-19/aa-ui-merged-experiment`
- Base commit: `a55e215e4bf0eddb20330283d9a4f8e853bda49f`
- Started: 2026-07-19
- Last updated: 2026-07-19
- Planned archive:
  `docs/exec-plans/completed/aa-ui-merged-experiment.md`

## Goal and Done Condition

Goal: combine the complete local anti-aliasing experiment based on `8970838`
with GitHub `main` at `a55e215`, preserving the newest Canonical verified
settings UI and producing a local experimental executable.

Done when:

- [x] The accepted ImGui/UI behavior remains present.
- [x] The complete current AA menu, algorithms, static shaders, MSAA visibility
      bridge, tests, and benchmark controls are integrated.
- [x] Release build, registered tests, documentation checks, and a labeled
      runtime smoke pass.
- [x] A launchable experimental executable is copied beside its runtime assets
      and linked to the user.

## Scope

In scope:

- Three-way integration of the current AA experiment onto live GitHub `main`.
- Semantic conflict resolution in renderer UI, build, shader, and documentation
  hotspots.
- Local build and verification.

Non-goals:

- Push, pull request, Canonical merge, release, or modification of pinned Donut
  and Dear ImGui histories.

Affected subsystems and paths:

- `CMakeLists.txt`, `README.md`, `src/uvsr.cpp`, `src/shaders*.cfg`
- AA, CMAA2, SMAA, temporal-core, deferred-MSAA, visibility, tests, and docs

Shared hotspots reserved for the coordinator:

- All integration files and the build/runtime lease

## Baseline

- Canonical repository/remote: `origin/main` at `a55e215`
- Local versus remote state: branch created equal to live `origin/main`
- Verified source commit/build: UI contender `df097d7`, merged on `a55e215`;
  AA input is the technically verified dirty candidate based on `8970838`
- GPU, scene, camera, resolution, and settings preset when relevant:
  target benchmark contract is Intel 185H Arc at PBR Sponza Benchmark Position
  1 and 1920x1080; the composed runtime smoke used the available NVIDIA GeForce
  RTX 4090 Laptop GPU
- Known pre-existing failures: none recorded

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Newest UI | GitHub `main` `a55e215` | Integrated as base | Whole experiment |
| AA candidate | Dirty worktree based on `8970838` | Preserved and read-only | Integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve the UI build-time override patch model and pinned dependencies.
- Preserve AA static PSO and temporal reset contracts.
- Keep visibility/MSAA guide ownership coherent and reverse-Z aware.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `integrate` | `/root` | `codex/aa-ui-merged-experiment` | `a55e215` | All scoped files | Both inputs | Completed |

## Assignment Contracts

### Integrate: Compose and Verify the Experimental Build

- Owner/thread: `/root`
- Branch/worktree: `codex/aa-ui-merged-experiment`
- Base commit/state: clean `a55e215`
- Read scope: both source worktrees and full repository
- Write scope: this isolated worktree
- No-touch scope: source AA worktree, canonical worktree, pinned dependencies,
  remote refs beyond read-only fetch
- Build directory and runtime/GPU/resource lease: worktree-local `build`
- Dependencies already integrated: newest UI base
- Interface/invariant contract: preserve both valid feature intents
- Deliverable: local executable and verification evidence
- Done when: every done-condition item has evidence
- Required verification: Release build, full CTest, document checker,
  `git diff --check`, labeled runtime smoke
- Allowed Git and external actions: local branch/worktree only; no publication
- Stop and report if: the two accepted product behaviors are materially
  incompatible rather than mechanically conflicting
- Handoff revision/artifact: `build/bin/usvr.exe`, SHA-256
  `E342CAAA0830074C1543BD4EE2D8C74EE14716878DDEAA3C173FFD1E17ADDF82`
- Handoff acknowledged by/on: coordinator verification on 2026-07-19;
  user evaluation pending

## Integration Order

1. Replay non-overlapping AA files.
2. Three-way merge UI/build/shader/documentation hotspots.
3. Build and repair only composed failures.
4. Verify, launch, and package the experimental executable.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Source composition | Conflict-free semantic diff | Git and source audit | All 63 unchanged AA payload files byte-identical; seven integration hotspots audited |
| Static correctness | Release build and full CTest | CMake/CTest | Developer 13/13; production 14/14; production bundle 99 shader tasks |
| UI/AA runtime | Labeled responsive launch | Required launcher | `aauimerge-a55e215-1221`, responsive after scene load |
| Documentation | Self-test, full scan, diff check | Repository tools | 29-case self-test and 394-heading scan passed; diff check clean |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-19 | Base on `a55e215` | It is live GitHub `main`, includes the merged verified UI contender, and descends from the AA candidate's base. | Whole experiment |
| 2026-07-19 | Use a semantic three-way merge | Copying either `uvsr.cpp` wholesale would discard accepted behavior from the other input. | Renderer integration |
| 2026-07-19 | Carry the UI backdrop through both shader configs | The production-bundle test exposed that the UI shader was staged by merged CMake but absent from the AA production config. Adding the source entry and expected bundle member made developer and production packaging agree. | Build and shader packaging |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-19 | `integrate` `/root` | Completed | `build/bin/usvr.exe`, SHA-256 `E342CAAA0830074C1543BD4EE2D8C74EE14716878DDEAA3C173FFD1E17ADDF82` | Developer and production builds, 13/13 and 14/14 tests, runtime, docs, source audit | User evaluation |

## Risks and Escalation Triggers

- `src/uvsr.cpp` contains substantial valid changes on both sides.
- The AA input is intentionally dirty and must remain untouched.
- UI override patches must remain reproducible without dirty submodules.

Stop and ask the user if:

- preserving both accepted behaviors requires choosing one visible behavior over
  the other;
- publication becomes necessary.

## Completion

- Final integrated commit: local dirty experiment based on `a55e215`; no commit
  or push requested
- Verification summary: developer Release build and 13/13 tests; production
  Release build, 99 shader tasks, and 14/14 tests; labeled responsive runtime;
  document self-test/full scan and clean diff check
- Independent review: source-level integration audit by coordinator
- Coming Soon/documentation update: local experiment entry updated
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual evaluation of AA methods and
  newest UI before any Canon contender decision
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/aa-ui-merged-experiment.md`
