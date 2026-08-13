# Path Tracing Transport

## Product Contract

UVSR has two lighting solutions over the same loaded scene:

- **Ray Marching** is the established deferred renderer. It retains raster
  visibility, screen-space effects, selective ray queries, and its existing
  anti-aliasing and denoising routes.
- **Path Tracing** is a zero-raster lighting pipeline. A compute shader creates
  primary rays, reconstructs committed triangle hits, and follows complete
  light paths before the common exposure, AgX, presentation anti-aliasing, and
  output stages.

The solution selector changes renderer topology, not scene identity. Material,
camera, physical light, environment, noise, and world-representation settings
remain shared. Inactive Ray Marching settings are retained while their drawers
are hidden.

Path Tracing is deliberately one small transport implementation with policy
controls around it. **RTX PT** is the reference Monte Carlo solver. **RESTIR
PT** adds executable seed-space replay, and **RESTIR GI** adds executable
same-pixel temporal indirect checkpoints. Both are first-party clean-room UVSR
subsets over the shared integrator. Neither claims NVIDIA namesake parity,
hybrid or geometric reconnection, or a spatial GI transformation; they are not
alternate hidden renderers.

## Supported Transport Domain

The complete-transport claim is bounded to geometry and materials that UVSR's
world representation can trace correctly:

- opaque and alpha-tested indexed triangles;
- base-color, opacity, metalness or reconstructed specular-gloss parameters,
  roughness, normal, and emissive inputs exposed by Donut's bindless scene
  buffers;
- Lambert diffuse and GGX metallic-roughness reflection;
- supported analytic directional, point, and spot lights;
- emissive material hits and the selected infinite environment; and
- arbitrary diffuse and glossy bounce sequences up to the selected limit,
  followed by Russian roulette.

Zero-radius positional lights and zero-angular-size directional lights use
point or directional visibility rays. Positive-radius and nonzero-angular-size
lights use transport-local finite-emitter proposals: directional disks are
sampled uniformly in solid angle, positional emitters sample their visible
sphere, and each visibility ray ends at the sampled emitter point. The exact
discrete light-selection PDF and directional PDF normalize every proposal;
direct-reservoir history retains the selected sample seed so temporal and
spatial donors can be re-evaluated at the receiving surface.

Alpha-tested candidates use the same interpolated UV, opacity source, and
cutoff as UVSR's selective ray-visibility passes. Alpha-blended geometry has no
sound stochastic-opacity contract yet, so UVSR reports and omits it while
continuing Path Tracing over the scene's supported opaque and alpha-tested
geometry. This preserves the shipped Sponza decals' explicit limitation without
disabling transport for the complete architectural scene.

Transmissive, subsurface, hair, curves, volumes, nested dielectrics, and
participating media are not silently approximated. A scene that contains one of
those unsupported physical or primitive domains reports the limitation in the
Pathing drawer and keeps the complete live raster fallback active. Every omitted
or rejected domain remains outside the path-tracing world representation until
it has an explicit sampling, evaluation, PDF, visibility, and energy-accounting
contract.

Within that boundary, RTX PT is a conventional Monte Carlo estimator of the
configured finite-bounce transport model. It is not noise-free or
mathematically exact at a finite sample count. Enabling the firefly filter
deliberately biases each successful contribution before it enters the running
mean. The optional direct reservoir changes the primary direct-light estimator
and is reported separately.

## Frame Topology

```text
Scene refresh
  -> world representation build or update
  -> environment and analytic-light preparation
  -> path transport and optional reservoir policy
  -> scene-linear running mean or current transport result
  -> auto exposure
  -> AgX
  -> optional Fast Approximate AA and CMAA2
  -> output transfer and dither
```

Path Tracing does not execute the raster G-buffer, screen-space visibility,
deferred lighting, selective ray-traced shadow or sky passes, the separate
environment-background pass, MSAA, or TAA. The shared render-target owner may
retain inactive raster support allocations so solution changes remain safe and
reversible. Raster material picking may still run on demand because it is an
inspection tool rather than a lighting prerequisite.

