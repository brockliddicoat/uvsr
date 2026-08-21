# Amp Font Family Selector

## Status

- State: complete; locally verified and intentionally unpublished
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-engine-ui-consistency` in `work/launcher-reliability`
- Base commit: `42c2036dc5549eaca1c7a51d84e020018a6a089f` plus the current uncommitted Noto/launcher candidate
- Started: 2026-08-20
- Last updated: 2026-08-21
- Planned archive: `docs/exec-plans/completed/amp-font-family-selector.md`

## Goal and Done Condition

Goal: add an **Interface** dropdown that selects among three truthful, visibly
distinct font presentations: **Codex (Segoe UI)** for the prior Windows look,
**Noto Sans**, and **Ogg (ProggyClean)**. Segoe UI is read only from the running
Windows installation and is never redistributed; the other required bytes are
already bundled under permissive terms.

Done when:

- [x] The three labels map to verified distinct font identities and no Windows font is copied or redistributed.
- [x] The selector switches Amp text at runtime from the Interface category with a deterministic default and reset behavior.
- [x] Build, package, source, legal, settings, and update contracts remain fail-closed and backward compatible where required.
- [x] Exact rebuilt renderer and launcher/package tests plus visual comparison pass.

## Scope

In scope:

- First-party renderer font ownership, selection state, Interface drawer UI, and focused settings/tests.
- Required redistributable font assets, CMake staging, package validation, notices, and legal records.
- Launcher release identity only if launcher/package binary inputs change.

Non-goals:

- Editing Donut, replacing the overall Amp skin, changing established point sizes,
  publishing, signing, or changing the public feed.

Affected subsystems and paths:

- `src/uvsr.cpp`, `CMakeLists.txt`, `assets/fonts/`, renderer source-contract tests.
- Launcher package validation/tests and font legal documentation if the inventory changes.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, launcher identity metadata, this plan, combined builds, and runtime windows.

## Baseline

- Canonical repository/remote: live `origin/main` is `42c2036dc5549eaca1c7a51d84e020018a6a089f`.
- Local versus remote state: the task branch has the verified but uncommitted Noto/launcher candidate; all unrelated worktree state is preserved.
- Verified source/build: local launcher 1.1.10 sequence 11 and renderer SHA-256 `edec6f4c1d7b29e3b7a98fb9797028f4e08c483f5c00aba12c4e0178c6e5e027` are the starting evidence and become stale for changed artifacts.
- GPU, scene, camera, resolution, and settings preset: NVIDIA GeForce RTX 4090 Laptop GPU, Sponza Decorated, Position 1, 1920 x 1080 for visual smoke only.
- Known pre-existing failures: none in the prior 97/97 launcher and 5/5 native contract suites.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Font identity audit | Exact meaning and redistributable bytes for Codex, Noto Sans, and Ogg | Complete; “Codex” has no real family identity | Renderer, CMake, package/legal |
| Interface/settings audit | Exact selector state, reset, persistence, and runtime switching contract | Complete | Renderer implementation/tests |
| Package/legal audit | Exact inventory and compatibility rules without Windows fonts | Complete; ProggyClean notice required | CMake, launcher package validation |

The completed selector uses Noto Sans as the deterministic default, historical
Ogg/ProggyClean as the opt-in comparison, and the truthful label
`Codex (Segoe UI)` for the former Windows appearance. Segoe UI is read from the
Windows Fonts directory at runtime and is never copied into UVSR source, build
output, or packages.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| FONT-DROPDOWN-1 | `/root/bridge_identity_design` | Shared/read-only | Current dirty candidate | None | None | Complete |
| FONT-DROPDOWN-2 | `/root/bridge_implementer` | Shared/read-only | Current dirty candidate | None | None | Complete |
| FONT-DROPDOWN-3 | `/root/bridge_patch_audit` | Shared/read-only | Current dirty candidate | None | None | Complete |
| FONT-DROPDOWN-WRITER | `/root/bridge_implementer` | Shared | Current dirty candidate | Renderer selector, command, and focused tests | Frozen mapping | Complete and released |
| FONT-DROPDOWN-PACKAGE | `/root/bridge_identity_design` | Shared | Current dirty candidate | Proggy notice, package validation, and legal records | Frozen mapping | Complete and released |
| FONT-DROPDOWN-REVIEW | `/root/bridge_patch_audit` | Shared/read-only | Frozen integrated candidate | None | Integrated verification | Approved; no P0-P2 findings |

## Integration Order

1. Freeze truthful family identities and the settings/runtime interface.
2. Assign one renderer/CMake implementation writer and one disjoint package/legal writer only after contracts are stable.
3. Reconcile launcher identity if locked package inputs change.
4. Run combined native/launcher tests and one serialized visual comparison.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Truthful distinct fonts | OpenType family/weight metadata and exact hashes | Asset inspection and source contracts | Passed; Noto v2.015 assets are exact, Segoe remains system-owned, and ProggyClean uses the pinned Dear ImGui bytes |
| Runtime selector | Three exact labels, deterministic state/reset, immediate switch | Native contracts and renderer visual smoke | Passed; live Amp switch covered Noto Sans to Codex to Ogg and back to Noto Sans |
| Preserved emphasis | Body/header roles remain distinct where supported | Source contract and live comparison | Passed; ProggyClean supplies the body while authored Amp headings retain Noto Sans Bold |
| Package/update safety | Exact inventory and historical recovery behavior | Launcher/package suite | Passed; 98 of 98 launcher tests and launcher health check passed |
| Complete native regression | Renderer build, runtime bundle, and all registered tests | Release `ALL_BUILD` and CTest | Passed; 50 of 50 tests |
| Update identity | Unique launcher identity and frozen historical bridge | Identity and bridge verifiers | Passed; launcher 1.1.11 sequence 12, bridge 24,581 bytes and SHA-256 `e68f814e2e838ef08bf8561bfa033dbaa0b5a523f776983dca18e8dd83ad799a` |
| Documentation quality | Current counts, Title Case, and diff hygiene | Repository validators | Passed; 2,325 headings and lead-ins, current counts, and clean diff check |

## Decisions

| Date/Time | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-20 | Treat the current Amp font as Noto Sans | Exact source routes and prior build evidence bind Amp body/header to Noto Sans SemiBold/Bold; visual similarity does not change byte identity. | All |
| 2026-08-20 | Audit “Codex” before implementation | The historical `CodexUI` paths may be an internal alias rather than a redistributable family. A dropdown label must not misrepresent the underlying font. | All |
| 2026-08-20 | Pause before assigning a writer | `CodexUI` was an alias for Segoe UI on Windows and Geist Medium elsewhere, not a font family. Historical Ogg is ProggyClean Regular 13 px, which has no true SemiBold or Bold face. Selecting either substitute changes visible identity or the established emphasis contract and requires user direction. | All |
| 2026-08-20 | Implement the requested comparison without another pause | Treat “Codex” as the prior Windows `CodexUI` appearance and disclose it as `Codex (Segoe UI)`; load it from Windows at runtime without copying it. Expose historical Ogg as `Ogg (ProggyClean)`. Keep Noto Sans as the deterministic default. Because ProggyClean has no Bold face, keep the existing Noto Sans Bold face for authored Amp headings and disclose that mixed role in the control help text. | All |

## Risks and Escalation Triggers

- Do not reintroduce Windows system font copying or an unlicensed asset.
- Do not collapse body/header emphasis silently when a family has only one face.
- Do not edit Donut to obtain its embedded default font.
- A represented/persisted setting may require the repository settings-snapshot schema procedure.

The completed implementation maps **Codex** truthfully to the installed Windows
Segoe UI family without redistributing it. No unresolved product decision
remains.

## Completion

- Final integrated commit: local and uncommitted; no publication was authorized.
- Verification summary: Release renderer build and all 50 native tests passed;
  all 98 launcher tests passed; launcher health, identity, frozen bridge,
  documentation, and diff checks passed. Live UI validation switched through
  all three dropdown options and restored the Noto Sans default.
- Renderer artifact: `work/noto-sans-seq10-renderer-build/bin/uvsr.exe`,
  3,044,352 bytes, SHA-256
  `DF6E07245F0E6A86BE5B0DECF8879B5B19422B3624E83DC8B1B0928C1055A400`.
- Launcher artifact: `work/font-dropdown-seq12-launcher-artifacts/UVSR-Launcher-Windows-11-x64.exe`,
  59,054,577 bytes, version 1.1.11 sequence 12, SHA-256
  `4D0593732530C4D584DD4F7651442AB048A576DDA07D72E0754B4FBBC7D17437`.
- Independent review: approved with no remaining P0-P2 finding.
- Coming Soon/documentation update: reconciled locally.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: optional CI expansion and stronger
  installed-Segoe metadata validation; neither blocks this local candidate.
- Active ownership released: yes.
- Archived to completed/abandoned path: completed path on 2026-08-21.
