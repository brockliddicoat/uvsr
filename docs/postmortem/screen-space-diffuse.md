# screen-space diffuse removal

screen-space ambient occlusion and diffuse global illumination were removed from
the official rewrite on 2026-09-06. the decision removes the Diffuse drawer,
its debug views, its settings, and every runtime consumer. it also removes the
extra deferred-lighting outputs, private sampling state, reconstruction, and
diagnostic cases that existed only to support those effects.

the product decision was that this implementation had become too complicated
to analyze or tune, with no clear best configuration and insufficient value
relative to the intended ray tracing direction. this is not a controlled claim
that all screen-space methods are slower than all ray traced methods. historical
AO timings below come from specific older implementations and hardware. they
cannot establish a current RT-versus-screen-space winner.

the purpose of this record is to make a future attempt smaller and more
testable. the useful research is preserved even where the implementation failed.
method changes, experiments, limitations, and recovery boundaries are recorded;
name-only changes are omitted from this account.

## scope and evidence

the removal starts from the intentionally dirty checkout
`codex/pre-nrd-features-20260905`, based on
`c4dbcb31c2a241f5864cb08ae2f80551ef2615fb`. that commit alone is not the
pre-removal source. the dirty official rewrite had already removed denoising,
TAA, MSAA, and several older framework layers. those removals were not repeated
or credited to this cut.

the exact changed-file preimages are preserved in
[`archive/screen-space-diffuse-before-source.zip`](archive/screen-space-diffuse-before-source.zip).
its internal provenance lists source paths, hashes, the base revision, and the
pre-existing index fingerprint. it is recovery evidence, not a build input or
a drop-in restoration. the [publication manifest](publication-manifest.tsv)
binds every archived file by SHA-256 and provenance. the full pre-removal
working-tree and index diffs remain in the local execution evidence.

the most detailed historical evidence is retained alongside this report:

| record | what it preserves |
| --- | --- |
| [optimization ledger](evidence/screen-space-diffuse/ao-optimization-ledger.md) | all 414 recorded candidate IDs, source links, measurements, classifications, rejections, and unbuilt ideas. these are 414 inventory items, not 414 successful experiments. later retirement tables override older rows that describe an implementation as current. |
| [completed optimization plan](evidence/screen-space-diffuse/ao-performance-optimization.md) | the investigation sequence, controlled comparisons, conditions, and remaining acceptance work. |
| [DXIL evidence](evidence/screen-space-diffuse/visibility-dxil-evidence.md) | historical compiler and shader-cost evidence, distinct from rendered quality. |
| [historical PBR foundation](evidence/screen-space-diffuse/pbr-foundation.md) | the original material, energy, source-radiance, and composition reasoning. current ownership is in [architecture](../architecture.md). |
| [shader-path retirements](shader-path-retirements.md) | fixed versus runtime loops, noise delivery, permutation reduction, and the criteria for retaining an optimization. |
| [sample-rotation postmortem](visibility-sample-rotation-v1.md) | the negative visual result of rotating low-resolution receiver phases. |
| [estimator research handoff](evidence/screen-space-diffuse/visibility-research-handoff.md) | the separate Horizon, bent-normal, and cosine-estimator investigation, numerical findings, and historical artifact identity. |
| [HZB research record](evidence/screen-space-diffuse/visibility-hzb-research.md) | the separate SSRT3-inspired depth/mip candidate, its assumptions, source pins, and unresolved validation. |
| [denoising replacement](denoising-replacement.md) | a separate abandoned denoiser lineage. its historical build and image claims do not certify this renderer. |
| [stage-two decisions](engine-cutdowns/2026-08-23-stage-two-cutdown-decisions.md) | why the later official implementation returned to one current-frame trace and automatic reconstruction. |

archived records retain their original text, labels, and links. some links name
retired files or local artifacts. use their recorded Git revision or the
publication manifest to recover those sources. none of these records is an
instruction to restore old controls or change current ownership.

## what remained at the removal boundary

the immediate predecessor was already much narrower than the earlier research
engine. one current-frame trace produced scalar ambient visibility and
one-bounce diffuse irradiance from the raster depth/material surface. AO and GI
shared angular sampling and source acquisition. a reduced trace used a fixed
depth/normal-guided reconstruction before composition. there was no remaining
private temporal filter, recursive bounce loop, depth hierarchy, adaptive
planner, or fused AO-only path in this official endpoint.

