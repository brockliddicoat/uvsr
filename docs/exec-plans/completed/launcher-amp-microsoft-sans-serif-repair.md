# Launcher and Amp Noto Sans Repair

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-engine-ui-consistency` in `work/launcher-reliability`
- Base commit: `42c2036dc5549eaca1c7a51d84e020018a6a089f` plus the uncommitted launcher/engine UI candidate recorded in the prior plan
- Started: 2026-08-20
- Last updated: 2026-08-20
- Planned archive: `docs/exec-plans/completed/launcher-amp-microsoft-sans-serif-repair.md`

## Goal and Done Condition

Goal: replace Segoe UI and the internal CodexUI font contract with bundled Noto Sans in both the launcher and Amp skin, preserve the existing regular/semibold/bold visual hierarchy, remove obsolete CMake font requirements, and fix clipped launcher button glyphs such as the `p` in Update.

Done when:

- [x] Launcher and Amp render through bundled Noto Sans with no Segoe UI or CodexUI runtime load route.
- [x] Existing weight roles remain visually and mechanically distinct without silently substituting a heavier or lighter role.
- [x] CMake, packaging, source contracts, and legal documentation require only the replacement font inputs.
- [x] Every main launcher button label is unclipped across the supported DPI/work-area matrix.
- [x] Exact rebuilt launcher and renderer artifacts pass automated, package, and visual checks.

## Scope

In scope:

- Launcher font declarations, font construction, text measurement/rendering, and button layout.
- Amp renderer font loading and selection.
- Windows font discovery/staging, package validation, legal documentation, and focused tests.
- Launcher release identity advancement required by binary-input changes.

Non-goals:

- General UI redesign, changing established font sizes or emphasis levels, editing Donut, publishing, signing, or updating the public feed.

Affected subsystems and paths:

- `installer/src/UVSR.Installer/`, launcher tests and release identity metadata.
- `CMakeLists.txt`, `src/uvsr.cpp`, renderer/package/source-contract tests, and font legal documentation.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, launcher release identity files, this plan, combined builds, and runtime windows.

## Baseline

- Canonical repository/remote: public `origin/main` must be refreshed before final source/update claims.
- Local versus remote state: task branch is dirty on `42c2036`; unrelated worktrees are preserved.
- Verified source commit/build: prior local candidate launcher 1.1.8 sequence 9 and renderer artifacts are historical comparison evidence only.
- Known pre-existing failures: none in the prior 93/93 launcher and 49/49 renderer suites.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Font identity audit | Exact Noto Sans release files and truthful 400/600/700 weight mapping | Complete: Noto Sans v2.015 selected under SIL OFL 1.1 | Launcher, renderer, CMake, packaging |
| Button clipping audit | DPI-safe text-rendering and geometry contract | Complete: vertical padding clips descenders | Launcher implementation/tests |

Public contracts:

- Font files must be the exact official Noto Sans v2.015 Regular, SemiBold, and Bold release files, embedded or staged under stable first-party package names, and validated before use.
- Existing font sizes and emphasis roles remain unchanged. Amp uses distinct static Regular 400, SemiBold 600, and Bold 700 files; the launcher uses its existing Regular and Bold roles through the exact 400 and 700 files rather than synthesis or fallback.
- Current renderer build output contains canonical Noto Sans files plus exact Noto-backed historical-path aliases so the retained sequence-9 transition candidate can validate the source without Windows fonts. Sequence 10 and later remove those aliases before the manifest is created.
- Installed schema-1 packages remain valid only when they contain the complete historical contract, complete Noto Sans contract, or exact hash-matched dual transition; partial, substituted, and arbitrary mixed contracts fail closed.
- The frozen `0c807484` renderer bridge bytes, blob identities, result tree, and synthetic commit remain unchanged.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| FONT-1 | `/root/bridge_identity_design` | Shared/read-only | Current dirty candidate | None | None | Complete |
| BUTTON-1 | `/root/bridge_implementer` | Shared/read-only | Current dirty candidate | None | None | Complete |
| REVIEW-1 | `/root/bridge_patch_audit` | Shared/read-only | Current dirty candidate | None | None | Complete |
| FONT-2 | `/root/bridge_identity_design` | Shared writer | `42c2036` plus current candidate | Font assets, CMake, renderer font routes/tests, package validation, legal font records | Noto Sans contract | Complete |
| FONT-3 | `/root/bridge_identity_design` | Shared writer | Integrated FONT-2 state | Exact dual transition and staging normalization | Release-order audit | Complete |
| BUTTON-2 | `/root/bridge_implementer` | Shared writer | `42c2036` plus current candidate | Launcher font loader/UI, project resources, button geometry, launcher tests | Noto Sans contract | Complete |
| REVIEW-2 | `/root/bridge_patch_audit` | Shared/read-only | Integrated candidate | None | FONT-2, FONT-3, BUTTON-2 | Complete; no remaining P0-P2 |

## Integration Order

1. Establish the exact font-family and weight-file contract.
2. Implement renderer/CMake/package changes and launcher/button changes with disjoint ownership.
3. Advance launcher identity and reconcile documentation after writers freeze.
4. Run combined launcher and renderer builds, tests, package validation, and exact-artifact visual checks.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Exact requested family | Embedded font metadata, byte provenance, source routes | Font-table inspection and source-contract tests | Passed: exact Noto Sans v2.015 hashes and 400/600/700 metadata; launcher resources and renderer routes validated |
| Preserved weights | Exact role mapping and visual comparison | Source assertions plus Amp/launcher screenshots | Passed: native source contract plus final 125%-scale launcher and Amp runtime inspection |
| No clipped glyphs | Update and all fixed labels fit at supported DPI | Launcher tests and Windows visual matrix | Passed: 96-384 DPI render matrix with 12-pixel minimum clearance; final `Update` descender visible at 125% |
| Update compatibility | Monotonic launcher identity and ordinary public-source engine update | Identity and update tests | Passed locally at 1.1.10 sequence 11; live feed/source URLs validated after automatic retry; source-first publication remains required |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-20 | Interpret “sans serif, not Segoe or Codex” as the Windows `Microsoft Sans Serif` family | Native C++ and WinForms require a concrete font family rather than a CSS-style generic family; exact installed files and weight support are being audited before edits | All |
| 2026-08-20 | Pause font implementation for a product choice | Microsoft Sans Serif supplies only one native Regular face; Amp cannot preserve distinct Regular, Semibold, and Bold roles with its current loader | All |
| 2026-08-20 | Bundle Noto Sans v2.015 under SIL OFL 1.1 | The user selected the more legally redistributable option. Noto Sans supplies distinct static 400/600/700 faces and permits application bundling under the license conditions. Copying Microsoft Windows font files was rejected because ordinary Windows font use does not grant redistribution rights, and Microsoft Sans Serif lacks native 600/700 faces. | All |
| 2026-08-20 | Keep historical package compatibility without changing schema 1 | Strict older launchers reject new JSON fields. The validator recognizes complete historical, complete Noto, or the later exact dual transition inventory, preserving rollback while rejecting ambiguous mixtures. | FONT-2 |
| 2026-08-20 | Use a bounded exact dual build transition | The retained sequence-9 transition candidate requires the historical font and Geist-notice filenames. Current CMake creates those aliases from the verified Noto bytes, sequence 10 and later strip them from staging, and only an exact dual installed package is accepted. This supports source-first publication without copying or loading a Windows or Geist font. | FONT-3, BUTTON-2 |
| 2026-08-20 | Advance the launcher to 1.1.9 sequence 10 | The embedded Noto resources, typography loader, package classifier, and button geometry changed locked launcher inputs after the sequence-9 artifact was built. This intermediate local artifact was superseded before handoff. | Coordinator |
| 2026-08-20 | Advance the final launcher to 1.1.10 sequence 11 | The source-first Noto preflight, legacy-stage rejection, and host typography health check changed launcher bytes after the sequence-10 preview was built. The exact input lock is `d91a28bce7f0c9db652a1e521aaffe2a0224b2c7e2a0145202287570073733af`. | Coordinator |
| 2026-08-20 | Require source-first publication for this transition | The live feed remains sequence 2, while sequence 9 is only a retained local transition candidate. Validate the dual source with that exact candidate and publish Noto source before configuring the permanent signer. That locked signer change must receive a fresh launcher identity, build, signature, and verification before its feed is published; do not describe the unsigned sequence-11 preview as install-ready. | Coordinator, REVIEW-2 |

## Risks and Escalation Triggers

- Noto Sans assets must retain exact upstream hashes and the OFL text; fail closed on missing or substituted files.
- Do not copy or package Windows system fonts.
- Fixed-height WinForms buttons must be proven against actual glyph metrics at high DPI, not only preferred-size estimates.

Stop and ask the user if a requested follow-up would require changing the selected Noto Sans family, its weight hierarchy, or its redistribution terms.

## Completion

- Final integrated commit: local/uncommitted; no commit or publication was authorized.
- Verification summary: launcher 97/97; native renderer contracts 5/5; exact build-output validation passed; launcher health check returned 0; frozen bridge check passed; live update check recovered from transient failures and validated every logged URL; final launcher and Amp visual smokes passed.
- Exact artifacts: launcher SHA-256 `c627cf4f93a78ea111e14665ba2b275a962859dcb86f28aedbaf551f0e4d57b6`; renderer SHA-256 `edec6f4c1d7b29e3b7a98fb9797028f4e08c483f5c00aba12c4e0178c6e5e027`.
- Independent review: complete; no remaining P0-P2 finding.
- Coming Soon/documentation update: reconciled with the Noto candidate and source-first publication gate.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Active ownership released: yes.
- Archived to completed path after final repository checks.