The shared **Show Environment Background** setting suppresses only a primary
miss. Secondary misses continue to carry environment radiance through the
transport solution.

## Shared Complete Transport Core

### Inputs and Ownership

The path pass consumes one authoritative set of renderer inputs:

- nonjittered current camera matrices and output extent;
- the prior nonjittered camera matrices used only to locate revalidated
  proposal and replay-seed donors after camera motion;
- the ready TLAS and compact-to-global geometry index map;
- scene geometry and material structured buffers;
- the scene bindless buffer and texture descriptor table;
- packed analytic-light constants;
- the selected raw radiance cube and its physical scale;
- the global noise selection and ordinary animated sample phase;
- the selected accumulation policy and a scheduling serial for deterministic
  revisit cycles; and
- sanitized path settings plus one history epoch.

The path pass always owns four full-resolution textures: the `RGBA32F` running
mean, `R32_UINT` successful-sample count, `RGBA32F` per-channel variance, and
`RGBA16F` display surface. Each optional history family becomes full resolution
only while its effective solver and reuse policy need it:

- direct reuse owns two `RGBA32F` reservoirs, two `RGBA32F` primary-surface
  signatures, and two `R32_UINT` selected finite-emitter sample seeds;
- RESTIR GI owns two `RGBA32F` local indirect checkpoints and two `R32_UINT`
  local-validity counts; and
- RESTIR PT owns two `RG32_UINT` 64-bit local seeds and two `RGBA32F` local
  statistics records containing weight sum, selected target, `M`, and validity;
  and
- Spatial Path Resolve owns two `RGBA16F` accumulated path signals, one
  `RGBA16F` primary normal-and-roughness guide, and one `R32_FLOAT` primary
  view-depth guide.

Inactive families keep stable one-pixel dummy bindings rather than dormant
full-resolution allocations. RESTIR PT and RESTIR GI histories are mutually
exclusive because only one solver is effective at a time; the orthogonal direct
reservoir may accompany either. Spatial Path Resolve signals become full
resolution for every solver while one or two groups are requested; the
three-group diffuse/specular continuation split is available only for RTX PT,
whose current sample retains an unambiguous first-lobe identity. No other pass
may reuse any history resource under a different epoch.

### Resource Budget

The baseline path pass uses 44 bytes per output pixel: 16 for the running mean,
4 for the sample count, 16 for RGB variance, and 8 for display. Optional direct
history adds 72 bytes, Spatial Path Resolve signals add 28 bytes, RESTIR GI
history adds 40 bytes, and RESTIR PT history adds 48 bytes per pixel. At 3840
by 2160, excluding negligible one-pixel dummy bindings, the exact current
allocation totals are:

| Active History | Bytes Per Pixel | Total At 3840 By 2160 |
| --- | ---: | ---: |
| Base transport only | 44 | 348.0 MiB |
| Base plus Spatial Path Resolve signals | 72 | 569.5 MiB |
| Base plus direct reuse | 116 | 917.6 MiB |
| Base plus direct reuse and Spatial Path Resolve signals | 144 | 1,139.1 MiB |
| Base plus RESTIR GI | 84 | 664.5 MiB |
| Base plus RESTIR GI and direct reuse | 156 | 1,234.0 MiB |
| Base plus RESTIR PT | 92 | 727.7 MiB |
| Base plus RESTIR PT and direct reuse | 164 | 1,297.3 MiB |

Direct RIS without temporal or neighbor reuse retains the baseline allocation.
Allocation-topology changes clear bindings and every dependent history before
dispatch.

### Primary and Continuation Rays

The camera ray is derived directly from the inverse nonjittered projection and
view transforms. Each continuation ray begins outside the committed geometric
triangle plane and uses the current scene scale-aware ray limits. The transport
state is intentionally compact: origin, direction, throughput, accumulated
radiance, bounce index, and random state.

