# Ratio Estimation

UVSR exposes **Ratio Estimator** as an independent producer choice for
ray-traced sun shadows and sky visibility. Both are current-frame multisample
estimators, but they resolve different quantities and are not interchangeable.

## Estimator Families

- **Sun Shadows.** Correlated visible and unshadowed RGB material responses
  share every sample except binary ray visibility. This is UVSR's
  variance-reduction adaptation of the estimator described in Eric Heitz,
  Stephen Hill, and Morgan McGuire's
  [2018 paper](https://research.nvidia.com/sites/default/files/pubs/2018-05_Combining-Analytic-Direct/I3D2018_combining.pdf)
  and [SIGGRAPH talk](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018SIGGRAPHTalk.pdf).
- **Sky Visibility.** A separate cosine-hemisphere estimator resolves
  `visibleSampleCount / sampleCount` and applies that geometric visibility to
  the selected diffuse and specular environment-lighting consumers.

Both settings offer 1, 2, 4, 8, 16, 32, or 64 samples while Ratio Estimator is
on. Turning it off selects each producer's raw single-ray route. Neither
producer owns private temporal history.

## Sky Visibility Contract

Sky visibility runs at full resolution in the current frame and samples the
cosine-weighted hemisphere around the geometric normal. With Ratio Estimator
on, the selected 1 through 64 rays resolve `visibleSampleCount / sampleCount`
into scalar geometric visibility. Turning it off traces one raw stochastic ray.

Opaque and alpha-tested candidates use the shared material-aware traversal
contract. The ray origin uses geometric-normal clearance, the selected
nonnegative **Ray Bias**, and a representable-position nudge. **Max Distance**
selects either the scene-diagonal reference or a finite bounded approximation;
**Output Hit Distance** independently records the nearest committed blocker.

The resolved scalar modulates only the selected diffuse and specular
environment-lighting consumers. Diffuse application also affects environment
radiance before it becomes the GI source. The estimator does not alter direct
lights, direct shadows, emissive, or AO, and it does not directly multiply the
traced-indirect output. Its single scalar is not roughness- or
reflection-direction-resolved specular visibility.

## Directional Shadow Contract

With **Ratio Estimator** on, one compute dispatch accumulates matched RGB
estimates of the unshadowed response `U_N` and visible response `S_N`. Each pair
shares the sample direction, proposal, BRDF, cosine, validity decision, and
normalization. Binary ray visibility is the only difference. UVSR resolves:

```text
W = clamp(S_N / U_N, 0, 1)
final directional light = analytic U * W
```

A channel whose denominator is below `1e-4`, or whose result is not finite,
resolves to neutral visibility. The shadow resolve bounds `W` to `[0, 1]`; the
shared correlated ratio helper remains unbounded for future nonshadow users.

The matched numerator and denominator remain in registers and are divided in
the current dispatch. The ratio route owns no private spatial filter or temporal
history. Final image TAA can still accumulate the completed frame.

At 1x the pass exports both the total-response ratio used by final direct
lighting and a diffuse-only ratio used when direct diffuse radiance becomes a
screen-space GI source. This separation prevents glossy or mixed-response
materials from applying a specular-weighted shadow ratio to transported diffuse
energy. Both ratios reuse the same rays and matched emitter samples.

## Multisampled Receiver Contract

With deferred MSAA and Multisample Adaptive **Per-Sample Shadows** enabled, the
estimator keeps a separate matched ratio for every valid covered raster
receiver. It loads depth, material, shading normal, geometric normal, emissive,
and material AO from the same `Texture2DMS` sample index; it never averages
attributes or substitutes the closest-surface resolve. For receiver `r` and `N`
emitter samples:

```text
U_r = (1 / N) * sum over emitter i of f(r, i)
S_r = (1 / N) * sum over emitter i of f(r, i) * V(r, i)
W_r = clamp(S_r / U_r, 0, 1)
```

Each receiver normalizes before the fixed `1e-4` denominator guard, so a
one-sample silhouette retains the same ratio at 2x through 16x. Receiver and
emitter indices are flattened into one low-discrepancy sequence index so every
ray receives a distinct direction.

The pass emits two single-sample RGBA16F textures without tracing extra rays.
Final MSAA lighting consumes an analytic-response-weighted resolve:

```text
W_resolved = sum over r of A_r * W_r / sum over r of A_r
```

`A_r` is the same center-direction total response evaluated by deferred PBR.
Multiplying every receiver's analytic term by `W_resolved` and resolving then
reproduces `sum(A_r * W_r)` channel by channel, including heterogeneous glossy,
horizon, and material samples. A second texture carries the closest reverse-Z
receiver's diffuse-only ratio. The auxiliary PBR pass uses it when writing the
direct-diffuse radiance transported by screen-space GI. The deterministic
analytic resolve divides every finite positive response channel exactly; only
a zero or nonfinite analytic denominator resolves to neutral white. The fixed
`1e-4` Monte Carlo guard is applied only to each receiver's local `S_r / U_r`.

The supported receiver counts are 1x, 2x, 4x, 8x, and 16x. With Per-Sample
Shadows on, worst-case shadow ray count is covered receivers times **Samples Per
Pixel**, up to `16 * 64 = 1024` rays for a fully covered pixel.

Turning Per-Sample Shadows off under MSAA performs a complete raw depth/normal
scan before any ray query, selects the finite-positive-depth, nonzero-normal,
greatest-reverse-Z receiver, and evaluates only that receiver. Equal depths
retain the lower sample index. The selected receiver keeps its original sample
index in the stochastic sequence. The pass writes its total modulation directly
to final MSAA lighting and its diffuse modulation directly to the closest-source
GI output; neither value is analytically reweighted or coverage-scaled. This
reduces the soft ratio upper bound to **Samples Per Pixel** and the hard or raw
one-ray upper bound to one ray. It deliberately broadcasts one receiver's
visibility to other covered receivers, so it is exact when only one receiver
contributes or all receivers share the same modulation, but it can lose or bleed
shadow detail across heterogeneous surfaces and shadow boundaries. A selected
owner that cannot reconstruct fails open to neutral white without falling back
to a farther surface. The setting is inert at 1x.

## Single-Ray-Per-Receiver Shadow Route

Turning **Ratio Estimator** off selects one stochastic scalar visibility ray for
a positive angular size. At 1x, the optimized path replicates that scalar across
RGB. Under MSAA with Per-Sample Shadows on, each valid receiver traces one ray
and its visibility is weighted by that receiver's center-direction RGB analytic
response before the pixel ratio is resolved. With Per-Sample Shadows off, only
the selected closest receiver traces and its binary result is reused. A zero
angular size or **Hard Shadows** uses the center direction instead. Stored sample
count remains unchanged but is inactive on either one-ray route.

**Output Hit Distance** selects an R16 physical distance output for the 1x
one-ray route. A committed triangle records its nearest ray distance, a miss
records the finite FP16 miss value, and an invalid receiver records zero. It
defaults off, does not change ray count, and adds a texture allocation plus one
output write. A ratio-estimated area-light signal and a resolved MSAA pixel have
no single matched physical blocker distance, so UVSR preserves the stored switch
but suppresses the texture and reports why it is inactive.

NVIDIA NRD SIGMA expects the raw shadow and physical hit distance pair. Sun
SIGMA therefore requires Ratio Estimator off and Output Hit Distance on at 1x.
Built-in bilateral sun denoising also remains 1x-only because its guide owns one
closest surface rather than a heterogeneous MSAA response. It needs no hit
distance at 1x. Selecting a method in the Denoising drawer never changes either
producer setting. Under MSAA, or if a selected method's inputs or optional NRD
backend are unavailable, UVSR applies the raw resolved shadow and reports the
reason.

## UVSR Adaptation

The published rectangular emitter implementation combines light and LTC BRDF
sampling with adaptive MIS. UVSR currently has a directional emitter and no
shared LTC sampling path, so the ratio route uniformly samples the solid angle
cap defined by the directional light.

The effect inherits the Noise drawer until **Specify Noise** is enabled. Its
private Pattern, Resolution, and Animate Samples controls then select one of the
same precomputed `R8_UNORM` textures without changing another effect. Two fixed
semantic streams provide the Cranley-Patterson shifts applied to the progressive
low-discrepancy emitter sequence. Sampling is centered in the local dispatch;
Spatial White and Spatial Blue translate the tile when animated, while
Spatiotemporal Blue advances its fixed XY addresses through 64 layers.

This adapts the spatial blue noise rotation and low discrepancy temporal idea
from Demofox's
[blue noise soft shadow article](https://blog.demofox.org/2020/05/16/using-blue-noise-for-raytraced-soft-shadows/)
while retaining UVSR's exact uniform spherical cap mapping. The uniform
proposal has one constant PDF; its reciprocal cancels in the correlated ratio
and is omitted from both sums.

Deferred PBR still supplies analytic `U` using its center-direction
approximation. The paper permits an approximate analytic term, but glossy
highlights and horizon-clipped emitters are consequently less exact than a
shared analytic directional-cap response. With Per-Sample Shadows enabled,
MSAA resolves the finite independent receiver estimators exactly relative to
that same center-direction analytic approximation; it does not remove the
approximation itself. The disabled policy explicitly trades that property for
the closest-owner ray budget.

When screen-space visibility is also active under MSAA, its closest-surface PBR
preparation pass is only an auxiliary source for indirect diffuse tracing. It
uses the diffuse-only ratio belonging to the same finite-positive-depth,
nonzero-normal, greatest-reverse-Z receiver selected by the coherent G-buffer
resolve. Equal depths retain the lower sample index. This prevents unshadowed
sun radiance from entering the GI source while final sample-frequency deferred
lighting consumes the independently resolved total-response modulation.

This repair does not make the existing screen-space visibility topology fully
sample-frequency. Its auxiliary source and correction still represent one
closest surface for the pixel. A pixel containing multiple covered primitives
can therefore broadcast that owner's indirect correction to non-owner samples,
and a sparsely covered source or occluder can still enter screen-space traversal
as a full pixel. Exact mixed-surface GI needs owner or coverage metadata and a
sample-aware source/traversal path; those cases are regression fixtures and a
known boundary, not part of the direct-sun exactness claim.

MSAA depth is per sample, while UVSR's retained G-buffer attributes were written
by ordinary pixel-frequency interpolation. Heitz and deferred MSAA lighting
therefore reconstruct each sample depth at the shared pixel center to keep their
response weighting internally coherent. Correct sample-position ray origins
require a coordinated sample-frequency G-buffer path; changing only the ray
origin would mix incompatible interpolation conventions.

Inline ray queries commit opaque candidates directly. Alpha-tested candidates
fetch the exact instance geometry and material, interpolate the authored UV,
sample base-color alpha when present, and commit only when it passes the
material cutoff. Blended and transmissive domains are excluded from the binary
visibility hierarchy. The same helper and resource contract is used by sky and
flashlight visibility, preventing foliage cards from becoming solid rectangles
in any of the three effects.

## Softness and Sampling

The light's **Angular Size** is a full angular diameter in degrees. UVSR's
primary sun initializes to `0.2 deg` and irradiance `8`. The ray tracer converts
half the angular size to a cone radius and samples directions uniformly over
that cone.

- `0 deg` is a zero extent emitter and takes the hard center ray path.
- A positive value produces geometric penumbrae whose width grows with blocker
  and receiver separation.
- **Hard Shadows** explicitly selects the center ray without changing stored
  angular size.
- Resolved **Animate Samples** advances the dedicated phase after an actual
  stochastic dispatch. When disabled, phase zero remains fixed.
- **Samples per Pixel** offers `1`, `2`, `4`, `8`, `16`, `32`, and `64` on the
  ratio route. The raw route always traces one ray.

## Ray Origin Safety

UVSR stores separate shading and geometric G buffer normals. The shading normal
remains smooth or normal mapped for the BSDF. The geometric normal follows the
raster triangle plane derived from world position derivatives, faces the
camera, and drives ray origin construction.

For each active receiver, UVSR reconstructs the surface and its depth quantization
clearance, takes the maximum of that internal distance and nonnegative **Ray
Bias**, moves the origin once along the view facing geometric normal, and then
applies a representable position nudge. The query uses `TMin = 0`, avoiding a
second contact shadow gap.

The factory Ray Bias is `0.002` world units and its diagnostic range is `0`
through `0.1`. Larger values can miss a blocker within that gap or detach
contact shadows. Bias changes no dispatch dimensions or intended ray count.

## World Space Representation

Representation owns the shared triangle acceleration structure. **Allow Ray
Traversal** is the master permission for every traversal consumer. Turning it
off stops the sun shadow dispatch without clearing **Enabled**, Ratio Estimator,
Output Hit Distance, or any other shadow choice.

One BLAS is built per unique instanced triangle mesh. Initial construction is
staged one BLAS per loading frame, followed by a coherent TLAS. Runtime changed
geometry updates BLAS before TLAS. The shadow pass consumes a ready TLAS but
never owns it.

## Controls and Commands

The Shadows drawer contains one independently collapsible **Ray Traced
Shadows** group. It exposes **Enabled**, **Ratio Estimator**, **Output Hit
Distance**, **Hard Shadows**, **Samples Per Pixel**, **Specify Noise**, **Max
Distance**, and **Ray Bias**. Factory defaults keep the producer enabled, Ratio
Estimator on, Output Hit Distance off, two ratio samples, hard mode off, global
noise inheritance active, and Ray Bias at `0.002`. Enabling Specify Noise
reveals private Pattern, Resolution, and Animate Samples controls. Multisample
Adaptive Advanced owns the default-on **Per-Sample Shadows** receiver-frequency
policy because it changes how directional shadow work scales with raster sample
count.

Commands use the `shadows.ray-traced.` prefix. Changing a sampling setting
restarts deterministic sample phase and any downstream history that depends on
that signal. The MSAA-specific policy uses
`anti-aliasing.msaa.per-sample-shadows` with `on` or `off`; changing it resets
lighting history but does not rebuild the Heitz shader pipeline or resources.

## Main Branch Boundary

Screen space directional shadows are removed from main and are not combined
with this result. Their code and debug modes are quarantined with the CSM and
SVSM experiments on local branch `codex/svsm-csm-preserved`.

Each direct visibility texture is matched to the exact light that produced it.
Sun visibility uses the sun slot; flashlight visibility uses a separate slot
and separate finite light query.

## Performance

The Performance panel's Directional Shadows view and Complete Renderer table
use **Shadow Ray Dispatch** for the raw sun and flashlight query work and
**Shadow Denoise** for the optional SIGMA work. Sky visibility has its own Ray
Dispatch and Denoise rows. A timing is shown only after a completed GPU query;
inactive or newly selected work reports unavailable instead of a fabricated
zero. The removed screen space breakdown does not remain in main.

## Runtime Limits

- DirectX Raytracing 1.1 acceleration structures and inline ray queries are
  required.
- Deferred receiver counts are limited to the renderer's supported 1x, 2x, 4x,
  8x, and 16x modes.
- Ratio-estimated 1x uses one RGB total-response ratio for final direct lighting
  and one diffuse ratio for the GI source. It exposes no matched physical hit
  distance and cannot feed SIGMA, but built-in bilateral filtering may process
  its total-response signal; the separately matched diffuse source remains raw.
- MSAA uses the same two output roles with a closest-receiver diffuse ratio. It
  exposes no physical hit distance and cannot feed any sun denoising because
  the available guide owns only one surface.
- Disabling Per-Sample Shadows preserves those two output roles and the physical
  MSAA receiver count, but both signals come from the selected closest receiver.
  It is a performance approximation rather than the exact heterogeneous-receiver
  resolve.
- Opaque and alpha-tested triangle geometry is supported. Alpha-tested
  candidates honor base-color alpha and cutoff; blended and transmissive
  domains are not binary occluders.
- Uniform light only sampling has more glossy penumbra variance than the
  paper's rectangular light LTC/MIS implementation.
- Animated one sample output flickers without TAA or SIGMA by design.

## Validation

CPU reference tests cover correlated ratio endpoints, matched current-frame
accumulation, per-receiver normalization, analytic-response-weighted MSAA
resolution, diffuse-only closest ownership, sparse coverage at 2x through 16x,
low-response deterministic composition, binary-visibility threshold bypass,
invalid and equal-depth ownership, failed-owner reset, per-channel finite
sanitization, closest-only ray budgets and direct output routing, single-covered
on/off parity, geometric bias, precomputed noise sampling, hit-distance
sentinels, and settings bounds. Source and shader tests protect
coherent `Texture2DMS` loads, the two-output MSAA contract, the 1x-through-16x
pipeline matrix, geometric normal, single origin bias, absence of private ratio
history, owner selection before ray work, original sample-sequence identity,
material-aware candidate traversal, single-sample-only hit output and sun-denoising
routing, command domains, and exact light matching. The closest-surface
resolve validates exact format/topology contracts and returns explicit success;
on failure, GI and denoisers skip the frame, TAA presents the current raw MSAA
color, and temporal histories reset instead of reading stale resolved depth or
motion. Release verification
compiles every required shader variant and exercises the exact renderer
artifact.
