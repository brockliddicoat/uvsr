# Bent-Normal AO

## Status

- State: repaired exact candidate built, tested, measured, and launched;
  reduced-evidence timing indicates the target is missed, publication-grade
  benchmark acceptance and user visual acceptance remain pending
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/bent-normal-ao` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\bent-normal-ao`
- Base commit: `bed36f951407e23d9e6b4a1a9462a96abc059e8a`
- Started: 2026-07-31
- Last updated: 2026-08-01
- Planned archive: `docs/exec-plans/completed/bent-normal-ao.md`

## Goal and Done Condition

Goal:

Add a safe optional directional AO output derived from UVSR's existing
32-sector visibility evidence, reconstruct its unnormalized world-space first
moment and confidence, and consume that single output for diffuse IBL,
specular IBL occlusion, or both. Preserve the current scalar AO estimator and
make the unchecked path retain its existing rendering and resource cost.

Done when:

- [x] The UI adds exactly `Bent Normal Output` and its conditional `Apply To`
      dropdown with the three user-requested values, with no other bent-normal
      controls.
- [x] Settings persistence, presets, planner and cache keys, history identity,
      fused and quality paths, MSAA, and production/runtime shader bundles all
      cover the feature.
- [x] The producer reuses the existing AO trace and stores one reusable signal:
      scalar visibility, unnormalized world-space first moment, and confidence.
- [x] Spatial and temporal reconstruction occur before normalization, reject
      invalid history, enforce the geometric-normal hemisphere, and use a
      finite surface-normal fallback.
- [x] Diffuse IBL samples along the reconstructed bent normal while retaining
      scalar AO; specular IBL intersects an AO-derived visibility cone with the
      rough reflection lobe without replacing the shading or reflection normal.
- [x] The disabled path has no extra shader work, allocations, bindings,
      histories, dispatches, or active permutations, and matches the baseline.
- [x] Full-precision storage passes the requested reference and runtime matrix;
      packing is adopted only if explicit range, angular-error, temporal, and
      bandwidth evidence passes.
- [x] Targeted and full automated checks, production shader packaging, runtime
      checks, an independent high-risk review, matched reduced-evidence GPU
      diagnostics, an exact candidate build, and an exact candidate launch are
      complete.
- [x] The enabled Bent temporal pass tiles its 8x8 group plus one-pixel halo in
      group-shared memory without changing reconstruction arithmetic or adding
      work to disabled variants.
- [x] The 32-sector resolver has exact mask fast paths and shares endpoint
      trigonometric evaluations while preserving scalar and directional output.
- [x] Specular cone/lobe overlap classifies non-partial cases in cosine space
      and reuses prepared cap data while preserving the bounded reference result.
- [x] The compact 2x2 fused reduced-resolution route is proven correct without
      full-resolution directional outputs and has recorded diagnostic
      performance evidence.
- [x] A TAA-on product-toggle protocol and a TAA-off temporal-matched
      decomposition each run three repeats per mode and report the full effect
      envelope, total renderer cost, clocks, and protocol identity. The matrix
      is explicitly reduced-evidence because every candidate carried limiter
      mask `0x4`.
- [x] Exact full-open and endpoint-reuse trace paths, duplicate-free Bent IBL
      application, a center-specialized temporal neighborhood, and an SoA
      group-shared directional tile pass reference, DXIL, runtime, and matched
      performance checks.
- [x] A durable report cross-references additional primary implementations,
      separates measured facts from architectural inference, and gives a
      documented diagnostic likelihood that the feature is meeting its intended
      functional and performance behavior.

## Scope

In scope:

- One optional near-field directional-visibility producer derived from the
  existing one-slice 32-sector AO visibility mask.
- Joint scalar, moment, and confidence spatial/temporal reconstruction.
- Diffuse IBL and specular IBL-occlusion consumers.
- Existing debug-dropdown integration for reconstructed direction, confidence,
  and applied specular-cone occlusion. Confidence returns to current one-slice
  support when history is rejected, covering history/disocclusion inspection
  without another Bent AO control or resource.
- CPU reference math, deterministic contracts, shader and runtime checks,
  source attribution, user documentation, and matched diagnostic performance
  evidence.

Non-goals:

- Replacing or retuning UVSR's scalar AO estimator.
- A second depth march, additional visibility samples, or another AO system.
- GI guiding, GI UI, dormant GI flags, GI resources, GI bindings, or GI shader
  branches.
- Editing Donut, enabling Vulkan or DX11, publishing, or integrating into
  canonical `main`.

Affected subsystems and paths:

