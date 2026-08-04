# TAA Jitter Sequences

## Status

- State: completed
- Coordinator: `/root`
- Project branch and worktree: `codex/fast-approximate-aa` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\fast-approximate-aa`
- Base commit: `ff5adbd1f4cd9592cfac0fd1ab0e91720027772e` plus the complete
  uncommitted Fast Approximate AA candidate recorded in
  `docs/exec-plans/completed/fast-approximate-aa.md`
- Started: 2026-08-03
- Last updated: 2026-08-03
- Planned archive: `docs/exec-plans/completed/taa-jitter-sequences.md`

## Goal and Done Condition

Goal: let users select the Temporal Reconstructive camera-jitter sequence,
including every current Google Filament TAA jitter pattern and at most one
additional sequence whose advantage is supported by primary evidence and fits
UVSR's temporal reconstruction contract.

Done when:

- [x] Every current Filament jitter pattern is represented exactly, named
      clearly, and selectable while Temporal Reconstructive is enabled.
- [x] The selector lives in Advanced's Algorithm section, has a deterministic
      default and reset, and has matching Settings command behavior.
- [x] Changing the sequence resets temporal history, the previous-view jitter
      basis, and phase so history never mixes two sample patterns.
- [x] Any extra sequence has a documented narrow advantage, deterministic
      reference coverage, bounded coordinates, and no unsupported universal
      quality claim.
- [x] Maintained documentation, command counts, source contracts, complete
      Release build/tests, independent review, and an exact rebuilt UI smoke
      pass for the final artifact.

## Scope

In scope:

- Filament's current RGSS, Uniform Helix, and Halton 2,3 jitter choices.
- One optional progressive sequence when primary evidence establishes a useful
  advantage over Filament's fixed patterns for UVSR.
- TAA settings and resolved contracts, reference generation, temporal reset and
  phase behavior, Aliasing UI, Settings commands, tests, and maintained docs.
- A single dropdown presentation throughout the UVSR UI, matching the native
  integrated-arrow presentation used by the existing Stationary Bypass row.
- Preservation of the existing Fast Approximate AA candidate and all previously
  verified AA behavior.

Non-goals:

- Replacing UVSR's TAA reconstruction algorithm, changing Quality or Cost
  recipes, adding per-pixel stochastic sampling, or claiming performance.
- Editing Donut, reviving retired visibility sample rotation, publishing Git
  changes, or changing the default sequence without evidence and compatibility
  review.

Affected subsystems and paths:

- `src/temporal_aa_options.h`, `src/temporal_aa_reference.h`, `src/uvsr.cpp`
- `src/ui_settings_command_catalog.h`
- focused tests under `tests/`
- `README.md`, `docs/temporal-aa-options.md`,
  `docs/advanced-settings.md`, and the UI integration reference

Shared hotspots reserved for the coordinator:

- all affected source, command, test, README, documentation, Git, build, and
  renderer-runtime paths; research agents are read-only.

## Baseline

- Canonical commit lineage: `ff5adbd1f4cd9592cfac0fd1ab0e91720027772e`.
- Active candidate: complete dirty `codex/fast-approximate-aa` worktree; it must
  remain intact and uncommitted unless the user separately asks to save it.
- Last verified artifact: `build-fast-approximate-aa/bin/uvsr.exe`, SHA-256
  `77E922DE7968E9BFF6212D36323432D3915C2B1E2E83F1E235DF0D3009966938`.
- Existing jitter: an eight-phase centered Halton 2,3 table selected implicitly
  by `GetTemporalAaJitter(frameIndex)`.
- Existing reset: temporal image-key changes reset history, previous view, and
  `m_AntiAliasingPhase`; the sequence is not yet part of that key.
- Repository overlap: open PRs #10 and #11 affect Visibility tests/helpers, not
  the TAA jitter path. Other local AA experiments remain isolated and untouched.
- Known pre-existing failures: none in the candidate's 30-of-30 Release suite.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Current Filament source | Revision `47c86eec`; RGSS x4, Uniform Helix x4, and Halton 2,3 x8/x16/x32 | Confirmed | Settings and reference generator |
| Comparative sequence research | Fixed seed-43 optimized stochastic Sobol (0,2) x32 | Confirmed | Product option set |
| UVSR architecture audit | Sequence belongs in resolved settings and the temporal image key | Confirmed | Coordinator implementation |
| Fast Approximate AA candidate | Preserve all current dirty changes and verification contracts | Confirmed | Combined candidate |

Public interface and state contracts:

- `TemporalAaSettings` gains one concrete jitter-sequence enum with a sanitized
  resolved value.
- `GetTemporalAaJitter` consumes both the selected sequence and absolute TAA
  phase and always returns a finite offset inside the half-pixel footprint.
- The temporal image key includes the sequence so a change cannot reuse history
  or a previous view generated under another pattern.
- One Settings command path owns the selector; `set`, `get`, and `reset` follow
  normal enum mutation semantics because the selector has no hidden override.

## Assignment Summary

| Task ID | Owner | Branch Or Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `JITTER-FILAMENT` | `/root/filament_jitter_audit` | Read-only external and task tree | Current upstream plus dirty candidate | None | None | Complete |
| `JITTER-QUALITY` | `/root/jitter_quality_research` | Read-only primary sources | Current dirty candidate | None | None | Complete |
| `JITTER-ARCH` | `/root/uvsr_jitter_architecture` | Read-only task tree | Current dirty candidate | None | None | Complete |
| `JITTER-IMPLEMENT` | `/root` | `codex/fast-approximate-aa` | Dirty combined candidate | All scoped files | Research handoffs | Complete |
| `JITTER-FOLLOWUP-AUDIT` | `/root/jitter_ui_followup_audit` | Read-only task tree | Current dirty candidate | None | User follow-up | Complete |
| `DROPDOWN-UNIFICATION-AUDIT` | `/root/dropdown_unification_audit` | Read-only task tree | Current dirty candidate | None | User follow-up | Complete |
| `DROPDOWN-CONTRACT-REVIEW` | `/root/dropdown_contract_review` | Read-only task tree | Current dirty candidate | None | User follow-up | Complete |
| `JITTER-REVIEW` | Independent read-only reviewers | Final dirty snapshot | Final candidate | None | Implementation freeze | Complete |

## Assignment Contracts

### Jitter Filament: Audit Upstream Patterns

- Owner/thread: `/root/filament_jitter_audit`
- Base commit/state: current official Filament source and current UVSR candidate
- Read scope: Filament TAA public options and runtime jitter generation
- Write scope: none
- No-touch scope: repository files, build tree, renderer, and Git
- Deliverable: exact options, values/math, default, revision, URLs, and license
- Done when: implementation requires no guessed Filament behavior
- Allowed actions: read-only primary-source research
- Stop and report if: upstream identity or provenance is ambiguous

### Jitter Quality: Evaluate One Additional Sequence

- Owner/thread: `/root/jitter_quality_research`
- Base commit/state: current primary literature/engine sources and UVSR contract
- Read scope: progressive low-discrepancy and blue-noise jitter evidence
- Write scope: none
- No-touch scope: repository files, build tree, renderer, and Git
- Deliverable: at most one recommendation with exact deterministic construction,
  narrow advantage, tradeoffs, and source links, or an explicit no-add decision
- Done when: the coordinator can include or reject an extra without marketing
  overclaim
- Allowed actions: read-only primary-source research
- Stop and report if: no source supports comparative superiority

### Jitter Architecture: Map UVSR Integration

- Owner/thread: `/root/uvsr_jitter_architecture`
- Base commit/state: dirty task candidate over `ff5adbd`
- Read scope: TAA reference/settings, renderer phase/reset, UI, commands, tests,
  package-independent docs, and current diff
- Write scope: none
- No-touch scope: files, build tree, renderer, processes, and Git
- Deliverable: implementation map, counts, hazards, and verification matrix
- Done when: the coordinator has exact insertion and reset contracts
- Allowed actions: repository read-only
- Stop and report if: another active task owns an overlapping write path

## Integration Order

1. Freeze the exact Filament option set and coordinate convention.
2. Accept or reject one extra sequence from primary evidence.
3. Add settings, deterministic jitter generation, and reset-key behavior.
4. Add the selector under Advanced Algorithm, command path, and complete
   focused contracts; unify every UI dropdown on the same integrated-arrow
   presentation.
5. Update maintained docs and counts, build the combined candidate, run all
   tests, complete independent review, and smoke the exact rebuilt UI.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Exact Filament parity | Sample-by-sample or generator reference for all upstream options | Temporal AA reference tests | Passed for all five pinned Filament choices |
| Extra-sequence validity | Bounds, determinism, repeat/prefix behavior, and source rationale | Focused reference tests and review | Exact 32-point seed-43 Sobol table and provenance passed |
| Reset safety | Sequence participates in the temporal image key and restarts phase | Settings/reset tests and source contract | Passed |
| UI and commands | Algorithm placement, concise labels, unified dropdown presentation, reset and dispatch | UI/catalog tests and exact UI smoke | Passed; final Amp smoke confirmed placement, all six labels, and integrated arrows |
| Combined integration | Release all-target build and complete registered CTest suite | Existing isolated build tree | 258 core shader tasks, 46 directional-shadow tasks, and 30 of 30 CTest tests passed |
| Documentation | Counts, Title Case scan, README updater, patch checks | Repository tools | 1,180 headings and lead-ins passed with zero violations; patch check passed |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-03 | Continue the existing Fast Approximate AA lineage | The request extends the just-verified Aliasing UI and should not fork or duplicate its uncommitted settings work | All |
| 2026-08-03 | Move Jitter Sequence into Advanced's Algorithm section | The user explicitly grouped jitter pattern selection with the reconstruction algorithm; this supersedes the earlier same-day visible-before-Advanced decision. It remains independent from the Quality recipe and keeps its dedicated reset | Settings, UI, commands |
| 2026-08-03 | Use Filament Halton (2,3) x16 as the factory default | It is Filament's exact default, removes UVSR's corner sample, and avoids a seventh legacy compatibility option with no continuing product value | Settings and runtime |
| 2026-08-03 | Include one fixed stochastic Sobol (0,2) x32 sequence | Its seed creates the first point directly, then each later point selects the best-spaced candidate from 100 points in the required Sobol stratum. Its power-of-two prefixes preserve base-2 net stratification, and its measured toroidal minimum pair spacing exceeds the matching pinned Filament Halton prefixes; it is not claimed to win every scene | Reference generator, UI, and docs |
| 2026-08-03 | Use concise visible labels Halton 8/16/32, Sobol 32, and Depth Validation | The shorter names fit the drawer; tooltips and maintained docs retain the exact construction and provenance | UI and docs |
| 2026-08-03 | Use one native integrated-arrow dropdown presentation everywhere | The shared custom arrow layer double-composited its translucent background and created the unwanted dark button; the existing native combo presentation does not | Shared UI helper and contracts |

## Progress and Handoffs

| Date | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-03 | `JITTER-IMPLEMENT` setup | Active | Dirty `codex/fast-approximate-aa` candidate over `ff5adbd` | Status, worktrees, active plans, open PRs, Coming Soon, current jitter path | Await read-only handoffs before editing |
| 2026-08-03 | `JITTER-FILAMENT` and `JITTER-ARCH` | Complete | Filament `47c86eec`; dirty candidate unchanged | Exact upstream source and local reset-path audit | Implement five exact upstream choices; make X16 default |
| 2026-08-03 | `JITTER-QUALITY` | Complete | Stochastic-generation `f90b1158`; dirty candidate unchanged | Fixed seed-43 table, dyadic-prefix and toroidal-spacing audit | Include experimental Sobol 32 |
| 2026-08-03 | `JITTER-IMPLEMENT` source and docs | Active | Dirty combined candidate | Four focused Release targets/tests passed; 1,179-heading scan and patch check passed | Run complete build/tests, independent review, and exact UI smoke |
| 2026-08-03 | User follow-up | Active | Dirty combined candidate | Screenshot identifies two dropdown presentations | Move Jitter into Algorithm, shorten labels, unify dropdowns, close provenance review gaps, then rebuild and reverify |
| 2026-08-03 | `JITTER-IMPLEMENT` follow-up | Complete | Dirty combined candidate | Jitter moved into Algorithm; concise labels and six exact command mappings locked; all combo triggers use one native integrated-arrow path | Freeze implementation for final review |
| 2026-08-03 | Independent final reviews | Complete | Final dirty source snapshot | Dropdown, jitter, Sobol provenance, command-domain, docs, and lifetime reviews found no remaining P0-P2 issues | Rebuild and smoke exact artifact |
| 2026-08-03 | Final Release verification | Complete | `build-fast-approximate-aa/bin/uvsr.exe`, SHA-256 `B0C15AE5858BE7927A02DA18E44CF97DF9D693FBCB3986E17595D3BD8D63F90F` | 258 core shaders, 46 directional-shadow shaders, 30 of 30 CTest tests, Title Case validator, and patch check passed | Exact UI smoke |
| 2026-08-03 | Final UI smoke | Complete | Exact final executable, task-owned PID 29588 left open | Amp drawer confirmed Jitter Sequence first in Algorithm, Depth Validation, all six concise menu names, and backgroundless integrated arrows; automated contracts cover the OG skin | Await optional user visual acceptance |

## Risks and Escalation Triggers

- A different sequence changes every projection sample and must invalidate both
  history and previous-view jitter provenance.
- Filament's fixed patterns may start at a different Halton index or use a
  different centering/sign convention than UVSR's current table.
- A sequence can have better discrepancy or arbitrary-prefix behavior without
  being universally better for every scene; documentation must state the narrow
  supported advantage.
- Longer or non-repeating sequences can reduce periodicity while increasing
  low-frequency wander or slowing short-history convergence.

Stop and ask the user if:

- primary evidence leaves two materially different extra sequences with no
  defensible product choice;
- exact Filament parity would require changing the default image behavior rather
  than adding selectable alternatives; or
- verification requires disturbing a user-owned renderer or unrelated task.

## Completion

- Final integrated commit: intentionally uncommitted on
  `codex/fast-approximate-aa`; the user did not request a save or publication
- Verification summary: Release build completed 258 core shader tasks and 46
  directional-shadow shader tasks; all 30 registered CTest tests passed; the
  final exact artifact passed the Amp UI smoke; documentation and patch checks
  passed
- Independent review: complete with no remaining P0-P2 findings
- Coming Soon and documentation update: complete
- Pushed, pull request, merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: optional user visual acceptance only;
  no implementation work remains
- Active ownership released: yes
- Archived path: `docs/exec-plans/completed/taa-jitter-sequences.md`
