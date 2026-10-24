# Ratio-Estimator MSAA Sparse-Edge Repair

## Status

- State: complete; user visual acceptance pending
- Coordinator: `/root`
- Project branch/worktree: `codex/ratio-shadow-msaa-cmaa2-prep` in
  `work/ratio-shadow-msaa-cmaa2-prep`
- Base state: live main commit `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`
  plus the refreshed uncommitted preparation diff recorded in
  `docs/exec-plans/completed/ratio-shadow-msaa-cmaa2-preparation.md`
- Rejected candidate: `b/bin/uvsr.exe`, SHA-256
  `4D3E4561263D3D0E275CB6893AC2B5A9BEC91C994877DCC332CADDBF834A8297`
- Repaired candidate: `b/bin/uvsr.exe`, SHA-256
  `23F23079623F5B890D6F326157A6430DA1358C780E32D2279B033CE1F235CB65`
- Started: 2026-08-14
- Last updated: 2026-08-14
- Planned archive:
  `docs/exec-plans/completed/ratio-shadow-msaa-sparse-edge-repair.md`

## Goal and Done Condition

Goal:

Repair the physically excessive global illumination observed when MSAA is
enabled in the ratio-estimator preparation build, use it as a canary for other
sparse-coverage and heterogeneous-sample defects, and preserve the isolated
candidate without touching the active path-tracing worktree.

Done when:

- [x] The direct-light-to-indirect-source path has a coherent sample owner and
      does not gain energy merely because MSAA is enabled.
- [x] Sparse coverage, heterogeneous samples, zero coverage, and sample-count
      transitions have deterministic regression contracts.
- [x] Other first-party MSAA paths are audited and every confirmed P0-P2 issue
      is repaired or explicitly bounded.
- [x] Primary sources constrain the resolve, coverage, and sample-frequency
      rules used by the implementation.
- [x] The isolated Release candidate builds, packages every shader, passes the
      full suite, and receives an independent rendering review.

## Scope

In scope:

- Screen-space GI source radiance under deferred MSAA.
- Closest-surface ownership, per-sample G-buffer access, direct-light
  visibility, coverage normalization, and resolved energy.
- Ratio-estimator sun shadows and their interaction with AO, illumination,
  temporal history, denoising, and sample-count transitions.
- Other confirmed MSAA sparse-edge correctness bugs found by the canary audit.
- Deterministic tests, source contracts, durable documentation, build evidence,
  and an exact candidate artifact.

Non-goals:

- Editing, building, rebasing, staging, or launching
  `work/path-tracing-resampling-controls`.
- Integrating either dirty worktree, publishing, pushing, opening a pull
  request, merging, releasing, or claiming canonical verification.
- Performance optimization that changes sampling quality without separate
  evidence.

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, MSAA resolve and PBR CPU/HLSL contracts, visibility bindings,
  shader manifests, tests, README, and task documentation.

## Baseline

- The user visually rejected the exact candidate recorded above because MSAA
  makes global illumination much brighter than physically expected.
- The rejected candidate previously compiled 326 shader permutations and passed 43 of
  43 tests, proving that the existing tests missed the reported semantic bug.
- The prior design intentionally cleared pooled sun visibility from the
  closest-surface auxiliary PBR pass. That policy is suspect because this pass
  supplies radiance to screen-space GI.
- Path-tracing transport shipped to live main at `0224649`; this isolated branch
  was fast-forwarded to that exact base before combined verification. Its
  separately running renderer remains no-touch.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Prior preparation | Exact dirty source and rejected executable identity | Preserved | Repair baseline |
| GI root-cause audit | Producer/consumer and energy contract | Complete | Fix design |
| Sparse-edge audit | Other P0-P2 MSAA ownership and coverage findings | Complete | Repair scope |
| Primary sources | Official sample, coverage, and resolve rules | Complete | Design and documentation |
| Path-tracing release | Refresh onto exact live main `0224649` | Complete | Combined verification |

Public contracts under review:

- A single-sample auxiliary PBR source must use visibility that belongs to its
  selected receiver; pooled visibility across different receivers has no unique
  owner.
- A multisampled final-lighting path must not infer coverage from uninitialized
  attributes or divide sparse contributions by a covered count when the output
  represents pixel area.
- Any sample-indexed metadata emitted by the closest-surface resolve must remain
  valid across resize and sample-count changes.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Status |
| --- | --- | --- | --- | --- | --- |
| GI-MSAA-ROOT | `/root/gi_msaa_root_cause` | Shared read-only | Dirty candidate | None | Complete |
| MSAA-SPARSE-AUDIT | `/root/msaa_sparse_edge_audit` | Shared read-only | Dirty candidate | None | Complete |
| MSAA-SOURCES | `/root/shadow_msaa_architecture` | Web/repository read-only | Dirty candidate context | None | Complete |
| REPAIR-INTEGRATE | `/root` | Isolated task worktree | Dirty candidate | Task-owned files | Complete |

## Assignment Contracts

### GI-MSAA-Root: Trace the Energy Error

- Read scope: renderer routing, closest-surface resolve, PBR preparation,
  screen-space visibility/GI, direct visibility, shaders, tests, and task diff.
- Write scope: none.
- Contract: identify the exact producer, selected receiver, visibility owner,
  weighting, and consumer; distinguish final direct lighting from GI-source
  lighting.
- Deliverable: ranked findings, smallest coherent repair, and regression tests.
- Stop and report if: the resource owner cannot be established from current
  first-party data.

### MSAA-Sparse-Audit: Audit Sparse Coverage