- `src/screen_space_visibility*`
- `src/screen_space_indirect_composite*`
- `src/pbr_lighting*`, deferred-lighting and MSAA IBL paths
- `src/uvsr.cpp`, settings persistence, presets, cache/history identity, and
  performance planning
- `src/shaders.cfg`, `src/shaders_production.cfg`, runtime shader packaging,
  reference and source-contract tests
- `docs/screen-space-visibility.md`, `README.md`, and attribution records

Shared hotspots reserved for the coordinator:

- `README.md`, this execution plan, root build files, `src/shaders.cfg`,
  `src/shaders_production.cfg`, `src/uvsr.cpp`, every CPU/HLSL resource-layout
  contract, and all implementation and integration edits.

## Baseline

- Canonical repository/remote: `origin/main` at
  `bed36f951407e23d9e6b4a1a9462a96abc059e8a` after a live fetch.
- Local versus remote state: the task branch equals live `origin/main`; the
  original checkout's local `main` is separately ahead by two and behind by
  fourteen commits and is intentionally untouched.
- Verified source commit/build: renderer commit `a159625c` was Canonically
  verified before the documentation-only `4a5e498` and `bed36f9` commits.
  Recorded production executable SHA-256:
  `AF1EA0DE66B2DE4ED54BFE5889D64294CE2FBD899CB414F6E216F56951266AF8`.