the controls still exposed three estimators, three resolutions, sample counts
through an exact 1..64 range, radius, thickness, distribution,
private noise choices, separate AO/GI enables and gains, and separate 16/32-bit
output choices. presets selected combinations of those values. this was a
large analysis surface even after earlier cutdowns. a scene could look different
because of estimator measure, representation error, sample placement, output
resolution, reconstruction, or gain. another option did not identify which
cause was responsible.

the deferred pass also maintained a special split: base lighting for later
composition and outgoing direct-diffuse/emissive source radiance for GI. the
screen-space result had its own allocation, shader, binding, dispatch, validity,
debug, timing, reset, settings, snapshot, and diagnostic paths. these costs
extended well beyond the trace shader.

## method lineage

### approximate raster AO before diffuse transport

`1bb1b989` introduced the PBR foundation with Donut's `SsaoPass` on 2026-07-12.
`c174264f` repaired Bistro material ORM/SSAO behavior. material occlusion and
screen-space AO modulated approximate indirect diffuse environment lighting.
this was not a screen-space transport solver and did not reconstruct bounced
light from visible surfaces.

the useful invariant was the energy boundary: ambient occlusion should not
darken direct light or emissive energy. authored material occlusion, a geometric
visibility estimate, and indirect transport are different inputs. that boundary
survives the removal. the early integration also showed why material decoding
and scene content must be verified before judging an AO method from its image.

### finite-thickness visibility bitmasks and one-bounce GI

`65aca78d` replaced the framework AO path with first-party full-resolution
screen-space visibility on 2026-07-12. the algorithm followed hemisphere slices
with a register-local 32-sector mask. samples claimed angular intervals from
near to far. finite thickness allowed light behind a surface where a single
opaque horizon would block the entire region beyond it. the same traversal
supported AO and one-bounce diffuse irradiance from shadowed direct/emissive
source radiance.

the first version included temporal AO/GI outputs, bilateral spatial filtering,
and an optional five-level AO-only depth hierarchy. it introduced 4,849 lines
in the recorded change. a temporary benchmark scene was removed immediately in
`0bc4a306`; it was experimental scaffolding, not a retained product scene.
`e833e7df` refined the radial mask and telemetry, including sample accounting:
the paper's 16 samples per side meant 32 total in that implementation.

the important lessons were semantic. sectors encode an angular approximation,
not a set of interchangeable rays. sample count must state whether it is per
side, per slice, or total. first-claim ownership determines which surface
contributes radiance when projected intervals overlap. finite thickness repairs
one limitation of an opaque horizon but introduces a new geometric assumption;
it cannot recover hidden layers or off-screen geometry.

### finite multi-bounce propagation

`cc3d0f23` expanded the method to one through four finite bounces on 2026-07-13.
ping-pong frontier textures transported the next bounce while another output
accumulated irradiance. each bounce applied receiver diffuse throughput.
later bounces reduced their sample budget to a floor of eight, with contribution
cutoffs and early exits intended to avoid tracing negligible energy.

this was the largest algorithmic expansion in the lineage, adding 2,213 lines
and removing 744 in that change. it made source-radiance ownership and energy
accounting harder to inspect. an error in the first visible-surface estimate
could be reinjected into later bounces. extra frontiers, scratch resources,
termination rules, and sample schedules created coupled tuning decisions.
screen-space incompleteness did not disappear because the loop ran again.

the later return to one bounce was a design reduction, not a proof that
multi-bounce transport is unimportant. a future system should obtain additional
bounces from a representation that can retain world-space information, such as
the planned RT or surfel work, before adding screen-space feedback again.

### filtering removal and estimator reference work

`ab6df6f6` removed visibility-owned denoising and temporal stages, deleting 1,119
lines and returning to current-frame trace/composition. filtering later returned
in a different form. this was an early indication that temporal state was not
a free quality improvement: it brought guide ownership, reset, disocclusion,
and reconstruction obligations alongside the estimator itself.

`7a54c098` and `0b7ad45c` moved estimator math into shared C++/HLSL definitions
and added uniform solid-angle sectors. `1eebbe70` completed the joint-cosine
CDF and normalization reference. these were substantive changes in integration
measure, not alternative labels for the same calculation.

projected-angle coverage, uniform solid-angle coverage, and receiver-cosine
weighted visibility answer different integrals. a darker result is not
automatically more physically accurate. the useful outcome was an executable
CPU reference and explicit normalization. future optimizations must identify
which measure they preserve before comparing instruction counts or images.

### adaptive scheduling, reduced resolution, and reconstruction

