# Command Download Completion Menu

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/cmd-download-completion-menu`
  in `work/cmd-download-completion-menu`
- Base commit: `6d8b95dc25b1b560f62708d97f4614422d853238`
- Started: 2026-08-15
- Last updated: 2026-08-15
- Archived:
  `docs/exec-plans/completed/cmd-download-completion-menu.md`

## Goal and Done Condition

Goal: replace the README's direct post-build renderer command with a persistent
CMD-hosted menu that can launch `uvsr.exe` or open its file location, reports
success or failure, and presents the same choices again after every action.

Done when:

- [x] Option 1 starts the configured build nonblocking and reports the result.
- [x] Option 2 selects `uvsr.exe` in Explorer and reports the result.
- [x] Missing executables, action failures, and invalid input do not close the
  menu and are followed by the original choices.
- [x] Existing direct `tools/launch_uvsr.ps1` calls remain one-shot.
- [x] Focused behavior, documentation, line-count, title, and diff checks pass.
- [x] The focused change is published to GitHub `main` as requested.

## Scope

In scope:

- `LaunchUVSR.cmd`, `tools/launch_uvsr.ps1`, the README build/run block, focused
  validation, and this execution plan.

Non-goals:

- A binary installer, GitHub Release, packaging workflow, renderer behavior
  change, or automatic prerequisite installation.

Shared hotspots reserved for the coordinator:

- `README.md` and all task-owned source, documentation, Git, and publication
  state.

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main`.
- Local versus remote state: isolated task branch equals live `origin/main`;
  the primary local `main` is intentionally untouched because it is ahead two
  and behind fifty-five with unrelated untracked files.
- Verified source commit/build: canonical source base `6d8b95d`; no renderer
  rebuild was required because this task changes only launch tooling and docs.
  The active lineage's last verified renderer remains
  `work/performance-menu-frame-repair/build-ui/bin/uvsr.exe` and was not rebuilt.
- Known pre-existing overlap: the stale `legal-and-licensing` plan names the
  README, but its branch is already integrated, no pull request is open, and no
  visible active task is editing this launcher or build/run block.

## Assignment Summary

| Task ID | Owner | Base | Write Scope | Status |
| --- | --- | --- | --- | --- |
| Flow Audit | `/root/locate_download_flow` | `6d8b95d` | None | Complete |
| Test Design | `/root/test_strategy` | `6d8b95d` | None | Complete |
| Independent Review | `/root/independent_menu_review` | `6d8b95d` | None | Complete |
| Implementation and Publication | `/root` | `6d8b95d` | Task scope | Complete |

## Integration Order

1. Freeze the launcher/menu contract from read-only findings.
2. Implement the launcher and README route in the isolated worktree.
3. Run focused behavioral and repository checks.
4. Independently review the frozen diff, repair findings, and reverify.
5. Archive this plan, commit, publish through GitHub, and verify live `main`.

## Verification Plan

| Acceptance Criterion | Evidence Required |
| --- | --- |
| Persistent choices | Windows contract passed all seven launch, open, invalid-key, missing-file, and rejected-request scenarios while the menu remained alive |
| One-shot compatibility | The unchanged no-menu branch still forwards `%*`; option 1 exercises the direct one-shot PowerShell helper |
| Safe paths | Contract passed from a path containing spaces, `&`, parentheses, `^`, `%`, and `!`; Explorer received one exact `/select,<path>` argument |
| Repository consistency | CTest line-count self-test/contract passed at 128,617 first-party lines; 2,175 Title Case headings passed; PowerShell parsing and `git diff --check` passed |
| Publication | Ready PR [#34](https://github.com/brockliddicoat/uvsr/pull/34) passed Document Title Case, Legal Inventory, and README Line Counts before the archival update and merged after its final check set |

## Risks and Escalation Triggers

- Process creation proves the requested action was started, not that UVSR later
  completed renderer initialization.
- Stop if the launcher requires a visible behavior choice beyond the requested
  two options, a peer modifies an owned path, or live `main` advances with a
  conflicting launcher/README change.

## Completion

- Final implementation commit:
  `935f2306cfb1286a7ac23854a529d870f4af1b77`
- Verification summary: `uvsr_launch_menu_contract` and both README line-count
  tests passed from the committed snapshot; PowerShell parsing, all 2,175
  document headings, exact argument/exit-code behavior, and diff hygiene passed.
- Independent review: the first review found launch-error propagation and
  Explorer-argument assertion gaps; both were repaired, and the second review
  completed clean with no findings.
- Coming Soon/documentation update: no roadmap entry; README route only
- Pushed/PR/merged, or intentionally local: branch pushed; ready PR
  [#34](https://github.com/brockliddicoat/uvsr/pull/34) merged to `main` after
  its final required checks.
- Active ownership released: yes, after live `main` verification
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/cmd-download-completion-menu.md`