Inline DXR 1.1 `RayQuery` traversal keeps the baseline shader at Shader Model
6.5. Path rays force triangles through one material-aware candidate callback.
It rejects single-sided backfaces and unsupported hair, subsurface, or
transmissive materials, accepts supported opaque candidates, and commits
alpha-tested candidates only when their material coverage test passes.

### Committed-Hit Reconstruction

The committed instance contribution and geometry index resolve through the
world representation's compact geometry map. The shader then loads the indexed
triangle's positions, normals, tangents, and UVs from the bindless geometry
buffers, interpolates attributes with committed barycentrics, and transforms
the interaction into world space.

Geometric and shading normals remain distinct. The geometric normal controls
hemisphere validity and ray offsets. The material-derived shading normal
controls BSDF shape only after it has been oriented consistently with the
geometric surface and the incoming path.

### Material and BSDF Contract

The material loader evaluates authored factors and optional bindless textures
at the committed UV. Metalness partitions the base color between diffuse
reflectance and conductor F0. Roughness is clamped away from the singular limit
before GGX evaluation or sampling.

Every supported lobe provides all three operations from the same equations:

1. evaluate reflected radiance response;
2. sample an outgoing direction; and
3. return the matching solid-angle PDF.

The integrator uses a diffuse/specular mixture whose selection probability is
included in both evaluation and PDF. This shared contract prevents a solver
from changing energy merely by changing how a direction was proposed.

Camera paths transport radiance, so the integrator does not apply the adjoint
importance-mode shading-normal factor. BSDF evaluation and cosine terms use the
material shading frame, while geometric-normal hemisphere checks prevent
transport through the back of the underlying triangle.

### Emission and Direct Lighting

An environment miss adds its radiance and terminates the current ray. An
emissive committed hit adds authored emission before ordinary continuation.
Both emitter types are reached only through BSDF continuation in the current
implementation. Analytic lights use a separate next-event estimator. Because
the shader does not propose the same emitter through overlapping light and BSDF
techniques, it does not currently execute an active
multiple-importance-sampling combination between them.

The light sampler has three policy choices:

- **Uniform** samples eligible lights uniformly.
- **Power** samples them by a stable emitted-power estimate.
- The **NEE-AT** policy selects a UVSR-owned binary adaptive tree whose weights
  are rebuilt at the current path vertex over the complete submitted analytic
  light buffer.

Each policy returns the discrete light-selection probability that normalizes
its own direct estimate. **NEE Candidates** averages the configured number of
independent estimates in conventional RTX PT and supplies the same number of
local candidates to the direct reservoir. The NEE-AT label does not claim
NVIDIA RTXPT parity, identical feedback state, or identical output.

The analytic-light buffer grows with the submitted scene list; the transport
pass does not silently discard lights beyond a fixed constant-buffer limit.

When **RTXDI Reservoir Stages** is enabled, UVSR's first-party direct reservoir
replaces conventional NEE at the primary hit; later vertices retain
conventional NEE. With direct reuse enabled and compatible history available,
the reservoir projects the current primary world position through the prior
nonjittered view, then considers the candidate at that prior-frame pixel and one
neighboring candidate around it. Both require a compatible surface signature;
reused targets replay the selected light and complete persisted 32-bit emitter
sample seed at the current surface, and selected-endpoint visibility is traced
again. Invalid or off-screen reprojection contributes no temporal donor.
This is an RTXDI-like policy, not NVIDIA RTXDI source or a claim of one-to-one
behavior.

### Continuation and Termination

After direct and emitted contributions, one BSDF proposal advances throughput
by `f * abs(n dot wi) / pdf`. A finite-value guard terminates invalid paths
instead of storing NaN or infinity.
After the configured starting bounce, Russian roulette uses a
throughput-derived survival probability and divides surviving throughput by
that probability. The explicit bounce cap remains a safety and performance
bound.

The current core derives stable semantic random domains from one persisted
64-bit sample seed. Camera jitter, continuation sampling, primary direct
lighting, and reservoir selection therefore cannot shift one another's random
dimensions. RESTIR PT can replay a donor seed through this exact integrator at
the receiving pixel without depending on mutable frame, noise-texture, or
prior-call state. Camera-only reuse locates that seed by projecting the current
primary world position into the previous view; it does not graft old radiance
onto the new camera path. A future hybrid reconnection solver may add recorded
vertices and Jacobians, but it must not replace the material or transport math.