`a5a75d50` rebuilt the visibility system around reduced resolution, adaptive
sparse scheduling and history, reconstruction/filtering, and temporal reuse.
the broader change added 3,287 lines and removed 9,980. fewer traced samples
could reduce arithmetic, but the scheduler, confidence/history state, neighbor
reconstruction, and guide reads increased the cost of explaining an image.

`4c09b36a` added profiling and safe upsampling. `725cbfde` soon removed
refinement slices and sample profiling. `d2a8eccc` replaced ring/band-prone
sampling with first-party toroidal blue sequencing. `537e4c42` added a
filter-adapted rank asset, later retired. the lesson was not that noise choice
is irrelevant. sample placement, receiver resolution, spatial reconstruction,
and temporal phase interact, so isolated still images and a single aggregate
error number cannot select a whole reconstruction system.

### extensive optimization and same-engine controls

`b53ac848` introduced a large AO benchmark/profile matrix, adding 15,261 lines.
it explored fixed/generic loops, diagnostics, radius clamps, group shapes,
packed edges, pass fusion, feature-off variants, and benchmark orchestration.
`96939e13` added the Activision PS4 GTAO approximation and XeGTAO source-port
controls. those controls were different estimators with their own reconstruction
assumptions; they were not exact substitutions for the bitmask integral.

the investigation produced useful measurements, but its experiment machinery
became part of the maintenance problem. profiles sometimes bundled several
changes. comparing such a profile only to the canonical baseline could credit
a neutral or harmful component with the gain of another component. the ledger's
correct-baseline tables are therefore more useful than a sorted list of all
timings.

`16d8fc88` removed failed comparison implementations, deleting 9,275 lines.
`553e6049` integrated the remaining matrix. `b63cda96` then removed fixed,
generic, and offline-noise runtime families. production visibility permutations
fell from 2,297 to 64, a 97.21% reduction; total production permutations fell
from 3,120 to 311. one guarded 1..64 runtime loop remained. fixed-loop gains
were too small or inconsistent to justify the multiplied code and validation
surface outside a demonstrated winning use.

### the official current-frame endpoint

`c4dbcb31` completed the stage-two endpoint on 2026-08-25. it retained one trace
shared by AO and one-bounce GI, exact 1..64 sample counts, three estimators, and
automatic four-tap guide-aware reconstruction for reduced resolution. private
temporal history, recursive bounces, the hierarchy, planner, and fused AO-only
routes were gone. optional hit-distance outputs still served denoisers at that
commit; the dirty official rewrite removed them with their last consumer before
this diffuse cut.

this endpoint was easier to follow but still exposed too many unresolved
method/quality choices for the value the user obtained. the present removal
finishes that decision by removing the supporting architecture as well as the
visible controls.

## optimization outcomes and failed experiments

the [414-item ledger](evidence/screen-space-diffuse/ao-optimization-ledger.md)
is the exhaustive item-level record. it preserves every row rather than only
the successful shortlist. its categories cover measurement, fast math,
precision, dispatch/resource policy, specialization, noise, depth acquisition,
trace/masks, edge data, spatial and temporal reconstruction, fusion,
multi-bounce GI, cache/LDS proposals, Activision/XeGTAO comparisons, and additional
primary-source candidates. “forecast”, “source inspection”, “unbuilt”, and
“measured” are different evidence classes.

the following summary records the consequential outcomes. the controlled Intel
scorecard used Sponza Decorated at 1920x1080, 120 warmup frames and 480 measured
frames per profile, with repeats. later tables used 600 measured frames and a
different recorded source state. percentages from separate tables must not be
combined into an expected speedup.

