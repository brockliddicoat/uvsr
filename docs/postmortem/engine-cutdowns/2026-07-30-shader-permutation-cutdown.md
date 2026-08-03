# Shader Permutation Cutdown — July 30, 2026

## Record Identity

This is the dated archive of UVSR's first shader-path retirement record through
commit `519306cbf7405939df73e5fc72aff48e79f9be4a`. Implementation and the local
candidate were recorded on July 29, 2026. The first durable report was committed
as `b63cda9639dedf820f1251aa390b162befa22dd7` on July 30 and extended later that
day with the emissive-transport and factory-experiment follow-up.

The historical counts and recommendations below describe that repository state,
not the later August 2 engine-core cleanup.

## Purpose

This is UVSR's record for the first shader paths that left the product. It
preserves:

- what was removed and what product behavior remained;
- the static axes that multiplied the shader catalog;
- measured or architectural evidence for the decision;
- the side effects that were explicitly avoided;
- the evidence required before restoration; and
- the next low-value candidates exposed by the smaller design.

The goal was not to minimize a number at any cost. It was to keep static
specialization only where it protected correctness, resource topology, or a
measured performance win.

## Count Definition

A permutation is one expansion of one non-comment line in `src/shaders.cfg` or
`src/shaders_production.cfg`. Every comma-separated value inside braces
multiplies that line. The catalog total is the sum of all expanded lines.

This count predicts shader compilation and packaging work. It is not the number
of source files, runtime-created PSOs, driver-internal variants, or instructions
executed by the GPU.

## Catalog History

| Repository State | Date | Production | Developer | Interpretation |
| --- | --- | ---: | ---: | --- |
| `4e0f6e9d9125` | 2026-07-13 | Not Recorded | 41 | The user's recollection of roughly 40 shaders is correct. |
| `58813cb94054` | 2026-07-20 | 98 | 16,220 | TAA developer experiments created a large Cartesian product before entering production. |
| `553e60491018` | 2026-07-20 | 2,214 | Not Recorded | Fixed visibility specialization moved the production catalog above 2,000. |
| `bcf4f7e4ade9` | 2026-07-29 | 3,120 | 13,107 | Baseline immediately before the first retirement batch. |
| `b63cda9639de` | 2026-07-30 | 311 | 2,809 | First retirement batch committed on `codex/prune-shader-paths`. |

The first batch removed 2,809 production permutations, a 90.03% reduction, and
10,298 developer permutations, a 78.57% reduction.

## Batch One: Runtime-Only Visibility and Focused TAA

### Result

| Catalog Area | Before | After | Removed | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Production Visibility | 2,297 | 64 | 2,233 | 97.21% |
| Developer Visibility | 2,297 | 64 | 2,233 | 97.21% |
| Production TAA | 771 | 195 | 576 | 74.71% |
| Developer TAA | 10,758 | 2,693 | 8,065 | 74.97% |
| Production Total | 3,120 | 311 | 2,809 | 90.03% |
| Developer Total | 13,107 | 2,809 | 10,298 | 78.57% |

The retained 64-permutation Visibility surface included its trace,
reconstruction, temporal, and application families. The retained 195
production TAA permutations were 192 blend variants, one resolve variant, and
two sharpening variants.

### Removed Paths and Takeaways

