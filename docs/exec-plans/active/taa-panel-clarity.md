# TAA Panel Clarity

## Status

- State: active
- Coordinator: `/root`
- Worktree:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\taa-shadow-cleanroom`
- Base: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` plus the local,
  unstaged cleanroom shadow and TAA candidate
- Started: 2026-07-30
- Planned archive:
  `docs/exec-plans/completed/taa-panel-clarity.md`

## Goal and Done Condition

Simplify Temporal AA Settings so the primary surface has one concise Temporal
Cost selector, Developer Options owns the history and actionable image controls,
its two toggle controls come first, the alternate depth-validation name is
concise, and cost-gated controls collapse instead of leaving inert status rows.

Done when:

- [x] Runtime-state, comparison guidance, and execution-topology presentation
  are absent from the panel.
- [x] Dejitter and the available Sharpness toggle precede dropdown tuning.
- [x] Sample Resurrection animates out completely outside Full Quality.
- [x] The alternate previous-depth policy has one concise, technically accurate
  name in UI, tests, and maintained documentation.
- [x] The primary selector is named Temporal Cost without a duplicate Requested
  qualifier.
- [x] History Frames and History Strength appear inside Developer Options after
  the toggle controls.
- [x] Production and factory Release builds and tests pass after the final
  layout revision.
- [ ] The exact production UI is visually checked after the user-owned renderer
  releases the GPU/UI lease.

## Scope and Ownership

The coordinator owns:

- `CMakeLists.txt`
- `README.md`
- `src/uvsr.cpp`
- `src/temporal_aa_options.h`
- `tests/ui_source_contract_tests.cpp`
- `tests/temporal_aa_tests.cpp`
- `docs/advanced-settings.md`
- `docs/temporal-aa-options.md`
- `docs/ui-integration-agent-procedure.md`
- this execution plan

Read-only agents may audit these paths but do not own writes. Backend topology,
shader permutations, runtime algorithms, presets, and user settings semantics
remain unchanged.

## Verification Plan

| Check | Evidence |
| --- | --- |
| UI source contract | Focused contract test and full CTest |
| Depth-policy label | Temporal AA reference test and maintained-tree scan |
| Production capability | Production Release build and 28-test suite |
| Factory stripping | Factory Release build and 28-test suite |
| Visible layout | Exact production renderer smoke after a clean lease |
| Documentation | Title Case validator and `git diff --check` |

## Coordination

PID 1352 is running the previous Antialiasing production artifact and PID 32672
is running the UI-surface-polish production artifact. Both were launched from
Explorer. Treat them as user-owned: do not close, restart, automate, or
overwrite either process. The revised Antialiasing artifact was built in
`build-taa-panel-production`; its exact visual smoke waits for a clean GPU/UI
lease.

## Completion

- Verification: isolated production Release build and factory Release rebuild
  pass; both full CTest suites pass 28/28; title validation checks 930 headings
  with zero violations; `git diff --check` passes; exact production visual
  smoke remains pending on the user-owned GPU/UI lease
- Exact production artifact:
  `build-taa-panel-production/bin/uvsr.exe`, SHA-256
  `26A702DFFE14A1DE376769AAD18FA26C0EE501BF8B494434F9A4AE87B6866CA4`
- Independent review: complete with no actionable P0-P2 findings
- Commit/publication: intentionally local and unauthorized
- Active ownership released: no
- Archived: pending