| experiment | recorded result | lesson or disposition |
| --- | --- | --- |
| exact fixed eight samples plus fused resolve/apply | 1.9453 versus 2.5543 ms, 23.85% faster in the controlled scorecard; earlier Intel tables reported 20.6–22.4% | a strong AO-only combination under its exact consumer contract. not a universal GI improvement. the specialized family was later retired. |
| exact fused resolve/apply | 2.0179 versus 2.5543 ms, 21.00% faster; earlier tables reported 17.7–18.8% | removing a full-resolution pass and intermediate traffic can matter more than shaving trace arithmetic. fusion must preserve material AO, environment application, and debug semantics. |
| fixed eight samples alone | 2.4903 versus 2.5543 ms, 2.51% faster; earlier Intel results 1.9–4.7% | a modest specialization benefit did not justify retaining a large fixed/generic/runtime family tree. |
| fused depth-normal reconstruction and apply | 1.9970 versus 2.5543 ms, 21.82% faster | algorithmic reconstruction change with a promising timing; edge and motion acceptance remained required. |
| Intel depth, depth-normal, slope, and leakage reconstruction | 9.44–12.04% faster than the scorecard reference | depth-guided was fastest there; depth-plus-normal was safer at discontinuities. a timing win alone did not settle visual behavior. |
| packed-edge 2x2 reconstruction | approximately 9.44–12.04% faster in the relevant reconstruction family | a local reuse opportunity, tied to the precise sample and guide layout. |
| packed-edge 4x4 reconstruction | about 4.70 ms versus about 2.71 ms, roughly 73% slower in its recorded comparison | extra packing/neighbor work outweighed the intended bandwidth savings. do not infer a win from fewer nominal edge bytes. |
| final GI RGBA16F instead of RGBA32F | 8.39% improvement in its recorded test | precision and traffic are useful candidates when dynamic range and visual error are controlled. this is not proof that every intermediate can be half precision. |
| depth hierarchy R16F | 3.88% improvement in its recorded test | smaller hierarchy reads helped locally; precision and hierarchy-build cost must still be included. |
| 16x8 and 8x16 groups | 0.02% slower and 0.09% faster than fixed eight | effectively neutral. comparison to the unspecialized reference would incorrectly credit the bundled fixed-loop change. |
| 32/64/128 radius clamps | scorecard clamp profiles were 0.24–0.83% slower than fixed eight at radius three | rejected there. radius-specific shortcuts need an actual workload distribution and an unchanged estimator contract. |
| conservative filter/numerical algebra | 0.27% slower than reference | no demonstrated performance reason to promote it. conservative spelling alone did not make the GPU implementation cheaper. |
| duplicate-rejection and full-mask-exit controls | feature-off repeats were 0.80% and 0.52% faster than fixed eight, inside observed drift | no universal gain established. the control variants were removed; internal shortcuts require measured justification. |
| adaptive feedback and sparse scheduling | inconsistent results and worse tail behavior in recorded comparisons | mean trace cost does not capture scheduler overhead, recovery, and p95 stability. retirement removed a coupled state machine. |
| temporal buffer/layout changes | performance deltas changed sign across compared conditions | no stable blanket winner. history bandwidth and consumer behavior must be measured together. |
| offline packed spacetime noise | 3.48% faster in one controlled scorecard, with less stable earlier retries | a small delivery win did not justify retaining offline/unpacked/packed variants and assets as a permanent family. |
| toroidal blue sample sequencing | addressed visible rings/bands associated with prior sampling | distribution must be judged after the intended filter and in motion; an attractive noise spectrum is not a complete rendered-quality result. |
| filter-adapted rank asset | implemented and later retired with offline sampling paths | assets, generation, delivery, and shader variants were part of its cost. the archived rows retain the research. |
| Activision scheduling in the UVSR comparison | 2.4385 versus 2.5543 ms, 4.54% faster in the scorecard | historical coupled scheduler result, later removed with the PS4 comparison. it was not an independent exact optimization of every UVSR path. |
| UVSR Horizon GTAO comparison | 16.16% faster than the scorecard reference | a different estimator and useful algorithmic control; not an exact replacement for finite-thickness bitmask transport. |
| PS4 scalar approximation | 23.24% slower than the canonical reference | rejected in this engine and workload. another platform's published timing cannot decide this comparison. |
| PS4 packed-gather approximation | 3.74% faster than PS4 scalar, but still about 18.63% slower than canonical reference | local improvement of a losing alternative did not make it the preferred implementation. |
| XeGTAO source-port variants | every tested variant was slower than the canonical reference in the recorded comparison | preserve as same-engine controls and source research, not as a claim that the upstream method is generally slow. integration and reconstruction assumptions matter. |
| finite multi-bounce frontier scheduling and contribution termination | implemented, then removed in the return to one bounce | reduced later-bounce work did not resolve visibility incompleteness, feedback error, extra resources, or the analysis burden. |
| mixed/aggressive presets that never compiled | no completed runtime result | do not report forecast performance as measured failure or success. |
| AO-aware deferred application, combined spatial/temporal passes, LDS/coarsening, and mixed precision | deferred or unbuilt ideas in the ledger | these remain hypotheses. no runtime benefit is established by their inclusion in a plan. |