## Solver Presets

| Requested Preset | Effective Solver | Direct Reservoir | Solver Reuse |
| --- | --- | --- | --- |
| **RTX PT** | RTX PT | Optional; preset default is off | Off |
| **RESTIR PT** | RESTIR PT seed-replay subset | Optional; preset default is off | Current path plus replayed reprojected prior-pixel and one-neighbor seeds |
| **RESTIR GI** | RESTIR GI checkpoint subset | Optional; preset default is off | Current plus prior same-pixel indirect checkpoint |

Selecting a preset produces a fresh editable settings recipe. Later changes do
not create another transport implementation; they change policy fields and
invalidate histories whose estimator changed. The UI reports the qualified
clean-room subset while it is executable and reports an effective fallback only
when a required shader permutation or resource format is unavailable.

The 18 executable shader variants are packaged independently as three solvers
times RTXDI-off/on times Uniform, Power, and NEE-AT. Together with the two
single-permutation lighting-accumulation shaders and one spatial path-layer
resolve shader, they raise the production catalog from 306 to 327 shader tasks
while remaining four additional staged families in a 50-binary runtime
bundle. A missing optional variant does not
disable Path Tracing or strand the selector on a raster image. UVSR tries the
requested solver and NEE mode without RTXDI, then the same solver with Uniform
NEE, then RTX PT with the requested NEE mode, and finally baseline Uniform RTX
PT. The authored preset remains visible, the effective fallback is reported,
unavailable choices are disabled, and only failure of the baseline variant
makes the renderer use its live raster fallback.

### Reference Preset

RTX PT is the reference preset. Direct reservoirs and the biased firefly filter
are off by default. Uniform, power-weighted, and UVSR adaptive-tree NEE remain
configurable. With the firefly filter disabled, the result is the shared
integrator's current output or running mean without a reconstruction stage.

### Seed-Replay Path Subset

The RESTIR PT recipe enables deterministic seed-space replay. The current
pixel's local indirect suffix is one proposal. When compatible prior-frame
history exists, the solver projects the current primary world position through
the prior view, then replays the seed at that prior pixel and one neighboring
seed through the exact same path integrator at the current pixel. The neighbor
is chosen independently before any proposal contribution or target is
evaluated.

Every finite proposal, including a successful black path, increments `M`.
Because every RGB proposal is already fully evaluated, the solver uses their
bounded online conditional arithmetic mean instead of randomly selecting one
and multiplying it by `sumTarget / (M * selectedTarget)`. This
Rao-Blackwellization preserves the old selector's conditional expectation while
removing its avoidable selection variance. The current primary emission,
environment, and analytic-direct base is then added exactly once. Replays may
reconstruct the complete integrator result internally, but their primary base
is discarded so the solver never double-counts it.

History persists only the current frame's local 64-bit seed and local proposal
statistics. It never feeds the combined contribution mean back into the next
frame. This is an executable clean-room
seed-replay subset, not NVIDIA RTXDI-Library parity: it has no stored
reconnection vertex, hybrid shift, geometric reconnection, or donor-camera
path graft.

### Temporal GI Checkpoint Subset

The RESTIR GI recipe combines the current pixel's local indirect suffix with
the previous frame's same-pixel local indirect checkpoint. It uses the same
finite-proposal count and bounded conditional arithmetic mean as the seed-replay
path. A successful black checkpoint participates in `M`, and the current
primary base is added exactly once after combining the eligible suffixes.

The persistent payload is only the current local RGB indirect suffix, its
target, and a one-proposal validity count. The combined estimate never feeds
back into history. Because reuse is same-pixel and same-epoch, the subset needs
no cross-pixel transform, visibility replay, Jacobian, secondary-surface
reconnection, or spatial GI transform. It is not NVIDIA RTXDI-Library parity.

### Direct-Lighting Reservoir Option

