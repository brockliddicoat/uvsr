# Cosine Weighting Comparison

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/bent-normal-ao` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\bent-normal-ao`
- Base commit: `bed36f951407e23d9e6b4a1a9462a96abc059e8a` plus the existing
  uncommitted Bent-Normal AO candidate recorded by
  `docs/exec-plans/active/bent-normal-ao.md`
- Started: 2026-08-20
- Last updated: 2026-08-20
- Archive path: `docs/exec-plans/completed/cosine-weighting-comparison.md`

## Goal and Done Condition

Goal:

Build a local comparison candidate that preserves the current cosine-CDF
visibility bitmask as a reference and adds cheaper, bounded ways to apply
receiver cosine weighting to scalar AO and screen-space diffuse GI without a
second trace or new persistent resources.

Done when:

- [x] The current cosine-CDF bitmask remains selectable as a reference.
- [x] At least two cheaper experimental weighting modes reuse an existing mask
      construction and provide finite, bounded AO with a fully open result of
      exactly one.
- [x] Each new mode applies an explicit, documented receiver-cosine policy to
      GI and AO and resets incompatible temporal history when selected.
- [x] CPU reference fixtures cover open, closed, grazing, backfacing, thin,
      disjoint, and stochastic-sector cases.
- [x] Every required runtime shader permutation builds and the targeted and
      full deterministic suites pass.
- [x] Static shader evidence distinguishes plausibly cheaper math from measured
      GPU-performance claims. Runtime smoke was explicitly deferred because an
      unrelated UVSR executable was already running.
- [x] The exact local `uvsr.exe` comparison candidate is handed to the user
      with concise mode descriptions.

## Scope

In scope:

- Shared AO/GI estimator experiments using the existing one-slice,
  32-sector visibility trace.
- A repaired normal-domain policy for new experimental weighting modes.
- Estimator UI/command labels, shader permutations, history identity, tests,
  experiment documentation, static DXIL evidence, build, and runtime-smoke
  disposition.

Non-goals:

- Selecting a new production default or claiming a GPU speedup without a
  matched benchmark window.
- A second trace, a new AO/GI buffer, a new pass, a new denoiser, or changes to
  Donut.
- Commit, push, pull request, merge, release, or deployment.

Affected subsystems and paths:

- `src/visibility_estimator_shared.h`
- `src/screen_space_visibility.h`
- `src/screen_space_visibility.cpp`
- `src/screen_space_visibility_cs.hlsl`
- `src/uvsr.cpp`
- `src/ui_settings_command_catalog.h`
- `src/shaders.cfg`, `src/shaders_production.cfg`, and experiment defaults
- Visibility estimator, shader-bundle, runtime-source, and UI tests
- Relevant visibility documentation and this execution plan

Shared hotspots reserved for the coordinator:

- All implementation and documentation writes, CPU/HLSL contracts,
  `src/uvsr.cpp`, shader catalogs, build tree, renderer process, and final
  verification.

## Baseline

- Canonical repository/remote: not used as the implementation base; the user
  referred to the active Bent-Normal AO lineage.
- Local versus remote state: the task branch is 53 commits behind
  `origin/main`; its substantial existing dirty Bent-Normal AO diff is
  intentionally preserved. The root `main` checkout is unsafe for writes
  because Git sees repository-wide staged deletions and replacement untracked
  files.
- Verified source commit/build: parent plan records the exact repaired
  Bent-Normal AO candidate and prior executable; all evidence becomes stale
  after this extension.
- GPU, scene, camera, resolution, and settings preset when relevant: runtime
  smoke only unless the user provides a matched benchmark window; PBR Sponza
  Decorated and benchmark position 1 are the comparison scene/control.
- Known pre-existing failures: publication-grade Bent-Normal AO benchmark and
  user visual acceptance remain pending in the parent plan.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Existing Bent-Normal AO candidate | Preserve dirty implementation and resource contracts | available | all comparison modes |
| CW-MATH | Ranked formulas and source seams | completed | implementation |
| CW-INTEGRATION | Minimal UI/permutation/history design | completed | implementation |
| CW-QUALITY | Failure analysis and validation matrix | completed | verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- New modes extend the existing estimator enum and shader macro only; they add
  no shader bindings, constants, textures, passes, or packaged assets.
- The default and existing three estimator values retain their numeric values
  and behavior.