the [engine-core cleanup](engine-cutdowns/2026-08-02-engine-core-cleanup.md)
and [shader cutdown](engine-cutdowns/2026-07-30-shader-permutation-cutdown.md)
preserve the larger removal inventories. an older engine-core report also
recorded disposition of the completed AO plan, abandoned Bend, SVSM, diagnostic
CSM, and emissive plans, plus deleted DXIL/schema evidence. the AO plan and DXIL
record are now preserved directly here. the other named plans were separate
experiments; their mention does not make them part of the removed diffuse
implementation.

## separate investigations that must not be merged into the official history

### rotating low-resolution receiver phases

the sample-rotation experiment changed which pixel in a 2x2 full-resolution
region supplied the half-resolution receiver each frame. it preserved existing
resource/ABI contracts and was default off. the user saw no improvement and
reported noisier or worse output, including after history had settled.

one low-resolution history value was being asked to represent four changing
receiver identities. rotating the sampling phase did not provide storage for
four depth/normal/material histories. reprojection, bilateral rejection, and
normalization could not reliably combine incompatible receivers. this is a
representation problem before it is a sample-count problem.

a future attempt would need explicit receiver identity and disocclusion proof,
or a reconstruction formulation that keeps those identities distinct. do not
restore the toggle merely because the old implementation was ABI-compatible.
the negative visual result remains decisive.

### Horizon, bent normals, and cosine references

a preserved research checkout at detached `bed36f9` explored a mask-free
Horizon skyline estimator, bent-normal output, and multiple cosine treatments.
it was a dirty, separate candidate, not an ancestor to import into this cut.
the Horizon path kept a rising horizon per side and compiled out bitmask sector
construction. optional thickness and bent-normal consumers introduced distinct
assumptions. its old 722-shader build, 33/33 tests, and DX12 smoke are historical
evidence only.

the full receiver-cosine CDF served as a mathematical reference. an alternative
assigned cosine mass when a directional sector was first claimed. another
integrated cosine over the completed visible runs of the final mask. the former
depended on traversal order; the latter's scalar AO did not. GI radiance
ownership still used first-claim surface ownership, so scalar AO order
independence did not prove GI order independence.

across 15 deterministic fixtures averaged over sector phases, comparison to a
dense physical cosine AO reference produced:

| estimator | signed bias | root mean square error |
| --- | ---: | ---: |
| cosine first claim | -0.054892 | 0.084815 |
| cosine over final visibility | -0.020839 | 0.037989 |
| directional visibility | -0.041236 | 0.080780 |

first-claim cosine was darker than the dense reference in 13 of 15 fixtures and
darker than final visibility in 12 of 15. the user's preference for its richer
image was real, but the numerical result supported the concern that some of
that richness was extra darkening. this does not establish a universal visual
winner. it identifies a measurable bias and a traversal-order dependency that
must be tested at matched average visibility.

the one-measured-frame automated benchmark in that research timed out while
draining GPU timer sets. it was neither a valid timing result nor evidence of an
estimator failure. retain that distinction when using the handoff.

### SSRT3-inspired mip sampling and shared HZB

the separate dirty `fbc8` candidate explored a shared RG32F min/max depth
hierarchy and SSRT3-inspired lossy sampling mips. its first-trace sequence used
LOD `0,1,1,2,2,3,3,4` for depth, normal, and radiance together. those ordinary
averaged mips were not conservative visibility bounds. they could blur thin
geometry or combine neighboring surfaces.

the hierarchy used conservative min/max reduction, explicit empty intervals,
and odd-dimension handling. all estimators could reject empty cells. only the
Horizon path used the additional world-radius rejection because that integral
already had a world-distance cutoff. imposing the same rejection on a mask
estimator with projected reach would silently change its method.

the candidate also explored directional environment fallback and screen-space
angular shadow work. the latter is a separate direct-light experiment, not
diffuse transport. a depth hierarchy shared by real consumers may amortize its
cost, but a serial mip builder and divergent traversal can cost more than the
loads they skip. the record explicitly does not claim a fused LDS/SPD builder,
tile compaction, or asynchronous scheduling as completed optimizations.

CPU comparisons included dense leaf DDA, empty/invalid depth, odd sizes,
reciprocal depth, shifted origins, long rays, and 2,000 seeded random cases.
they were double-precision reference checks, not GPU execution proof. build
history included compile failures and final shader corrections without a fresh
complete verification. this candidate must remain unaccepted research.

environment fallback supplies missing radiance, not hidden/off-screen blockers.
neither an averaged mip nor a probe can prove local geometric visibility that
is absent from the depth buffer. this distinction governs any future hybrid.

## the removal and the remaining lighting foundation