The RTXDI Reservoir Stages option selects UVSR's RTXDI-like direct-light
reservoir stage for any preset. It is orthogonal to the effective solver.
Disabling it returns the primary hit to conventional NEE without changing hit
reconstruction, materials, or later path vertices.

## Progressive Accumulation

**Accumulate Samples** is a UVSR feature shared by both lighting solutions.
Factory-default **Variance Guided** first takes 16 successful samples per pixel,
then maps the largest RGB-channel relative standard error to an integer revisit
interval with a two-percent target and a guaranteed 1/16 minimum rate.
**Progressive Mean** attempts every pixel whenever its progressive lattice phase
is eligible and updates a cumulative scene-linear RGB mean. **Responsive Mean**
uses the same attempts with a 32-sample exponential history. All averaging,
scheduling, history, warmup, error, and revisit controls remain exposed.
Editing one displays `<Profile> (Custom)` without changing its named origin;
explicitly reselecting the profile reapplies its complete vector.

Variance-guided revisits are deterministic. A stable per-pixel hashed phase and
bounded interval replace the old random `1 / (successfulSampleCount + 1)`
retry, which starved convergence to roughly square-root sample growth. A finite
black result or environment miss is successful and increments the count. This
avoids preferentially retaining bright or nonzero outcomes. An attempted finite
estimate updates the online RGB mean and variance; a skipped pixel retains its
prior state exactly.

The accepted stationary sample's RNG phase is the pixel's prior successful
count. A skipped frame therefore does not consume a sample, and disabling
ordinary authored noise animation does not repeat phase zero while progressive
accumulation is retaining history. A camera-driven reset instead uses the live
frame phase when **Animate Samples** is enabled, so moving-camera noise remains
visibly animated rather than appearing attached to the lens.

Path Tracing performs the scheduling decision before traversal, so skipped
pixels avoid the expensive path. Ordinary presets dispatch every output pixel
on each sampling pass. A one-Gi-work-unit safety budget retains a bounded
progressive lattice only for extreme resolution, bounce, light, or replay
combinations; the Pathing drawer reports its estimated work per pixel, lattice
phase count, and submitted sampling passes. On a reset with a remaining lattice,
a separate full-resolution presentation-only preview prevents untouched black
pixels without writing radiance history.

Ray Marching dispatches a prepare shader before its stochastic screen-space
visibility, Heitz shadow, ray-traced flashlight, and ray-traced sky producers.
Those producers consume one shared attempt token and return early for rejected
pixels. Required raster, deferred, and presentation work still runs; the token
avoids the guarded stochastic work, not the entire frame.

For Ray Marching, a matching transactional resolve commits the raw scene-linear
frame before TAA. A rejected pixel copies its prior mean, RGB variance, and
count exactly. A non-finite attempted sample is not successful and also
preserves history. Only a valid matching prepare/resolve transaction may
advance the history epoch and ping-pong index. Ray Marching accumulation is the
sole long-term history owner while active, so TAA history and temporal blending
are bypassed rather than averaged into a second estimator.

With accumulation disabled, Ray Marching bypasses its full-resolution history,
while Path Tracing attempts every eligible pixel and replaces history with
count one. The latter remains a continuously refreshed one-sample estimate and
can be extremely noisy in environment-lit interiors. Every accumulation mode
converges only a stationary scene: any camera, geometry, material, light,
environment, solver, or accumulation-policy change conservatively restarts all
progressive radiance histories.

## History Invalidation

Progressive history, optional direct-reservoir history, RESTIR PT seed history,
RESTIR GI checkpoint history, and any future reconstruction history share one
monotonic epoch. The epoch changes when any estimator input changes:

- nonjittered camera view or projection;
- output extent;
- scene activation or shader reload;
- world-representation allocation generation or in-place content revision;
- geometry topology, instance transform, or dynamic vertex content;
- material data or textures;
- analytic-light data;
- environment source, content, exposure, or radiance scale;
- lighting solution, solver, NEE, bounce, roulette, RTXDI, reconstruction
  method, noise, or accumulation settings; or
