# Visibility Optimization and AA/UI Integration

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/aa-ui-merged-experiment` at
  `C:\Users\brock\Documents\Codex\2026-07-19\aa-ui-merged-experiment`
- Base commit: `58813cb` (`checkpoint: preserve newest aa and ui candidate`)
- Started: 2026-07-20
- Last updated: 2026-07-22
- Planned archive:
  `docs/exec-plans/completed/visibility-aa-ui-integration.md`

## Goal and Done Condition

Goal: integrate the complete visibility and optimization implementation ending
at source checkpoint `16d8fc8` into the newest named AA/UI candidate without
replacing shared files wholesale or losing either feature set.

Done when:

- [x] The four visibility presets, buffer controls, benchmark/statistics
      workflow, production shader permutations, optimization ledger, and
      source behavior from `16d8fc8` remain present.
- [x] The destination's MiniEngine TAA, CMAA2, deferred MSAA, production shader
      bundle, and accepted ImGui UI behavior remain present. Diagnostic SMAA
      was subsequently retired by explicit user request.
- [x] Shared CPU, HLSL, build, shader-registry, schema, documentation, and
      `src/uvsr.cpp` conflicts are resolved semantically.
- [x] All developer and production shaders plus the Release executable rebuild.
- [x] Relevant CPU tests, production shader-bundle tests, document checks, and
      source audits pass, with any unavoidable incompatibility documented.
- [x] The result remained local through user evaluation and received separate
      canonical publication authorization on 2026-07-22.

## Scope

In scope:

- Import the source branch history from `3339505` through `16d8fc8`.
- Manually compose all overlapping visibility, AA, UI, build, schema, and
  documentation behavior.
- Repair integration failures, rebuild, test, audit, and document the candidate.

Non-goals:

- Pushing, opening a pull request, merging to `main`, publishing, or releasing.
- Redesigning accepted AA/UI behavior or reviving retired visibility experiments
  that the source checkpoint intentionally removed.
- Committing generated build products or raw local benchmark captures.

Affected subsystems and paths:

- `CMakeLists.txt`, `README.md`, `docs/`
- `src/uvsr.cpp`, visibility CPU/HLSL contracts, shader registries, indirect
  composite, AA/deferred-lighting integrations
- Visibility performance/statistics tests and production shader-bundle tests

Shared hotspots reserved for the coordinator:

- All files in this single-writer integration worktree
- The worktree-local `build` and `build-production` directories
- The renderer runtime and GPU benchmark lease

## Baseline

- Canonical repository/remote:
  `https://github.com/brockliddicoat/uvsr.git`, live `main` `a55e215`
- Local versus remote state: local AA/UI branch is one checkpoint commit ahead
  of live `main`
- Verified source commit/build: visibility source checkpoint `16d8fc8`;
  pre-integration AA/UI executable SHA-256
  `5F82CC5FBE4B2870ABF5BB958BB6FF72B2929AF2BC86DF4D6CF8DE3704F74D80`
- GPU, scene, camera, resolution, and settings preset when relevant: reuse the
  repository's Sponza benchmark camera and preset-defined settings
- Known pre-existing failures: the AA-options active plan had not yet exercised
  every AA method interactively; vendored third-party AA sources retain upstream
  trailing whitespace and are excluded from first-party diff-check assessment

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Canonical UI merge | `a55e215` | Integrated in `58813cb` | Whole candidate |
| Newest AA/UI candidate | `58813cb` | Checkpointed | Visibility integration |
| Visibility optimization source | `16d8fc8` and its four ancestors after `5f43205` | Ready | Visibility integration |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Preserve destination AA/UI layout, ordering, defaults, tooltips, fade timing,
  crosshair, and shader-bundle split.
- Preserve source visibility preset identity, configuration-to-permutation
  mapping, resource/barrier lifetime, buffers, benchmark execution, in-memory
  results, and statistics semantics. The export schema was removed later by
  explicit user request.