- Temporal history is invalidated on every estimator change.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| CW-MATH | `/root/cosine_math_audit` | shared read-only | `bed36f95` plus dirty candidate | none | current shader math | completed |
| CW-INTEGRATION | `/root/cosine_integration_audit` | shared read-only | `bed36f95` plus dirty candidate | none | current runtime/UI contracts | completed |
| CW-QUALITY | `/root/cosine_quality_review` | shared read-only | `bed36f95` plus dirty candidate | none | current tests/artifacts | completed |
| CW-IMPL | `/root` | task worktree | frozen post-audit dirty candidate | task files above | three audits | completed |

## Assignment Contracts

### Cw-Math: Weighting Formula Audit

- Owner/thread: `/root/cosine_math_audit`
- Branch/worktree: shared read-only task worktree
- Base commit/state: `bed36f95` plus the existing dirty candidate
- Read scope: estimator, trace, composite, Bent AO, tests, and visibility docs
- Write scope: none
- No-touch scope: repository files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: existing Bent-Normal AO candidate
- Interface/invariant contract: preserve mask/reference behavior; bounded and
  finite output; no second trace or extra pass/resource
- Deliverable: ranked formulas, exact seams, costs, risks, and test needs
- Done when: a minimal comparison set can be chosen without guessing math
- Required verification: cited source symbols and numerical bounds
- Allowed Git and external actions: read-only local inspection and essential
  primary-source research only
- Stop and report if: the source changes or a method needs an unscoped resource
- Handoff revision/artifact: read-only formula and source audit
- Handoff acknowledged by/on: `/root`, 2026-08-20

### Cw-Integration: Runtime and UI Audit

- Owner/thread: `/root/cosine_integration_audit`
- Branch/worktree: shared read-only task worktree
- Base commit/state: `bed36f95` plus the existing dirty candidate
- Read scope: settings, UI, cache/history, planner, shader catalogs, and tests
- Write scope: none
- No-touch scope: repository files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: existing Bent-Normal AO candidate
- Interface/invariant contract: extend existing enums/permutations without new
  bindings or changing default/reference behavior
- Deliverable: exact integration seams, commands, tests, and risks
- Done when: implementation and verification paths are fully enumerated
- Required verification: cited symbols and build/test commands
- Allowed Git and external actions: read-only local inspection only
- Stop and report if: the source changes or a public contract is undecided
- Handoff revision/artifact: read-only runtime and permutation audit
- Handoff acknowledged by/on: `/root`, 2026-08-20

### Cw-Quality: Independent Failure and Validation Review

- Owner/thread: `/root/cosine_quality_review`
- Branch/worktree: shared read-only task worktree
- Base commit/state: `bed36f95` plus the existing dirty candidate
- Read scope: estimator, reconstruction, tests, docs, and prior artifacts
- Write scope: none
- No-touch scope: repository files, Git state, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: existing Bent-Normal AO candidate
- Interface/invariant contract: separate AO visibility from GI transport and
  static cost evidence from matched GPU measurements
- Deliverable: ranked failure mechanisms, fixtures, smoke matrix, and risks
- Done when: black-output and regression signatures are testable
- Required verification: source/artifact citations
- Allowed Git and external actions: read-only local inspection and essential
  primary-source research only
- Stop and report if: the source changes or evidence cannot distinguish causes
- Handoff revision/artifact: independent post-implementation review with no
  remaining shader or math blocker
- Handoff acknowledged by/on: `/root`, 2026-08-20

### Cw-Impl: Comparison Candidate

- Owner/thread: `/root`
- Branch/worktree: task worktree
- Base commit/state: frozen post-audit dirty candidate
- Read scope: full task scope
- Write scope: task implementation, tests, documentation, and plan only
- No-touch scope: Donut, unrelated dirty work, Git history, and external state
- Build directory and runtime/GPU/resource lease:
  `build-cosine-weighting`; coordinator only
- Dependencies already integrated: three read-only audit decisions
- Interface/invariant contract: no new binding/resource/pass; default and
  existing estimator behavior preserved; exact enum-to-macro identity
- Deliverable: built and verified local comparison executable
- Done when: every done condition above has recorded evidence
- Required verification: reference/source tests, all shader permutations,
  deterministic suite, static DXIL comparison, and a recorded runtime-smoke
  disposition
- Allowed Git and external actions: scoped local edits/build/tests/runtime only
- Stop and report if: implementation needs a new resource/pass, touches Donut,
  or encounters unrelated source mutation
- Handoff revision/artifact:
  `build-cosine-weighting/bin/uvsr.exe`, SHA-256
  `D87A875D4EF8159D74C9571A95D4455EA9F49AED0149499ED5BE0268F2649642`
- Handoff acknowledged by/on: local user handoff, 2026-08-20

## Integration Order