| Removed Path | Bloat Mechanism | Why It Left | Retained Boundary | Restoration Bar |
| --- | --- | --- | --- | --- |
| Visibility Fixed sample counts | Count, estimator, AO/GI ownership, bounce metadata, depth selection, scheduler, and output topology multiplied one another. Fully unrolled wrappers also produced large shader IR. | The isolated Fixed trace result was small and inconsistent, while Runtime preserved every 1–64 count. | Runtime kept a guarded general loop plus even and odd hot variants for the validated AO+GI topology. | Show a repeatable whole-frame gain over Runtime at equal samples on target adapters, including compile time, code size, registers, occupancy, and image identity. |
| Visibility Generic mode | A second general-purpose profile and UI selection duplicated the same count range and orchestration surface. | Runtime already provided the safe guarded fallback and the optimized parity contract. | Non-hot combinations used Runtime Guarded; no count was rounded or substituted. | Identify behavior Runtime cannot express without a new static resource or correctness contract. |
| Unpacked Offline noise | A separate asset, upload path, SRV, scheduler identity, tests, packaging, and shader axis accompanied one filter-specific objective. | It was coupled to a fixed Gaussian-plus-EMA objective and did not reduce sample count. | Independent Hash and first-party Toroidal Blue remained. All factory presets used Toroidal Blue. | Supply a current-filter asset, visible convergence win, asset provenance, and evidence that runtime-uniform delivery cannot express it. |
| Packed Offline noise | Fixed-eight delivery added wrapper shaders, an RGBA8 asset/resource path, bindings, profiles, and metadata cases. | It depended on the retired Fixed-eight topology and did not represent a distinct product estimator. | Toroidal Blue used one uniform Runtime branch and one resident rank field. | Prove a material whole-frame and convergence win after counting its complete resource and permutation cost. |
| TAA Stable Interior | One binary option doubled every motion, current reconstruction, history filter, rectification, reuse, fusion, and developer execution variant. It also required moment history, an extra binding, an extra output, debug storage, and recreation logic. | It was off in every preset and expanded resource topology for a dormant clarity policy. | Existing motion, reverse-Z, disocclusion, rectification, history-strength, and frame-horizon validation remained. | Demonstrate a visible improvement that cannot be obtained through a runtime scalar or an existing retained policy, then include memory, bandwidth, and full Cartesian-product cost. |
| TAA Per-Pixel RGB and Per-Pixel YCoCg rectification | Four rectification choices multiplied every other TAA axis. | Pair Tristimulus and Variance YCoCg already covered the useful simple-versus-variance policy boundary. | Pair was ID 0 and Variance was ID 1. | Show a repeatable artifact solved by neither retained policy and compare against adding one runtime-uniform policy selector. |

The removed Offline asset and license files remained recoverable from Git
history. No Donut source was changed.

### Preserved Diagnostics and Performance Paths

- **Closest Cross** remained motion-source ID 1 because the user observed less
  TAA motion blur with it during motion.
- **9x Bicubic** remained history-filter ID 3 and continued to execute the full
  nine-bilinear-tap Catmull-Rom diagnostic reconstruction.
- Shared-work reuse remained a production static axis. On the controlled Intel
  run, Off plus packed LDS measured 5.46 ms median and 6.38 ms worst case; On
  plus packed LDS measured 4.88 ms median and 5.91 ms worst case. That was a
  10.6% median and 7.4% worst-case improvement.
- Estimator, AO/GI ownership, bounce-output topology, packed-edge output, TAA
  motion source, TAA reconstruction, retained rectification, fusion, and
  sharpening remained static where their compiled work or resource contract was
  materially different.

### Compilation Workflow Finding

Large developer catalogs amplified transient worker failures and diagnostic
noise as well as deterministic shader failures. The first 2,809-permutation
developer build reported one failed pixel-TAA job, but compiling that exact
macro set directly with DXC succeeded. Two complete ShaderMake reruns then
compiled all 2,809 permutations successfully.

UVSR began requesting compact percentage progress while preserving ShaderMake's
existing default retry policy. Compact output made the actual error visible
instead of burying it among thousands of successful command lines. When one job
failed but its exact direct compile passed, the workflow was to rerun the
complete catalog once; a repeatable diagnostic remained a code failure, while a
clean rerun was evidence of a transient build-worker failure.

## Batch Two: Emissive Transport and Factory Experiment Profile

### Result

The normal production and developer core catalogs remained 311 and 2,809
permutations. Removing emissive transport saved zero static permutations because
its toggle and gain were runtime constant-buffer fields, not manifest axes. It
still removed a shader branch and multiply, two UI/settings fields, preset and
history-key state, one source-activity bit, one metadata bit, and their CPU/HLSL
API surface. The PBR deferred extension shrank from three constant-buffer
registers to two, and the visibility constants shrank by one register to nine
registers after the view.

Authored emission remained visible in forward, deferred, MSAA, and G-buffer
rendering. G-buffer emissive alpha still carried metalness. Only shadowed direct
diffuse and directly reflected diffuse environment lighting entered
first-bounce screen-space transport. This was an intentional secondary-lighting
change, not an attempt to preserve the old gain-four image.

The new factory-settings experiment profile was an opt-in build workflow, not a
replacement production catalog:

