# Windows 11 Bootstrap Installer

## Status

- State: local implementation complete; public release validation pending
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/windows-installer` at `C:\Users\brock\OneDrive\Documents\uvsr\work\windows-installer`
- Base commit: `5d15efd730c574e433bfc58ea4727010ff3a6619`
- Started: 2026-08-17
- Last updated: 2026-08-17
- Planned archive: `docs/exec-plans/completed/windows-11-bootstrap-installer.md`

## Goal and Done Condition

Goal: Build a public-distribution-ready single Windows 11 installer executable that assumes no developer setup or technical knowledge, acquires UVSR build prerequisites, builds the newest public `main`, optionally creates a desktop shortcut, and becomes a repeatable install/update/reinstall/uninstall manager.

Done when:

- [x] A self-contained Windows 11 x64 installer executable is produced from repository-owned source.
- [ ] A clean compatible Windows 11 x64 user can follow plain-language UI from download to a launchable UVSR without manually identifying or installing developer prerequisites.
- [ ] A reproducible GitHub Actions build emits the same single executable for later attachment to a public GitHub release.
- [ ] Fresh install, update, reinstall, uninstall, failure recovery, and optional shortcut behavior have automated evidence at the non-destructive layers and an end-to-end managed install exercise where safe.
- [x] Prerequisite sources, versions, commands, install layout, logs, and limitations are documented.
- [x] The installer source and tests pass independent packaging/security review.

## Scope

In scope:

- Windows 11 x64 only.
- Per-user managed UVSR cache, build, runtime, state, log, shortcut, and Apps & Features registration.
- Current public `https://github.com/brockliddicoat/uvsr.git` `main` resolution at each install or update.
- Acquisition of pinned managed Git, CMake, and Python tools; detection or acquisition of Visual Studio 2022 Build Tools with MSVC, the Windows SDK, and the Visual C++ runtime; followed by a Release build.
- Plain-language progress, recovery guidance, and public GitHub artifact generation.

Non-goals:

- Windows 10 or non-Windows support.
- Silent enterprise deployment, automatic background updates, actually publishing a release, or provisioning a code-signing identity.
- General-purpose source checkout management or preservation of edits inside the installer-managed tree.

Affected subsystems and paths:

- `installer/`
- `.github/workflows/windows-installer.yml`
- `README.md`
- `docs/exec-plans/completed/windows-11-bootstrap-installer.md`

Shared hotspots reserved for the coordinator:

