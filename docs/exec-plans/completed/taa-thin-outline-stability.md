# TAA Thin-Outline Stability Repair

## Status

- State: locally verified; user visual confirmation pending
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ratio-shadow-msaa-cmaa2-prep` in `work/ratio-shadow-msaa-cmaa2-prep`
- Base commit: `0224649055f2218dcf1dbab4af4a1ea8a6b894f9` plus the preserved local Ratio/MSAA/CMAA2, Per-Sample Shadows, and first TAA edge-shake repair candidate
- Started: 2026-08-14
- Last updated: 2026-08-14
- Planned archive: `docs/exec-plans/completed/taa-thin-outline-stability.md`

## Goal and Done Condition

Goal: restore the previously achieved stationary stability of small and thin outlines without undoing the repaired TAA/accumulation history ownership or moving-disocclusion validation.

Done when:

- [x] Historical and current shader behavior are compared at the exact thin-silhouette paths, with the missing invariant identified rather than inferred from broad edge tests.
- [x] A deterministic thin-outline canary fails the current candidate and passes the coherent repair.
- [x] Every shader permutation, the exact runtime bundle, focused tests, and the complete CTest suite pass on a rebuilt executable.
- [x] Independent review reports no unresolved stationary, moving, viewport-edge, or accumulation-lifecycle regression, and the exact executable is ready for user visual confirmation.

## Scope

In scope:

- Stationary thin/subpixel silhouette coverage, history acceptance, current reconstruction, motion ownership, depth support, history filtering, rectification, and related TAA tests and documentation.
- Preservation of the first repair's Ray Marching accumulation ownership and Path Tracing Shared Primary jitter exception.
- The Path Tracing Shared Primary adaptive-revisit canary exposed by restoring the authored camera-jitter sequence.

Non-goals:

- General TAA redesign, new temporal resurrection, visibility sample rotation, unrelated anti-aliasing features, performance benchmarking, publication, or changes to the other task's renderer process.

Affected subsystems and paths:

- `src/temporal_aa_blend_cs.hlsl`, `src/temporal_aa_minimum_cs.hlsl`, `src/temporal_aa_reference.h`, and only directly required CPU/UI wiring.
- The existing Path Tracing scheduling flag, input, and recurrence paths; no new resource, binding, or permutation.
- TAA, accumulation, Path Tracing, renderer, and UI source contracts; maintained TAA/path documentation; and this plan.

Shared hotspots reserved for the coordinator:

- Every writable path above, build tree `b`, executable artifacts, Git/index state, and final documentation integration.

## Baseline

- Canonical repository/remote: live base previously coordinated at `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`; remote freshness is not required for this local repair.
- Local versus remote state: intentionally dirty feature candidate; unrelated work is preserved.
- Verified source commit/build: first TAA repair candidate built 332 shaders, staged 47 production shader files, and passed 43/43 CTests; user visual feedback rejects its thin-outline stability.
- GPU, scene, camera, resolution, and settings preset when relevant: exact previously handed executable, stationary small/thin outlines; settings details will be captured if a runtime smoke window becomes available.
- Known pre-existing failures: no automated failure; the missing canary is the current evidence gap.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| First TAA edge-shake repair | Preserve accumulation lifecycle, center ownership, stationary raw-depth bypass, and moving point validation | Integrated locally | Thin-outline repair |
| Dormant TAA depth-validation candidate | Recover only evidence-backed thin-silhouette invariants | Read-only reference | Investigation |
| Settings-menu task | Keep its running renderer and worktree untouched | Active elsewhere | Build/runtime serialization |

Public interface, ABI, shader binding, resource layout, serialized setting, or asset/package contracts:

- No public setting, resource, binding, history format, or shader-permutation change is expected.
- Legacy Four-Texel behavior remains unchanged. Moving Nearest intentionally restores conservative one-sided ownership: stale nearer history is rejected while a slightly farther history sample may remain valid.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| TAA-THIN-HISTORY | `/root/taa_thin_history` | Shared read-only | Current dirty candidate | None | None | Complete |
| TAA-THIN-SHADER | `/root/taa_thin_shader` | Shared read-only | Current dirty candidate | None | None | Complete |
| TAA-THIN-TESTS | `/root/taa_thin_tests` | Shared read-only | Current dirty candidate | None | None | Complete |
| TAA-THIN-IMPLEMENT | `/root` | Current worktree | Current dirty candidate | In-scope paths | All investigations | Complete |
| TAA-THIN-REVIEW | Independent subagents | Shared read-only frozen diff | Final candidate | None | Implementation | Complete |

## Assignment Contracts

### `TAA-THIN-IMPLEMENT`: Repair and Verify Thin-Outline Stability

- Owner/thread: `/root`
- Branch/worktree: current feature worktree
- Base commit/state: `0224649` plus preserved local candidate
- Read scope: TAA source/history/tests/docs and historical isolated candidate
- Write scope: only the in-scope source, tests, docs, and this plan
- No-touch scope: `donut/`, other worktrees, unrelated candidate paths, Git history/index, remotes, and the other task's renderer process
- Build directory and runtime/GPU/resource lease: coordinator exclusively owns `b`; runtime launch waits until the other task releases UVSR
- Dependencies already integrated: first TAA edge-shake repair
- Interface/invariant contract: one temporal owner; center surface ownership; stationary phase invariance; moving depth rejection; no phase-dependent thin-outline history gate
- Deliverable: deterministic failing canary, minimal local repair, rebuilt exact executable, full verification, and independent review
- Done when: all goal criteria have evidence or a clearly reported user-visual remainder
- Required verification: focused TAA/source tests, 332 shader tasks, 47-file bundle, complete CTest, heading/legal/diff checks, independent review, and runtime smoke when safe
- Allowed Git and external actions: local edits/build/tests only; no stage, commit, push, PR, merge, release, or process control outside this task
- Stop and report if: historical behavior requires a material ghosting/stability tradeoff, an unexpected peer write appears, or correctness requires a new resource/public contract
- Handoff revision/artifact: pending
- Handoff acknowledged by/on: pending

## Integration Order

1. Compare the historical stable candidate and current shader, then define a thin-outline oracle.
2. Add the canary before the smallest coherent production change.
3. Freeze for independent review, rebuild every shader and UVSR, then run focused and full verification.
4. Archive the plan and hand off the exact artifact for visual confirmation.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Missing thin-outline invariant identified | Historical/current semantic diff | Read-only audits | Cleared-center coverage loss, symmetric moving-depth rejection, and adaptive phase locking identified |
| Current instability reproduced deterministically | CPU or compiled shader/reference fixture | Focused TAA test | Exact-background, four-orientation viewport, Halton-16, and one-sided-depth canaries pass |
| Stationary and moving contracts compose | Thin outline, broad edge, viewport, and motion fixtures | Focused tests and independent review | Focused suite passes; shader and lifecycle reviews report no unresolved issue |
| Shader/package/build integrity | Exact task and staged-file counts | Release build | 332 shader tasks pass; 47-file production bundle passes; Release `uvsr.exe` rebuilt |
| Combined candidate is regression clean | Complete automated suite | `ctest -C Release` | 43/43 tests pass |
| User can inspect the exact repair | Executable path and SHA-256 | Artifact handoff | `b/bin/uvsr.exe`, SHA-256 `1FFCA0C6F5C585EE14DF06CC30F9994D7EB623B25B8294F96E3D22B24620F728` |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-14 | Reopen as a new plan rather than editing the archived broad-edge plan | Repository policy requires resumed archived work to start from a fresh plan; user evidence exposes a narrower missing canary. | All |
| 2026-08-14 | Treat the prior executable as visually rejected for thin outlines | Passing broad-edge tests do not prove subpixel coverage stability. | All |
| 2026-08-14 | Do not restore whole-neighbor foreground ownership | The remembered path stabilized cleared-center phases by replacing motion, depth, validity, and ownership from a cross neighbor, which can retain unrelated foreground history. The repair authorizes only an exact cleared-background handoff after point-depth continuity and keeps current/output ownership at the center. | Shader, tests |
| 2026-08-14 | Restore one-sided moving-point depth validation | A symmetric fixed-depth threshold rejected harmless slightly farther history whenever a thin edge crossed the stationary threshold. Moving Nearest now rejects stale nearer ownership while conservatively accepting bounded farther history; Legacy Four-Texel remains unchanged. | Shader, reference, tests, docs |
| 2026-08-14 | Restore Halton 16 as the factory camera-jitter sequence | Sixteen unique phases reduce the cadence of thin-coverage repetition and match the newer path-transport lineage. Jitter sequence changes still retire temporal history. | Options, UI, tests, docs |
| 2026-08-14 | Decouple full-rate Shared Primary adaptive revisits from camera-jitter phase | A fixed 16-cycle revisit could permanently sample one Halton phase. The effective accumulation-jitter flag uses an overflow-safe successful-batch phase; full batches traverse every phase and partial successes advance eventually. Even-phase safety lattices remain a documented follow-up. | Path transport, accumulation tests, docs |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | Investigation team | Complete | Current dirty candidate | Historical/current shader, lifecycle, test, and scheduler audits | Missing cleared-center coverage handoff, moving-point asymmetry, Halton default, and Shared Primary phase lock identified |
| 2026-08-14 | Coordinator | Complete | `b/bin/uvsr.exe` SHA-256 `1FFCA0C6F5C585EE14DF06CC30F9994D7EB623B25B8294F96E3D22B24620F728` | Focused tests, 332 shaders, 47-file bundle, Release build, 43/43 CTest, legal, headings, and diff checks pass | Archive plan and hand off for visual confirmation |
| 2026-08-14 | Independent reviewers | Complete | Final dirty candidate | Shader/ownership/performance audit clean; batch-normalized path recurrence proven; contract findings repaired and retested | No unresolved automated-review finding |

## Risks and Escalation Triggers

- A phase-invariant history gate can still shimmer if current reconstruction, anti-ringing bounds, or sample ownership remains phase dependent.
- Thin geometry may expose an unavoidable coverage-versus-ghosting tradeoff; do not silently choose one without evidence.
- The active UVSR process belongs to another task and cannot be used or closed by this repair.

Stop and ask the user if:

- The evidence leaves two materially different visible defaults with a real stability-versus-ghosting tradeoff, or new external/destructive authority is needed.

## Completion

- Final integrated commit: intentionally local and uncommitted on base `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`
- Verification summary: 332 shader tasks, exact 47-file runtime bundle, Release build, and 43/43 CTests pass; legal inventory, 300 scoped headings, and `git diff --check` pass
- Independent review: no unresolved shader, lifecycle, recurrence, ownership, resource-safety, or source-contract finding
- Coming Soon/documentation update: no Coming Soon entry required; maintained TAA, Path Tracing, advanced-settings, and integration-procedure documentation reconciled
- Pushed/PR/merged, or intentionally local: intentionally local; no external publication authorized
- Remaining experiments or follow-ups: user visual confirmation remains; an extreme even-phase Path Tracing safety lattice still needs a future lattice-level sequence for guaranteed complete phase coverage
- Active ownership released: yes
- Archived to completed/abandoned path: `docs/exec-plans/completed/taa-thin-outline-stability.md`
