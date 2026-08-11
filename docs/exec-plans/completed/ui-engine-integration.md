# UI Engine Integration Candidate

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-engine-integration` in
  `work/ui-engine-integration`
- Base commit: live `origin/main`
  `43b8b5253eafd7818967b76e3319cc6f530ff9ef`
- Feature input: dirty source candidate in `work/ui-material-hardware-skin`, based
  on `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`, whose verified executable is
  SHA-256 `A98E94D6E3470EBF6F392E48ECC1B086408ABFE64D17D25EC8260D0D38F4DF18`
- Current policy version: `AGENTS.md` `2026-08-11.1`
- Current canonical UI reference version: `2026-08-11.4`
- Started: 2026-08-11
- Last updated: 2026-08-11
- Planned archive: `docs/exec-plans/completed/ui-engine-integration.md`

## Goal and Done Condition

Goal: create a local integration candidate that preserves every retained UI
feature and interaction proven by the `A98E94...` build while adopting live
main's Engine naming, legal/governance rules, formatting, build contracts, and
README structure.

Done when:

- [x] The integrated source is based exactly on live `43b8b52` and remains on a
      separate local integration branch/worktree.
- [x] Every current-main Engine-era naming and legal/governance contract wins
      over stale Renderer-era wording or structure.
- [x] All retained UI behavior from the `A98E94...` candidate is present,
      including detached Performance, Settings layout and motion, Material and
      Interface drawers, authored controls, picker, CLI, popup lifecycle,
      scrolling, reset, and palette behavior.
- [x] Current-main rendering, licensing, legal inventory, source attribution,
      formatting, and build changes remain intact.
- [x] `README.md` retains current-main's banner, badges, compact organization,
      licensing language, and Engine identity, with only the smallest accurate
      UI additions.
- [x] Current-main tests and the UI candidate's focused contracts are reconciled
      rather than weakening either accepted behavior.
- [x] Release build, shader bundle, focused tests, complete CTest, legal
      inventory, README counts, Title Case, and diff hygiene pass.
- [x] The exact integration executable launches responsively at High priority
      and is handed off for fresh visual acceptance.

## Scope

In scope:

- Semantic source integration from the verified UI candidate onto `43b8b52`.
- Engine-first naming reconciliation in source, tests, UI copy, build metadata,
  documentation, and the runtime title.
- Main-first legal, governance, attribution, formatting, README, and test rules.
- Focused repairs required by integration and complete local verification.

Non-goals:

- Publishing, pushing, opening a pull request, merging into remote `main`, or
  releasing an artifact.
- Adding behavior not present in either input lineage.
- Reverting or rewriting either source worktree.
- Benchmarking or making performance claims.

Affected subsystems and paths:

- `src/uvsr.cpp`, UI helpers, authored ImGui/Donut overrides, build rules, tests,
  README, advanced/UI reference documentation, and legal inventory inputs.

Shared hotspots reserved for the coordinator:

- `README.md`, `AGENTS.md`, `CMakeLists.txt`, all override patches, execution
  plan, UI reference, legal files, and final build/test/runtime resources.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`,
  live `refs/heads/main` = `43b8b5253eafd7818967b76e3319cc6f530ff9ef`.
- Local versus remote state: the new integration worktree is clean and equal to
  `origin/main`; the pre-existing root `main` is divergent and left untouched.
- Verified source/build: UI input executable `A98E94...` passed its Release
  build, 306 shader tasks, 40/40 CTest, documentation checks, and responsive
  High-priority runtime smoke.
- Known pre-existing failures: none recorded for either declared input; the
  integration candidate requires fresh verification.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Live main | `43b8b525...`; Engine naming and current rules win | Frozen | Integration |
| UI input | Dirty tree associated with executable `A98E94...` | Frozen | Integration |
| Naming contract | User-visible program identity uses Engine, not Renderer | Decided | Source/docs/tests |
| README contract | Keep current-main structure and wording wherever accurate | Decided | README integration |
| UI contract | Preserve accepted candidate behavior; reconcile against current canonical reference | Reconciled | UI integration |

Public contracts:

- Executable and engineering slug remain `uvsr`; displayed identity follows
  current-main Engine wording.
- No new serialized compatibility layer is introduced solely for integration.
- Main's legal attribution and source-available governance remain authoritative.
- UI resource changes continue through narrow dependency overrides; `donut/`
  remains unmodified.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| MAIN-RULES | `/root/main_rules_readme_audit` | Shared read-only | Both frozen inputs | None | None | Complete; released |
| SOURCE-OVERLAP | `/root/source_overlap_audit` | Shared read-only | Both frozen inputs | None | None | Complete; released |
| INTEGRATION-TESTS | `/root/integration_test_audit` | Shared read-only | Both frozen inputs | None | None | Complete; released |
| FINAL-REVIEW | `/root/final_integration_review` | Shared read-only | Integrated candidate | None | Integration | Complete; released |
| INTEGRATE | `/root` | `work/ui-engine-integration` | `43b8b52` | All integration paths | Audit handoffs | Complete |