- Read scope: all first-party MSAA C++/HLSL/tests/docs.
- Write scope: none.
- Contract: prioritize configured-count versus covered-count errors, broadcast
  surfaces, stale uncovered data, edge energy gain/loss, and topology changes.
- Deliverable: P0-P2 findings, audited no-finding areas, and deterministic test
  gaps.
- Stop and report if: a repair requires a new product-quality tradeoff.

### MSAA-Sources: Establish Primary Rules

- Read scope: official Microsoft specifications/documentation, primary papers,
  and official reference source.
- Write scope: none.
- Contract: separate sourced rules from UVSR-specific inference.
- Deliverable: direct URLs and implications for implementation and tests.

### Repair-Integrate: Implement and Verify

- Owner: `/root`.
- Write scope: task-owned source, shaders, tests, and documentation in this
  isolated worktree only.
- Build/resource lease: coordinator alone owns `b/`, shader packaging, test
  execution, and any UVSR process.
- Contract: re-read every file before patching, preserve unrelated preparation
  work, and keep the final MSAA direct-light path and GI-source path physically
  coherent.
- Required verification: focused tests, complete Release build/shader package,
  full CTest, legal and heading checks, exact executable hash, and independent
  frozen-diff review.

## Integration Order

1. Freeze and trace the rejected candidate.
2. Reconcile the root-cause, sparse-edge, and primary-source findings into one
   sample-ownership contract.
3. Add failing deterministic tests before or with the repair.
4. Implement the smallest complete fix and any confirmed adjacent repairs.
5. Run targeted checks, the full Release build and suite, then independent
   review.
6. Produce an exact candidate for user visual acceptance on the already
   composed live-main path-tracing base.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| GI energy invariance | Deterministic 1x/MSAA source-radiance comparison | Focused CPU/source tests | Passed; closest-owner diffuse source and analytic-weighted final signal |
| Sparse coverage | Zero, one, partial, and full coverage fixtures | Focused MSAA tests | Passed for 2x/4x/8x/16x estimator coverage; traversal limitation documented |
| Heterogeneous samples | Different depths/materials/visibility retain ownership | Source/reference tests | Passed; pooled counterexample and failed-owner reset covered |
| Topology transitions | Resize and 1x/2x/4x/8x/16x changes invalidate state | Source/runtime contract | Passed; descriptor rejection now fails closed |
| Shader completeness | Every required permutation compiles and packages | Release build and bundle tests | Passed; 332 tasks and 47 staged binaries |
| Complete regression | All configured tests pass | Full CTest | Passed; 43 of 43 |
| Product evidence | Exact build no longer exhibits excess GI | User visual review | Pending |

## Decisions

| Date | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-08-14 | Reject the previous auxiliary unshadowed-sun policy | The user observed a large physical energy error; clearing visibility is not a neutral approximation when direct lighting seeds GI | All |
| 2026-08-14 | Keep all workers read-only and one coordinator writer | The failing contracts overlap renderer, shader, and test hotspots | All |
| 2026-08-14 | Resolve final direct lighting with independent receiver ratios and route the closest diffuse ratio to GI | A pooled ratio can cross-couple heterogeneous analytic responses, while a per-sample output array is unnecessary for the two current consumers | Repair |
| 2026-08-14 | Fail closed on resolve rejection and low-response math | Stale closest-surface data and a second stochastic epsilon both create subtle light leaks | Repair |
| 2026-08-14 | Bound mixed-surface screen-space GI rather than claim exactness | Exact ownership/coverage needs new metadata and sample-aware traversal beyond the direct-sun repair | Follow-up |

## Progress and Handoffs

| Date | Task Or Owner | Status | Revision Or Artifact | Checks | Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | `/root` | Active | Rejected candidate frozen by path and hash | Prior 43-test pass classified insufficient | Trace and repair |
| 2026-08-14 | Read-only audit team | Complete | Root-cause, sparse-edge, math, code, and test handoffs | No P0/P1 left in core estimator after review; adjacent issues ranked | Implement findings |
| 2026-08-14 | `/root` | Complete | Repaired candidate on `0224649`; SHA-256 `23F23079623F5B890D6F326157A6430DA1358C780E32D2279B033CE1F235CB65` | 332 shaders, 47 staged binaries, Release `ALL_BUILD`, 43/43 CTest, legal and title checks, final independent review | User visual acceptance |

## Risks and Escalation Triggers

- Reusing pooled sun visibility for one closest receiver can replace excessive
  energy with cross-surface bias; the repair must preserve ownership rather than
  merely attenuating brightness.
- Sample-indexed visibility or owner metadata can expand resource ABI and memory
  cost; use it only if the current data cannot express the exact result.
- Sparse-edge tests can pass while full-coverage shadow boundaries remain wrong;
  both cases are required.
- Any source or settings change invalidates the rejected executable and prior
  visual evidence.

Stop and ask the user only if a verified repair requires a material quality
versus performance choice that cannot be resolved from the stated physical
correctness priority.

## Completion

- Final integrated commit: none; commits are not authorized.
- Verification summary: 332 shader tasks, 47 staged binaries, Release
  `ALL_BUILD`, 43/43 CTest, legal inventory, and 247 in-scope document headings
  pass on the refreshed dirty candidate.
- Independent review: final post-refresh math, code, and test reviews complete;
  no actionable finding remains.
- Coming Soon/documentation update: no current section exists; durable design
  documentation is updated in scope.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: user visual acceptance; sample-aware
  mixed-primitive GI correction, sparse-source coverage, sample-position
  reconstruction, and TAA coverage metadata remain bounded architectural work.
- Active ownership released: yes.
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ratio-shadow-msaa-sparse-edge-repair.md`.
