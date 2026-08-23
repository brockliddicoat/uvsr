# D3D12 Cross-Vendor Startup Repair

## Status

- State: completed locally; Intel Xe3 acceptance remains external
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/d3d12-cross-vendor-startup`
  in `work/d3d12-cross-vendor-startup`
- Base commit: `13bd1f2ce9afb344cfe9e4b3b611ee8fe599bacd`
- Started: 2026-08-21
- Last updated: 2026-08-22
- Archived:
  `docs/exec-plans/completed/d3d12-cross-vendor-startup.md`

## Goal and Done Condition

Goal: make the current UVSR engine start reliably on a fresh Windows 11 install
with any genuinely compatible D3D12 Shader Model 6.5 GPU, including Intel Xe3,
and replace opaque root-signature/compute-pipeline dialogs with actionable
device and capability evidence.

Done when:

- [x] The failing `CreateRootSignature` and compute-PSO paths are traced to
  exact first-party or pinned-dependency inputs and their portability contract.
- [x] Startup selects or rejects adapters from explicit D3D12/SM capability
  evidence and handles vendor/architecture differences without hard-coded GPU
  model exceptions.
- [x] Root-signature and pipeline creation no longer trigger the reported
  device-removal sequence on supported hardware; unsupported capabilities fail
  before renderer initialization with one actionable diagnostic.
- [x] Automated contracts cover the repaired startup/capability behavior and
  preserve the packaged Agility SDK and shader identities.
- [x] A clean Release build, relevant CTest suite, package validation, local
  runtime smoke, and independent rendering/startup review pass on the exact
  candidate.
- [x] The exact candidate `uvsr.exe` is handed off for testing on the Galaxy
  Book 6; Intel Xe3 validation remains explicitly pending until that hardware
  runs it.

## Scope

In scope:

- D3D12 adapter/device creation, feature probing, root signatures, pipeline
  creation, startup diagnostics, and the minimal shader/build contracts needed
  for portability.
- Vendor-neutral fallbacks or capability gates required by D3D12 SM 6.5
  hardware.
- Focused tests and durable engineering documentation for the repaired
  compatibility boundary.

Non-goals:

- Launcher UI or update-flow redesign.
- Vulkan or DirectX 11 support.
- Editing pinned `donut/` source; dependency behavior must be changed through
  first-party code or narrow overrides.
- Performance benchmarking or changing visible renderer defaults.
- Commit, push, pull request, merge, release, or launcher-feed publication.

Affected subsystems and paths:

- `src/**`, `overrides/**`, `cmake/**`, `tests/**`, and relevant renderer or
  build documentation as investigation establishes.

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`, `README.md`, `AGENTS.md`, `src/shaders.cfg`, shared CPU/HLSL
  contracts, this execution plan, Git state, builds, and renderer processes.

## Baseline

- Canonical repository/remote: live `origin/main` at
  `13bd1f2ce9afb344cfe9e4b3b611ee8fe599bacd` after a fresh fetch.
- Local versus remote state: task branch starts equal to `origin/main`; the
  original `main` checkout is independently ahead 3, behind 81, and has 6,692
  pre-existing status entries, so it is excluded from all writes.
- Verified source commit/build: `13bd1f2c` is the exact public package identity
  shown in both failure screenshots and the completed launcher release plan.
- Separate-hardware evidence: Galaxy Book 6, Intel Core Ultra 355, Xe3 iGPU;
  engine first reports `CreateRootSignature` failure with HRESULT `0x887A0005`
  (`DXGI_ERROR_DEVICE_REMOVED`) and may instead report failed compute-PSO
  creation. Both dialogs carry renderer identity `13bd1f2`.
- Known pre-existing failures: current public engine does not reach a rendered
  frame on the target Intel system. No crash dump, DRED report, debug-layer
  message, adapter driver version, or device-removed reason was captured by the
  released build.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| STARTUP-TRACE | Exact failing call chain and resource/shader identity | Complete | IMPLEMENTATION |
| PORTABILITY-RESEARCH | Primary-source D3D12 capability and failure contract | Complete | IMPLEMENTATION |
| TEST-AUDIT | Existing coverage and reproducible cross-adapter test plan | Complete | IMPLEMENTATION |
| IMPLEMENTATION | Stable vendor-neutral repair and diagnostics | Complete | VERIFICATION |
| REVIEW | Independent high-risk D3D12/rendering audit | Complete | Closeout |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- The renderer remains D3D12-only and requires Shader Model 6.5.
- Hardware is accepted by queried API capabilities, not vendor, architecture,
  marketing name, or assumed wave width.
- First-party code must not mutate pinned `donut/`; any required NVRHI/Donut
  correction uses a narrow build-time override.
- Failure handling must preserve the original HRESULT, obtain the device-
  removed reason when a device exists, identify the adapter and driver, and
  distinguish unsupported hardware from a driver/device fault.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| STARTUP-TRACE | Read-only explorer | Shared task worktree | `13bd1f2c` | None | None | Complete |
| PORTABILITY-RESEARCH | Read-only researcher | Shared task worktree plus primary sources | `13bd1f2c` | None | None | Complete |
| TEST-AUDIT | Read-only test explorer | Shared task worktree | `13bd1f2c` | None | None | Complete |
| IMPLEMENTATION | `/root` | Task worktree | `13bd1f2c` | Task-scoped source/tests/docs | All exploration | Complete |
| REVIEW | Independent read-only reviewer | Frozen candidate | Candidate revision | None | IMPLEMENTATION | Complete |

## Assignment Contracts

### Startup-Trace: Trace Failing D3D12 Objects

- Owner/thread: assigned read-only explorer
- Branch/worktree: shared task worktree
- Base commit/state: clean `13bd1f2c`
- Read scope: `src/**`, `overrides/**`, `donut/**`, build shader manifests, and
  relevant history
- Write scope: none
- No-touch scope: all files, Git/index, build directories, and processes
- Deliverable: exact call chains, likely object/shader identities, portability
  hazards, evidence paths/lines, and ranked repair options
- Done when: both dialogs map to concrete startup objects or a bounded set with
  a deterministic way to identify the exact one
- Required verification: source and history searches only
- Allowed Git and external actions: read-only
- Stop and report if: evidence requires editing pinned dependency source

### Portability-Research: Establish Vendor-Neutral D3D12 Contract

- Owner/thread: assigned read-only researcher
- Branch/worktree: shared task worktree
- Base commit/state: clean `13bd1f2c`
- Read scope: relevant source plus Microsoft/DirectX primary documentation
- Write scope: none
- No-touch scope: repository, Git state, builds, and processes
- Deliverable: primary-source-backed explanation of `0x887A0005`, root-
  signature/PSO validation behavior, capability queries, wave-size and shader-
  model portability rules, and recommended fail/fallback policy
- Done when: implementation decisions can be made without GPU-vendor guesses
- Required verification: cite only official documentation or specifications
- Allowed Git and external actions: read-only browsing
- Stop and report if: a claim depends on unpublished Xe3 behavior

### Test-Audit: Design Reproduction and Regression Coverage

- Owner/thread: assigned read-only explorer
- Branch/worktree: shared task worktree
- Base commit/state: clean `13bd1f2c`
- Read scope: `tests/**`, `tools/**`, CMake/package validation, startup flags,
  diagnostics, and existing build artifacts where source identity matches
- Write scope: none
- No-touch scope: repository, Git/index, shared builds, GPU, and renderer windows
- Deliverable: focused test matrix, usable local adapter/WARP/debug-layer hooks,
  packaging checks, and gaps that require instrumentation
- Done when: the candidate can be checked proportionately without claiming the
  local NVIDIA/Intel adapter proves Xe3 behavior
- Required verification: read-only discovery; no builds or launches
- Allowed Git and external actions: read-only
- Stop and report if: a proposed check would require controlling user hardware

## Integration Order

1. Complete call-chain, portability, and test audits.
2. Freeze the capability, fallback, and diagnostic contracts.
3. Implement the smallest coherent source/override/test/documentation change.
4. Configure and build in the task-owned isolated build directory.
5. Run focused contracts, full relevant CTest, package checks, and local smoke.
6. Freeze the candidate for independent D3D12/rendering review and repair any
   accepted finding.
7. Archive the plan and hand off the exact executable for Galaxy Book testing.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Exact failure is understood | Call chain plus HRESULT/device-removal evidence | Source/history audit and new diagnostic path | Invalid multi-unbounded RS1.1 table repaired; original device removal is now recorded with HRESULT, reason, breadcrumbs, and page-fault data |
| Capability behavior is vendor-neutral | Deterministic tests over synthetic capability records | Focused native contract tests | Passed for SM, feature-level, binding-tier, and DXR combinations without vendor checks |
| Root signatures and PSOs are portable | Shader/root-signature validation and runtime object creation | Release build, CTest, local D3D12 smoke | Release build and both normal/debug local scene smokes passed without object-creation or validation errors |
| Fresh package remains complete | Exact Agility SDK, shaders, assets, and executable inventory | Package-output validator | All 311 shader permutations compiled; runtime shader, production shader, scene, launcher, and app-local D3D12 runtime contracts passed |
| Target hardware starts | Exact candidate reaches first rendered frame on Xe3 | Galaxy Book 6 test | Pending external hardware |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-21 | Base work on live public `13bd1f2c` in a new isolated worktree. | It is the exact failing release; the original checkout is heavily dirty and stale, while the retired launcher worktree is on a closeout branch. | All |
| 2026-08-21 | Keep explorers read-only and make the coordinator the sole writer/build operator. | The root-signature, shader, adapter, and diagnostics contracts are coupled high-risk startup work. | All |
| 2026-08-21 | Treat Intel Xe3 as the first external acceptance target, not a vendor-specific code path. | The stated product boundary is every genuinely D3D12 SM 6.5-capable GPU. | All |
| 2026-08-21 | Do not touch launcher code unless evidence later proves it corrupts or omits engine runtime inputs. | Both screenshots show the launcher completed and the engine itself raised D3D12 errors. | IMPLEMENTATION |
| 2026-08-21 | Emit one root descriptor-table parameter per immutable unbounded bindless range and bind each to the shared table base. | UVSR declares two aliased register spaces; NVRHI put both unbounded ranges in one table even though D3D12 permits only the final range in a table to be unbounded. Separate parameters preserve the intended descriptor-index mapping and make every range the valid final range of its own table. Affected shaders explicitly opt in to resource aliasing. | IMPLEMENTATION, VERIFICATION |
| 2026-08-21 | Keep ray-query rendering optional behind Resource Binding Tier 2 and DXR 1.1 capability evidence. | Shader Model 6.5 alone does not imply either feature. The raster and screen-space renderer remains the supported baseline on SM 6.5 adapters. | IMPLEMENTATION, VERIFICATION |
| 2026-08-21 | Create the optional path-tracing pass only when Path Tracing is selected. | The public build compiles 24 path-tracing PSOs before the message loop even though Ray Marching is the default; this both caused the earliest failing call and imposed unnecessary driver compiler work at startup. | IMPLEMENTATION, VERIFICATION |
| 2026-08-21 | Request D3D feature level 11.0 and query higher capabilities separately. | D3D12 support begins at feature level 11.0, while the prior inherited 11.1 creation request excluded otherwise valid SM 6.5 adapters for an unrelated reason. | IMPLEMENTATION, VERIFICATION |
| 2026-08-22 | Persist first-failure D3D12 evidence and terminate object creation after device removal. | The public dialogs discarded compute HRESULTs and continued into dependent calls. The override now records the original object, HRESULT, removed reason, DRED breadcrumbs/page faults, and fails before null root signatures can reach graphics, mesh, compute, or ray-tracing callers. | IMPLEMENTATION, REVIEW |
| 2026-08-22 | Rate-limit only repeated adjacent warnings in the durable engine log. | A pre-existing per-frame light warning grew a 25-second smoke log beyond one megabyte. Errors and fatal records remain immediate; repeated warnings become five-second summaries, reducing the equivalent final smoke log to about 16 KB. | IMPLEMENTATION, VERIFICATION |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-21 | `/root` | Active | Clean task worktree at `13bd1f2c` | Live remote, worktrees, branches, active plans, open PRs, visible tasks, screenshots, and prior task reviewed | Start parallel read-only diagnosis |
| 2026-08-21 | STARTUP-TRACE, PORTABILITY-RESEARCH, TEST-AUDIT | Complete | Read-only handoffs on `13bd1f2c` | Call chain, D3D12 specification contract, history, and regression matrix reviewed | Implement bounded bindless layout, lazy optional PSOs, capability gates, and diagnostics |
| 2026-08-22 | `/root` implementation | Complete | Local candidate based on `13bd1f2c`; final `uvsr.exe` SHA-256 `3D9B359F53F487D4307E6477A8F584F9902EA6F69BD3E7C0BEBE0A3CD5BF079E` | Release build, 311 shaders, app-local D3D12 runtime verification, and 51 of 51 CTest passed | Freeze for runtime and independent review |
| 2026-08-22 | Independent startup and portability reviewers | Complete | Final frozen task diff | DRED consumption, complete null-root guards, first-removal termination, shader-reload laziness, root-signature 1.0 static samplers, and repeat-log suppression were rechecked; no actionable defect remains | Hand off after exact-artifact checks |
| 2026-08-22 | `/root` final verification | Complete | Exact local candidate `3D9B359F...BF079E` | Normal and `-debug` RTX 4090 Laptop smokes reached scene preparation, remained responsive at High priority, and logged zero error/fatal/object-failure records; all 2,416 headings, README counts, override application, and diff checks passed | Hand exact executable to Galaxy Book 6 |

## Risks and Escalation Triggers

- The target Intel machine is not directly accessible from this worktree, so
  local validation cannot by itself prove the Xe3 result.
- `DXGI_ERROR_DEVICE_REMOVED` is a symptom; without `GetDeviceRemovedReason`,
  DRED, or debug-layer output, the released dialogs do not prove which command
  or shader caused removal.
- A fallback that silently disables visible renderer features would change
  product behavior and requires evidence that the feature is outside the
  stated SM 6.5 baseline rather than a driver/engine defect.
- Packaging or Agility runtime changes can fix one machine while breaking
  another and require exact package validation plus independent review.

Stop and ask the user if:

- Correct operation on valid SM 6.5 hardware would require an accepted visible
  quality/default reduction rather than a semantics-preserving fallback.
- Progress requires installing or changing drivers, enabling developer mode,
  or otherwise mutating the separate Galaxy Book outside the package itself.

## Completion

- Final integrated commit: none; no commit was authorized, so the candidate is a local task diff based on `13bd1f2ce9afb344cfe9e4b3b611ee8fe599bacd`
- Verification summary: full Release build passed with 311 shaders and app-local D3D12 runtime verification; 51 of 51 CTest passed; normal and `-debug` scene smokes passed with zero D3D12 object-failure records; README counts are current at 131,798 first-party lines
- Independent review: two read-only reviews completed; all four actionable diagnostics, null-guard, device-removal termination, and shader-reload findings were repaired and reverified
- Coming Soon/documentation update: README has no Coming Soon section at the
  selected base; durable compatibility documentation will be updated if the
  repaired contract changes user-visible requirements or diagnostics
- Pushed/PR/merged, or intentionally local: local-only unless later authorized
- Remaining experiments or follow-ups: Galaxy Book 6 Intel Xe3 acceptance run; .NET 10 launcher test project was not runnable because this machine has only .NET SDK 9.0.315, while the native launcher contract passed and launcher source was unchanged
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/d3d12-cross-vendor-startup.md`