17 dedicated runtime files were deleted: the screen-space trace, filter,
composite, settings/defaults and constant-buffer owners; radial-mask and shared
estimator/projection mirrors; and diffuse-only scheduler, pipeline, and timing
contracts. `uvsr_ao_gi.cpp` became `uvsr_lighting_history.cpp` because its
remaining work is environment preparation and retained history invalidation.

the frame no longer constructs, creates, dispatches, deactivates, or times a
screen-space diffuse pass. it no longer resolves its noise, publishes its raw
outputs, owns its sampling phase, or chooses a diffuse debug presentation.
the old `BaseLighting` and `DirectDiffuseRadiance` targets and their topology
flags are gone. deferred PBR now has one output route directly to `HdrColor`.
the corresponding source-radiance shader variant and composite bindings are
gone. constant-buffer layout assertions and reflection checks were updated.

the cleanup also removed unused history flags and resource/cancellation wrappers
that had become test-only abstractions. it removed diffuse-specific tests while
retaining resource publication, ray scene, environment, material, snapshot,
rollback, and shader contracts. generic snapshot fixtures now use retained
fields instead of depending on the removed feature.

| fixed-scope count | before | after |
| --- | ---: | ---: |
| runtime source files under `src/` | 220 | 203 |
| runtime source lines | 53,381 | 47,733 |
| runtime source bytes | 2,015,147 | 1,783,425 |
| first-party shader compilation tasks | 47 | 32 |
| first-party shader families | 27 | 24 |
| shipped shader blobs, including retained framework blobs | 36 | 33 |
| represented setting values | 134 | 115 |
| persisted setting values | 129 | 110 |
| retained runtime diagnostic cases | 64 | 30 |

these source counts use UTF-8 files in the same membership before and after the
cut. runtime reduction is 5,648 lines and 231,722 bytes. archived source and
historical documents are counted separately. they intentionally increase the
recoverable archive, and are excluded from runtime/build inputs and routine
context. character counts are exact; a token estimate would depend on the
tokenizer and is not reported as an exact measurement.
byte deltas also include line-ending normalization in edited text; the line
reduction is independent of that formatting difference.

the retained inputs are a borrowed `RaySceneView`, a single-sample
`LightingSurfaceView`, shared material/light semantics, direct lighting,
environment IBL, ray traced directional/flashlight/sky visibility, and path
tracing. authored material AO remains. global noise and cumulative lighting
accumulation remain because retained ray techniques use them. removing them
would be an additional visible feature cut and would damage those consumers.

no speculative RT diffuse, RTAO, surfel pool, generic technique registry,
producer graph, or future-only output was introduced. future concrete producers
can use the existing scene/surface inputs and contribute at the scene-linear
composition boundary. their output units and coverage must be explicit before
introducing shared result structures. a dormant framework would recreate the
maintenance cost this removal is intended to eliminate.

## settings and runtime compatibility

schema `0015`, fingerprint `7fc6d7fb14f51a13d735b5e54eded9c6`, represents the
new catalog. older registry entries remain immutable. every supported `0007`
through `0014` snapshot validates and discards these exact retired fields before
the ordinary transaction runs:

| retired field | admitted legacy domain |
| --- | --- |
| `visibility.enabled` | `on`, `off` |
| `visibility.quality` | `low`, `medium`, `high`, `ultra`, `custom` |
| `visibility.estimator` | `projected-angle`, `solid-angle`, `cosine-weighted` |
| `visibility.resolution` | `full`, `half`, `quarter` |
| `visibility.samples` | integer 1..64 |
| `visibility.radius` | finite 0.1..10 |
| `visibility.thickness` | finite 0.01..2 |
| `visibility.distribution` | finite 0.25..8 |
| `visibility.specify-noise` | `on`, `off` |
| `visibility.noise-pattern` | `spatial-white`, `spatial-blue`, `spatiotemporal-blue` |
| `visibility.noise-resolution` | `64x64`, `128x128`, `256x256`, `512x512` |
| `visibility.animate-samples` | `on`, `off` |
| `visibility.ao.enabled` | `on`, `off` |
| `visibility.ao.strength` | finite 0..8 |
| `visibility.ao.precision` | `16-bit`, `32-bit` |
| `visibility.gi.enabled` | `on`, `off` |
| `visibility.gi.intensity` | finite 0..16 |
| `visibility.gi.precision` | `16-bit`, `32-bit` |
| `debug.visibility.view` | `final`, `ambient-visibility`, `traced-indirect`, `applied-indirect` |