- Baseline artifact:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\canonical-main-verify\build-canonical-main-production\bin\uvsr.exe`.
- GPU, scene, camera, resolution, and settings preset when relevant: Brock's
  current laptop, PBR Sponza Decorated, benchmark camera position 1, matched
  output resolution/display state, and fixed factory AO/IBL settings. Exact
  session identity will be recorded before measurement.
- Known pre-existing failures: none in the recorded canonical automated suite;
  runtime and benchmark evidence must be refreshed for this task.
- Overlap: open PRs #10 and #11 touch shared visibility helpers and degenerate
  test coverage. They remain external review inputs, not feature dependencies;
  no concurrent write ownership is assumed.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| [XeGTAO HLSL](https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli) and [license](https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/LICENSE) | MIT-licensed optional bent math, joint AO/bent filtering, final-filter normalization, and packing evidence at `a5b1686`; UVSR's end-to-end unnormalized history is independent | inspected | producer and tests |
| [Capsaicin SSGI](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/src/core/src/render_techniques/ssgi/ssgi.comp), [GI-1.0 paper](https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf), and [license](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/LICENSE) | Same-loop AO/bent/GI and spatiotemporal architecture at `914b915`; UVSR's raw world-space moment-plus-confidence contract is independently designed | inspected | producer contract |
| [GTAO/GTSO](https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf) | Published directional AO and specular-occlusion formulation; paper citation, not a software-license grant | inspected | CPU oracle and specular consumer |
| [Lumen documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine) | Behavioral reference only; no Unreal Engine source | inspected | future compatibility boundary |
| UVSR architecture audit | Exact trace, filter, temporal, IBL, MSAA, planner, and bundle seams | final frozen-source rescan complete; two focused follow-up findings integrated | independent review |
| Canonical matched baseline | Clean exact settings and thermal/process preflight | still blocked; same-executable bracket controls collected only as reduced-evidence diagnostics under the user's cold-gate waiver | benchmark comparison |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Provisional CPU setting contract: one disabled-by-default boolean and one
  three-value application enum. The enum is meaningful only while the boolean
  is enabled; no hidden per-consumer controls are permitted.
- GPU payload contract: full-precision `float4(momentWS.xyz, confidence)` plus
  full-precision physical cosine visibility. Both are reconstructed jointly;
  the existing estimator-specific scalar AO remains independent and unchanged.
  The moment remains unnormalized through trace, spatial reconstruction, and
  temporal history. Confidence is effective reconstruction support from valid
  slices and accepted history, not measured angular coverage or directional
  concentration.
- Disabled mode must select the existing shader/pipeline/resource plan exactly;
  enabled resources and variants are created lazily and only for active AO/IBL
  consumers or an existing debug-view request.
- Enabled bindings, formats, history versioning, and separate/fused topology
  are implemented and have passed compiled-shader, automated, and exact-runtime
  verification.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| BN-REF | `/root/reference_research` | shared read-only | `bed36f9` | none | public references | complete |
| BN-ARCH | `/root/repo_architecture` | shared read-only | `bed36f9` | none | repository state | complete |
| BN-VERIFY | `/root/verification_design` | shared test writer | `bed36f9` | seven disjoint test files | frozen implementation contract | complete |
| BN-IMPL | `/root` | task worktree | `bed36f9` | all task implementation and docs | BN-REF, BN-ARCH, BN-VERIFY | active |
| BN-OPT-TRACE | `/root/directional_math_final_audit` | shared task worktree | dirty candidate at `bed36f9` | `src/visibility_estimator_shared.h` only | existing producer contract | complete and audited |
| BN-OPT-TEMP | `/root/offpath_final_audit` | shared task worktree | dirty candidate at `bed36f9` | `src/screen_space_visibility_temporal_cs.hlsl` only | existing resource/binding contract | complete and audited |
| BN-OPT-SPEC | `/root/repo_architecture` | shared task worktree | dirty candidate at `bed36f9` | `src/image_based_lighting_shared.h` only | existing consumer contract | complete and audited |
| BN-OPT-FUSED | `/root` | task worktree | dirty candidate at `bed36f9` | fused shader, planner/runtime, tests, docs, catalogs | integrated optimization handoffs | complete and audited |
| BN-REPAIR-REF | `/root/new_reference_research` | shared read-only | dirty candidate at `bed36f9` | none | new primary sources | complete |
| BN-REPAIR-BENCH | `/root/repair_benchmark_audit` | shared read-only | dirty candidate at `bed36f9` | none | capture machinery and existing artifacts | complete |
| BN-REPAIR-SHADER | `/root/offpath_final_audit` | shared read-only | dirty candidate at `bed36f9` | none | current shaders, tests, and DXIL | complete; active-limiter reporting finding integrated |
| BN-REPAIR-IMPL | `/root` | task worktree | dirty candidate at `bed36f9` | all repair implementation, tests, scripts, report, and plan | three repair audits | complete; visual acceptance pending |

## Assignment Contracts

### Reference Math and Licensing Audit

- Owner/thread: `/root/reference_research`
- Branch/worktree: shared read-only task worktree
- Base commit/state: `bed36f9`; external sources pinned by the user
- Read scope: XeGTAO, Capsaicin SSGI, GI-1.0, GTAO/GTSO, Lumen behavioral docs
- Write scope: none
- No-touch scope: repository files, Git state, build trees, renderer processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: preserve scalar AO and one depth trace; return
  math suitable for a world-space unnormalized first moment and safe GTSO
  consumer, plus exact attribution obligations.
- Deliverable: distilled formulas, source locations, license conclusions,
  implementation pitfalls, and reference-test recommendations.
- Done when: the producer and specular consumer can be implemented without
  guessing source semantics or copying incompatible code.
- Required verification: primary-source links and pinned revisions.
- Allowed Git and external actions: read-only web and file inspection only.
- Stop and report if: licensing or formula provenance is ambiguous.
- Handoff revision/artifact: pinned reference and license/formula audit
- Handoff acknowledged by/on: `/root`, 2026-07-31

### UVSR Architecture Audit

- Owner/thread: `/root/repo_architecture`
- Branch/worktree: shared read-only task worktree
- Base commit/state: `bed36f9`
- Read scope: AO trace/filter/history, IBL/deferred/MSAA consumers, settings,
  presets, planner/cache keys, shader catalogs, and runtime packaging.
- Write scope: none
- No-touch scope: all files, Git state, build trees, renderer processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: identify the smallest enabled-only resource and
  variant topology while proving the disabled path can remain identical.
- Deliverable: exact files, symbols, bindings, path matrix, hazards, and a
  recommended integration order.
- Done when: CPU/HLSL binding and history contracts can be frozen.
- Required verification: cited file and line references from `bed36f9`.
- Allowed Git and external actions: read-only repository inspection only.
- Stop and report if: another active plan or dirty state owns an affected path.
- Handoff revision/artifact: frozen-source seam audit plus focused directional-math and Bent-Off cache audits
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Test, Runtime, and Benchmark Audit

- Owner/thread: `/root/verification_design`
- Branch/worktree: shared task worktree
- Base commit/state: `bed36f9`
- Read scope: reference/source-contract tests, shader-bundle tests, build scripts,
  runtime controls, performance capture, and thermal/process hygiene tools.
- Write scope: seven disjoint test files assigned by the coordinator
- No-touch scope: implementation, documentation, Git state, build trees, and
  renderer processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: map every requested correctness case and the
  zero-cost-off claim to concrete automated/runtime/performance evidence.
- Deliverable: test matrix, exact commands, benchmark protocol, and gaps.
- Done when: verification can distinguish mathematical correctness, resource
  correctness, visual behavior, and matched performance.
- Required verification: cited repository paths and existing harness behavior.
- Allowed Git and external actions: assigned test edits and read-only
  inspection only.
- Stop and report if: a requested measurement cannot be captured by existing
  infrastructure without new instrumentation.
- Handoff revision/artifact: BN-TESTS shared dirty test diff
- Handoff acknowledged by/on: `/root`, 2026-07-31

### Bn-Opt-Trace: Exact 32-Sector Resolver Fast Paths

- Owner/thread: `/root/directional_math_final_audit`
- Branch/worktree: shared task worktree
- Base commit/state: `bed36f9` plus the existing Bent-Normal AO dirty diff
- Read scope: `src/visibility_estimator_shared.h` and its reference tests
- Write scope: `src/visibility_estimator_shared.h` only
- No-touch scope: tests, temporal/IBL/fused shaders, CPU runtime, catalogs,
  documentation, Git/index, build trees, and renderer processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: scalar estimator and directional-moment
  contracts in the dirty candidate
- Interface/invariant contract: preserve the scalar estimator bit-for-bit where
  practical, preserve exact physical visibility and first-moment semantics,
  reuse no additional samples, and keep all new work under `ENABLE_BENT_AO`
  unless it is a provably exact simplification of existing scalar work
- Deliverable: exact full-open/full-closed and mask-iteration fast paths plus
  shared endpoint trigonometric data, with a distilled diff/risk handoff
- Done when: all mask topologies retain finite bounded scalar/directional output
  and the disabled source/DXIL contract is unchanged
- Required verification: scoped diff review and any lightweight static/compiler
  check that does not use the shared build tree
- Allowed Git and external actions: assigned file edit only; no Git history,
  build, runtime, benchmark, or publication actions
- Stop and report if: compiler intrinsics are backend-ambiguous, the scalar path
  would change numerically, or another writer changes the owned file
- Handoff revision/artifact: integrated exact fast paths and shared endpoints;
  independent bit-scan, scalar, moment, and mask-topology audit
- Handoff acknowledged by/on: `/root`, 2026-08-01

### Bn-Opt-Temp: Group-Shared Bent Temporal Tile

- Owner/thread: `/root/offpath_final_audit`
- Branch/worktree: shared task worktree
- Base commit/state: `bed36f9` plus the existing Bent-Normal AO dirty diff
- Read scope: temporal shader, shared constant/resource declarations, and source
  contract tests
- Write scope: `src/screen_space_visibility_temporal_cs.hlsl` only
- No-touch scope: bindings, CPU runtime, trace/IBL/fused shaders, tests, catalogs,
  documentation, Git/index, build trees, and renderer processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: enabled-only full-precision payload and the
  current 8x8 temporal dispatch contract
- Interface/invariant contract: use a 10x10 one-pixel-halo tile only in the
  `ENABLE_BENT_AO` variant; preserve tap coordinates, weights, history rejection,
  jitter reprojection, hemisphere enforcement, and finite fallback exactly;
  disabled compilation must not declare LDS or add barriers/loads
- Deliverable: tiled current-frame inputs with one unconditional group barrier
  before any early return and a distilled correctness/register-risk handoff
- Done when: all participating lanes initialize the tile safely at screen edges,
  no barrier is divergent, and reconstruction arithmetic is unchanged
- Required verification: scoped diff review and any lightweight static/compiler
  check that does not use the shared build tree
- Allowed Git and external actions: assigned file edit only; no Git history,
  build, runtime, benchmark, or publication actions
- Stop and report if: the dispatch group is not fixed at 8x8, a required tap lies
  beyond the one-pixel halo, or another writer changes the owned file
- Handoff revision/artifact: integrated 10x10 tile; independent lane, edge,
  barrier, disabled-variant, and diagnostic-copy audit
- Handoff acknowledged by/on: `/root`, 2026-08-01

### Bn-Opt-Spec: Prepared Specular Cone/Lobe Intersection

- Owner/thread: `/root/repo_architecture`
- Branch/worktree: shared task worktree
- Base commit/state: `bed36f9` plus the existing Bent-Normal AO dirty diff
- Read scope: `src/image_based_lighting_shared.h`, CPU reference math, and IBL
  source-contract tests
- Write scope: `src/image_based_lighting_shared.h` only
- No-touch scope: reference tests, trace/temporal/fused shaders, CPU runtime,
  catalogs, documentation, Git/index, build trees, and renderer processes
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: bounded three-cap GTSO-style consumer and
  confidence fallback contract
- Interface/invariant contract: classify disjoint and containment cases using
  cosine-space comparisons, prepare/reuse cap sine/radius/area terms, reserve
  inverse trig for true partial overlap, and preserve the shading/reflection
  normal plus existing finite/clamp behavior
- Deliverable: reusable prepared-cap math and reduced inverse-trig intersection
  evaluation with a distilled equivalence/risk handoff
- Done when: open, closed, disjoint, containment, symmetric, tangent, and partial
  cases remain bounded and agree with the current reference within its tolerance
- Required verification: scoped diff review and any lightweight static/compiler
  check that does not use the shared build tree
- Allowed Git and external actions: assigned file edit only; no Git history,
  build, runtime, benchmark, or publication actions
- Stop and report if: exact classification needs a new approximation, tangent
  behavior changes, or another writer changes the owned file
- Handoff revision/artifact: integrated prepared-cap math; independent CPU/HLSL
  parity, tangent-boundary, nonfinite, and inverse-trig audit
- Handoff acknowledged by/on: `/root`, 2026-08-01

## Integration Order

1. Freeze and automate the two benchmark protocols without changing product UI.
2. Add exact full-open and endpoint-reuse trace math plus deterministic tests.
3. Remove the redundant Bent IBL specular-occlusion evaluation in separate and
   fused paths while preserving the disabled shader path.
4. Specialize the temporal center tap, then replace the AoS group-shared Bent
   payload with an equal-size SoA layout.
5. Rebuild, inspect optimized DXIL, run the full automated and runtime matrix,
   collect three-repeat full and compact measurements, and compare total frame.
6. Complete an independent high-risk review, write the performance/correctness
   report, calculate the evidence-calibrated likelihood, and launch the exact
   candidate.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Scalar estimator unchanged | Source contract plus reference outputs | targeted estimator tests and disabled shader comparison | 33/33 CTest passed; scalar trace and temporal DXIL remain byte-identical at the recorded SHA-256 values |
| Directional moment correctness | Dense CPU oracle across all requested shapes | bent-visibility reference tests | Passed open, blocked, one-sided, symmetric, thin/disjoint, rotated, hemisphere, and finite-fallback reference cases |
| Safe reconstruction/history | Motion, camera cut, disocclusion, edge, and finite fallback checks | unit/source/runtime matrix | Passed history rejection, disocclusion, edge, confidence, jitter/TAA, and fallback contracts; exact TAA-on screenshots captured |
| Correct IBL consumers | Rotated HDR, normal-map, roughness, and combined-mode comparisons | reference and runtime captures | CPU/HLSL reference and source contracts passed; duplicate-free diffuse, specular, and combined runtime variants compiled and ran |
| Zero-cost disabled mode | identical resource plan/bindings/dispatches and matched timings | planner tests, bundle audit, baseline benchmark | Planner, bundle, and byte-identical disabled-DXIL contracts passed; same-executable timing controls are diagnostic because the loaded limiter was active |
| Complete path coverage | quality, fused, MSAA, persistence, presets, and bundles | focused tests plus CTest | 33/33 CTest passed, including production/runtime bundles and planner, UI, persistence, fused, quality, and MSAA contracts |
| Performance target | headline `taaon` complete-visibility-pipeline overhead at or below the [roughly 25 percent relative effect-cost increase reported by XeGTAO](https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/README.md) when correctness permits; Intel's absolute timings are not transferable, UVSR total frame is also reported, and the TAA-off temporal-matched protocol is decomposition only | three matched `taaon` runs per mode plus three temporal-matched decomposition runs per mode | Target missed in reduced-evidence diagnostics: 82.40-85.10% TAA-on effect overhead and 25.49-26.12% total-renderer overhead; active limiter mask `0x4` prevents benchmark acceptance |

For performance work, record:

- baseline and candidate commits plus dirty-diff identity;
- executable SHA-256, adapter/driver, OS, display, HAGS, power, priority, scene,
  camera, resolution, and full settings identity;
- warmup and at least 1,000 frames or 15 seconds per accepted sample;
- three clean-preflight repeats for the baseline and matched diffuse, specular,
  and combined modes in both the core and `taaon` protocols;
- total frame time, full AO-pipeline time, feature-stage time, memory, clocks,
  TFLOPS, temperatures, limiter state, and before/after position-1 controls;
- correctness captures and reasons for rejecting any run.

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-07-31 | Base the isolated branch on live `origin/main` at `bed36f9`. | The original `main` is clean but diverged; the live main contains the newest Canonically verified renderer plus documentation-only closeout commits. | all |
| 2026-07-31 | Keep all implementation writes with the coordinator and use parallel agents only for read-only audits until the resource interface is frozen. | AO history, IBL consumers, shader catalogs, and CPU/HLSL bindings are coupled shared hotspots. | all |
| 2026-07-31 | Start with full-precision unnormalized moment storage. | Packing is explicitly evidence-gated and normalization before reconstruction would bias direction. | producer and verification |
| 2026-07-31 | Independently implement the referenced equations and architecture without copying XeGTAO, Capsaicin, paper, or Unreal Engine source; retain pinned code licenses and paper attribution. | This preserves the user-stated source and licensing boundary while making UVSR's distinct payload and bounded specular approximation explicit. | implementation and docs |
| 2026-07-31 | Carry physical cosine visibility beside legacy scalar AO. | The unnormalized moment and specular-cone scalar must use the same measure; diffuse attenuation still uses unchanged legacy AO. | producer and consumers |
| 2026-07-31 | Keep full-precision `RGBA32_FLOAT` moment/confidence plus `R32_FLOAT` physical visibility. | Packing remains gated on range, angular-error, temporal, and bandwidth evidence. | resources and benchmark |
| 2026-07-31 | Use a temporal-on, TAA-off Bent-Off matched decomposition control. | Matching scalar temporal reconstruction isolates directional-payload overhead, but does not represent the complete optional feature cost. | performance capture |
| 2026-07-31 | Define confidence as bounded effective reconstruction support, not literal angular coverage. | UVSR's existing scheduler rotates slices continuously; preserving it avoids quantizing the scalar estimator or adding orientation state, while the confidence still distinguishes unsupported, current-only, and accumulated signals. | producer contract and future consumers |
| 2026-08-01 | Keep renderer TAA active with Bent Normal Output by adding previous-minus-current jitter only in the enabled Bent temporal shader. | G-buffer motion is de-jittered while every visibility history remains on the raw jittered grid. Reusing the retired AO Power and padding slots preserves the shared constant-buffer size, and compile-time gating preserves the unchecked shader instruction path. The legacy non-Bent visibility temporal mutex remains unchanged. | temporal reconstruction, TAA, and off-path contract |
| 2026-08-01 | Retire AO Power and retain AO Strength. | Strength is the direct, understandable amount control; Power duplicated response shaping, added command and shader axes, and was identity in every preset. | UI, settings, shaders, catalogs, and documentation |
| 2026-08-01 | Make the separate TAA-on matched protocol the headline AO-pipeline and total-frame comparison. | A temporal-on Bent-Off control still takes the legacy non-Bent mutex and would silently disable TAA. The `taaon` protocol keeps the explicit scalar-temporal setting off for every mode, so baseline and candidates retain the same renderer AA while enabled modes pay for Bent's required reconstruction. That complete effect-envelope delta, not the temporal-matched decomposition, is compared with Intel's reported relative overhead. | performance capture and total-frame evidence |
| 2026-08-01 | Keep the directional payload fully `RGBA32_FLOAT` plus `R32_FLOAT` during this optimization pass. | The user explicitly deferred packing until range, angular, temporal, and bandwidth tests pass; the requested work attacks redundant math and traffic without changing representation. | all optimization tasks |
| 2026-08-01 | Use clock normalization for functionality-first diagnostic comparisons and record, but do not gate on, the cold thermal preflight. | The user explicitly prioritized proving behavior and relative direction over publication-grade accuracy. Loaded limiter/headroom, process identity, clocks, and before/after controls remain recorded. | performance capture |
| 2026-08-01 | Report TAA-on product-toggle cost separately from a TAA-off temporal-matched decomposition. | Bent reconstruction is jitter-aware, while the unchanged scalar temporal path retains its TAA mutex. Combining those two questions into one percentage either removes TAA from the product comparison or compares unlike reconstruction work. | benchmark automation and report |
| 2026-08-01 | Keep all 48 timing captures as reduced-evidence diagnostics rather than accepted benchmarks. | Every candidate telemetry window carried limiter mask `0x4`. The user's cold-gate waiver authorized functionality-first clock normalization, but UVSR policy still requires an inactive loaded limiter for benchmark acceptance. | benchmark report and completion state |
| 2026-08-01 | Report 0.14% as the conservative diagnostic likelihood of meeting the intended full-resolution performance, alongside 94% functional confidence and a 70% measurement-confidence ceiling. | Three rotated brackets per mode put the normalized product overhead 82.40-85.10%, far outside the 25% target, but the active limiter and absent native counters prevent a publication-grade probability claim. | report and handoff |
| 2026-08-01 | Identify temporal/history bandwidth as the leading hypothesis, not a proven hardware bottleneck, and keep packing deferred. | The repaired trace delta fell to 0.104-0.109 ms and combined IBL no longer doubled work, while the raw temporal pass remained 0.369-0.372 ms. Compact 2x2 reduced footprint by 75% and temporal-matched overhead to 29.56%, but native counters and the required packing-quality gates are still absent. | architecture and future optimization |
| 2026-08-02 | Use Filament's 15-31% runtime band plus XeGTAO's roughly 25% reference to retain a 25% compact-path target and a 35% diagnostic guardrail. | Filament is the only new reference with a complete optional-path control. Capsaicin's roughly 3% producer point estimate is below same-code drift, lacks the fourth factorial corner, and cannot establish a total. Published paired same-evidence studies place producer work around 5-12% and filtering as high as roughly 25%. | cross-renderer report and future performance acceptance |
| 2026-08-02 | Classify the fresh Capsaicin install as functionally working but the sustained timing campaign as diagnostic only. | The exact executable completed full GI 1.2 runs in A, B, and native C, while four of eight sustained runs later truncated around `0x141` driver recoveries. The complete loaded windows were power-cap limited and began warm in P0, so clock normalization cannot make them matched. | reference install and performance report |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-31 | `/root` preflight | complete | `bed36f9` | clean task worktree; live fetch; worktrees, active plans, open PRs, and visible tasks inspected | freeze interfaces from audits |
| 2026-07-31 | `/root/reference_research` | complete | read-only pinned-source handoff | formula, license, and failure-mode audit | integrated into shared CPU/HLSL math |
| 2026-07-31 | `/root/verification_design` | complete | seven test files in shared task diff | scoped diff check; static bundle recount before final consumer-axis reduction | compile and run after integrated build |
| 2026-07-31 | `/root` pre-freeze verification | complete for the preceding source snapshot | `build-bent-normal`; 666 production DXIL tasks; prior dirty candidate executable | Release build and 33/33 CTest passed before the final source-only safety, catalog, and MSAA-contract fixes | rebuild and rerun after a cool preflight; do not reuse the stale executable as final evidence |
| 2026-07-31 | `/root` thermal preflight | rejected | `outputs/bent-ao-preflight.txt` and `outputs/bent-ao-preflight.csv` | zero renderer/build/capture processes, 8.127% adjusted CPU average, AC power, and inactive limiter passed; RTX 4090 hotspot 57.406 C exceeded the 55 C gate, P0 remained at 1815 MHz, and zero stable seconds accumulated | no runtime, benchmark, or launch until the user provides a fresh cool testing window and the complete preflight passes |
| 2026-07-31 | `/root` final source audits | complete; fixes integrated | shared dirty candidate | main architecture rescan found no defect; focused audits identified and fixed zero-confidence directional-denominator dilution and a dormant Apply-To plan-cache miss; reference/source contracts extended | cool-window build, tests, runtime matrix, matched benchmark, and launch |
| 2026-08-01 | `/root` Bent/TAA and UI follow-up | source and static verification complete; build/runtime pending | shared dirty candidate | removed Bent from the effective TAA mutex, added enabled-only jitter-aware visibility reprojection, renamed/reordered the two Bent controls, removed AO Power end to end, repaired confidence endpoints and benchmark matching, completed three independent final audits with no remaining findings, passed `git diff --check` and the 1,021-heading Title Case scan, and recounted 654 production, 2,864 developer, and 61 experiment shader tasks | cool-window build, CTest, runtime matrix, matched `taaon` benchmark, and launch |
| 2026-08-01 | `/root` functionality-first diagnostic | complete for pre-optimization candidate | `build-bent-normal/bin/uvsr.exe`, SHA-256 `8ED043D8A3244BE1777D26B7AA733BD00501AF1C110D9C570B9F31EA4DD3C1E8` | Release build, 33/33 CTest, matched TAA-on screenshots, and clock-normalized diagnostic captures; Bent effect overhead measured about 84-86 percent and total-frame overhead about 24-28 percent | implement the four requested optimizations and compare against these raw artifacts |
| 2026-08-01 | `/root` optimization follow-up | complete for the reviewed candidate | `build-bent-normal/bin/uvsr.exe`, SHA-256 `7DAF3A93AB70AE5305B18F51B832C026382E9A3436B01700212294445BA5FD6C`; diagnostic outputs `20260801T164248Z-optimized-final-full` and `20260801T164445Z-optimized-final-compact2x2`; final TAA-on captures under `outputs/bent-ao-review` | tiled Bent temporal neighborhood; exact mask/run endpoint reuse; cosine cap classification; exact compact fusion; repaired executed-resolution capture metadata; three independent reviews with all findings fixed; Release build; 33/33 CTest; direct DXC variants; matched clock-normalized full and bracketed compact captures; exact artifact recaptured and launched as PID 14236 with Bent and TAA enabled | temporal tiling was neutral on this GPU; diagnostic limiter bits and short aggregate-clock windows prevent publication-grade claims; user visual review remains |
| 2026-08-01 | `/root` repeat-work audit | complete; repairs active | read-only source, optimized-DXIL, timer, and artifact audit | found an unmatched XeGTAO comparison, duplicate Bent specular-occlusion evaluation, default-mode endpoint trig repeat, a general full-open path, a duplicate temporal center reconstruction, and a plausible float4 LDS bank-conflict pattern; found no duplicate depth march, producer, IBL sample, or effect timer | implement in dependency order and remeasure both protocols |
| 2026-08-01 | `/root` repaired candidate and final audit | complete; user visual acceptance pending | `build-bent-normal-repair/bin/uvsr.exe`, SHA-256 `AF876EF31B01F651F09843F52B19732ECCC4D52866A6A9E968D18CA72E571FFC`; [repair report](../../performance/bent-normal-ao-repair-report.md); full matrix `outputs/bent-ao-repair/20260801T202500Z-repair-final-full`; compact matrix `outputs/bent-ao-repair/20260801T204000Z-repair-final-compact2x2`; screenshots `outputs/bent-ao-review/20260801-repair-final` | isolated Release build; 33/33 CTest; optimized DXIL; byte-identical disabled variants; 30 full and 18 compact frame-aligned diagnostic captures; exact candidate launched with TAA and Bent enabled; independent high-risk audit found no shader/architecture defect and corrected the active-limiter acceptance claim | functional confidence 94%; conservative intended-performance likelihood 0.14%; temporal/history bandwidth is the leading unproven bottleneck; limiter-free benchmark and user visual acceptance remain |
| 2026-08-02 | `/root` cross-renderer runtime study | complete; diagnostic evidence | [portable report](../../../outputs/bent-normal-cross-renderer-report/report.html); Capsaicin v1.3 at `C:\rr\capsaicin-v1.3`; Filament v1.71.5 at `C:\rr\filament-v1.71.5` | fresh Capsaicin Release build; exact GI 1.2 A/B/native runs; earlier path-tracer capture; Filament TAA plus Bent screenshots; nine-run balanced Filament matrix; two independent Capsaicin timing audits; report schema/package/structural validation; 1,055-heading Title Case scan | Capsaicin cost unresolved after TDRs and power limiting; Filament range remains diagnostic; use native counters and a stable limiter-free window for acceptance |

## Risks and Escalation Triggers

- One rotating slice can yield an unbiased scalar estimate but an unstable
  direction; normalization before enough spatial/temporal evidence would
  amplify noise.
- A nominally fused implementation can accidentally impose resources or
  bindings on the disabled path, or omit quality/MSAA variants.
- Specular occlusion can become an incorrect normal substitution or over-darken
  smooth reflections if lobe/cone overlap and confidence fallback are wrong.
- Full-precision moment history has material bandwidth and residency cost; an
  unvalidated packed representation can introduce angular or temporal bias.
- Open PRs #10 and #11 may later cause semantic conflicts; they are not merged
  or modified under this local task.
- Publication-grade GPU benchmarking is blocked whenever the full machine,
  process, adapter, display, temperature, and loaded-control gates are not
  satisfied. An explicitly authorized diagnostic may bypass the cold gate only
  when its limiter and temperature caveats remain attached to every result.

Stop and ask the user if:

- correctness requires a second depth march, more samples, a scalar-AO change,
  or a visible quality/performance tradeoff outside the requested design;
- uncertain deletion, unrelated active work, external publication, or a
  material product choice becomes necessary;
- the user is actively controlling the renderer or does not make a matched
  benchmark window available.

## Completion

- Final integrated commit: pending; no commit requested
- Verification summary: final dirty candidate passes Release build, 33/33 CTest,
  direct shader compilation, source/reference/bundle contracts, document and
  diff checks, interactive TAA/Bent verification, and frame-aligned diagnostic
  full/compact GPU captures; active limiter mask `0x4` prevents calling the
  timing matrix an accepted benchmark
- Independent review: complete; no remaining trace, temporal, specular, fused,
  off-path, compact, probability, attribution, or licensing defect; its one
  final reporting finding downgraded the active-limiter captures from accepted
  to reduced-evidence diagnostics
- Coming Soon/documentation update: active entry and durable producer,
  consumer, optimization, resource, and diagnostic-performance contracts updated
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: limiter-free native-counter benchmark,
  packing decision after all quality/bandwidth gates, and future GI consumer
- Active ownership released: pending user visual review
- Archived to completed/abandoned path: pending