| Integrated Build Surface | Complete Production | Complete Developer | Factory Experiment |
| --- | ---: | ---: | ---: |
| UVSR core tasks | 311 | 2,809 | 51 |
| Bend tasks | 46 | 46 | 0 |
| SVSM tasks | 105 | 105 | 0 |
| Diagnostic CSM tasks | 54 | 54 | 0 |
| Total first-party tasks | 516 | 3,014 | 51 |
| Staged runtime blobs | 76 | 76 | 37 |

The experiment profile removed 465 of 516 production compile tasks, or 90.12%,
and 2,963 of 3,014 developer tasks, or 98.31%. It still compiled Donut's pinned
76-task framework catalog once in a fresh build tree. Runtime guards reset
shader-selecting state to factory defaults, omitted the three shadow pass
constructors, rejected incompatible command-line profiles, and disabled the
Settings drawers with a visible explanation. Production and developer builds
retained their complete settings and diagnostic surfaces.

One local ShaderMake verification reported 2.70 seconds for the 51-task
experiment core, 15.86 seconds for the 311-task production core, and 115.13
seconds for the 2,809-task developer core. These were illustrative local build
observations, not a controlled performance benchmark; the task and blob counts
were the durable contract.

### Distribution History

Distribution was a runtime-uniform exponent, not a permutation axis. The
shipped default began at `1.0`; `a5a75d50565a` changed it to `2.0` when
progressive radial strata adopted the `x^2` near-receiver concentration. It
remained `2.0`. No reachable or historical repository revision used `3.0` as
the product default; `3.0` appeared only in runtime-uniform tests and manual
values.

### Removal Takeaways

- A feature can add no permutations and still bloat shader ABI, constant
  buffers, metadata, source masks, resource planning, UI, presets, history
  identity, tests, and documentation. Count both catalog growth and end-to-end
  ownership.
- Do not keep a runtime toggle solely because it is cheaper than a static axis.
  A default-on policy with no retained diagnostic or experiment still creates
  two behavioral contracts and a restoration burden.
- Preserve visible material emission independently from diffuse transport.
  Deleting transport must not erase authored appearance or repurpose G-buffer
  channels that still carry unrelated material data.
- A minimal experiment catalog must fail closed. An abbreviated manifest
  without runtime topology locks merely turns UI choices into missing-shader
  crashes.
- Keep fast experiment profiles opt-in and isolated. Their counts are workflow
  evidence, not permission to weaken production or release verification.

### Restoration Bar

Restoring emissive transport requires a current product need, an energy and
exposure contract, image evidence against the retained direct/environment
sources, and a decision on whether one fixed policy can replace the removed
toggle/gain surface. Restoring any omitted experiment-profile shader requires
either making it part of factory startup topology or defining a separate
explicit profile with its own exact manifest and runtime lock.

## Branchless Shader Finding

Branchless code is not a permutation-reduction mechanism by itself. Replacing
an `if` with `lerp`, masks, or predication leaves every preprocessor axis and
manifest expansion intact. For expensive alternatives it can also execute both
sides, extend live ranges, raise register pressure, or retain the larger LDS
contract.

The useful distinction is branch coherence:

- Scheduler, radial exponent, and direct-versus-hierarchy depth selection were
  frame- or workload-uniform. They used uniform Runtime branches inside one
  trace family.
- Even and odd loop termination changed inner-loop structure on the validated
  hot topology. It remained two compact static variants.
- Consumer outputs, packed edges, and bindings that changed resource topology
  remained static.

Prefer a coherent uniform branch when both choices share bindings and output
topology. Prefer static specialization only when measurement or a genuine
resource contract justifies multiplying the catalog.

## Shader-Bloat Predictors

Before adding a compile-time option, answer these questions:

1. How many existing axes will it multiply? State the exact production and
   developer delta, not only the new option count.
2. Does the option change resources, bindings, render targets, thread-group
   shape, or output ownership? If not, a runtime-uniform value is the default.
3. Is it active in a factory preset, required diagnostic, or measured hot path?
   A hidden or default-off policy should not double the production matrix.
4. Does a wrapper shader add a real implementation, or only pin macros already
   expressible by the shared shader?
5. Does the option require a new asset, staging rule, upload path, lifetime
   state, benchmark identity, and test matrix? Count that maintenance surface
   alongside permutations.