- Keep CPU/HLSL constant-buffer layouts synchronized after composition.
- Keep visibility output consumable by both normal deferred lighting and the
  destination's multisampled deferred-lighting path.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Integrate | `/root` | `codex/aa-ui-merged-experiment` | `58813cb` | Entire composed diff | Source `16d8fc8` | Completed |

## Assignment Contracts

### Integrate: Compose and Verify Both Feature Sets

- Owner/thread: `/root`
- Branch/worktree: `codex/aa-ui-merged-experiment`
- Base commit/state: clean `58813cb`
- Read scope: both named repositories and their relevant history
- Write scope: destination worktree only after the source checkpoint
- No-touch scope: source raw benchmark captures, generated build products in
  Git, canonical refs, remote branches, and unrelated open pull requests
- Build directory and runtime/GPU/resource lease: destination-local `build` and
  `build-production`; one serialized renderer process
- Dependencies already integrated: AA/UI checkpoint `58813cb`
- Interface/invariant contract: preserve both feature sets and resolve shared
  files semantically, never by wholesale `ours` or `theirs`
- Deliverable: local commit, rebuilt Release executable/shaders, checks, and
  documented conflict decisions
- Done when: all acceptance criteria map to evidence
- Required verification: developer and production Release builds, full CTest,
  production shader bundle, document checker self-test/full scan, diff check,
  feature-marker and shader-registry audit, labeled smoke launch when feasible
- Allowed Git and external actions: local fetch/import, merge, commits, builds,
  tests, and runtime only; no push, PR, canonical merge, or release
- Stop and report if: retaining the source visibility semantics necessarily
  changes an accepted AA/UI visible outcome or retaining AA/UI necessarily
  removes a required visibility behavior
- Handoff revision/artifact: merged candidate base `553e604` plus the accepted
  working-tree refinements recorded in this publication
- Handoff acknowledged by/on: user authorization on 2026-07-22

## Integration Order

1. Import the source branch through `16d8fc8` into the destination object store.
2. Merge it into `58813cb` and inventory every conflict.
3. Resolve shared files semantically, preserving destination AA/UI behavior and
   source visibility behavior.
4. Reconfigure and rebuild developer/production shaders and Release targets.
5. Run tests, documentation checks, source/feature audits, and runtime smoke.
6. Archive this plan and commit the verified local candidate.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command or Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Four visibility presets retained | CPU/UI/shader mapping and tests | Source audit plus visibility tests | Low, Medium, High, and Ultra mappings retained; visibility sampling/performance tests pass |
| Buffer controls retained | UI/state/resource path audit and build | Source audit plus Release build | Four buffer presets and per-surface controls compile in both Release builds |
| Benchmark/statistics retained | Targets, tests, and CLI workflow | CTest and source audit | Statistics and performance-plan tests pass; the runner, progress, cancel action, and in-memory latest summary remain after the later requested export removal |
| Shader permutations retained | Developer/production compile and registry audit | Build both shader bundles | Developer bundle passes; production compiles 2,214 tasks and its bundle contract passes |
| Optimization ledger retained | Title-case checker and content audit | Document checks and `rg` | Ledger imported; checker self-test and 487-heading full scan pass |
| AA/UI retained | AA tests, production bundle, UI source audit | CTest, build, labeled smoke | MiniEngine TAA and CMAA2 tests pass; deferred MSAA remains; retired SMAA names, source, and blobs are absent; the executable opens a responsive window |
| Composed candidate valid | Clean first-party diff, full tests, executable hash | Repository verification suite | The integration checkpoint passed developer 14/14 and production 15/15 tests; later combined verification is recorded under Canonical Publication |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-20 | Use `58813cb` as the destination checkpoint | The user explicitly named this candidate; reverting to canonical would discard newer AA work | Whole integration |
| 2026-07-20 | Import the complete source branch, not only `16d8fc8`'s last delta | The required benchmark/statistics infrastructure begins in four earlier source commits | Visibility integration |
| 2026-07-20 | Keep raw benchmark captures local | They are generated evidence rather than implementation and exceed focused-source scope | Source checkpoint |
| 2026-07-20 | Preserve the destination's fixed AgX path and omit the source fork's retired configurable AgX/Kodak LUT UI | The source branch forked before the destination intentionally retired those controls; restoring them would regress the newest accepted UI and is unrelated to visibility/optimization behavior | Tonemapping/UI conflict |
| 2026-07-20 | Extend the destination's dependency-tracked shader target instead of restoring the source's second `uvsr_shaders` target | One target must own the output; the composed target selects developer/production registries, invalidates on every HLSL/config change, and includes every source permutation | CMake and shader rebuilds |
| 2026-07-20 | Use the source's fixed emissive gain in the destination's deferred-MSAA visibility prepass | The source removed user-selectable emissive gain while the destination added MSAA; the fixed contract retains source visibility behavior in both single-sample and multisample paths | Deferred MSAA and visibility |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-20 | Source checkpoint `/root` | Completed | `16d8fc8` | Document self-test/full scan; staged diff check | Import full branch |
| 2026-07-20 | AA/UI checkpoint `/root` | Completed | `58813cb` | Document self-test/full scan; first-party staged diff check | Merge source |
| 2026-07-20 | Manual semantic merge `/root` | Completed | Local staged merge of `58813cb` and `16d8fc8` | No conflict markers; core visibility files match the source checkpoint while shared AA/UI paths compile | Final validation |
| 2026-07-20 | Validation `/root` | Completed | Developer hash `3A2BD22E303EDCC8C6E396137A95A696F7F0C1E84DAC0B7063B1EF1295E21DEC`; production hash `984DEB6AD57ABBCFB21C98006CF368E863C525F891E5A1799DF814F84E672FB4` | Both shader/Release builds, 14/14 developer tests, 15/15 production tests, document checks, diff check, and responsive-window smoke launches | Create local merge commit |

