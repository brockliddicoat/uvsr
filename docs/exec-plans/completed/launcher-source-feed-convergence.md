# Launcher Source and Feed Convergence

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-reliability` at `C:\Users\brock\OneDrive\Documents\uvsr\work\launcher-reliability`
- Base commit: `0c8074848985152ed83f83b4087aaf10013de590`
- Started: 2026-08-20
- Last updated: 2026-08-20
- Predecessor: `docs/exec-plans/completed/launcher-reliability-hardware-compatibility.md`
- Planned archive: `docs/exec-plans/completed/launcher-source-feed-convergence.md`

## Goal and Done Condition

Goal: repair the resumed launcher candidate so its update check accepts the live legacy feed without weakening schema validation, exposes the exact update source and failure reason, and reports renderer-source/launcher release skew accurately instead of recommending an unnecessary launcher update.

Done when:

- [x] The current public PascalCase feed and canonical camelCase feed both parse under explicit strict schemas, while mixed, duplicate, unknown, malformed, or ambiguous fields fail closed.
- [x] Launcher update status and logs include the exact feed URL, running and published version/sequence, and a specific transport, schema, or identity reason.
- [x] A strict versioned renderer build contract replaces inferred CMake syntax, and incompatible or missing public-source contracts preserve the existing install with neutral, actionable release-pair guidance.
- [x] Launcher identity advances beyond `1.1.2` sequence `3`, all targeted/full tests pass, and the exact rebuilt artifacts are recorded.

## Scope

In scope:

- Launcher feed parsing, update-status diagnostics, copied logs, and release-source documentation.
- A strict first-party renderer build contract and source/launcher compatibility classification.
- Release identity, input lock, source-contract and live-feed fixture tests, and local build verification.

Non-goals:

- Pushing renderer source, updating the public feed, creating or replacing GitHub release assets, signing, installing over the user's live package, or claiming that public installation can succeed before the matching renderer source is published.
- Weakening same-sequence/different-artifact protection or trusting dependency URLs/hashes supplied by downloaded source.

Affected subsystems and paths:

- `installer/src/UVSR.Installer/**`, `installer/tests/**`, `installer/README.md`, launcher release metadata and identity gates.
- A first-party versioned source contract under `cmake/`, plus focused workflow/release validation where required.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `.github/workflows/**`, `installer/README.md`, `ProductConstants.cs`, source-contract schema, tests, this plan, Git state, and build trees.

## Baseline

- Canonical repository/remote: live `origin/main` is `0c8074848985152ed83f83b4087aaf10013de590` on 2026-08-20.
- Local versus remote state: isolated branch at the same commit with the predecessor plan's uncommitted launcher/runtime candidate preserved.
- Verified source commit/build: predecessor candidate passed 49 native tests and 84 launcher tests before this resumed work; those results become stale for affected launcher/source-contract paths.
- GPU, scene, camera, resolution, and settings preset when relevant: no renderer runtime or performance claim is planned; an unrelated renderer task owns the active GPU/window resource.
- Known pre-existing failures: the live feed is HTTP 200 but PascalCase and rejected by the candidate's camelCase-only parser; the live immutable 1.1.1 asset URL is missing and the mutable fallback does not match the feed; public `main` lacks the stable-runtime source contract and renderer fixes expected by the local launcher.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| CM-1 source assignment audit | Distinguish release-pair skew from syntax/version errors | Complete | Coordinator implementation |
| UP-1 update feed audit | Exact live schema, version/sequence, asset, and diagnostic failures | Complete | Coordinator implementation |
| IMPL-2 integrated repair | Strict contracts, diagnostics, identity, tests, and docs | Complete | Final reviewer |
| REVIEW-2 independent final review | Complete dirty candidate and verification evidence | Complete | Completion claim |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Canonical launcher feeds use strict camelCase; an explicit strict whole-document PascalCase schema is accepted only for the legacy published v1 feed.
- The renderer source contract is versioned, bounded, strict, first-party data. It identifies a recognized compatibility contract and minimum launcher sequence; trusted dependency locations and hashes remain compiled into the launcher.
- A missing or unrecognized source contract means the public source and launcher are not a compatible release pair. It does not imply that a newer launcher exists.
- A launcher update is recommended only after a successfully validated feed proves a newer compatible release sequence.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| CM-1 | `cmake_assignment_audit` | shared worktree, read-only | dirty candidate at `0c807484` | none | none | Complete |
| UP-1 | `update_feed_audit` | shared worktree, read-only | dirty candidate at `0c807484` | none | none | Complete |
| IMPL-2 | `/root` | `codex/launcher-reliability` | dirty candidate at `0c807484` | all scoped implementation, tests, docs, and metadata | CM-1, UP-1 | Complete |
| REVIEW-2 | independent reviewer | shared worktree, read-only | final dirty candidate | none | IMPL-2 | Complete |

## Assignment Contracts

### Cm-1: Diagnose Source Assignment Failure

- Owner/thread: `cmake_assignment_audit`
- Branch/worktree: shared isolated worktree, read-only
- Base commit/state: dirty predecessor candidate at `0c807484`
- Read scope: `SourceManager.cs`, CMake source/dependency pins, public `main`, related tests and logs.
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, installed state, and external publication.
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: predecessor candidate
- Interface/invariant contract: determine whether the screenshot is parser syntax, stale launcher, dependency mismatch, or release-order skew.
- Deliverable: exact cause, safe contract design, diagnostic wording, and test matrix.
- Done when: the failure is reproducible against exact public source and the smallest safe compatibility contract is specified.
- Required verification: read-only source, Git, and public endpoint inspection.
- Allowed Git and external actions: read-only only
- Stop and report if: repair would require weakening trusted dependency pins or publishing source.
- Handoff revision/artifact: CM-1 handoff received 2026-08-20
- Handoff acknowledged by/on: `/root`, 2026-08-20

### Up-1: Diagnose Launcher Feed Check Failure

- Owner/thread: `update_feed_audit`
- Branch/worktree: shared isolated worktree, read-only
- Base commit/state: dirty predecessor candidate at `0c807484`
- Read scope: live feed and release endpoints, launcher parser/status/log paths, docs, and tests.
- Write scope: none
- No-touch scope: all files, Git state, build trees, installed state, and external publication.
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: predecessor candidate
- Interface/invariant contract: distinguish reachability, schema, identity, artifact, and signing failures and preserve strict validation.
- Deliverable: exact cause, live publication audit, repair design, and regression coverage.
- Done when: screenshot behavior is reproduced and the current/newer result for launcher sequence 3 is specified.
- Required verification: read-only public endpoint and local source inspection.
- Allowed Git and external actions: read-only only
- Stop and report if: a fix requires mutating the live feed or release.
- Handoff revision/artifact: UP-1 handoff received 2026-08-20
- Handoff acknowledged by/on: `/root`, 2026-08-20

## Integration Order

1. Freeze strict feed and renderer-source compatibility contracts from CM-1 and UP-1 evidence.
2. Implement parsers, classification, exact diagnostics, documentation, and release identity in one coordinator-owned change.
3. Run targeted contract tests, then full launcher/native/document and identity verification.
4. Obtain an independent read-only review, repair accepted findings, rerun affected checks, and archive this plan.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Live feed compatibility | Exact public legacy fixture parses as current/newer for the candidate | launcher contract tests | Passed in the 86-test launcher suite |
| Strict schema preservation | Canonical, legacy, mixed, duplicate, unknown, malformed, null, and oversized cases | launcher contract tests | Passed for both canonical and legacy schemas |
| Useful update diagnostics | Exact URL, versions/sequences, and root cause in status/log assertions | launcher contract tests and source inspection | Passed, including HTTP 500 and nested TLS failures |
| Source release-pair safety | Strict source contract and missing/unknown/minimum-sequence cases preserve installation | launcher contract tests | Passed; public source skew disables the unsafe renderer action without changing installed state |
| Unique launcher identity | Version/sequence metadata, input lock, and identity verifier agree | build and identity scripts | Passed for `1.1.3`, sequence `4`, input hash `a0117d20a182ccbe0add88b7fc84271aabb8d67ab8471c9787e4b7ff7640cebc` |
| Integrated candidate quality | Full launcher tests, native tests as affected, documentation validators, diff check, independent review | repository checks | 86/86 launcher and 49/49 native tests passed; final 2,262-heading scan passed; reviewer found no P0-P2 issues and all three P3 items were repaired |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-20 | Accept exact strict camelCase and legacy PascalCase feed schemas. | The public endpoint is reachable but uses the legacy casing. Global case-insensitive parsing would weaken all JSON state and permit ambiguous documents. | IMPL-2 |
| 2026-08-20 | Replace CMake-syntax inference with a strict versioned renderer source contract. | Public source and launcher compatibility is a release interface, not a stable property of CMake spelling or submodule layout. Falling back to the old preview dependency would recreate the runtime failure. | IMPL-2 |
| 2026-08-20 | Report release-pair skew neutrally and preserve the existing install. | The running launcher is newer than the published feed, while public renderer source lacks its required fixes. An update recommendation is false and cannot solve the mismatch. | IMPL-2 |
| 2026-08-20 | Stage the compatible launcher/feed before source that requires sequence 4. | Publishing the new source contract first would strand older launchers. The initial signed launcher requires a manual bootstrap because the existing signer pin is empty; after feed propagation, the renderer source can safely advance. | Publication prerequisite |
| 2026-08-20 | Keep all public changes local. | The user authorized implementation and verification, not a push, release, feed edit, signing, or install mutation. | All |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-20 | CM-1 | Complete | read-only handoff | exact public source and CMake contracts inspected | Stage the compatible launcher/feed first, allow propagation, then publish source that requires sequence 4 |
| 2026-08-20 | UP-1 | Complete | read-only handoff | live feed, release URLs, hashes, parser, logs, and UI inspected | Live immutable asset and feed identity are also inconsistent |
| 2026-08-20 | `/root` | Complete | `installer/artifacts/1.1.3-seq4-final/UVSR-Launcher-Windows-11-x64.exe` | 86/86 launcher tests, 49/49 native tests, renderer package validation, identity lock, metadata, checksum, and launcher health check passed | Unsigned local preview; public source/feed/assets remain unchanged |
| 2026-08-20 | REVIEW-2 | Complete | read-only final handoff | no P0-P2 issues; three P3 findings repaired and affected checks rerun | Ownership released |

## Risks and Escalation Triggers

- Public installation of the corrected renderer cannot succeed until its matching source commit is actually reachable from public `main`; local launcher changes can diagnose and preserve state but cannot publish that source.
- The live 1.1.1 feed points to an absent immutable asset and disagrees with the mutable fallback's final bytes; do not advertise or activate it as a valid update.
- The predecessor candidate contains broad uncommitted work. Re-read and preserve each relevant diff before editing.

Stop and ask the user if:

- Completion would require a push, release/feed mutation, code-signing action, overwrite of installed state, or a product decision that changes the trust boundary.

## Completion

- Final integrated commit: none; the verified candidate remains a deliberate local dirty diff based on `0c8074848985152ed83f83b4087aaf10013de590`.
- Verification summary: 86/86 launcher tests and 49/49 native tests passed. Exact renderer package inputs validated. Launcher `1.1.3` sequence `4` has SHA-256 `652e573a2148d1ac4f61109c130d2c0beb8fe48f9dfa58c72d23a0cf289b9652`, size `58,383,720`, matching checksum, correct product metadata, and a successful identity health check. It is intentionally unsigned.
- Independent review: complete; no P0-P2 findings, and all three P3 UI/document/test findings were repaired.
- Coming Soon/documentation update: complete, including exact update sources, persistent log location, troubleshooting, and consumer-first staged release ordering.
- Pushed/PR/merged, or intentionally local: intentionally local unless separately authorized
- Remaining experiments or follow-ups: sign and manually bootstrap the sequence-4 launcher, publish and validate its immutable asset/feed, allow propagation, then publish the renderer source contract; complete clean Windows and multi-vendor GPU release-matrix validation.
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/launcher-source-feed-convergence.md`
