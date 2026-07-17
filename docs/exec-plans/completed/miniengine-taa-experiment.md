# Miniengine Temporal Anti-Aliasing Experiment

This execution plan records a deliberately narrow successor to UVSR's retired
native-resolution analytical/reconstructive temporal anti-aliasing v1
experiment.

## Status

- State: complete; clean local Canon Contender, not Canonical verified
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/taa-miniengine-experiment` in `work/uvsr-taa-miniengine`
- Base commit: `a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`
- Canon Contender renderer commit:
  `6f84a9987ef7f6629d0ce98e4047f0a46bc033ec`
- Accepted implementation manifest SHA-256:
  `6332fc4ff1082ce25320e94c23b83ab802a9ad96d6d3cca57f6ee4aca786a398`
- User-verified predecessor runtime:
  `UVSR Renderer D3D12 (sharpenctl-a7e51b7-0338)`, TAA enabled with the
  plain resolve output at 1920x1080 in the benchmark Sponza view
- User-verified predecessor executable SHA-256:
  `55c6076af13bc3e79a070cd4700f649085f1fb99eaee2ece1c6b37a0c7cceca5`
- Clean contender runtime:
  `UVSR Renderer D3D12 (taacontender-6f84a99-0404)`
- Clean contender executable SHA-256:
  `22b215e594ee60e526ca8f3bc0dd93c85d7f1e2ed94350b099406668c72df146`
- Clean contender executable size: `2116608` bytes
- Started: 2026-07-17
- Last updated: 2026-07-17
- Archived to:
  `docs/exec-plans/completed/miniengine-taa-experiment.md`

## Goal and Done Condition

Goal: implement Microsoft's MiniEngine temporal anti-aliasing resolver in UVSR
as directly as the renderer contracts allow, prioritizing a fast, clear, and
working scene-linear path over feature breadth.

Done when:

- [x] The default deferred PBR path jitters one MiniEngine Halton sequence,
      produces the existing UVSR motion contract, resolves scene-linear HDR
      color through the MiniEngine temporal blend and sharpening passes, and
      resets history on invalidating changes.
- [ ] Fixed-camera and moving-camera smoke checks show stable output without
      crashes, NaNs, stale history after resize or mode changes, or dependence
      on AgX or another display tonemapper.
- [ ] Release renderer and reference tests pass, the document validator passes,
      and controlled baseline/candidate GPU timing plus captures are recorded.
- [x] The accepted implementation is committed as a clean local contender;
      no push, pull request, merge, or Canonical claim is made.

The implementation, fixed-camera smoke, user-driven moving-camera smoke,
TAA enable/disable control, Release build, 12/12 CTest suite, document
validation, exact-manifest review, independent rendering review, and local
contender checkpoint are complete. The user explicitly verified the predecessor
artifact carrying the same accepted 11-file implementation manifest and
directed its promotion to Canon Contender. The clean committed executable was
then rebuilt, tested, and passively smoke-checked with its embedded `6f84a99`
identity. Resize smoke and a thermally controlled matched baseline/candidate
sample window remain integration acceptance work. The feature-pass timer is
usable now; long uncapped total-frame samples are not treated as controlled
evidence.

## Scope

In scope:

- MiniEngine's eight-phase Halton 2/3 jitter sequence.
- MiniEngine's two-history confidence-weighted temporal blend, bilinear
  reprojection, plus-shaped nearest-depth velocity selection, RGB neighborhood
  clipping, reversible luminance compression during blending, and selectable
  plain resolve or adjustable sharpening output.
- The smallest UVSR adaptations required for NVRHI bindings, reverse-Z depth,
  arbitrary render dimensions, UVSR's unpacked RGBA16F motion target, and
  explicit invalid-motion handling.
- One enabled/default experiment control, a reference-default `0.5` sharpness
  control with a true no-sharpen resolve path, and concise timing and memory
  visibility.
- Focused coordinate, sequence, and reset-contract reference tests.

Non-goals:

- Restoring any retired NRA-RTAA v1 code, resurrection, reactive masks,
  material/object identity validation, thin-geometry classification, spatial
  fallback, quality profiles, Catmull-Rom history filtering, or debug modes.
- Coupling TAA to AgX, local tone mapping, exposure, grading, LUTs, or display
  transfer.
- Editing the pinned `donut/` dependency.
- Claiming production promotion before controlled temporal-quality acceptance.

Affected subsystems and paths:

- `src/taa_miniengine*`
- `src/uvsr.cpp`
- `src/pbr_gbuffer_ps.hlsl`
- `src/shaders.cfg`
- `tests/taa_miniengine_tests.cpp`
- `CMakeLists.txt`
- `README.md`

Shared hotspots reserved for the coordinator:

- `CMakeLists.txt`
- `README.md`
- `src/shaders.cfg`
- `src/uvsr.cpp`
- CPU/HLSL constant-buffer and motion contracts

## Baseline

- Canonical repository/remote: `brockliddicoat/uvsr`, live `origin/main`
  `9f2e69e832c81c105b059a83e4e72daa25b15783`
- Current Canonical verified renderer checkpoint:
  `0b4279839f9f24399dcecefca139af67fc70404f`; `9f2e69e` is its
  documentation-only verification record.
- Local versus remote state: the contender retains its exact accepted
  `a7e51b7` base and is 13 commits behind live `origin/main`. The later canonical
  line removes the tonemapper and overlaps `CMakeLists.txt`, `README.md`, and
  `src/uvsr.cpp`. Composition was deliberately deferred because it would create
  a different renderer artifact requiring fresh technical and product
  acceptance.
- Verified source commit/build: `a7e51b7`; the repository's completed
  visibility-sample-rotation plan records a Release renderer build, 11/11
  tests, document validation, and a live `main-a7e51b7` smoke check. It was the
  newest Canonical verified renderer when this experiment began.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU; PBR Sponza Decorated; benchmark preset
  `intel-pbr-sponza-courtyard-simplified-v1`; 1920x1080; deferred PBR defaults.
- Known pre-existing failures: none recorded for the verified checkpoint. The
  benchmark record notes a pinned GLFW input-automation fault, so passive
  capture is preferred.

## Dependencies and Interfaces

| Dependency/Task | Required Revision Or Decision | Status | Consumer |
| --- | --- | --- | --- |
| MiniEngine reference | Microsoft `DirectX-Graphics-Samples` commit `357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a` | Inspected | TAA CPU and shaders |
| Retired TAA postmortem | Follow the required successor order and omit recovery systems | Complete | Whole experiment |
| Tonemapper removal | Treat display tonemapping as absent and retain a scene-linear boundary | Active externally; no code dependency | TAA integration |
| Visibility helper PRs | No overlap with TAA-owned paths | Confirmed | Integration risk |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Motion remains current-to-previous XY in pixels, previous-minus-current
  device depth in Z, and validity in A.
- Jittered color and current depth use the current sample grid; temporal color
  history uses the output pixel grid; previous depth uses the previous sample
  grid and receives the explicit previous-minus-current jitter delta.
- TAA reads and writes scene-linear HDR before any display transform.
- No setting is serialized by this private experiment.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `taa` | `/root` | `codex/taa-miniengine-experiment` | `a7e51b7` | All task-owned paths | MiniEngine reference and postmortem | Complete |

## Assignment Contracts

### `taa`: Implement and Verify the Miniengine Resolver

- Owner/thread: `/root`
- Branch/worktree: `codex/taa-miniengine-experiment`,
  `work/uvsr-taa-miniengine`
- Base commit/state: clean `a7e51b7`
- Read scope: whole UVSR repository and official MiniEngine reference
- Write scope: paths listed under Affected Subsystems And Paths
- No-touch scope: `donut/`, AgX shader/pass implementation, visibility
  estimator math, assets, unrelated open-PR paths
- Build directory and runtime/GPU/resource lease: task-local `build/`; one UVSR
  process at a time
- Dependencies already integrated: canonical verified `a7e51b7`
- Interface/invariant contract: preserve the motion/sample-grid contract above;
  keep all display transforms downstream
- Deliverable: locally verified TAA experiment and evidence-backed next steps
- Done when: all plan done conditions are mapped to evidence
- Required verification: Release build, complete CTest suite, shader build,
  document checker self-test and full scan, `git diff --check`, passive fixed
  and moving smoke checks, captures, and controlled baseline/candidate timing
- Allowed Git and external actions: local branch/worktree operations only; no
  commit, push, pull request, or merge without follow-up authorization
- Stop and report if: correct operation would require editing `donut/`, coupling
  to the removed tonemapper, or restoring a retired NRA-RTAA recovery system
- Handoff revision/artifact: final task-local diff, build, timing record, and
  captures
- Handoff acknowledged by/on: pending

## Integration Order

1. Establish and measure the clean `a7e51b7` baseline.
2. Add the isolated MiniEngine TAA pass, shaders, and reference tests.
3. Integrate jitter, motion/resource eligibility, history resets, and UI.
4. Rebuild, test, inspect, smoke-test, capture, and benchmark.
5. Update experiment documentation and archive this plan with results.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command/Experiment | Result/Artifact |
| --- | --- | --- | --- |
| Canonical base is exact | First-parent and verification record | Git history plus completed plan inspection | `a7e51b7` resolved |
| Shader/CPU contracts compile | Release renderer and ShaderMake success | `cmake --build build --config Release --target uvsr` | Passed; all three TAA DXIL shaders and renderer built |
| Reference math is stable | Focused tests and full suite | TAA target plus `ctest --test-dir build -C Release --output-on-failure` | Passed 12/12 |
| Scene-linear integration works | Passive fixed and moving smoke checks | Required experiment launcher | Fixed and user-driven moving views rendered coherently; resize still pending |
| TAA is fast enough to iterate | Matched baseline/candidate frame and pass timing | 1920x1080 benchmark preset, fixed warmup/sample window | Blend 0.12 ms, sharpen 0.03 ms or resolve 0.04 ms; matched total-frame window pending thermal protocol |
| Clarity is preserved | Matched captures and temporal inspection | Static thin detail plus camera motion | Fixed and moved Sponza views remained clear; extended capture matrix pending |
| Documentation is conforming | Validator self-test and full scan | `tools/check_document_title_case.cmd` | Passed self-test and 214-item scan |

For performance work, record:

- baseline `a7e51b7` and the complete uncommitted candidate diff identity;
- NVIDIA GeForce RTX 4090 Laptop GPU, PBR Sponza Decorated, benchmark preset,
  1920x1080, deferred PBR defaults;
- fixed warmup and sample window/count;
- total frame time plus temporal blend and sharpen/resolve costs;
- crash/NaN/history-reset checks and matched captures;
- raw measurement artifacts under the task workspace, not the repository.

## Decisions

| Date/Time | Decision | Reasoning And Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-17 | Base the experiment on exact `a7e51b7`. | It is the newest first-parent checkpoint with trustworthy canonical renderer verification; later main commits are documentation-only without a newer renderer verification record. | All |
| 2026-07-17 | Port MiniEngine from Microsoft commit `357ade6`. | Pinning the inspected source makes copied behavior reviewable and avoids silently following later reference changes. | TAA |
| 2026-07-17 | Keep the experiment scene-linear and independent of AgX. | The tonemapper is being removed; TAA belongs before display transforms. MiniEngine's local reversible luminance compression is blend math, not a display tonemapper. | TAA |
| 2026-07-17 | Start with one resolver and one sharpness default. | The postmortem requires proving the base loop before profiles, reactive logic, resurrection, or fallback systems. | TAA |
| 2026-07-17 | Expose bounded `0.0`-to-`1.0` sharpness and a true no-sharpen path. | The control retains MiniEngine's `0.5` default and exact weight mapping. Disabled or zero sharpening selects MiniEngine's copied ResolveTAACS structure instead of approximating resolve through sharpening or changing temporal history. | TAA |
| 2026-07-17 | Retain MiniEngine's exact absolute `+0.001` depth allowance after reverse-Z linearization. | A first draft used a relative allowance. Source-to-source review found that it broadened acceptance without evidence, so the final shader copies the reference threshold exactly. | TAA |
| 2026-07-17 | Do not attribute long-run FPS drift to TAA or add a TAA-local frame cap. | Fifteen live samples showed zero private-memory, working-set, or handle growth while the uncapped renderer held the GPU at 100%, raised temperature from 80 to 85 degrees C, and reduced clocks. A later sample at 86 degrees C showed 1455 MHz versus 2025 MHz when cool. UVSR explicitly disables VSync, so thermal steady state is a benchmark-control problem. | Verification |
| 2026-07-17 | Preserve the accepted implementation as a local contender before composing it with the newer tonemapper-free canonical line. | The user verified the exact dirty artifact and requested Canon Contender promotion. Rebasing first would change overlapping renderer and build files, invalidate that artifact's acceptance, and repeat the prior postmortem's mistake of combining implementation with integration uncertainty. | Integration |

## Progress and Handoffs

| Date/Time | Task/Owner | Status | Revision/Artifact | Checks | Risks/Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-17 | `taa` `/root` | Complete | Base `a7e51b7`; MiniEngine `357ade6`; accepted manifest `6332fc4`; contender `6f84a99` | Release build, all three TAA DXIL shaders, 12/12 CTest, document validator, `git diff --check`, fixed and moved Sponza smoke, live sharpen-to-resolve switch, clean contender smoke, independent review | Compose onto live canonical in a successor plan, then repeat affected checks and product acceptance |

## Risks and Escalation Triggers

- UVSR uses reverse-Z device depth while MiniEngine compares forward linear
  depth; the adaptation must preserve the same nearest/farthest inequality and
  be tested directly.
- UVSR's motion buffer is produced only by the deferred PBR G-buffer contract;
  silently presenting TAA as correct in unsupported forward modes would be a
  product bug.
- Background pixels lack valid object motion. Invalid motion must select current
  color instead of reusing stale sky history.
- Two RGBA16F color histories plus two R32F depth histories cost 24 bytes per
  pixel before allocation alignment; the exact payload and pass timings must be
  visible.
- Sky/background pixels have no valid object motion in UVSR's deferred
  G-buffer. The minimal resolver correctly selects current color there rather
  than inventing history. A camera-velocity pass is a separate measured
  successor only if sky shimmer proves material.
- UVSR deliberately runs without VSync. Long uncapped observation heats the
  laptop GPU enough to reduce clocks and destabilize total FPS even with flat
  TAA pass cost and resource counts.

Stop and ask the user if:

- supporting a materially different renderer path would require a new camera
  velocity pass or another visible quality/performance tradeoff;
- measured speed or clarity requires choosing between a non-MiniEngine
  algorithmic change and the requested close transcription;
- external publication becomes necessary.

## Next Steps

1. Establish a thermal benchmark protocol before comparing total frame time:
   start baseline and candidate at the same temperature/power state, use the
   same warmup and sample counts, run only one visible UVSR process, and record
   clock, temperature, power, total frame time, blend time, and output time.
   For long visual inspection, use a renderer-level frame-cap or VSync
   experiment rather than changing TAA.
2. Run the postmortem capture matrix at 1920x1080: TAA off fixed, TAA on fixed,
   slow pan, translation/disocclusion around a foreground column, stop-and-
   settle, thin ivy/curtain detail, and sky silhouette. Add resize and mode-
   switch history-reset checks. Capture resolve-only plus sharpness `0.25`,
   `0.50`, and `1.00` at the same fixed pose.
3. Repeat the matrix with visibility temporal reconstruction off and on. Do not
   add cross-temporal fixes unless that comparison names a reproducible failure.
4. Start a successor integration plan from live `origin/main` `9f2e69e`,
   transplant contender commit `6f84a99`, and resolve only the build,
   scene-linear insertion, UI, and documentation conflicts created by the
   tonemapper sunset. The TAA pass itself has no display-stage dependency.
   Repeat the Release build, full suite, resize/mode smoke, independent review,
   and exact-artifact product acceptance after composition.
5. Add one mechanism at a time only after a measured failure: camera velocity
   for sky history, a different sharpness value for demonstrated blur, or a
   revised rejection contract for demonstrated ghosting. Do not restore
   profiles, reactive masks, resurrection, thin-feature classification,
   Catmull-Rom, or spatial fallback speculatively.

## Completion

- Final local implementation commit:
  `6f84a9987ef7f6629d0ce98e4047f0a46bc033ec`
- Final integrated commit: pending; this contender is not on canonical `main`
- Verification summary: clean-commit Release build, three TAA DXIL shader
  builds, 12/12 CTest, document validation, diff validation, fixed/moving
  smoke, live sharpen/resolve selection, clean contender smoke, 0.15 ms
  reference-sharpen feature timing, and 47.5 MiB 1080p logical history verified
- Independent review: passed with no promotion blocker; shader bindings,
  ping-pong history, reset and resize recreation, motion/jitter grids, reverse-Z
  rejection, scene-linear placement, and sharpen/resolve selection reviewed
- Coming Soon/documentation update: user-visible behavior is documented in the
  contender README; a separate roadmap entry is deferred to the successor that
  composes this branch with the current canonical README
- Pushed/PR/merged, or intentionally local: committed locally; not pushed,
  opened as a pull request, or merged
- Remaining experiments or follow-ups: compose onto `9f2e69e`, controlled
  capture/thermal matrix, resize/mode smoke, and fresh product acceptance of the
  exact integrated artifact
- Active ownership released: yes
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/miniengine-taa-experiment.md`