- the accumulation toggle itself.

Jitter alone does not reset history because Path Tracing does not use raster
TAA jitter. **Reuse Validated RESTIR Proposals During Motion** is an opt-in
exception for camera-only changes. It may retain direct-light proposals and
RESTIR PT seeds, locate their donors through prior-view reprojection, validate
them against the current primary surface, and fully re-evaluate them at the
current pixel.
Means, variances, counts, stable signals, and RESTIR GI radiance checkpoints
always reset, and non-camera changes clear every affected family. A reset is
otherwise conservative: discarding valid proposals costs convergence, while
retaining stale radiance corrupts the image.

## Spatial Path Resolve and Denoising

Every solver can persist a coherent clean-room signal interchange after one
complete sampling cycle: primary transport, indirect transport, primary shading
normal plus roughness, and primary view depth. RTX PT can additionally separate
the continuation into diffuse and specular groups because its unresampled
current path retains a trustworthy first-lobe identity. RESTIR PT and RESTIR GI
produce a combined selected or replayed continuation suffix, so they correctly
cap the control at two groups.

**Spatial Path Resolve** is UVSR's bounded spatial-only edge-aware resolve over
those accumulated signals. One group filters the combined path signal; two
split primary from indirect transport; three split RTX PT into primary, diffuse
continuation, and specular continuation. A 5-by-5 filter uses view depth,
normal, roughness, successful-sample count, and RGB variance for confidence and
radiance-edge rejection. **Resolve Strength** blends from exact raw radiance at
zero percent to the complete spatial correction at 100 percent, and the
correction fades toward raw as the standard error of the accumulated mean
falls. Exponential accumulation caps that effective sample count at twice its
authored history minus one. Variance never becomes an
inverse-variance estimator weight. The pass never temporally reprojects and
never alters the raw mean; missing confidence, invalid guides, nonfinite output,
or allocation failure returns exact sanitized raw radiance.

Normal, roughness, and depth guides come from a deterministic center-pixel
primary query on the first accumulated sample and remain fixed until transport
history resets. Later jittered radiance rays cannot flip a silhouette guide.
Changing Resolve Strength or the active signal-group presentation re-runs the
resolve without discarding converged transport; changing Raw versus Spatial
Path Resolve still resets because it changes which persistent signals exist.

This is a first-party biased spatial reconstruction, not NVIDIA RTXPT Stable
Planes parity. Primary-Surface Replacement remains unavailable; changing a
signal-group count alone would not constitute PSR.

There is no validated path-transport NRD adapter in this build, even when the
separate Ray Marching NRD backend is compiled. Path Tracing therefore exposes
Raw and Spatial Path Resolve, but does not claim NRD ReBLUR or ReLAX processing.
A truthful path NRD adapter requires separately demodulated diffuse and specular
radiance, matching in-lobe hit distances, and path-valid motion guides; UVSR's
existing Ray Marching adapter does not satisfy that contract.

**Firefly Clamp (Biased)** is executable. It limits each successful high-energy
contribution before the persistent mean is updated. Consequently, the
accumulated result is intentionally biased while the option is enabled; the
pass exposes that state explicitly.

## Shader Execution Reordering Capability Boundary

Shader Execution Reordering is not simulated. The current Shader Model 6.5
RayQuery megakernel reports native SER unavailable, disables the control, and
continues ordinary tracing without claiming reordered execution. A future
native HitObject permutation needs its own capability and packaging contract.

## NVIDIA Reference and Licensing Boundary

The design was checked against these official revisions:

