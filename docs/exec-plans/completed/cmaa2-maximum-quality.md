# CMAA2 Maximum-Quality Audit and Repair

## Status

- State: complete locally; technically verified and awaiting product acceptance
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/cmaa2-maximum-quality` in `work/cmaa2-maximum-quality`
- Base commit: `402ebb042957eeba8149eee19e857b1e5452880a`
- Started: 2026-07-31
- Last updated: 2026-07-31
- Planned archive:
  `docs/exec-plans/completed/cmaa2-maximum-quality.md`

## Goal and Done Condition

Goal: determine why UVSR's Conservative Morphological Anti-Aliasing appears
materially weaker than Counter-Strike 2's reported CMAA2 presentation, repair
every source-backed quality or integration defect, and provide the strongest
correct CMAA2 result the renderer can support without disguising a different AA
algorithm as CMAA2.

Done when:

- [x] Publicly verifiable CS2 CMAA2 behavior is separated from community report
      and inference.
- [x] UVSR is audited against Intel's complete CMAA2 reference and at least
      three other genuine open integrations where available.
- [x] Every demonstrated preset, color-space, resource, dispatch, blending, and
      pipeline-order defect is repaired with focused contract tests.
- [x] The production shader matrix, Release application, focused tests, and full
      relevant CTest suite pass from this worktree.
- [x] The exact final artifact passes a bounded CMAA2 Ultra runtime smoke; a
      matched timing/capture comparison is explicitly deferred because the
      active NVIDIA overlay/PresentMon and P0 state invalidate that evidence.
- [x] An independent read-only rendering review finds no unresolved P0-P2 issue.

## Scope

In scope:

- CMAA2 quality preset and compile-time feature selection.
- HDR/luma/color-space handling and input/output format correctness.
- Edge/candidate/deferred-apply resource creation, clearing, barriers, counters,
  indirect dispatch, dimensions, and viewport boundaries.
- UVSR's post-processing order, quality-tier control, optional sharpening, UI
  defaults, statistics, documentation, and tests where they materially affect
  visible CMAA2 quality.
- Source-backed comparison with Intel CMAA2, CS2 evidence, and other genuine
  CMAA2 integrations.

Non-goals:

- Reconstructing proprietary Valve shaders or claiming equivalence that public
  evidence cannot establish.
- Replacing CMAA2 with SMAA, FXAA, TAA, DLAA, supersampling, or a hidden temporal
  accumulation path.
- Editing Donut, unrelated renderer features, scenes, or publication state.
- Broad performance optimization unless a quality repair creates a material
  regression that needs a bounded correction.

Affected subsystems and paths:

- `src/cmaa2.cpp`, `src/cmaa2.h`, and `src/cmaa2.hlsl`
- `src/third_party/intel_cmaa2/` for read-only provenance comparison; edit only
  if a demonstrated upstream-version defect cannot be fixed at the wrapper
- CMAA2 shader packaging and renderer call sites in `src/uvsr.cpp`
- Anti-aliasing option/reference contracts and focused tests
- `README.md`, `docs/advanced-settings.md`, and relevant AA documentation
- This execution plan and a task-local build directory

Shared hotspots reserved for the coordinator:

- All writable first-party files, `README.md`, global shader/build configuration,
  tests, documentation, Git state, build trees, and runtime/GPU control.

## Baseline

- Canonical repository/remote: live `origin/main`
  `402ebb042957eeba8149eee19e857b1e5452880a`, confirmed with `git ls-remote` on
  2026-07-31.
- Local versus remote state: this isolated branch starts equal to live canonical
  main. The primary checkout is dirty and diverged and will not be modified.
- Verified source commit/build: the task base is the current canonical merge;
  this task will establish its own baseline/candidate build evidence and will not
  call a dirty candidate Canonical verified.
- GPU, scene, camera, resolution, and settings preset when relevant: fixed PBR
  Sponza view at 1920x1080, standalone CMAA2 Ultra, no temporal AA or post-AA
  sharpening unless the compared configuration explicitly includes it.
- Known pre-existing failures: none established. The earlier comparison process
  has exited; runtime work still requires the normal process and thermal
  preflight before launch.

## Dependencies and Interfaces

| Dependency Or Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| Live canonical line | `402ebb0` | Integrated as base | Whole task |
| CS2 CMAA2 research | Source-backed facts and explicit unknowns | Complete | Comparison and defaults |
| Intel parity audit | Exact integration deltas and quality symptoms | Complete | Repair design |
| Other implementations | Genuine CMAA2 integration patterns | Complete | Cross-check and tests |
| Independent review | Frozen candidate patch | Complete | Final handoff |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- CMAA2 remains a single-frame spatial method operating on full-resolution
  single-sample resolved color.
- Quality remains the four official edge-threshold tiers; CMAA2 has no external
  original/result strength blend.
- Any resource-format or binding change must remain consistent between NVRHI,
  HLSL declarations, clear/barrier state, and shader-package permutations.
- Third-party license/provenance text is preserved; no Valve proprietary code or
  unverifiable reconstruction enters the repository.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `cs2-cmaa2-research` | `/root/cs2_cmaa2_research` | Read-only public research | `402ebb0` context | None | Public sources | Complete |
| `intel-cmaa2-parity-audit` | `/root/intel_cmaa2_parity_audit` | Read-only task worktree | `402ebb0` | None | Intel source | Complete |
| `cmaa2-other-integrations` | `/root/cmaa2_other_integrations` | Read-only public research | `402ebb0` context | None | Public sources | Complete |
| `integrate` | `/root` | This worktree | `402ebb0` | All task paths | Audit handoffs | Complete |
| `independent-review` | `/root/cmaa2_posttone_design_review` | Read-only frozen candidate | Candidate | None | Integrated patch | Complete |

## Assignment Contracts

### Cs2 CMAA2 Research: Establish Verifiable Valve Behavior

- Owner/thread: `/root/cs2_cmaa2_research`
- Branch/worktree: read-only public research
- Base commit/state: `402ebb0` context
- Read scope: Valve, Steam, Intel, and corroborated CS2/Source 2 public material
- Write scope: none
- No-touch scope: workspace, Git, builds, processes, and user browser sessions
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: separate public fact, credible secondary report,
  inference, and unknown; do not use leaked/proprietary source
- Deliverable: cited facts about CS2 selection/defaults/pipeline plus concrete
  implications for UVSR
- Done when: every material claim has a source or an explicit uncertainty label
- Required verification: primary sources preferred and sources cross-checked
- Allowed Git and external actions: read-only web access
- Stop and report if: evidence requires circumvention or cannot be verified
- Handoff revision/artifact: cited Valve/CS2 preset and cvar evidence
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Intel CMAA2 Parity Audit: Trace the Complete Local Contract

- Owner/thread: `/root/intel_cmaa2_parity_audit`
- Branch/worktree: read-only task worktree
- Base commit/state: clean `402ebb0`
- Read scope: complete CMAA2 source, integration, shader packaging, tests, and
  official Intel reference/guide
- Write scope: none
- No-touch scope: all files, Git, builds, processes, and runtime
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: trace preset, HDR/luma, formats, sRGB conversion,
  buffers, counters, clears, barriers, dimensions, dispatch, and aliasing
- Deliverable: ranked concrete defects/deviations, symptoms, repair, and tests
- Done when: CPU/HLSL/resource contracts are reconciled end to end
- Required verification: read-only source diff against official Intel revision
- Allowed Git and external actions: read-only filesystem and web
- Stop and report if: version provenance or licensing is ambiguous
- Handoff revision/artifact: Intel commit `071c6b0` parity report
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Other Integrations: Cross-Check Real CMAA2 Deployments

- Owner/thread: `/root/cmaa2_other_integrations`
- Branch/worktree: read-only public research
- Base commit/state: `402ebb0` context
- Read scope: genuine open CMAA2 engine/sample integrations and UVSR mapping
- Write scope: none
- No-touch scope: workspace, Git, builds, processes, and runtime
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: do not substitute CMAA, SMAA, or FXAA; record
  exact presets, color formats, pipeline position, and blend/sharpen behavior
- Deliverable: comparison table, ranked UVSR differences, and test ideas
- Done when: at least three genuine integrations are audited or the public-source
  limitation is explicitly documented
- Required verification: primary repository source and revision context
- Allowed Git and external actions: read-only web/filesystem
- Stop and report if: no additional genuine CMAA2 source can be verified
- Handoff revision/artifact: four-engine comparison and ranked repair report
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Integrate: Implement and Verify the Repair

- Owner/thread: `/root`
- Branch/worktree: `codex/cmaa2-maximum-quality` in this worktree
- Base commit/state: clean `402ebb0`
- Read scope: full first-party repository, vendored CMAA2, public audit sources,
  and relevant history
- Write scope: all scoped first-party source, tests, documentation, and this plan
- No-touch scope: Donut, unrelated worktrees, primary dirty checkout, unrelated
  features, generated build content, and remote state
- Build directory and runtime/GPU/resource lease:
  `build-cmaa2-maximum-quality`; coordinator alone controls it and any UVSR window
- Dependencies already integrated: audit handoffs before final algorithm choice
- Interface/invariant contract: preserve Intel CMAA2 semantics and color/resource
  correctness; expose quality-tier semantics honestly; avoid speculative Valve
  emulation
- Deliverable: locally verified candidate and source-backed root-cause report
- Done when: focused/full checks pass, candidate provenance is recorded, and the
  independent review clears the source
- Required verification: Release shader/application build, focused/full CTest,
  heading validation, `git diff --check`, runtime smoke and controlled visual
  comparison when exclusive GPU control is available
- Allowed Git and external actions: local branch/worktree edits, builds, tests,
  and task-owned runtime only; no commit, push, PR, merge, release, or deployment
- Stop and report if: the repair needs proprietary assumptions, a different AA
  algorithm, uncertain deletion, or a material quality/performance policy choice
- Handoff revision/artifact: `build-cmaa2-maximum-quality/bin/uvsr.exe`, SHA-256
  `177CB419B3BE0B3550BA7C3541D1B39BAF7C86A12C8A011A0E12FA47650AFB0F`
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Independent Review: Audit the Frozen Candidate

- Owner/thread: `/root/cmaa2_posttone_design_review`
- Branch/worktree: read-only task worktree
- Base commit/state: frozen candidate diff on `402ebb0`
- Read scope: complete task diff and CMAA2 producer/consumer contracts
- Write scope: none
- No-touch scope: all files, Git, build trees, and processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: completed implementation and tests
- Interface/invariant contract: review color space, formats, resource lifetime,
  indirect dispatch, shader permutations, quality/defaults, and test coverage
- Deliverable: prioritized findings with exact evidence
- Done when: no unresolved P0-P2 finding remains
- Required verification: read-only diff/source/test inspection
- Allowed Git and external actions: read-only
- Stop and report if: candidate changes during review
- Handoff revision/artifact: final read-only review with no P0-P2 finding
- Handoff acknowledged by/on: `/root`, 2026-07-31

## Integration Order

1. Freeze `402ebb0`, trace UVSR's complete CMAA2 CPU/HLSL/resource/color path,
   and establish a reference baseline without altering source.
2. Reconcile CS2 evidence, Intel reference behavior, and other open integrations.
3. Select the smallest source-backed repair and encode invariants in focused
   reference/source-contract tests.
4. Patch first-party integration and only the UI/documentation made inaccurate.
5. Build production shaders/application and run focused then full checks.
6. When exclusive runtime control is available, compare the exact baseline and
   candidate with matched scene/settings and record image/performance evidence.
7. Freeze the patch, obtain independent review, repair findings, rerun invalidated
   checks, and archive this plan at handoff.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command Or Experiment | Result Or Artifact |
| --- | --- | --- | --- |
| Canonical base | Live remote SHA and isolated clean branch | `git ls-remote`, `git status`, `git rev-parse` | `402ebb0`; clean at creation |
| Intel parity | Exact preset/color/resource/dispatch comparison | Source audit and focused contracts | Intel `071c6b0` reconciled; topology and existing dispatch contracts match |
| CS2 claim accuracy | Facts separated from inference and unknown | Public-source audit | Shipped Valve presets use MSAA/off; manual CMAA2 cvars report Ultra and extra-sharp off |
| Maximum quality | Demonstrated edges receive intended Ultra behavior without global blur | Source contracts and shader compile | Post-AgX Ultra `0.05` with full-color path 0; exact-scene visual acceptance remains with the user |
| HDR/color correctness | Display and HDR inputs use coherent range contracts | Source contracts and shader build | LDR E4 and HDR float-packing PSOs compiled separately; transfer/dither follows CMAA2 |
| Resource correctness | Counters, clears, indirect args, capacities, and bounds cannot silently drop work | Source contracts and extent guard | Existing safe capacities retained; added the missing 26-bit dense-list guard |
| Shader coverage | Every affected permutation compiles and packages | Task-local Release build and shader tests | 618 production permutations and 62 factory permutations compiled; runtime bundles pass |
| Integration health | Focused/full tests, clean diff, Title Case documents | CTest, heading checker, `git diff --check` | 33/33 production tests, factory bundle test, 142 headings, and diff check pass |
| Runtime viability | Exact candidate launches and remains responsive | Task-local controlled smoke | Final SHA-256 artifact reached ten consecutive responsive samples at High priority and closed cleanly |
| Independent review | No unresolved P0-P2 finding | Frozen diff audit | Complete; no unresolved P0-P2 source finding |

For any performance comparison, record:

- baseline and candidate source/diff and executable SHA-256;
- fixed GPU, scene, camera, resolution, CMAA2 quality tier, and sharpen state;
- warmup and sample window/count;
- total frame time plus CMAA2 edge, candidate, apply, and total costs;
- high-contrast diagonal, thin-line, foliage, text, and non-edge quality guards;
- before/after captures and raw measurement artifact.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-31 | Use live canonical `402ebb0` in a new isolated worktree | Primary `main` is dirty and diverged; the active TAA worktree remains user-owned and must not be reused | Whole task |
| 2026-07-31 | Use one writer with three read-only audits | CMAA2 CPU, HLSL, resource, UI, and tests are a coupled contract; parallel competing patches would obscure causality | Research and integration |
| 2026-07-31 | Treat CS2 visual strength as a hypothesis until public evidence identifies its exact configuration | Valve's proprietary renderer may combine CMAA2 with pipeline choices not present in public Intel source | Research and claims |
| 2026-07-31 | Move normal CMAA2 after AgX on an undithered display-linear RGBA16F intermediate | Intel's reference and Shaderpatch classify post-tone edges; UVSR's pre-tone LDR thresholds rejected visibly strong edges created or expanded by AgX | Renderer, shaders, and tests |
| 2026-07-31 | Start standalone CMAA2 at Ultra and use Intel's full-color detector for that tier | The old shared Medium default used a 0.10 threshold; Ultra uses 0.05, and path 0 detects isoluminant chromatic edges missed by luma | Defaults and shader matrix |
| 2026-07-31 | Apply sRGB transfer and stable dither in the final output pass after CMAA2 | Dither cannot independently cross Ultra's threshold but can perturb marginal moving edges and be blended on processed pixels | AgX and output presentation |
| 2026-07-31 | Retain a separate HDR-range permutation only for explicit tonemapperless presentation | Intel's LDR E4 packing is more precise for bounded display color, while scene-linear fallback can exceed one | CMAA2 PSOs and packaging |

## Progress and Handoffs

| Date/Time | Task And Owner | Status | Revision Or Artifact | Checks | Risks Or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-31 | `integrate` `/root` | Active | Clean branch/worktree at `402ebb0` | Live remote, worktree identity, Coming Soon, active plans, and collaboration protocol checked | Trace local source while audits run |
| 2026-07-31 | `cs2-cmaa2-research` `/root/cs2_cmaa2_research` | Complete, read-only | Valve files, patch notes, SteamDB, and current cvar dumps | Cross-checked shipped presets and cvars | Valve presets use MSAA/off rather than CMAA2 by default; manually selected CMAA2 reports Ultra and extra-sharp off |
| 2026-07-31 | `intel-cmaa2-parity-audit` `/root/intel_cmaa2_parity_audit` | Complete, read-only | Intel commit `071c6b0` and local parity trace | CPU/HLSL/resource/dispatch audit | Post-tone domain, Ultra detector, method default, 26-bit address guard, and split timer identified |
| 2026-07-31 | `cmaa2-other-integrations` `/root/cmaa2_other_integrations` | Complete, read-only | Shaderpatch, OpenGothic, Unity, and LumeRender revisions | Four source integrations compared | Post-tone placement independently corroborated; no hidden strength lerp found |
| 2026-07-31 | `independent-review` `/root/cmaa2_posttone_design_review` | Complete, read-only | Final integrated working diff | Color, timer, resource, CLI, dither, packaging, and output-order inspection | No unresolved P0-P2 source issue; global RGBA16F cost and fixed-SRGB assumption documented |
| 2026-07-31 | `integrate` `/root` | Complete | Final SHA-256 `177CB419...AFB0F` | Release build, 33/33 production tests, factory bundle, heading/diff checks, and exact-artifact runtime smoke pass | User exact-scene visual acceptance remains; timing evidence deliberately unclaimed |

## Risks and Escalation Triggers

- CS2 may use CMAA2 alongside proprietary sharpening, dynamic resolution,
  supersampling, or rendering choices that cannot be attributed to CMAA2 alone.
- A lower edge threshold can strengthen AA but also blur texture detail; maximum
  capacity means correct Ultra behavior, not indiscriminate full-frame blur.
- HDR/luma detection or color blending in the wrong transfer space can make edge
  response appear weak while introducing brightness shifts when corrected.
- Indirect-work capacity or counter bugs can drop dense edge workloads without
  obvious crashes and require adversarial fixtures.
- Runtime evidence requires exclusive UVSR/GPU control and a clean preflight.

Stop and ask the user if:

- evidence leaves two materially different visible quality policies with no
  defensible default;
- CS2 equivalence would require proprietary reconstruction or another AA method;
- a repair materially changes presentation sharpness outside detected edges;
- runtime verification needs closing or replacing a user-controlled process.

## Completion

- Final integrated commit: intentionally uncommitted unless the user asks to
  save it
- Verification summary: Release application and shaders built; 33/33 production
  tests plus the reduced factory-bundle contract pass; Title Case and diff checks
  pass; exact final artifact is responsive and closes cleanly
- Independent review: complete with no unresolved P0-P2 source finding
- Coming Soon/documentation update: ready-for-visual-review entry retained until
  integration
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: user exact-scene visual acceptance and a
  matched complete-frame cost comparison when overlay/capture and P0 blockers
  are absent; true per-sample Intel CMAA2+MSAA remains separate work
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/cmaa2-maximum-quality.md`