1. Freeze and reconcile the three read-only audit recommendations.
2. Add shared CPU/HLSL math and reference fixtures.
3. Extend enum, UI, command, history, planner mapping, and shader catalogs.
4. Build targeted tests and every affected shader permutation.
5. Run the full deterministic suite and static DXIL comparison, then determine
   whether a runtime smoke can proceed without disturbing unrelated work.
6. Record evidence, independent review, and local artifact handoff.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Bounded open/closed and malformed-normal behavior | CPU fixtures | estimator reference tests | passed, including azimuth/tilt, grazing, overlap, and near-full/full claims |
| Exact UI/runtime/permutation mapping | Source-contract and bundle tests | targeted CTest set | passed |
| Shader validity | All new AO/GI/Bent/edge permutations | Release shader build | 702 production permutations compiled |
| No deterministic regressions | Full repository suite | `ctest --test-dir build-cosine-weighting -C Release --output-on-failure` | 33/33 passed in 3.71 seconds |
| Plausibly cheaper math | Comparable static DXIL metrics | `tools/measure_visibility_dxil.ps1` | report in `build-cosine-weighting/shader_evidence/cosine-comparison/visibility-dxil-report.md` |
| Launchable comparison candidate | Exact build and executable identity; runtime smoke when process lease is available | Release renderer build and process preflight | build passed; smoke deferred because unrelated PID 63700 was already running |

For performance work, record:

- source commit plus dirty-diff identity and executable SHA-256;
- exact variant, scene, camera, resolution, and settings;
- static DXIL separately from any GPU timing;
- total frame and visibility trace time only in a matched accepted window;
- open/closed/backface quality guardrails and captures when available.

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-20 | Continue in the Bent-Normal AO worktree | It is the exact user-referenced cosine-CDF lineage; root `main` is unsafe and unrelated | all |
| 2026-08-20 | Preserve the current CDF estimator only as a reference | It adds per-sample trigonometric CDF work and can force zero slice mass, matching the reported symptoms | all |
| 2026-08-20 | Require new modes to return one for a fully open slice | This removes the current deterministic black/open-slice failure class | math, quality, implementation |
| 2026-08-20 | Add Cosine Hit Weighting and Cosine Mask Resolve over Uniform Solid Angle sectors | Hit is the lighter Bent-off candidate; Mask is the order-independent scalar-AO quality candidate; neither needs another trace or resource | all |
| 2026-08-20 | Gate Hit Weighting's conditional result by uniform visibility | This preserves exact closed output and avoids the 31-to-32 sector black cliff identified by independent review | quality, implementation |
| 2026-08-20 | Keep GPU speed claims out of this handoff | The automated performance path still fixes Uniform Solid Angle; DXIL is static compiler evidence only | verification |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-20 | `/root` | active | active dirty worktree inspected | Git/worktree/plan/Coming Soon preflight | await audits and freeze formulas |
| 2026-08-20 | CW-MATH and CW-INTEGRATION | completed | read-only handoffs | formula bounds, GI normalization, enum/runtime/history/catalog mapping reviewed | recommendations integrated |
| 2026-08-20 | `/root` | completed | comparison implementation and test fixtures | Release build, 702 production permutations, 33/33 CTest | independent quality review |
| 2026-08-20 | CW-QUALITY | completed | final read-only review | no remaining shader or math blocker; scalar-AO/GI order-dependence disclosure corrected | local handoff |

## Risks and Escalation Triggers

- The branch contains a large user-owned dirty candidate; every patch must be
  narrow and preserve unrelated Bent-Normal AO work.
- Phase-shifted bit sectors can bias coarse post-resolve weighting; fixtures
  must cover sector phase and thin/disjoint intervals.
- Static instruction evidence can reject an obviously more expensive method
  but cannot prove a GPU speedup.

Stop and ask the user if:

- A credible method requires another trace, buffer, pass, or a visible
  quality-versus-performance choice outside the requested comparison build.
- Runtime verification requires interrupting unrelated user activity or a
  matched performance window the user has not made available.

## Completion

- Final integrated commit: intentionally uncommitted local candidate
- Verification summary: Release renderer build passed; 702 production shader
  permutations compiled; 33/33 deterministic tests passed in 3.71 seconds;
  static DXIL report generated for scalar AO, AO+GI, and Bent AO+GI. Runtime
  smoke was not launched because an unrelated UVSR executable was already
  running and this task did not own that process.
- Independent review: math, integration, and final quality reviews completed;
  no remaining shader or math blocker.
- Coming Soon/documentation update: no Coming Soon entry for a private
  comparison build; comparison documentation complete
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual comparison; matched GPU
  benchmarking if a clean measurement window is later made available.
- Active ownership released: yes
- Archived to completed/abandoned path: completed path