## Integration Order

1. Freeze and compare both source inputs, current policies, UI reference,
   Engine naming, README, build, legal inventory, and tests.
2. Port low-overlap UI helper/override/test files while preserving current-main
   legal and formatting conventions.
3. Semantically merge shared hotspots, especially `src/uvsr.cpp`, CMake,
   renderer/engine naming, README, and canonical UI documentation.
4. Run focused source/build tests, repair only integration-caused failures, and
   independently review the composed rendering/UI change.
5. Run the full verification matrix, launch the exact integration candidate,
   and request fresh visual acceptance.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Exact base | Live remote and worktree identity | `git ls-remote`, `git rev-parse` | `43b8b52` confirmed |
| Engine naming | Source/test/doc stale-term scan and runtime title | `rg`, focused tests, smoke | `UVSR Engine D3D12 (43b8b52)` |
| UI preservation | Focused UI/animation/popup/source tests | CMake/CTest targets | 12/12 focused contracts passed |
| Main preservation | Legal inventory, renderer/engine, asset, shader contracts | CTest/tools | Legal inventory and all main contracts passed |
| Build health | Release application and full shader bundle | CMake build | Release build and 306 shader tasks passed |
| Repository health | README counts, Title Case, links/legal, diff check | Repository tools | Counts current; 1,750 headings clean; diff check clean |
| Runtime | Exact path/hash, responsive High-priority process | Launcher/process inspection | `A50397A6...`; PID 35880 responsive at High priority |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-11 | Base the candidate on live `43b8b52`, not the divergent local root `main`. | The user named current GitHub main; live remote confirms the SHA, while the root worktree contains unrelated local state. | All |
| 2026-08-11 | Give Engine naming and current-main governance/README structure priority. | This is explicit user direction; wholesale feature-side copies would regress newer main. | Integration/docs |
| 2026-08-11 | Integrate source semantics, not the old binary or a wholesale tree replacement. | The verified hash establishes feature provenance, but only a source merge can preserve newer main changes. | Integration |
| 2026-08-11 | Keep current-main's canonical dropdown target and lifecycle test names while replacing their implementation with the complete accepted UI contract. | This preserves current rules and the 40-test surface without introducing a redundant alias or weakening popup-roll coverage. | Build/tests/docs |
| 2026-08-11 | Remove the unfinished Hardware view and GPU-monitor backend end to end. | The accepted UI candidate intentionally removed the incorrectly configured view; retaining orphaned runtime code would violate near-YAGNI and reset/UI contracts. | Source/build/tests/docs |
| 2026-08-11 | Introduce no new UI element during integration. | Every control, default, reset consumer, and animation path comes from the verified `A98E94...` input; integration only reconciles current-main naming, rules, legal evidence, and test identity. | Mandatory new-element intake |

## Progress and Handoffs

| Date | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-11 | `/root` | Complete | Branch/worktree based at `43b8b52`; executable `A50397A6...` | Release, 306 shaders, 40/40 CTest, legal, README, Title Case, High-priority smoke | Hand off local candidate for visual acceptance |
| 2026-08-11 | Read-only audit team | Complete | Main-rules, overlap, tests, and final integrated diff | No P0/P1 findings; two stale legal-evidence references corrected | Ownership released |

## Risks and Escalation Triggers

- `src/uvsr.cpp`, CMake, override patches, renderer source contracts, and docs
  changed in both lineages and require semantic reconciliation.
- The UI input is intentionally uncommitted; its exact tree must remain frozen
  and associated with the `A98E94...` evidence while being transferred.
- README and legal/governance files must not be copied from the older feature
  lineage wholesale.
- A passing textual patch is not proof that Engine naming, deferred mutation,
  picker layering, or reset behavior survived.

Stop and ask the user if:

- preserving a UI feature would require undoing a material current-main product
  or licensing decision; or
- the integration reveals two incompatible visible behaviors not prioritized by
  the user.

## Completion

- Final integrated commit: none; commit was not authorized.
- Verification summary: Release and 306 shader tasks passed; 40/40 CTest,
  legal inventory, README count self-test/check, Title Case self-test/full scan,
  stale-term scans, and `git diff --check` passed. The exact executable is
  SHA-256 `A50397A680C7330CD5C84C6691D532861BF6DADE0FDA40ADEBD536EF121E01B3`.
- Independent review: complete with no P0/P1 findings; its two P2 stale legal
  evidence references were corrected and revalidated.
- Coming Soon/documentation update: current-main's concise Coming Soon section
  remains unchanged because this is an unpublished local integration candidate;
  durable UI behavior is documented in the advanced settings and UI reference.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: fresh visual acceptance of the launched
  integration executable.
- Active ownership released: yes.
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ui-engine-integration.md`.
