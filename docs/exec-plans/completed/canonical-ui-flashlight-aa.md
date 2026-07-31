# Canonical Promotion: UI, Flashlight, and Antialiasing

## Status

- State: completed locally; GitHub merge authorized and in progress.
- Coordinator: `/root`.
- Promotion Branch and Worktree: `codex/canonical-ui-flashlight-aa` at `C:\Users\brock\OneDrive\Documents\uvsr\work\canonical-ui-flashlight-aa`.
- Base Commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` (`origin/main`).
- Promotion Commit: `aed8cda` (`feat: integrate approved ui, flashlight, and taa upgrades`).
- Pull Request: #27, `feat: integrate approved ui, flashlight, and taa upgrades`, targeting `main`.
- Started and Archived: 2026-07-31.

## Goal and Local Completion

Goal: promote the user-approved local candidate with UI skins and CLI, flashlight and ambient fill, upgraded antialiasing, Screen-Space Directional Shadows, and approved default profiles onto GitHub `main`.

- [x] Compared the approved candidate source against the live `origin/main` base and integrated only task-owned source, tests, and durable documentation.
- [x] Built and tested the integrated branch in production and factory DirectX 12 Release configurations.
- [x] Obtained an independent source review of the composed renderer and UI contracts.
- [x] Created the focused promotion commit and pushed the dedicated branch.
- [x] Opened the merge-authorized pull request.
- [ ] Merge #27 into `main` and verify the live remote head.

## Scope

In Scope:

- Selective source-level integration from `work\integrate-ui-flashlight-aa` onto the live `origin/main` base.
- Approved flashlight and TAA defaults, UI skins and CLI, Screen-Space Directional Shadows, shader packaging, tests, license notice, documentation, and README Coming Soon reconciliation.
- Branch, pull request, merge, and remote verification.

Out of Scope:

- The root worktree's pre-existing `AGENTS.md` change and divergent local-only `main` commit.
- Unrelated visibility PRs #10 and #11, generated build trees, candidate executables, input-worktree plans, historical artifacts, release packaging, and benchmarks.

## Baseline and Provenance

- The three user-supplied identifiers were verified executable SHA-256 values rather than Git commits. Their source worktrees all derived from `3ee4ceb`.
- Approved candidate artifacts: production `8DCE95F7D6B7E16E47DEDF863645A7229D3E57EFE59DF3C73D2D61D494D56A59`; factory `9880FCDF55EAA211F5414CBD922E8B4C7E5820571D7C12E98ABFF4DF6410BAB7`.
- The user approved the candidate build and its settings on 2026-07-31.
- Fresh promotion artifacts: production `1E287C6FDEAE73F4A3F646B499006DFE2C8093E5E986AC26A9F24364B03E836C`; factory `A64E0DFA94D8C2AB3FECEEEF862D1A560ABE8C42FF1DAAE4DCB3F0B9C4976575`.
- Before branch push, live `origin/main` remained `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`, equal to the promotion base.

## Integrated Contracts

- Preserve the expanded UI command catalog, post-ImGui mutation barrier, Amp and OG runtime policies, and DirectX 12-only packaging.
- Make `flashlight_1` editable while keeping its hotspot hidden and submitted before the visible cone; direct shadows and Ambient Fill retain independent gates.
- Use the approved flashlight profile: enabled, cast shadows, realistic lens, 25-degree beam, 0.70 roundness, 0.40 hotspot size, 0.70 hotspot strength, 0.20-degree sway, and 0.05-second aim correction.
- Make Temporal Reconstructive / Medium / Reduced the default TAA profile; Reduced inherits Stationary Bypass, while explicit overrides remain custom and reset correctly.
- Remove legacy Bend and MiniEngine runtime paths end to end; package Screen-Space Directional Shadows and Temporal AA shader paths instead.

## Verification Results

| Acceptance Criterion | Evidence | Result |
| --- | --- | --- |
| Candidate Fidelity | Scoped comparison of source, tests, CMake, overrides, third-party license, and durable docs against the approved candidate | Passed; excluded generated trees, historical plans, and root worktree changes |
| Production Build | Fresh Visual Studio 2022 x64 Release configuration with developer AA overrides off and experiment shaders off | Passed |
| Production Tests | `ctest --test-dir build-canonical-production -C Release --output-on-failure` | Passed, 33/33 |
| Factory Build | Fresh Visual Studio 2022 x64 Release configuration with developer AA overrides off and default-settings experiment shaders on | Passed |
| Factory Tests | `ctest --test-dir build-canonical-factory -C Release --output-on-failure` | Passed, 33/33 |
| Source Hygiene | `git diff --check` and scan for legacy Bend/MiniEngine runtime references | Passed; only CRLF normalization warnings |
| Document Hygiene | `tools\check_document_title_case.cmd` | Passed, 0 violations across 948 headings and bold lead-ins |
| README Line Counts | `tools\update_readme_line_counts.py --self-test` and `--check` after updating the ImGui override inventory | Passed |
| Independent Review | Read-only final review of the promotion diff | No blocker found |

## Review and Publication Notes

- The independent reviewer confirmed the DirectX 12-only build, AA and shadow manifests, default/reset semantics, flashlight submission, Ambient Fill behavior, and UI command/skin contracts. The reviewer did not rerun builds or visual runtime review; fresh 33/33 evidence covers the deterministic checks.
- The GitHub app connector could not create the pull request because its integration lacked repository write permission. The authenticated GitHub CLI successfully created #27 after the branch push.
- #27's initial README Line Counts check found that `imgui-runtime-policy.patch` was omitted from the tool's explicit override inventory. The focused documentation-tool repair added that inventory entry and regenerated README's tracked counts; local self-test and check both passed.
- Any source or settings-changing repair after this record requires fresh technical verification and renewed product acceptance before merge.

## Completion

- Local integration and verification are complete.
- The remaining authorized action is merge of #27 into `main`, followed by a live remote-head check.
- The completed-plan archive replaces the active plan; historical input plans remain excluded.