6. Is the measured result for the isolated option, or bundled with fusion,
   formats, bindings, or another optimization?
7. Can one guarded implementation safely cover the uncommon topology while a
   narrow measured hot path stays specialized?
8. Will a branchless rewrite execute both expensive alternatives or force the
   larger LDS/register footprint? If so, it is not a free consolidation.
9. Is the option merely an experiment? Keep it out of the production catalog
   and give it an expiration decision.
10. Can the intended diagnostic be preserved with captures, a reference test,
    or one retained oracle instead of a complete cross product?
11. What is the compile-time and failure-recovery cost of the complete
    Cartesian product? Count human retry time and diagnostic noise as part of
    the feature's maintenance cost.

The warning pattern was a default-off option with no preset consumer that
changed resource topology and multiplied four or more unrelated axes.

## Ranked Candidates Recorded on July 30

Savings in this table were individual and non-additive. Removing two axes from
the same Cartesian product saved less than the sum of their isolated rows.

| Rank | Candidate | Production Save | Developer Save | Value At The Time | Side-Effect Risk | Recommendation At The Time |
| ---: | --- | ---: | ---: | --- | --- | --- |
| 1 | Retire Visibility-owned temporal accumulation | 3 | 3 | No UI or factory consumer; renderer TAA owned current temporal stability | Low product risk, but broad resource/lifetime cleanup | Audit CLI and benchmark reachability, then remove end to end. |
| 2 | Keep one developer TAA LDS layout | 0 | 1,536 | Alternative LDS diagnostics only | Low production risk; loses layout comparisons | Retain the measured/default layout after one adapter matrix. |
| 3 | Retire the developer TAA early-rejection axis | 0 | 1,248 | Developer comparison with inconsistent prior direction | Low production risk; loses attribution | Re-run one controlled comparison, then remove if still neutral. |
| 4 | Retire the developer fullscreen-pixel TAA path | 0 | 192 | Diagnostic execution oracle; prior pixel timing was about 8.33/9.16 ms versus roughly 4–6/5.5–6.6 ms for compute | Low production risk; moderate ABI/test cleanup | Preserve compute reference fixtures and remove the slower path. |
| 5 | Retire Sample Resurrection | 0 | 193 | Developer-only older-history experiment | No product preset impact; loses a research path | Require current visual evidence or archive and remove it. |
| 6 | Retire packed-edge Visibility profiles | 23 | 23 | Four selectable reconstruction policies and a fused variant | Medium image-quality risk at reduced resolution | Compare against retained guide-aware reconstruction before deciding. |
| 7 | Retire Cosine-Weighted Solid Angle | 14 | 14 | Selectable estimator, not used by a factory preset | Medium rendering-policy loss | Keep until an estimator image/performance comparison proves redundancy. |
| 8 | Remove the Off state of production TAA shared-work reuse | 96 | 1,152 | Adapter/fallback state for a measured optimization | High cross-adapter and occupancy risk | Do not remove without multi-adapter evidence; Intel data favored reuse. |
| 9 | Retire the Visibility Reference profile identity | 0 | 0 | Benchmark identity over the guarded Runtime implementation | Low shader-package impact; affected evidence workflows | Merge identities only after benchmark provenance was migrated. |

The next likely low-risk compile-time win was developer-only TAA cleanup. The
next likely production cleanup was Visibility-owned temporal accumulation. The
largest apparent production saving, forcing shared-work reuse, was deliberately
ranked low because it could create adapter-specific performance side effects.

## Historical Continuous Batch Template

The original continuous record reserved this structure for later batches:

### Batch Name and Date

- Base and candidate revisions:
- Product behavior retained:
- Production count before and after:
- Developer count before and after:
- Files, assets, and resources removed:
- Controlled performance and image evidence:
- Side effects checked:
- Restoration bar:
- New bloat predictors learned:
- Ranked next candidates:

## Historical Restoration Boundary

The source of truth for this snapshot is Git history at `519306c`, with the
Batch-One-only state at `b63cda9` and the detailed completed plan at
`docs/exec-plans/completed/shader-path-retirement-runtime-only.md`.

Do not copy only a shader wrapper from this state. Restore its CPU settings,
manifest axes, bindings, resources, UI, packaging, tests, and evidence contract
together, then remeasure against the current engine at equal image quality.
