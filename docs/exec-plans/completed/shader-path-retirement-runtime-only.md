# Shader Path Retirement: Runtime-Only Visibility

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/prune-shader-paths` in
  `C:\Users\brock\OneDrive\Documents\uvsr`
- Base commit: `a4f9bf0541239026a424223536e63d166b376308`
- Started: 2026-07-29
- Last updated: 2026-07-29
- Planned archive:
  `docs/exec-plans/completed/shader-path-retirement-runtime-only.md`

## Goal and Done Condition

Goal:

Reduce production shader permutations and Settings complexity by retaining only
Runtime visibility sampling, removing both Offline noise delivery paths,
retiring low-value production TAA choices, and preserving the diagnostic
Closest Cross and 9x Bicubic TAA choices.

Done when:

- [x] Visibility has one Runtime sampling implementation with no sampling-mode
      selector, Fixed/Generic profiles, Offline noise resources, or stale
      packaged permutations.
- [x] Visibility presets use Toroidal Blue noise and the UI exposes only
      Independent Hash and Toroidal Blue.
- [x] Production TAA cannot select Stable Interior, Per-Pixel RGB, or
      Per-Pixel YCoCg; the measured shared-work-reuse axis remains packaged.
- [x] Closest Cross and 9x Bicubic remain selectable and covered.
- [x] A continuous shader-path retirement postmortem records removed paths,
      evidence, bloat mechanisms, restoration cautions, and future candidates.
- [x] Required Release builds, contracts, full tests, shader packaging, and
      labeled runtime checks pass with the final permutation count recorded.
- [x] An independent reviewer finds no unresolved high-risk shader, UI,
      resource-lifetime, packaging, or deletion defect.

## Scope

In scope:

- Runtime-only screen-space visibility sampling and planner simplification.
- Removal of Fixed and Generic sampling UI, profiles, PSOs, macros, and tests.
- Removal of unpacked and packed Filter Adapted Offline noise paths, resources,
  bindings, UI entries, assets if no remaining consumer exists, and tests.
- Production removal of Stable Interior, Per-Pixel RGB rectification, and
  Per-Pixel YCoCg rectification.
- Preservation of Closest Cross and 9x Bicubic diagnostic TAA choices.
- UI simplification, product documentation, contracts, line counts, and a
  continuous postmortem.

Non-goals:

- Removing Independent Hash, Toroidal Blue, any visibility estimator, AO, GI,
  packed-edge reconstruction, Closest Cross, or 9x Bicubic.
- Editing pinned Donut sources.
- Changing the display pipeline, lighting, shadows, IBL, AA method topology, or
  scene assets.
- Pushing, opening a pull request, merging, or releasing.
- Integrating PR #10 or PR #11.

Affected subsystems and paths:

- `src/screen_space_visibility*`
- `src/visibility_performance_plan.*`
- `src/visibility_sampling.*`
- `src/shaders.cfg`
- `src/shaders_production.cfg`
- `src/taa_miniengine*`
- `src/uvsr.cpp`
- `tests/*visibility*`
- `tests/*taa*`
- `tests/*ui*`
- `tests/production_shader_bundle_tests.cpp`
- `CMakeLists.txt`
- `README.md`
- `docs/screen-space-visibility.md`
- `docs/miniengine-taa-options.md`
- `docs/postmortem/shader-path-retirements.md`

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/uvsr.cpp`, shader configurations, CPU/HLSL
  contracts, tests, documentation, execution plan, build trees, and renderer
  runtime.

## Baseline

- Canonical repository/remote:
  `https://github.com/brockliddicoat/uvsr.git`, `origin/main`
- Local versus remote state: equal at
  `a4f9bf0541239026a424223536e63d166b376308` before branching.
- Verified source commit/build: canonical renderer commit `bcf4f7e`; canonical
  documentation record `a4f9bf0`; prior Release build and 28-test canonical
  verification recorded by the integration task.
- Baseline production shader count: 3,120.
- Baseline developer shader count: 13,107.
- GPU, scene, camera, resolution, and settings preset when relevant:
  local Windows DirectX 12 adapter; live smoke will use a packaged default scene
  and factory settings, with changed Visibility and Aliasing controls exercised.
- Known pre-existing failures: none recorded; two open visibility PRs own shared
  helper extraction and degenerate-path test coverage but are not integrated.

## Dependencies and Interfaces

| Dependency/Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| UI reference | `2026-07-29.4` | Read, updated, and reconciled | Settings changes |
| Agent policy | `2026-07-22.1` | Read and recorded | All work |
| PR #10 | Shared visibility helper extraction remains external | Serialized | Later integration |
| PR #11 | Degenerate-path tests remain external | Serialized | Later integration |
| User decision | Runtime only; remove both Offline paths | Accepted | Visibility |
| User decision | Keep Closest Cross and 9x Bicubic | Accepted | TAA |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Visibility settings no longer expose a sampling implementation choice.
- Sample count remains a 1-64 Runtime workload value shared by AO and GI.
- Factory Visibility presets retain their sample counts but use Runtime and
  Toroidal Blue.
- Removed Offline textures, upload state, bindings, and PSO macros must have no
  remaining CPU, HLSL, UI, benchmark, or packaging consumer.
- Production TAA retains Center, Closest Cross, and Center-First Edge Dilation;
  Direct and De-jittered current reconstruction; Bilinear, 1x, 5x, and 9x
  history filters; Pair Tristimulus and Variance YCoCg rectification; and both
  fused-output states.
- Shared-work reuse remains a production static axis because the controlled
  Intel result improved median TAA time from 5.46 ms to 4.88 ms and worst-case
  time from 6.38 ms to 5.91 ms. Turning it into a runtime branch would retain
  the larger LDS/register contract in both paths.
- Developer-only performance experiments may remain only where they have an
  active developer configuration consumer and do not inflate production.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| VIS-INV | Read-only explorer | Shared checkout | `a4f9bf0` | `tests/visibility_performance_plan_tests.cpp` after reassignment | Plan | Complete |
| TAA-INV | Read-only explorer | Shared checkout | `a4f9bf0` | Focused TAA source and test set after reassignment | Plan | Complete |
| IMPLEMENT | `/root` | `codex/prune-shader-paths` | `a4f9bf0` | All task paths | Inventories | Complete |
| REVIEW | Independent reviewer | Shared checkout | Final dirty snapshot | None | Implementation and tests | Complete |

## Assignment Contracts

### Vis-Inv: Map Visibility Retirement

- Owner/thread: read-only explorer
- Branch/worktree: shared checkout
- Base commit/state: `a4f9bf0` plus coordinator-owned plan only
- Read scope: Visibility source, shaders, configurations, tests, UI, and docs
- Write scope: none
- No-touch scope: all files, Git state, build trees, runtime, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: canonical main
- Interface/invariant contract: preserve Runtime counts 1-64, AO/GI consumers,
  all estimators, Independent Hash, Toroidal Blue, and packed-edge reconstruction
- Deliverable: exhaustive removal inventory, dependency risks, expected count,
  and targeted checks
- Done when: every Fixed/Generic/Offline reference is classified
- Required verification: read-only searches and arithmetic
- Allowed Git and external actions: read-only commands only
- Stop and report if: a removed path is required by a factory preset or an
  active external task
- Handoff revision/artifact: message
- Handoff acknowledged by/on: pending

### TAA-Inv: Map TAA Retirement

- Owner/thread: read-only explorer
- Branch/worktree: shared checkout
- Base commit/state: `a4f9bf0` plus coordinator-owned plan only
- Read scope: TAA settings, shader macros, PSO indexing, UI, tests, and docs
- Write scope: none
- No-touch scope: all files, Git state, build trees, runtime, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: canonical main
- Interface/invariant contract: retain Closest Cross, 9x Bicubic, factory
  presets, Pair Tristimulus, Variance YCoCg, and fused output
- Deliverable: exact production matrix, safe pruning map, indexing risks, and
  targeted checks
- Done when: every removed TAA value and consumer is classified
- Required verification: read-only searches and arithmetic
- Allowed Git and external actions: read-only commands only
- Stop and report if: a factory preset or runtime resource layout requires a
  removed value
- Handoff revision/artifact: message
- Handoff acknowledged by/on: pending

### Implement: Integrate the Retirement

- Owner/thread: `/root`
- Branch/worktree: `codex/prune-shader-paths`
- Base commit/state: `a4f9bf0`
- Read scope: complete repository as needed
- Write scope: task paths listed above
- No-touch scope: Donut, scene assets, shadow/IBL behavior, external PR branches,
  imported-scenes worktree, and unrelated user work
- Build directory and runtime/GPU/resource lease: coordinator-owned
  `build-shader-prune`; experiment label `shaderprune`
- Dependencies already integrated: inventory handoffs
- Interface/invariant contract: contracts above
- Deliverable: complete local candidate, documentation, evidence, and next
  candidate batch
- Done when: all acceptance criteria and verification gates pass
- Required verification: focused contracts, Release renderer/shader build, full
  CTest, source searches, line-count tools, documentation checker, runtime smoke
- Allowed Git and external actions: local edits, branch, configure/build/test,
  labeled launch; no commit/push/PR/merge
- Stop and report if: removal changes a retained estimator, AO/GI output,
  required scene asset, or active peer-owned path unexpectedly
- Handoff revision/artifact: final dirty branch
- Handoff acknowledged by/on: pending

### Review: Independent High-Risk Review

- Owner/thread: independent reviewer
- Branch/worktree: shared checkout, read-only
- Base commit/state: final dirty candidate
- Read scope: complete task diff and relevant untouched contracts
- Write scope: none
- No-touch scope: all files, Git state, builds, runtime, and external state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: implementation and automated verification
- Interface/invariant contract: all retained behavior and no stale removed paths
- Deliverable: severity-ranked findings with exact paths and evidence
- Done when: shader, UI, lifetime, packaging, tests, and docs are independently
  reviewed
- Required verification: diff inspection and read-only searches
- Allowed Git and external actions: read-only commands only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: message
- Handoff acknowledged by/on: pending

## UI Change Intake

- UI reference version: `2026-07-29.4`; the task reread and reconciled the
  updated normative reference before final verification.
- Agent policy version: `2026-07-22.1`.
- Owning drawer and sections: Visibility, Visibility Developer Options, and
  Aliasing Developer Options.
- Removed controls: Sample Count Mode and Stable Interior.
- Changed controls: Noise Pattern has Independent Hash and Toroidal Blue;
  Rectification has Pair Tristimulus and Variance YCoCg.
- Defaults and reset: all Visibility presets use Toroidal Blue; Noise Pattern
  resets to the selected preset; Rectification resets to its Aliasing preset.
- Control classification: retained Noise Pattern and Rectification are
  nonstructural deferred dropdowns using the existing shared queue. Removing
  build-time rows does not add runtime-dependent UI topology.
- Direct consumers: settings, preset reconciliation, PSO selection, shader
  macros, resource creation, binding sets, Statistics, benchmark metadata,
  reset state, tests, and product documentation.
- Animation and scroll owner: existing nested sections and `##SettingsBody`;
  no new animation owner.
- Renderer cost: Noise Pattern changes a runtime-uniform trace input without
  selecting another PSO; Rectification changes the TAA blend PSO and resets
  temporal history through the existing path.
- Tooltip contract: retained controls keep concise outcome-first tooltips.
- Presentation purity: existing deferred callbacks remain the only mutation
  point; no composition-time normalization is introduced.

## Integration Order

1. Inventory and freeze the retained public contract.
2. Simplify Visibility settings, planner, PSO selection, shaders, and resources.
3. Simplify Visibility UI and tests.
4. Simplify production TAA settings, PSO indexing, UI, and tests.
5. Update shader configurations and packaging contracts.
6. Update README, focused documentation, and continuous postmortem.
7. Configure, build, test, launch, review, repair, and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Removed paths absent | Whole-tree search | `rg` removal audit | Passed; no operational Fixed, Generic, Offline, Stable Interior, or retired rectification consumer remains |
| Production count reduced | Exact config expansion | permutation counter | Passed; 3,120 to 311 total, Visibility 2,297 to 64, TAA 771 to 195 |
| UI contracts hold | Focused Release tests | UI/animation/dropdown contracts | Passed |
| Renderer contracts hold | Focused Release tests | renderer and visibility contracts | Passed |
| TAA retained diagnostics work | Focused tests and compiled production matrix | TAA contract and `shaderprune` | Passed; Closest Cross and 9x Bicubic remain selectable and packaged |
| Complete build works | Release renderer and shader package | CMake build | Passed; 311 production and 2,809 developer permutations compiled |
| No broader regression | Full suite | CTest | Passed; 28 of 28 |
| Documentation valid | Title checker and links/diff | repository tools | Passed before final plan archive; rerun after archive |
| Independent review | Read-only reviewer | review handoff | Passed; no P0-P3 shader ABI, resource-lifetime, UI, packaging, or deletion finding remains |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-29 | Use Runtime as the only visibility sampling mode | It preserves arbitrary 1-64 counts without multiplying fixed profiles; retaining Fixed or Generic would keep redundant UI and shader families | Visibility |
| 2026-07-29 | Use Toroidal Blue for every factory preset | Both Offline paths are removed; Toroidal Blue is the established remaining first-party blue-noise option | Visibility |
| 2026-07-29 | Retain Closest Cross and 9x Bicubic | User relies on Closest Cross for lower motion blur and both serve diagnostics | TAA |
| 2026-07-29 | Retain static shared-work reuse | Controlled Intel evidence shows a 10.6% median and 7.4% worst-case TAA improvement; a runtime branch would not eliminate its resource pressure | TAA |
| 2026-07-29 | Use uniform branches for scheduler, radial exponent, and depth hierarchy | These frame/workload-coherent choices can share one compiled trace family; branchless predication would execute or retain both expensive paths without reducing static axes by itself | Visibility |
| 2026-07-29 | Use compact ShaderMake progress and preserve its default retry policy | The exact shader from one failed developer job compiled directly, and two complete 2,809-job reruns passed; concise output exposes the real diagnostic without weakening ShaderMake's existing ten-retry default | Build workflow |
| 2026-07-29 | Keep work local | Implementation authorization does not include commit or publication | All |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-29 | `/root` | Active | Branch and plan | Preflight complete | Run inventories |
| 2026-07-29 | Visibility explorer/writer | Complete | Runtime-only planner test rewrite | Source audit and `git diff --check` | Build after all writers release paths |
| 2026-07-29 | TAA explorer/writer | Complete | Eight focused TAA files | Source consistency and `git diff --check` | Integrated by coordinator |
| 2026-07-29 | `/root` | Complete | Runtime-only Visibility, focused TAA, UI, manifests, assets, contracts, and docs | 311 production shaders; 2,809 developer shaders; Release renderer and tests; 28 of 28 CTests; labeled `shaderprune` smoke | Archive plan and hand off |
| 2026-07-29 | Independent reviewer | Complete | Final dirty candidate | No P0-P3 shader ABI, resource-lifetime, UI, packaging, or deletion findings | UI sizing finding repaired and contract-tested |

## Risks and Escalation Triggers

- Fixed and Offline paths share shader functions with retained Runtime or
  packed-edge reconstruction.
- Resource removal can leave stale bindings or packaging expectations.
- TAA enum/index compaction can select the wrong PSO silently.
- PR #10 and PR #11 will require semantic integration after this local branch.
- UI row removal must preserve nested animation and scroll balance.

Stop and ask the user if:

- A required retained diagnostic depends on a path the user explicitly removed.
- Verification shows Toroidal Blue creates a material visual regression versus
  the removed Offline default that cannot be repaired without restoring it.

## Completion

- Final integrated commit: intentionally uncommitted unless separately
  authorized
- Verification summary: production and developer shader catalogs compile;
  Release renderer and test suite build; 28 of 28 CTests pass; runtime smoke
  rendered Sponza and opened Settings under
  `UVSR Renderer D3D12 (shaderprune-a4f9bf0-0051)`; document, line-count, and
  diff checks pass and will be rerun after plan archival
- Independent review: complete; no P0-P3 shader ABI, resource-lifetime, UI,
  packaging, or deletion findings remain
- Coming Soon/documentation update: README, focused design references, and
  continuous postmortem updated; no roadmap entry needed for this bounded
  cleanup
- Pushed/PR/merged, or intentionally local: local
- Remaining experiments or follow-ups: ranked second candidate batch is
  recorded in `docs/postmortem/shader-path-retirements.md`
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/shader-path-retirement-runtime-only.md`