- [RTXPT 1.8.1](https://github.com/NVIDIA-RTX/RTXPT/tree/v.1.8.1), tag commit
  `62b7d5c3d13f46b6f12b922212fc0120bcf27cfc`;
- [RTXDI 3.0.0](https://github.com/NVIDIA-RTX/RTXDI/tree/v3.0.0), tag commit
  `274141af082050c9d0ad6e01a2e591d0d66b7955`; and
- the reservoir path-tracing revision pinned by RTXDI 3.0,
  `a14e079c727ed8c4fd3173bd2aea8244c9d9f6d6`.

Those repositories use NVIDIA's RTX SDK license rather than a permissive
open-source license. UVSR does not copy, vendor, or compile their sample or
library source. The executable transport, adaptive light selection, direct
reservoir, seed replay, and temporal GI checkpoints are first-party UVSR code
informed only at a high level by public algorithm descriptions. The RESTIR PT
and RESTIR GI names identify explicitly qualified clean-room subsets. Nothing
here is NVIDIA-certified, one-to-one, bit-identical, source-compatible with an
RTX SDK, or a claim that seed replay equals hybrid geometric reconnection.

## External Design Cross-Check

The implementation was also cross-checked against the current public
[ZetaRay](https://github.com/alipbcs/ZetaRay),
[NVIDIA RTXPT](https://github.com/NVIDIA-RTX/RTXPT),
[NRD](https://github.com/NVIDIA-RTX/NRD), and
[RTXDI RESTIR PT](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirPT.md)
contracts. ZetaRay and RTXDI use substantially richer temporal/spatial path
state and replay or reconnection machinery than UVSR's deliberately qualified
subsets. RTXPT supplies multiple denoiser-ready path-space layers and guides,
while NRD requires split radiance, in-lobe hit distance, surface guides, and
motion data. Those comparisons motivated truthful naming, full-rate ordinary
dispatch, reprojected proposal/seed lookup, and solver-compatible signal groups;
they do not justify relabeling UVSR's combined radiance as valid NRD input.

UVSR still uses a compact counter-hashed sampler rather than a dimensioned
Owen-scrambled low-discrepancy sequence, and its light selection lacks the
broader emissive/environment proposal infrastructure of the reference
applications. Those are explicit future convergence opportunities rather than
claims made by this repair.

## Extension Rules

Future solvers extend policies and recorded path events, not the authoritative
surface or BSDF implementation. Hybrid geometric reconnection, RESTIR PT
Enhanced, spatial GI transforms, overlapping-technique MIS, larger neighbor
reuse, or guiding may add:

- stable semantic random dimensions;
- candidate-generation and reservoir adapters;
- compact recorded vertices with proposal PDFs;
- temporal and spatial compatibility tests; and
- new guide or debug outputs with explicit lifetime ownership.

An extension must not duplicate hit reconstruction, introduce a second material
model, double-count an existing light technique, retain history across an
epoch mismatch, or make the raw fallback depend on an optional SDK.

## Validation Contract

The implementation is accepted only when focused contracts prove settings and
presets, CPU/HLSL layout, all 18 path permutations, the 327-task/50-binary
shader bundle, committed-hit material coverage, finite accumulation math,
history invalidation inputs, renderer pass ordering, and drawer gating. A
Release build must exercise Ray Marching and RTX PT, the direct reservoir with
and without compatible previous-frame reuse, RESTIR PT reprojected prior-pixel
and neighbor seed replay, and RESTIR GI same-pixel checkpoint reuse. It must
also prove that camera, light, geometry, and material motion clear every
non-revalidated history.

Debug views for first-hit albedo, geometric and shading normal, sample count,
update rate, signal-group classification, the direct reservoir, the active
solver's resampled indirect suffix, primary transport, and indirect transport
provide runtime evidence. Primary Transport contains the first-hit
environment, emission, and direct-light contribution; Indirect Transport
contains the solved continuation suffix. Selecting a different Path Tracing
debug view preserves the history epoch but forces one all-pixel sampling pass
so transient views are coherent. Indirect Reservoir is available only while
effective RESTIR PT replay or RESTIR GI checkpoint reuse is active.

Runtime acceptance also requires that a successful Path Tracing dispatch reach
the common scene-color, exposure, tone-mapping, output, command-list close, and
submission stages. The raster fallback scope may contain only raster-specific
production; it must never enclose those shared presentation or submission
stages. Preparing or unavailable transport must keep a live raster frame and
scene-loading UI responsive until a complete path frame can be submitted.