## Risks and Escalation Triggers

- `src/uvsr.cpp`, CMake, shader registries, visibility contracts, schemas, and
  documentation changed independently and require manual semantic composition.
- The destination's deferred MSAA path consumes visibility resources that the
  source changes substantially.
- The source intentionally removes experimental PS4/XeGTAO and diagnostic
  permutations while retaining four production presets; restoration of removed
  experiments would contradict the checkpoint's optimization cleanup.
- Independent subagent review is unavailable under the current execution
  constraints; the coordinator will perform a separate post-integration audit.

Stop and ask the user if:

- Both implementations cannot coexist without choosing a different visible
  default, UI behavior, AA result, visibility result, or performance/quality
  tradeoff.
- Completion would require push, canonical integration, publication, release,
  destructive history rewriting, or deletion outside the source checkpoint.

## Completion

- Final integrated commit at this integration checkpoint: the local merge
  commit containing this completed plan
- Verification summary: developer and production shader targets and Release
  executables pass; developer CTest is 14/14 and production CTest is 15/15;
  document self-test/full scan, first-party diff check, feature-marker audit,
  shader-registry parity audit, and responsive-window smoke launches pass
- Independent review: separate coordinator post-build source and feature audit
  completed; no independent subagent was used under the execution constraints
- Coming Soon/documentation update: the imported visibility design, optimization
  ledger, DXIL evidence, and completed source/integration plans are reconciled
- Pushed/PR/merged at this integration checkpoint: intentionally local
- Remaining experiments or follow-ups at this integration checkpoint: user
  visual/product acceptance and the source plan's disclosed target Intel
  timing/dynamic-motion evidence
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/visibility-aa-ui-integration.md`

## Canonical Publication

The local-only and earlier test-count statements above preserve the original
2026-07-20 checkpoint evidence. The user subsequently accepted the combined
visibility, AA, and UI behavior, including later animation, scrolling,
magnifier, dropdown, naming, and statistics refinements. Benchmark export and
SMAA were deliberately removed without removing the retained visibility
presets, buffer controls, statistics, shader permutations, or optimization
ledger. On 2026-07-22, the complete combined candidate was authorized for a
fast-forward publication to canonical `main`; the publication verification is
the developer Release build with 20 of 20 tests, a from-source production build
of 2,211 UVSR DXIL tasks with 21 of 21 tests, the packaged-shader contract, the
documentation checks, and the line-count checks run for the containing commit.
