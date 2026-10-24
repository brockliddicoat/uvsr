# TAA Edge Shake Repair

## Status

- State: completed
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ratio-shadow-msaa-cmaa2-prep` in `work/ratio-shadow-msaa-cmaa2-prep`
- Base commit: `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`, preserving the existing task-owned Ratio/MSAA/CMAA2 candidate diff
- Started: 2026-08-14
- Last updated: 2026-08-14
- Archived: `docs/exec-plans/completed/taa-edge-shake-repair.md`

## Goal and Done Condition

Goal: repair the violent raster-edge shake introduced by the sample-accumulation TAA changes without reverting unrelated path-tracing, MSAA-shadow, or presentation work.

Done when:

- [x] Ray Marching sample accumulation cannot expose or advance camera jitter from a TAA resolve that is deliberately inactive.
- [x] Ordinary TAA keeps stationary silhouette history phase invariant while still rejecting moving disocclusions and preventing neighbor motion from changing center-surface ownership.
- [x] Focused CPU/source contracts, every packaged shader, the production bundle, and the complete CTest suite pass on one rebuilt candidate.
- [x] Independent shader and lifecycle reviews found no unresolved correctness issue, and the exact repaired executable is ready for user visual confirmation.

## Scope

In scope:

- Ray Marching accumulation/TAA history ownership, camera-jitter lifecycle, and phase advancement.
- Full-quality, reduced, and minimum TAA depth validation at stationary and moving silhouettes.
- Center-first edge-dilation ownership, point-versus-Gather bounds, regression fixtures, user-facing TAA documentation, and the active task plan.

Non-goals:

- Reintroducing retired temporal resurrection or visibility sample-rotation experiments.
- Changing path-tracing accumulation jitter, adding a second long-term temporal owner, redesigning TAA, or publishing repository changes.
- Reworking unrelated Ratio/MSAA/CMAA2 candidate behavior.

Affected subsystems and paths:

- `src/uvsr.cpp`, `src/temporal_aa_options.h`, `src/temporal_aa_options_shared.h`, `src/temporal_aa_reference.h`
- `src/temporal_aa_blend_cs.hlsl`, `src/temporal_aa_minimum_cs.hlsl`
- `tests/temporal_aa_tests.cpp`, `tests/renderer_source_contract_tests.cpp`, relevant UI contracts
- `docs/temporal-aa-options.md`, `docs/advanced-settings.md`, `docs/noise.md`, `docs/path-tracing-transport.md`

Shared hotspots reserved for the coordinator:

- Every writable path above, the shared build tree `b`, the renderer process, Git/index state, and final documentation integration.

## Baseline

- Canonical repository/remote: `origin/main` was last coordinated at `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`; no remote refresh is required for this local repair.
- Local versus remote state: feature worktree is intentionally dirty with completed, previously verified Ratio/MSAA/CMAA2 preparation and Per-Sample Shadows work; local root `main` is newer and out of scope.
- Verified source commit/build: the active candidate was previously built and passed 332 shader tasks, a 47-file production bundle, and 43/43 CTests; the current TAA repair invalidates that evidence.
- GPU, scene, camera, resolution, and settings preset when relevant: user-observed current candidate; focused visual matrix is Ray Marching Accumulate Samples off/on, TAA off/on, 1x/4x MSAA, stationary and lateral camera motion.
- Known pre-existing failures: none in the previously verified automated suite.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Current Ratio/MSAA/CMAA2 candidate | Preserve its complete dirty diff on base `0224649` | Ready | TAA repair |
| Settings-revamp task | Remain isolated; no cross-worktree integration during this repair | Coordinated | Repository integration |
| Historical TAA depth-validation candidate | Port only its proven ownership and stationary/moving invariants, not its stale broader patch | Reviewed | Shader repair |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- No resource, binding, shader-permutation, history-format, or serialized-layout change.
- Ray Marching Accumulate Samples preserves the stored TAA selection but resolves raster TAA inactive; path-tracing accumulation jitter remains the explicit exception.
- The low-cost depth policy becomes Stationary Bypass plus one point-depth read for moving samples. Legacy Four-Texel Footprint remains available as an explicit comparison policy.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| TAA-HISTORY | `/root/taa_history_bisect` | Shared read-only | `0224649` + candidate diff | None | None | Complete |
| TAA-MATH | `/root/taa_shader_math` | Shared read-only | `0224649` + candidate diff | None | TAA-HISTORY | Complete |
| TAA-LIFECYCLE | `/root/taa_lifecycle_tests` | Shared read-only | `0224649` + candidate diff | None | None | Complete |
| TAA-IMPLEMENT | `/root` | Current worktree | `0224649` + candidate diff | All in-scope paths | All audits | Complete |
| TAA-REVIEW | Two independent subagents | Shared read-only frozen diff | Final candidate | None | TAA-IMPLEMENT | Complete |

## Assignment Contracts

### `TAA-IMPLEMENT`: Repair and Verify the Two Regression Paths

- Owner/thread: `/root`
- Branch/worktree: `codex/ratio-shadow-msaa-cmaa2-prep` in the current worktree
- Base commit/state: `0224649` plus the preserved existing candidate diff
- Read scope: renderer/TAA sources, tests, maintained docs, Git history, and the isolated historical repair plan
- Write scope: only the in-scope source, tests, docs, and this execution plan
- No-touch scope: `donut/`, other worktrees, Git history/index, generated build products except through the existing build, unrelated candidate changes, remote/GitHub state
- Build directory and runtime/GPU/resource lease: coordinator exclusively owns `b` and any task-owned `uvsr.exe` process
- Dependencies already integrated: Ratio/MSAA/CMAA2 candidate and Per-Sample Shadows setting
- Interface/invariant contract: one temporal history owner; no unconsumed camera jitter; center surface retains depth/validity ownership; stationary history ignores phase-changing raw depth; moving history remains depth validated
- Deliverable: one local rebuilt executable, focused and full verification evidence, updated contracts/docs, and independent review
- Done when: every Goal and Done Condition item has evidence or an explicitly reported visual-only remainder
- Required verification: focused TAA and renderer tests, all shader tasks, production bundle, complete CTest, legal/heading checks, source-diff review, launch/smoke when safe
- Allowed Git and external actions: read-only Git inspection and local implementation/build/test only; no stage, commit, push, PR, merge, or release
- Stop and report if: an unexpected peer write appears in scope, current shader contracts cannot preserve center ownership, or repair needs a visible quality/performance tradeoff beyond the stated scope
- Handoff revision/artifact: local candidate on `0224649`; `b/bin/uvsr.exe`, SHA-256 `59A04E68C3E42DA50B1B33EFCAE2A89CBAD6ED66BAE88ECE1C878C6DFB02D7D4`
- Handoff acknowledged by/on: coordinator, 2026-08-14

## Integration Order

1. Encode deterministic lifecycle and silhouette canaries.
2. Repair accumulation ownership, motion ownership, and stationary/moving depth validation together.
3. Update current user/engineering contracts and archive this plan only after final checks.
4. Freeze the diff for independent review, then build and hand off the exact artifact.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Accumulation never exposes inactive-TAA jitter | CPU/source contract | Focused temporal and renderer tests | Passed, 3/3 focused tests |
| Stationary silhouettes are phase invariant | Eight-phase CPU fixture plus shader source contract | Focused temporal tests | Passed, including fractional viewport-edge witness |
| Moving disocclusions remain guarded | Ownership, bounds, and moving-depth fixtures | Focused temporal tests | Passed |
| Shaders and packaging remain coherent | Exact task and bundle counts | Release build and production bundle tests | Passed, 332 shader tasks and 47 staged files |
| Combined candidate is regression clean | Full automated suite | `ctest -C Release` | Passed, 43/43 tests |
| Rendering change is independently sound | Frozen-diff read-only review | Independent shader and lifecycle subagents | Passed, no remaining actionable issue |
| User can inspect the exact repair | Rebuilt executable path and SHA-256 | Local artifact handoff | `b/bin/uvsr.exe`, SHA-256 `59A04E68C3E42DA50B1B33EFCAE2A89CBAD6ED66BAE88ECE1C878C6DFB02D7D4` |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-14 | Repair both regressions introduced by `54a57b0` | Accumulation exposes raw jitter when it bypasses TAA; the same commit also made stationary acceptance depend on a four-texel raw-depth footprint. Fixing only one leaves a second deterministic shake path. | All |
| 2026-08-14 | Keep sample accumulation as the sole Ray Marching history owner and resolve raster TAA inactive | Running two long-term temporal filters or switching presentation owners is more complex and can bias history. Stored TAA settings remain intact; path-tracing jitter is a separate explicit contract. | TAA-LIFECYCLE, TAA-IMPLEMENT |
| 2026-08-14 | Port the historical center-owned Stationary Bypass invariant, not the stale patch | A blanket stationary bypass could reuse a foreground neighbor on an invalid/farther center. Borrowing only XY reprojection preserves the output surface's depth, Z delta, and validity. | TAA-MATH, TAA-IMPLEMENT |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | TAA-HISTORY | Complete | Read-only handoff | Bisected regression to `54a57b0`; compared parent and historical repair | Implement both failure paths |
| 2026-08-14 | TAA-LIFECYCLE | Complete | Read-only handoff | Proved jitter/TAA/accumulator phase mismatch | Resolve Ray Marching accumulation TAA inactive |
| 2026-08-14 | TAA-MATH | Complete | Read-only handoff | Proved phase-changing footprint and edge-dilation ownership break | Port constrained stationary/moving split |
| 2026-08-14 | TAA-IMPLEMENT | Complete | Local rebuilt candidate | 332 shaders, 47-file production bundle, 43/43 CTests, legal and heading validators | Hand exact artifact to user |
| 2026-08-14 | TAA-REVIEW | Complete | Final read-only shader and lifecycle reviews | No remaining actionable issue | User visual confirmation remains |

## Risks and Escalation Triggers

- Stationary history reuse is safe only after center ownership is preserved; source and CPU tests must pin that ordering.
- Four-Texel Footprint remains phase dependent by design and must not be the factory default.
- Automated fixtures cannot substitute for final user visual inspection of moving silhouettes.

Stop and ask the user if:

- the coherent repair would require choosing between visibly different ghosting and stability defaults not already resolved by the historical evidence, or destructive/external authority becomes necessary.

## Completion

- Final integrated commit: intentionally local and uncommitted on base `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`
- Verification summary: Release build passed; 332 shader tasks, 47-file production bundle, 43/43 CTests, legal inventory, diff check, and 285 in-scope document headings passed
- Independent review: shader and lifecycle reviews completed with no remaining actionable issue
- Coming Soon/documentation update: no Coming Soon section exists; maintained TAA, accumulation, and path-tracing documents updated
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user visual confirmation across Accumulate Samples off/on, TAA off/on, 1x/4x MSAA, and stationary/moving silhouettes; runtime launch was deferred because another task owns the current renderer process
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/taa-edge-shake-repair.md`
