# Front-End Fidelity Follow-Up

## Status

- State: complete
- Coordinator: `/root`
- Branch and worktree: `codex/engine-core-cleanup` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\engine-core-cleanup`
- Base commit: `f7c0c87d8cba6880428fbc34400eb2882fb5182e` plus the complete local
  engine-cleanup and front-end-restoration diff
- Prior completed plan:
  `docs/exec-plans/completed/frontend-functionality-restoration.md`
- Started and completed: 2026-08-03
- Planned archive:
  `docs/exec-plans/completed/frontend-fidelity-followup.md`

## Goal and Done Condition

Goal: make the retained front end match the pre-cutdown interface more closely
without reviving deleted renderer, planner, benchmark, or shader surfaces.

Done when:

- [x] Debug and all four nested groups start expanded; World displays `Scene`,
      while the three effect selectors display `Default`.
- [x] Visibility Reconstruction starts collapsed at full resolution and
      expanded at reduced resolution without a per-frame disclosure override.
- [x] Statistics restores the useful pre-cutdown information hierarchy and
      readability using only retained runtime data and effect timings.
- [x] The command interface remains one row, separates its guidance with
      slashes, replaces the removed automatic result bar with green/red in-input
      status, retains history, and exposes long results only on explicit request.
- [x] Focused UI contracts, full tests, Release build, exact-artifact
      presentation smoke, current documentation, and independent reviews pass.

## Scope

In scope:

- Debug drawer and nested disclosure defaults.
- User-facing debug selector labels only; runtime enum values remain unchanged.
- Resolution-aware initial Visibility Reconstruction disclosure.
- Statistics layout and retained effect-cost presentation.
- Compact command guidance, status presentation, history preservation, and
  explicit long-result access.
- Focused tests, current UI documentation, and dated cleanup reporting.

Non-goals:

- Restoring retired benchmark planners, Visibility-owned temporal history,
  depth hierarchy, multiple-bounce diffuse, sample resurrection, forward
  rendering, sparse virtual shadows, diagnostic cascaded shadows, or removed
  shader permutations.
- Reintroducing the removed transparent edge overlay or Packed Depth mode.
- Changing command grammar, command values, renderer settings, GPU bindings,
  runtime resources, or serialized settings.
- Performance claims, publication, commits, pushes, pull requests, merges, or
  releases.

Affected paths:

- `src/uvsr.cpp`
- `tests/ui_source_contract_tests.cpp`
- `README.md`
- current UI documentation
- `docs/postmortem/engine-cutdowns/2026-08-03-frontend-fidelity-restoration.md`
- this execution plan

## Baseline and Ownership

- The branch remains a large intentional dirty cleanup candidate over
  `f7c0c87`; unrelated state was not reset, tidied, staged, or committed.
- The entering artifact was
  `build-frontend-restoration/bin/uvsr.exe`, SHA-256
  `58B58FBD643859D12CFB9A746BD5FA563048CB1F569B56449E31FBE6A5F82458`.
- The final dirty build-input identifier is
  `b3ebf1493c768697f14b9bb2caccc4a2c1010058`; no untracked build inputs
  exist in the recorded `CMakeLists.txt`, `LaunchUVSR.cmd`, `src`, `tests`,
  `tools`, and `overrides` scope. The identifier is not a stored Git object or
  recoverable patch.
- The coordinator owned every writable path, the isolated
  `build-frontend-fidelity` tree, packaged shaders, and the candidate window.
  All delegated work was read-only.
- An older comparison process was never stopped or repurposed. The exact
  candidate was launched separately; later computer-control actions stopped
  when the guard detected live user input.

## Assignment Summary

| Task | Owner | Write Scope | Status | Handoff |
| --- | --- | --- | --- | --- |
| Pre-cutdown presentation map | `/root/first_cutdown_map` | None | Complete | Mapped retained Statistics data and safe restoration boundaries |
| Implementation and integration | `/root` | All task paths | Complete | Restored the frontend contracts without a backend change |
| Frozen source and test review | `/root/fidelity_code_review` | None | Complete | No unresolved priority-zero through priority-two findings |
| Frozen report and documentation review | `/root/fidelity_report_review` | None | Complete | Math, Title Case, links, evidence, and rollback ordering audited; findings incorporated |

## Interface Decisions

| Decision | Reasoning and Boundary |
| --- | --- |
| Use `Default` for the three neutral effect selectors and retain `Scene` for World. | `Default` describes the ordinary effect output; World is a material presentation rather than an effect-neutral state. Internal enums and the stable command value `final` remain unchanged. |
| Use conditional `DefaultOpen` for Reconstruction. | Full-resolution tracing needs no introductory reconstruction controls, while reduced resolution does. No every-frame `SetNextItemOpen` call forces later state. |
| Restore typed Statistics snapshots and detailed tables, not retired data producers. | Existing periodic data is sufficient for useful rows. Unavailable rows display `--`; no planner, benchmark, resource, or timer was revived. |
| Keep command feedback in the input. | Automatic result history could cover Settings. Green/red status now replaces the hint after Enter, and a tiny explicit details control retains access to long output without automatic overlap. |
| Preserve Up and Down command history. | The requested deletion concerns the visual history/result bar, not command recall. |

## Verification Evidence

| Criterion | Evidence | Result |
| --- | --- | --- |
| Debug defaults and copy | Focused source contract plus exact-artifact first-open exercise | Passed; drawer and four groups expanded, World `Scene`, three effect selectors `Default` |
| Reconstruction default | Focused source contract plus Full and Half Resolution exercise | Passed; Full collapsed and Half expanded; source has no per-frame override |
| Statistics fidelity | Typed snapshot and table source contracts plus Complete Renderer exercise | Passed; visible Effect label, full-width selector, striped detailed table, complete-frame context, populated stage timings |
| Command interface | Focused contract covers slash hint, success/error colors, edit dismissal, history, no automatic bar, and explicit read-only details | Passed; live manipulation was intentionally skipped after the input guard detected user activity |
| Backend remains pruned | Negative source contracts, shader compilation, and exact package contract | Passed; no retired backend surface returned |
| Complete build | Fresh Visual Studio 2022 x64 configure and Release all-target build | Passed; 268 core and 46 retained shadow tasks |
| Full test suite | Fresh `ctest` against the frozen source | Passed, 30 of 30 |
| Runtime package | Exact package inspection | Passed, 39 shader binaries |
| Exact artifact | `build-frontend-fidelity/bin/uvsr.exe` | 2,500,096 bytes; SHA-256 `CF8D6519939D30CFB203862F6C3BDC411E23E9D608A484D1885539EF92B38EB6` |
| Independent review | Frozen source/test and report/document reviews | Passed; no unresolved priority-zero through priority-two source finding and all report findings incorporated |
| Documentation hygiene | README count, Title Case, local links, stale-copy search, and `git diff --check` | Passed; line counts current, 1,034 headings and bold lead-ins clean, no patch whitespace error |

## Risks and Resolutions

- Restoring the older Statistics source wholesale would have revived deleted
  consumers. Only retained timing and resource data was used.
- Long command output does not fit one row. A deliberately clicked bounded,
  read-only details view preserves access without reviving the automatic
  floating bar.
- A control attempt encountered live user input. Further automated input was
  stopped; the command behavior relies on passing focused and full tests.
- The candidate is dirty and therefore cannot be called Canonical verified.
  Its complete scoped build-input identity and exact executable hash are
  recorded for local reproducibility and comparison.

## Completion

- Final integrated commit: none; no commit requested.
- Verification: source, build, 30 of 30 tests, 39-file package, exact-artifact
  presentation, independent reviews, line counts, Title Case, links, stale-copy
  search, and patch whitespace checks passed.
- Publication: intentionally local; no push, pull request, merge, or release.
- Durable report:
  `docs/postmortem/engine-cutdowns/2026-08-03-frontend-fidelity-restoration.md`.
- Remaining work: product acceptance and any future cutdown are separate work.
- Active ownership released: yes.
- Archived path:
  `docs/exec-plans/completed/frontend-fidelity-followup.md`.
