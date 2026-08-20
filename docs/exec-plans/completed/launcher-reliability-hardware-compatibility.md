# Launcher Reliability and Hardware Compatibility

## Status

- State: completed; local candidate technically verified
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/launcher-reliability` at `C:\Users\brock\OneDrive\Documents\uvsr\work\launcher-reliability`
- Base commit: `0c8074848985152ed83f83b4087aaf10013de590`
- Started: 2026-08-20
- Last updated: 2026-08-20
- Planned archive: `docs/exec-plans/completed/launcher-reliability-hardware-compatibility.md`

## Goal and Done Condition

Goal: make the UVSR Launcher a reliable first-run and recovery surface by repairing the installed renderer package/startup failure, automatically resolving safe launcher/install identity drift, reporting crashed renderer processes accurately, simplifying the copy action, and auditing the launcher for hardware- and lifecycle-dependent failures.

Done when:

- [ ] A fresh launcher-built and launcher-installed package starts without root-signature or pipeline-state creation failures on the available DirectX 12 adapter, and deterministic package tests prove all required runtime files are present. The exact package/runtime contract passes, but the physical installed-startup smoke was deferred because an unrelated UVSR renderer session remained active.
- [x] Safe, owned launcher/install identity drift is repaired or redirected automatically, while foreign or tampered paths still fail closed with a clear recovery action.
- [x] Renderer process state changes from running to stopped after a crash or exit, so the launcher never offers a stale Close action.
- [x] The visible `Copy Details` action is renamed to `Copy` without changing what is copied.
- [x] The launcher test suite, focused package/runtime checks, broad lifecycle audit, documentation checks, and independent high-risk review pass on the final candidate.

## Scope

In scope:

- Installed renderer payload construction, manifest validation, runtime asset/shader/DirectX 12 dependency staging, and startup preflight.
- Launcher ownership and installed-copy convergence, automatic safe recovery, update/install transition handling, renderer process liveness, crash/exit handling, visible copy wording, diagnostics, and deterministic tests.
- Vendor-neutral DirectX 12 adapter/capability failure handling and an extensive audit of download, package, install, update, launch, close, crash, uninstall, and recovery paths.

Non-goals:

- Publishing a GitHub release, pushing a branch, opening or merging a pull request, obtaining a signing certificate, installing GPU drivers, adding DirectX 11 or Vulkan, or promising support below the renderer's real DirectX 12 requirements.
- Rendering-quality or performance changes unrelated to reliable startup across supported hardware.

Affected subsystems and paths:

- `installer/**`, launcher-focused tests and workflows, runtime packaging rules, and user-facing launcher documentation.
- First-party renderer startup or diagnostics only where evidence shows a vendor-dependent initialization defect; `donut/**` remains read-only.

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `.github/workflows/**`, runtime shader/package configuration, shared CPU/HLSL contracts, this execution plan, Git history, build trees, and runtime/GPU ownership.

## Baseline

- Canonical repository/remote: `origin` at `https://github.com/brockliddicoat/uvsr.git`; live `origin/main` and remote `HEAD` both resolved to `0c8074848985152ed83f83b4087aaf10013de590` on 2026-08-20.
- Local versus remote state: isolated branch equals live remote main. The original `main` checkout is ahead 3, behind 67, and contains a repository-wide staged deletion set; it is preserved and excluded from this task.
- Verified source commit/build: the named release commit is the source baseline, but the user reports that its installed renderer fails with `Failed to create pipeline state object` and `CreateRootSignature call failed, HRESULT = 0x887a0005`; it is therefore not accepted as a working installed-package baseline.
- GPU, scene, camera, resolution, and settings preset when relevant: startup validation uses the launcher's default renderer configuration and the machine's selected DirectX 12 adapter; no performance claim is planned.
- Known pre-existing failures: installed package startup failure; launcher-copy/installed-copy identity error; stale Close state after renderer crash; overly verbose `Copy Details` label.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| PKG-1 installed startup diagnosis | Exact missing, mismatched, or unsupported runtime contract | Complete | Coordinator implementation |
| LIFE-1 lifecycle diagnosis | Safe automatic convergence and exact process-liveness invariants | Complete | Coordinator implementation |
| AUDIT-1 broad launcher audit | Prioritized reproducible defects and test gaps | Complete | Coordinator implementation and review |
| REVIEW-1 independent final review | Final integrated diff and verification evidence | Complete | Completion claim |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- Every release payload must be self-consistent: its manifest, executable, shaders, assets, DirectX 12 runtime files, and launcher-recorded identity come from the same source/build snapshot.
- Automatic recovery may replace or redirect only UVSR-owned, cryptographically identified launcher/install state. Foreign or tampered files remain protected and require an explicit safe action.
- Renderer liveness is derived from a still-live exact process identity, not from a retained `Process` object or last launch attempt; exit and crash converge to the same stopped UI state.
- Hardware handling stays vendor-neutral and capability-based. Unsupported adapters receive actionable diagnostics instead of vendor assumptions or opaque secondary failures.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| PKG-1 | explorer agent | shared isolated worktree, read-only | `0c807484` | none | none | Complete |
| LIFE-1 | explorer agent | shared isolated worktree, read-only | `0c807484` | none | none | Complete |
| AUDIT-1 | explorer agent | shared isolated worktree, read-only | `0c807484` | none | none | Complete |
| IMPL-1 | `/root` | `codex/launcher-reliability` | `0c807484` | task-owned implementation and tests | PKG-1, LIFE-1, AUDIT-1 | Complete |
| REVIEW-1 | independent reviewers | shared isolated worktree, read-only | final dirty candidate | none | IMPL-1 | Complete |

## Assignment Contracts

### Pkg-1: Installed Runtime and Hardware Startup Diagnosis

- Owner/thread: explorer agent assigned by `/root`
- Branch/worktree: `codex/launcher-reliability`, read-only
- Base commit/state: clean `0c8074848985152ed83f83b4087aaf10013de590`
- Read scope: installer payload code/tests, CMake staging, renderer startup, shader/root-signature creation, DirectX 12 Agility/runtime configuration, prior launcher plans and logs.
- Write scope: none
- No-touch scope: all files, Git state, submodules, build trees, processes, and external services.
- Build directory and runtime/GPU/resource lease: none; do not build or launch.
- Dependencies already integrated: none
- Interface/invariant contract: diagnose the installed-only failure and distinguish missing/mixed payload defects from genuinely unsupported hardware.
- Deliverable: ranked root causes with exact evidence, minimal repair design, test additions, and remaining hardware risks.
- Done when: findings explain both reported HRESULT/pipeline symptoms or clearly delimit what evidence is still missing.
- Required verification: source/history/package-contract inspection only.
- Allowed Git and external actions: read-only local Git/source inspection; no writes, commits, network publication, build, or runtime action.
- Stop and report if: evidence requires editing `donut/**`, uncertain deletion, or an incompatible renderer contract.
- Handoff revision/artifact: findings message to `/root`
- Handoff acknowledged by/on: pending

### Life-1: Launcher Identity and Process-State Diagnosis

- Owner/thread: explorer agent assigned by `/root`
- Branch/worktree: `codex/launcher-reliability`, read-only
- Base commit/state: clean `0c8074848985152ed83f83b4087aaf10013de590`
- Read scope: launcher ownership, installed-copy convergence, update/feed, process inspection/state, MainForm/dialog behavior, and launcher tests/history.
- Write scope: none
- No-touch scope: all files, Git state, build trees, processes, installed state, registry, shortcuts, and external services.
- Build directory and runtime/GPU/resource lease: none; do not build or launch.
- Dependencies already integrated: none
- Interface/invariant contract: propose automatic mitigation that cannot overwrite foreign/tampered files; model renderer state from exact live process identity.
- Deliverable: concrete failure sequences, repair state machine, affected methods, and deterministic regression tests including the `Copy` wording.
- Done when: the reported mismatch and stale Close behaviors have source-backed causes and safe repair paths.
- Required verification: source/history/test inspection only.
- Allowed Git and external actions: read-only local inspection; no writes, commits, network publication, build, runtime, registry, or install changes.
- Stop and report if: safe recovery requires a product choice or broader trust boundary.
- Handoff revision/artifact: findings message to `/root`
- Handoff acknowledged by/on: pending

### Audit-1: Broad Launcher Reliability Audit

- Owner/thread: explorer agent assigned by `/root`
- Branch/worktree: `codex/launcher-reliability`, read-only
- Base commit/state: clean `0c8074848985152ed83f83b4087aaf10013de590`
- Read scope: all `installer/**`, relevant workflows/docs/history, and launcher test coverage.
- Write scope: none
- No-touch scope: all files, Git state, submodules, build trees, processes, installed state, and external services.
- Build directory and runtime/GPU/resource lease: none; do not build or launch.
- Dependencies already integrated: none
- Interface/invariant contract: audit for correctness, recovery, security, accessibility, multi-adapter/vendor capability handling, cancellation, crash consistency, and actionable diagnostics without inventing speculative features.
- Deliverable: prioritized P0-P3 findings with reproducible sequence, exact path/line, missing test, and recommended smallest fix.
- Done when: every major launcher lifecycle is inspected and unsupported claims are separated from code defects.
- Required verification: source and deterministic test-gap inspection only.
- Allowed Git and external actions: read-only local inspection; no writes, commits, network publication, build, or runtime action.
- Stop and report if: an issue overlaps active non-launcher work or needs external authority.
- Handoff revision/artifact: findings message to `/root`
- Handoff acknowledged by/on: pending

## Integration Order

1. Complete PKG-1, LIFE-1, and AUDIT-1 read-only diagnosis against the exact release commit.
2. Freeze the repair contracts and implement one coherent launcher/package patch in IMPL-1.
3. Run targeted tests, build/package checks, and isolated install/startup verification; repair only task-introduced or source-proven launcher defects.
4. Run REVIEW-1 on the complete diff, address accepted findings, then rerun all affected checks.
5. Reconcile user documentation and archive this plan after final evidence is recorded.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Installed payload starts | Exact package inventory plus successful staged or isolated installed launch | launcher tests, package manifest comparison, installer build, isolated install/startup smoke | Exact production payload and Agility contract validated; physical installed launch deferred because another task's renderer was active |
| Root-signature/PSO failure prevented | Deterministic validation catches missing/mixed shaders and supported hardware reaches renderer startup | package contract tests and runtime log inspection | Passed: stable SDK `1.619.5`, exports `D3D12SDKVersion`/`D3D12SDKPath`, exact core hash, Shader Model 6.5 gate, Unicode path test |
| Identity drift auto-mitigates safely | State-machine tests cover owned stale launcher, installed launcher, foreign files, tampering, and failed recovery | launcher test suite | Passed in 84/84 launcher contract suite; identity advanced to `1.1.2` sequence `3` with deterministic input lock |
| Crash clears Close action | Process-exit/crash tests and UI state contract | launcher test suite plus UI inspection | Passed: timer/intent, exact PID/path/creation identity, native same-handle force-close tests |
| Copy label | UI tree/source test and visual inspection | launcher test suite plus screenshot | Passed in isolated final-artifact preview; visible label `Copy`, accessible name `Copy operation details` |
| Broad reliability | Full launcher suite, static audit, independent review, docs validation, clean diff | `installer/build.ps1`, focused tests, validators, `git diff --check` | Passed: Release `ALL_BUILD`, 49/49 CTest, 84/84 launcher tests, payload validator, 2,245-heading scan, line counts, input lock, diff check, two independent reviews |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-20 | Base the repair on exact live `origin/main` commit `0c807484`. | It is the user-named current version and remote HEAD; choosing the stale local `main` checkout would mix unrelated work and a repository-wide staged deletion set. | All |
| 2026-08-20 | Use one isolated worktree and one coordinator writer with parallel read-only diagnosis. | Launcher lifecycle, package identity, and process/UI state are coupled; competing writers would risk incompatible state-machine fixes. | All |
| 2026-08-20 | Treat automatic mitigation as owned-state convergence, not a blanket overwrite. | The existing mismatch guard protects against foreign/tampered files and must be retained while safe, known UVSR copies repair themselves. | LIFE-1, IMPL-1 |
| 2026-08-20 | Activate and verify the app-local Direct3D Agility runtime, pinned to stable `1.619.5`, and require Shader Model 6.5 before adapter selection. | The installed executable has no Agility exports, so its packaged runtime is inert; every packaged shader already requires Shader Model 6.5. Stable `1.619.5` is the current compatible release line and avoids shipping a preview runtime as the broad-hardware default. | PKG-1, AUDIT-1, IMPL-1 |
| 2026-08-20 | Assign the repaired launcher version `1.1.2`, release sequence `3`, while leaving the public feed on released `1.1.1` sequence `2`. | The published identity was reused for multiple different binaries. A new local candidate needs a unique identity, but changing the public feed or release asset is outside this task's authority. | LIFE-1, IMPL-1 |
| 2026-08-20 | Include source-proven recovery defects from the broad audit in the implementation pass. | The checked-in feed casing, empty-root crash recovery, activation-journal convergence, Git ancestry classification, and destructive close behavior are all launcher-first-experience failures with bounded repairs. | AUDIT-1, IMPL-1 |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-20 | `/root` | Active | isolated branch at `0c807484` | remote/worktree/plan/PR preflight complete | Await parallel diagnosis before freezing implementation contract |
| 2026-08-20 | PKG-1 explorer | Complete | read-only handoff at `0c807484` | installed payload, PE exports, CMake, shaders, and adapter-selection contracts inspected | Bundled Agility runtime is inert; add first-party exports, stable matching pins, build/package verification, and an SM6.5 gate |
| 2026-08-20 | LIFE-1 explorer | Complete | read-only handoff at `0c807484` | release history, activation, redirection, process state, and UI actions inspected | Bump identity; redirect equal/newer verified installed copies; use exact process identities and crash-aware UI intent |
| 2026-08-20 | AUDIT-1 explorer | Complete | read-only handoff at `0c807484` | every major launcher lifecycle and existing test area inspected | Repair real-feed parsing, empty-root recovery, journal convergence, ancestry errors, and close escalation; add exact-build CI coverage |
| 2026-08-20 | `/root` | Active | dirty isolated candidate | baseline launcher suite passed 71/71 under .NET SDK `10.0.400`; stable dependency archives downloaded and hashes verified | Implement the frozen runtime, lifecycle, recovery, and test contracts |
| 2026-08-20 | `/root` | Complete | renderer SHA-256 `022b676dadd5b83b81ed99fff6c42a1f935299ca2fb1aae816968c512f16abb1`; launcher SHA-256 `0b1d487f0665413a7a9d51a1f06d5cce5eee5eb550909a7b22b5e70d04562c25` | Release build; 49/49 native tests; 84/84 launcher tests; exact payload, exports, runtime hash, launcher health, UI preview, docs and diff checks passed | Preserve local candidate; complete clean install and Intel/AMD/NVIDIA release matrix before public distribution |
| 2026-08-20 | REVIEW-1 independent reviewers | Complete | final dirty candidate | GPU/package and launcher/lifecycle re-reviews found no remaining P0-P2 issue; all accepted review findings corrected | Physical hardware matrix and code-signing remain release validation, not local implementation evidence |

## Risks and Escalation Triggers

- `0x887a0005` is `DXGI_ERROR_DEVICE_REMOVED`; it may be a secondary symptom of a mixed shader/runtime package, a driver reset, or a real capability/driver problem. Do not claim one cause until package and runtime evidence agree.
- A repair that silently replaces arbitrary files would turn a reliability bug into an ownership/security bug.
- Runtime validation may be limited to the available physical GPU; deterministic capability and package tests must carry the cross-vendor coverage that cannot be exercised locally.
- The original checkout's staged deletion set is pre-existing and must remain untouched.

Stop and ask the user if:

- Supporting the affected hardware would require lowering renderer quality/feature requirements, adding a non-DX12 backend, installing or changing a driver, deleting uncertain installed data, or expanding the launcher trust boundary.
- Publication, release creation, signing, push, pull request, merge, or deployment becomes necessary.

## Completion

- Final integrated commit: none; the user did not authorize a commit
- Verification summary: local dirty candidate built successfully; 49/49 native and 84/84 launcher tests passed; exact production payload, Agility exports/runtime hash, launcher input identity, UI preview, documentation, and patch checks passed
- Independent review: GPU/package and launcher/lifecycle reviewers found no remaining P0-P2 issue after corrections
- Coming Soon/documentation update: reconciled to a local candidate awaiting physical release-matrix validation
- Pushed/PR/merged, or intentionally local: intentionally local; no publication action authorized
- Remaining experiments or follow-ups: clean disposable-VM install/startup, UAC/restart interruption matrix, permanent Authenticode identity, and physical Intel/AMD/NVIDIA supported-hardware matrix
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/launcher-reliability-hardware-compatibility.md`