- `README.md`, `.github/workflows/windows-installer.yml`, root build files, and this execution plan.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`, live `refs/heads/main` at `5d15efd730c574e433bfc58ea4727010ff3a6619` on 2026-08-17.
- Local versus remote state: isolated feature worktree is equal to `origin/main`; the original checkout is divergent and intentionally untouched.
- Verified source commit/build: no installer baseline exists; renderer verification will be tied to the exact installer-built commit and executable.
- GPU, scene, camera, resolution, and settings preset when relevant: installer work does not claim rendering or performance acceptance.
- Known pre-existing failures: none identified for this isolated worktree; Donut begins uninitialized by design.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Repository build discovery | Exact configure/build/output contract at base | Complete | Installer implementation |
| Prerequisite acquisition design | Official download/detection commands and reboot/exit semantics | Complete | Installer implementation |
| Lifecycle and security review | Safe state, staging, update, uninstall, and test invariants | Complete | Installer implementation and verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- The installer owns only `%LOCALAPPDATA%\Programs\UVSR`, a distinct `%LOCALAPPDATA%\UVSR Installer` state/log root, and exact registry/shortcut entries; it must never mutate arbitrary user checkouts or renderer data under `%LOCALAPPDATA%\UVSR`.
- Install/update resolves the public remote `main`, records the exact commit, and activates a newly successful build without destroying the previous working installation on failure.
- Reinstall rebuilds even when the recorded commit equals public `main`; uninstall removes only installer-owned state and leaves shared prerequisites installed.
- Repository checkout, CMake, and compilation always run unelevated. Only an Authenticode-validated official Microsoft prerequisite installer may request elevation.
- The installed payload is the allowlisted generated `bin` tree content plus sibling `media`; source and intermediate build output are never the active runtime.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| WI-1 | `/root/build_contract` | Shared read access | `5d15efd7` | None | None | Complete |
| WI-2 | `/root/prereq_contract` | Shared read access | `5d15efd7` | None | None | Complete |
| WI-3 | `/root/lifecycle_review` | Shared read access | `5d15efd7` | None | None | Complete |
| WI-4 | `/root` | `codex/windows-installer` | `5d15efd7` | `installer/`, `.github/workflows/windows-installer.yml`, `README.md`, execution plan | WI-1 through WI-3 findings | Complete |

## Assignment Contracts

### Wi-1: Discover the UVSR Build and Runtime Contract

- Owner/thread: read-only explorer, assigned after plan creation
- Branch/worktree: shared read access
- Base commit/state: `5d15efd730c574e433bfc58ea4727010ff3a6619`
- Read scope: `README.md`, `.gitmodules`, `CMakeLists.txt`, launcher/build scripts, generated layout evidence
- Write scope: none
- No-touch scope: all repository and generated files
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: report the minimal reliable Release configure/build command and exact runnable artifact/runtime dependencies
- Deliverable: distilled findings with risks and suggested checks
- Done when: installer inputs and output layout are evidence-backed
- Required verification: read-only commands only
- Allowed Git and external actions: read-only; no checkout, submodule, build, or publication changes
- Stop and report if: evidence conflicts across source and documentation
- Handoff revision/artifact: read-only build/runtime contract for `5d15efd7`
- Handoff acknowledged by/on: `/root`, 2026-08-17

### Wi-2: Validate Prerequisite Acquisition

- Owner/thread: read-only explorer, assigned after plan creation
- Branch/worktree: shared read access
- Base commit/state: `5d15efd730c574e433bfc58ea4727010ff3a6619`
- Read scope: repository requirements plus current primary vendor documentation
- Write scope: none
- No-touch scope: all repository files and machine installations
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: Windows 11 x64 only; bootstrapper must work before Git, CMake, or .NET are present
- Deliverable: official sources, supported commands, detection rules, exit/reboot behavior, and licensing/security risks
- Done when: recommended prerequisite strategy is actionable and sourced
- Required verification: read-only local/network checks only
- Allowed Git and external actions: browsing and read-only inspection; no installs or publication
- Stop and report if: a prerequisite cannot be redistributed or installed under the intended flow
- Handoff revision/artifact: initial lifecycle threat model and acceptance matrix
- Handoff acknowledged by/on: `/root`, 2026-08-17 after final process/prerequisite review

### Wi-3: Review Lifecycle, Safety, and Test Design

- Owner/thread: read-only reviewer, assigned after plan creation
- Branch/worktree: shared read access
- Base commit/state: `5d15efd730c574e433bfc58ea4727010ff3a6619`
- Read scope: plan, repository conventions, prospective `installer/` design after it appears
- Write scope: none
- No-touch scope: all repository and machine state
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: failures preserve a working install; uninstall deletes only owned paths; update is newest-public-main aware; shortcut choice is deterministic
- Deliverable: threat model, edge cases, acceptance matrix, and later independent patch review
- Done when: high-risk packaging behavior has concrete validation criteria
- Required verification: read-only inspection and non-mutating tests
- Allowed Git and external actions: read-only only
- Stop and report if: design requires unsafe deletion, trust-on-first-use without TLS, or ambiguous ownership
- Handoff revision/artifact: final read-only lifecycle and shell-transaction review of the local patch
- Handoff acknowledged by/on: `/root`, 2026-08-17

## Integration Order

1. Resolve repository output and prerequisite contracts.
2. Implement the bootstrapper, lifecycle service, UI, tests, and documentation under coordinator ownership.
3. Build and run deterministic tests, then exercise safe end-to-end lifecycle paths.
4. Freeze writes for independent review, repair findings, and repeat verification.
5. Archive this plan and hand off the local installer and exact UVSR executable.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Single executable starts on Windows 11 x64 without preinstalled .NET | Self-contained publish plus PE launch | `installer/build.ps1` and exact-artifact launch smoke | Passed: one x64 file, 58,288,167 bytes; `UVSR Installer` window opened and closed with exit code 0 |
| Public build is reproducible on a stock GitHub Windows runner | Workflow and local build-script parity | workflow validation plus local script | Workflow authored with pinned actions; local script passed; hosted run remains pending because nothing was pushed |
| Correct operation choices | State-driven UI/unit coverage | installer test suite | Passed: 24/24 contract tests |
| Newest public `main` build | exact remote SHA, recorded state, successful Release build | isolated source-build smoke | Passed at `5d15efd730c574e433bfc58ea4727010ff3a6619`; packaged executable SHA-256 `f32dc3b3db4d46ccd944709921377c00b1cda561dd8512c52f072ca9c869adf6` |
| Failure preserves prior install | injected command/download/build failures | lifecycle integration tests | Static transaction review passed; deterministic path/download/process recovery tests passed; full phase-fault VM matrix remains pending |
| Shortcut opt-in and removal | exact `.lnk` target and cleanup | integration tests/manual inspection | Static target/working-directory review passed; real shortcut lifecycle remains pending in a disposable VM |
| Uninstall is ownership-bounded | path/registry assertions | lifecycle integration tests | Ownership, reparse, and renderer-data separation tests passed; full helper/reboot lifecycle remains pending in a disposable VM |
| Documentation headings comply | repository validator | `tools/check_document_title_case.cmd` | Passed after final archive: 2,208 headings checked, 0 violations |

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-17 | Isolate from live public `main` in `codex/windows-installer`. | The original checkout is ahead two and behind 58 with unrelated untracked work; updating or building there risks user state. | All |
| 2026-08-17 | Target one self-contained Windows 11 x64 manager executable and a per-user installation. | Avoids requiring .NET before launch and avoids using `Program Files` as a mutable source/build tree. | WI-2, WI-4 |
| 2026-08-17 | Keep shared prerequisites installed during UVSR uninstall. | Removing Git, CMake, Visual Studio, or SDK components could damage unrelated user workflows. | WI-3, WI-4 |
| 2026-08-17 | Preserve `%LOCALAPPDATA%\UVSR` and use distinct fixed program and installer-state roots. | The renderer stores scene history and settings snapshots in `%LOCALAPPDATA%\UVSR`; that is user data, not installer ownership. | WI-3, WI-4 |
| 2026-08-17 | Download pinned portable Git, CMake, and Python under installer ownership and elevate only Microsoft Build Tools when missing. | A clean public install cannot assume winget, PATH, Git, CMake, Python, or .NET, while fetched repository code must never inherit elevation. | WI-2, WI-4 |
| 2026-08-17 | Package only the allowlisted generated runtime beside the complete generated `media` tree. | The CMake output is relocatable, while copying all build output would retain test and build-only executables. | WI-1, WI-4 |
| 2026-08-17 | Add a GitHub Actions artifact build without publishing it. | Public hosting needs a reproducible producer, but the current request does not authorize a push, release, or signing action. | WI-4 |
| 2026-08-17 | Keep the public checkout byte-clean and inject fixed `apply.ignoreWhitespace=change` only into the scrubbed Git child environment. | Public `main` contains reviewed patch context with mixed CRLF/LF endings. Context-only whitespace matching makes the clean Windows checkout buildable without altering the fetched commit or patch content. | WI-4 |
| 2026-08-17 | Treat the direct Git/CMake/MSBuild process as authoritative and terminate any remaining Job Object descendants after it exits. | MSVC can leave `VCTIP.exe` or worker nodes alive indefinitely; no installer build command permits detached background work. | WI-4 |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-17 | `/root` | Active | Worktree at `5d15efd7` | Remote, branch, worktree, active plan, Coming Soon, and open-PR preflight complete | Assign read-only explorers and implement after findings |
| 2026-08-17 | `/root/build_contract` | Complete | Read-only handoff | Exact x64 configure, app build, dependency, output, relocation, allowlist, and verification contracts recorded | Implement packaging allowlist and CMake 3.31+ tool |
| 2026-08-17 | `/root/lifecycle_review` | Initial Review Complete | Read-only handoff | P0/P1 ownership, transaction, elevation, deletion, process, trust, and clean-host test matrix recorded | Implement, freeze, and request final review |
| 2026-08-17 | `/root/prereq_contract`, `/root/lifecycle_review`, `/root/build_contract` | Complete | Final frozen-patch reviews | No remaining P0/P1 prerequisite, lifecycle, build, package, or process-containment defect | Rebuild final artifact and retain unsigned/VM release gates |
| 2026-08-17 | `/root` | Complete | Isolated source-build smoke | Downloaded and hash-verified managed Git/CMake/Python; configured and built public `main` `5d15efd7`; exact payload validation passed | Preserve exact smoke executable for handoff |
| 2026-08-17 | `/root` | Complete | `installer/artifacts/UVSR-Installer-Windows-11-x64.exe` | 24/24 tests; one-file self-contained publish; x64 PE; checksum matched; UI launch/close passed | Authenticode signing and disposable Windows 11 VM lifecycle remain release gates |

## Risks and Escalation Triggers

- Visual Studio Build Tools installation is large, may require elevation or restart, and must surface its official exit semantics clearly.
- The installer cannot be Authenticode-signed without an external certificate and publication/release decision; the local artifact will therefore trigger normal unknown-publisher warnings.
- Full pristine-machine proof may require a Windows 11 VM; local evidence must distinguish simulated dependency paths from a true clean-host exercise.

Stop and ask the user if:

- A required prerequisite cannot be installed from an official supported source without accepting materially different licensing or trust behavior.
- Correctness requires deleting a path whose installer ownership cannot be proven.
- Publishing, code signing, or a release destination becomes necessary.

## Completion

- Final integrated commit: intentionally uncommitted; commit/push/publication were not authorized
- Verification summary: 24/24 contract tests passed; isolated newest-public-main build/package smoke passed; final installer is x64, single-file, checksum-matched, and UI launch-smoked
- Independent review: three focused read-only reviewers found no remaining P0/P1 defects after final repairs
- Coming Soon/documentation update: root roadmap and installer documentation updated locally
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: Authenticode-sign the final artifact, execute the checked-in workflow, and pass fresh install/update/reinstall/uninstall/reboot/failure recovery on disposable stock Windows 11 x64 VMs before public release
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/windows-11-bootstrap-installer.md`