missing or malformed retired fields reject the older payload before mutation.
current-schema payloads reject these names as unknown. there is no broad
`visibility.*` ignore rule in the decoder or transaction. retained values are
applied normally, with existing selector, preflight, capture, rollback, and
readback behavior. tests exercise every supported legacy version, required
retired membership, malformed values, unknown current fields, and a changed
retained exposure value that survives migration and round-trips.

34 runtime cases belonged solely to diffuse and were removed. seven global
noise cases now use ray traced directional visibility. the remaining cases
retain HDR environments, snapshot/camera/scene/resize interactions, flashlight
causality, path history, and prerequisite recovery. the all-signal sequence now
has camera, scene, and resize actions; its obsolete diffuse-off reference is
gone. runtime JSON schema 4 removes the screen-diffuse dispatch field.
the unused option-family and timing-only comparison machinery is also gone.
the all-signal cases now directly require distinct scene output, with explicit
capture membership and scene identity.

## Postprocess and the tonemapper timing investigation

Tonemapper and FXAA now live in Postprocess. Lights became Light and Shadows
became Shadow. their stored names and retained controls were preserved.

the tonemapper switch previously meant “enable custom grading and the film
LUT”, not “skip all HDR-to-display processing”. `AgxToneMappingPass::Render`
selects neutral settings when it is off and chooses a shader with no LUT
binding. the shader skips neutral white-balance/exposure/grade math, but still
loads scene color, performs the AgX inset/log/contrast/outset transform, and
writes the display-linear intermediate. automatic exposure remains independent.
FXAA then filters that display-linear image, and output transfer/dithering
happen afterward.

therefore a nonzero GPU timing with the switch off is expected. removing AgX
would change the displayed image and HDR handling. this task corrects the
explanation instead: statistics call the pass AgX Display Transform, the group
is Display Processing, and the control tooltip explains the neutral cost.
no timing improvement is claimed from relabeling a measured pass. combining
display passes would require a separate measured design and preserve FXAA's
input domain.

## what the next implementation should do

start after the planned RT diffuse, RTAO, and surfel ownership is concrete.
write down the missing benefit first: equal-time near-field detail, radiance
reuse, or a measured fallback for specific hardware. if RTAO already supplies
the desired visibility, do not restore a second complete screen-space AO image
without evidence that it improves quality or cost.

the first candidate should have one current-frame one-bounce diffuse path, one
validated estimator, one resolution policy, one sample schedule, and one
automatic guide-aware reconstruction. keep gains physically neutral. use the
shared global noise initially. no recursive bounce feedback, private temporal
history, precision toggles, adaptive planner, implementation profiles, or
hit-distance output should exist without an actual accepted consumer.

choose the estimator with a small correctness study before adding GPU options.
use dense receiver-cosine integration as the oracle. final-mask cosine is a
better starting candidate than first-claim cosine for scalar AO because the
recorded bias was lower and scalar visibility did not depend on traversal order.
solid-angle integration remains a simpler candidate if its measured image/cost
tradeoff is better. select one after the study; do not ship the study as three
permanent user choices. separately verify radiance ownership and emitter versus
receiver cosine so the AO result cannot conceal a GI error.

represent screen-space misses as unknown directions. a hybrid may resolve those
directions with RT or surfels, then integrate once with defined weights and
normalization. multiplying independently completed screen-space AO and RTAO
double-counts occlusion. adding two full indirect-radiance estimates also
double-counts energy. environment/probe fallback can replace radiance, but it
cannot certify missing blockers.

use this finite acceptance sequence:

1. define units and invariants. specify radiance versus irradiance, cosine/PDF
   factors, radius/thickness semantics, energy bounds, sample count, valid
   directions, and the exact source signal. exclude display-mapped color from
   lighting feedback.
2. establish CPU references for an open hemisphere, full occlusion, a thin
   slab, overlapping intervals, grazing normals, reversed traversal, and
   multiple sector phases. compare scalar AO and radiance ownership separately.
3. implement a full-resolution current-frame GPU candidate with fixed internal
   settings. compare against the CPU/dense reference and a path-traced image at
   matched camera, materials, exposure, and light state.
4. capture rest, motion, disocclusion, thin geometry, silhouettes, screen edges,
   and off-screen occluders. missing information must remain visible in a
   developer validity view, not silently become “unoccluded”.
5. measure complete GPU frame cost, including depth preparation, source
   acquisition, allocations/residency, reconstruction, and composition. control
   other GPU users, power state, warmup, and timing windows. report p95 and
   incomplete frames as well as medians. compare with the RT baseline at equal
   time or equal error on the same build and hardware.
