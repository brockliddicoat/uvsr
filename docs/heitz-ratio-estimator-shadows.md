# Heitz Ratio Estimator Shadows

UVSR's public **Ray Traced Shadows** feature includes the correlated stochastic
shadow estimator described in Eric Heitz, Stephen Hill, and Morgan McGuire's
[2018 paper](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018Shadow.pdf)
and [SIGGRAPH talk](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018SIGGRAPHTalk.pdf).
The UI keeps the technique name out of the main group label and exposes Ratio
Estimator as an independent producer choice.

## Technique Contract

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

## Raw Denoising Route

Turning **Ratio Estimator** off selects one stochastic scalar visibility ray for
a positive angular size. The scalar is replicated across RGB so the same direct
visibility slot can feed deferred PBR. A zero angular size or **Hard Shadows**
uses the center direction instead. Stored sample count remains unchanged but is
inactive on the one ray route.

**Output Hit Distance** independently selects an R16 physical distance output.
A committed triangle records its nearest ray distance, a miss records the
finite FP16 miss value, and an invalid receiver records zero. It defaults off,
does not change ray count, and adds a texture allocation plus one output write.

NVIDIA NRD SIGMA expects the raw shadow and physical hit distance pair. Sun
shadow denoising therefore requires Ratio Estimator off and Output Hit Distance
on. Selecting SIGMA in the Denoising drawer never changes either producer
setting. If the pair or optional NRD backend is unavailable, UVSR applies the
raw shadow and reports the reason.

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

Deferred PBR still supplies analytic `U` using its center direction
approximation. The paper permits an approximate analytic term, but glossy
highlights and horizon clipped emitters are consequently less exact than a
shared analytic directional cap response.

Inline ray queries commit opaque candidates directly. Alpha-tested candidates
fetch the exact instance geometry and material, interpolate the authored UV,
sample base-color alpha when present, and commit only when it passes the
material cutoff. Blended and transmissive domains are excluded from the binary
visibility hierarchy. The same helper and resource contract is used by sky and
flashlight visibility, preventing foliage cards from becoming solid rectangles
in any of the three effects.

## Softness and Sampling

The light's **Angular Size** is a full angular diameter in degrees. UVSR's
primary sun initializes to `0.2 deg` and irradiance `10`. The ray tracer converts
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

For each receiver, UVSR reconstructs the surface and its depth quantization
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
Distance**, and **Ray Bias**. Factory defaults keep the producer disabled, Ratio
Estimator on, Output Hit Distance off, two ratio samples, hard mode off, global
noise inheritance active, and Ray Bias at `0.002`. Enabling Specify Noise
reveals private Pattern, Resolution, and Animate Samples controls.

Commands use the `shadows.ray-traced.` prefix. Changing a sampling setting
restarts deterministic sample phase and any downstream history that depends on
that signal.

## Main Branch Boundary

Screen space directional shadows are removed from main and are not combined
with this result. Their code and debug modes are quarantined with the CSM and
SVSM experiments on local branch `codex/svsm-csm-preserved`.

Each direct visibility texture is matched to the exact light that produced it.
Sun visibility uses the sun slot; flashlight visibility uses a separate slot
and separate finite light query.

## Statistics

The Statistics drawer's Directional Shadows effect and Complete Renderer table
use **Shadow Ray Dispatch** for the raw sun and flashlight query work and
**Shadow Denoise** for the optional SIGMA work. Sky visibility has its own Ray
Dispatch and Denoise rows. A timing is shown only after a completed GPU query;
inactive or newly selected work reports unavailable instead of a fabricated
zero. The removed screen space breakdown does not remain in main.

## Runtime Limits

- DirectX Raytracing 1.1 acceleration structures and inline ray queries are
  required.
- The material dependent RGB ratio result requires single sample deferred
  rendering.
- Opaque and alpha-tested triangle geometry is supported. Alpha-tested
  candidates honor base-color alpha and cutoff; blended and transmissive
  domains are not binary occluders.
- Uniform light only sampling has more glossy penumbra variance than the
  paper's rectangular light LTC/MIS implementation.
- Animated one sample output flickers without TAA or SIGMA by design.

## Validation

CPU reference tests cover correlated ratio endpoints, matched current frame
accumulation, integer sample mapping, geometric bias, hard and raw path
eligibility, precomputed noise sampling, hit distance sentinels, and settings
bounds.
Source and shader tests protect the geometric normal, single origin bias,
absence of private ratio history, material-aware candidate traversal, raw and
hit output variants, command domains, and exact light matching. Release
verification compiles every required shader variant and exercises the exact
renderer artifact.