6. try reduced resolution or one fusion optimization only after the reference
   passes. change one cause at a time, keep the unoptimized oracle available in
   developer research, and remove losing candidates promptly.
7. add a product enable control only after one configuration earns acceptance.
   add at most a coherent quality control if a second measured cost/quality
   point is useful. radius, thickness, estimator, precision, private noise,
   temporal recipe, and arbitrary gains should not become a troubleshooting UI.
8. stop if a candidate cannot beat the simpler retained approach on its stated
   purpose. preserve the experiment and failure in this archive, then remove
   its runtime machinery.

the main lesson is to reduce the number of independent explanations for a bad
image. clear units, one reference, one concrete producer, and controlled images
are more valuable than a larger preset matrix. passing compilation or unit
tests is necessary but does not overturn a negative visual result.

## publication and recovery

the local inventory found 709 copies across 52 registered worktrees, representing
26 distinct postmortem paths and 51 byte-distinct versions. all 20 archive
entries present at local `origin/main` revision
`4a07af5913c27b20eecd03d266578adf8c8fc336` were missing from the active working
tree, including a restore patch and two image assets already staged for deletion.
the four newer local rewrite reports were untracked. the separate denoising and
shader-retirement records were also recovered. variant review found no missing
diffuse experiment conclusion beyond the selected records; most differences
were later provenance or wording revisions.

the recovery restored file contents only. it did not stage, commit, change a
branch, undo the user's existing index, fetch a remote, push, or publish.
the archive scan was bounded to filenames and targeted historical evidence.
it did not claim to discover every unwritten report mentioned in every chat.

`VerifyPostmortems.cmake` now checks exact file membership, required diffuse
report presence, hashes, duplicate/unsafe paths, and new postmortem paths in
other registered worktrees. the full developer integrity gate runs it and its
missing/corrupt/unlisted-file tests. the production package gate and CI require
all manifest members to exist in the exact committed candidate. local dirty
development can verify preservation without touching the index; publication
readiness remains a separate failing gate until the authorized candidate
actually contains the records. the checker never uploads anything.

the archive is repository documentation, not part of the renderer runtime ZIP.
when publication is authorized, review and include every manifest member and
the guard in the source candidate, then run the existing clean-source, package,
legal, identity, and runtime gates. do not turn this reminder into an automatic
push or silently claim that a local manifest proves GitHub received the files.

## primary references

- Therrien, Levesque, and Gilet, [screen space indirect lighting with visibility
  bitmask](https://arxiv.org/abs/2301.11376), with the
  [published DOI](https://doi.org/10.1007/s00371-022-02703-y) and
  [authors' implementation discussion](https://cdrinmatane.github.io/posts/ssaovb-code/).
  this is the main reference for finite-thickness angular bitmask transport.
- Activision, [practical realtime strategies for accurate indirect
  occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf),
  ATVI-TR-16-01. horizon integration, filtering, multibounce correction, and
  specular occlusion are distinct ideas; its platform timings are not UVSR data.
- Intel, [XeGTAO at the researched
  revision](https://github.com/GameTechDev/XeGTAO/tree/a5b1686c7ea37788eeb3576b5be47f7c03db532c).
  retain its MIT attribution for any future copied/adapted implementation.
- CDRIN, [SSRT3 at
  `b38d82140acf646ee485f9ab16a5fa84906f2231`](https://github.com/cdrinmatane/SSRT3/tree/b38d82140acf646ee485f9ab16a5fa84906f2231),
  including its MIT license. the local HZB candidate was separate research;
  Unity APV/reflection helpers are not automatically portable dependencies.
- McGuire and Mara, [efficient GPU screen-space ray
  tracing](https://jcgt.org/published/0003/04/04/). perspective-correct DDA is a
  traversal reference, not the same integral as angular visibility slices.
- McGuire, Mara, and Luebke, [scalable ambient
  obscurance](https://diglib.eg.org/items/8c96d57d-3df3-43da-8663-07b3ecd60dde).
  hierarchical depth and bandwidth-oriented reconstruction are useful research
  references, subject to the complete-frame comparison above.
- AMD, [FidelityFX SSSR at the researched
  revision](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/tree/34dcacd1feefcfab2855b82e76c7d711f2020a75).
  coarse-cell skip/refine traversal informed side-branch research. it does not
  prove that a reflection ray's rejection rule preserves a diffuse integral.
