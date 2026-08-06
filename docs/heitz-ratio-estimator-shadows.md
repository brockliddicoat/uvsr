# Heitz Ratio-Estimator Shadows

UVSR implements the correlated stochastic shadow estimator described in Eric
Heitz, Stephen Hill, and Morgan McGuire's
[2018 paper](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018Shadow.pdf)
and [SIGGRAPH talk](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018SIGGRAPHTalk.pdf).
It is UVSR's first DirectX Raytracing consumer and its first use of the
Stachowiak/EA-style ratio-estimator pattern intended for later reuse.

## Technique Contract

One compute dispatch accumulates matched RGB estimates of the unshadowed
response `U_N` and visible response `S_N`. Each pair shares the sample
direction, proposal, BRDF, cosine, validity decision, and normalization. Binary
ray visibility is the only difference. After both sums are divided by the same
current-frame sample count, UVSR computes:

```text
W = clamp(S_N / U_N, 0, 1)
final directional light = analytic U * W
```

A channel whose denominator is below `1e-4`, or whose result is non-finite,
resolves to neutral visibility. The shadow resolve bounds `W` to `[0, 1]`; the
shared correlated-ratio helper remains unbounded for future non-shadow users.

The pass owns no spatial denoiser and no temporal history. Numerator and
denominator remain in registers and are divided in the tracing dispatch.
Final-color TAA is the sole temporal accumulator. This avoids filtering a
motion-reprojected ratio history and then filtering it again through TAA, which
was the source of the reported motion smear. Removing the four ping-pong
RGBA16F histories also saves about 63.3 MiB at 1920x1080. When neither TAA nor a
multisample Visibility resolve needs motion vectors, ray-traced shadows no
longer force that approximately 15.8 MiB target or its G-buffer output.

Current-frame division preserves the estimator's matched sampling contract but
does not turn final-color TAA into `sum(S) / sum(U)` across frames. At one sample
per pixel, non-negligible channels reduce to binary visibility for that frame.
Higher counts form the full correlated weighted ratio within the frame.

## UVSR Adaptation

The published rectangular-emitter implementation combines light and LTC BRDF
sampling with adaptive MIS. UVSR currently has a directional emitter and no
shared LTC sampling path, so it uniformly samples the solid-angle cap defined
by the directional light.

**Noise Pattern** selects Permutated White Noise or Void Cluster Blue Noise for
the two emitter coordinates. Blue noise applies independent per-pixel
Cranley-Patterson shifts from UVSR's first-party 64x64 void-cluster ranks to a
progressive low-discrepancy sequence. An integer golden-Weyl phase rotates the
angular coordinate without large-frame float drift.

This adapts the spatial blue-noise rotation and low-discrepancy temporal idea
from Demofox's
[blue-noise soft-shadow article](https://blog.demofox.org/2020/05/16/using-blue-noise-for-raytraced-soft-shadows/),
while retaining UVSR's exact uniform-spherical-cap mapping. The uniform
proposal has one constant PDF; its reciprocal cancels in the correlated ratio
and is omitted from both sums.

Deferred PBR still supplies analytic `U` using its center-direction
approximation. The paper permits an approximate analytic term, but glossy
highlights and horizon-clipped emitters are consequently less exact than a
shared analytic directional-cap response. The current ray query also treats
every triangle as opaque; material-aware candidate evaluation is reserved for
a future consumer-neutral representation contract.

## Softness and Sampling

The light's **Angular Size** is a full angular diameter in degrees. The ray
tracer converts half that value to a cone radius and samples directions
uniformly over the cone:

- `0 deg` is a zero-extent emitter and therefore a hard shadow. Extra samples
  would repeat one ray, so UVSR takes the hard path.
- A positive value produces geometric penumbrae whose width grows with
  blocker-receiver separation. A primary sun with a zero or invalid authored
  extent receives UVSR's `0.53 deg` default; a positive authored value is
  preserved.
- **Hard Shadows** explicitly selects the one-center-ray path without changing
  the stored angular size. It rejects surfaces that cannot receive the light
  before issuing a query, and skips emitter sampling, full material
  preparation, and ratio evaluation.
- **Animate Samples** is directly above **Samples Per Pixel**. When enabled, the
  dedicated phase advances after every actual stochastic shadow dispatch.
  When disabled, phase zero remains fixed.
- The **Samples Per Pixel** control exposes the integer powers of two `1`, `2`, `4`, `8`,
  `16`, `32`, and `64`. Every selected ray is evaluated in the current frame.

Animated output changes even when TAA is disabled. With TAA enabled, only the
renderer-wide final-color history accumulates those changing images. TAA may
still show its own configured temporal persistence, but there is no hidden
shadow history underneath it.

## Ray-Origin Safety

The previous `TMin`-only policy was geometrically incapable of guaranteeing an
exterior origin. If reconstruction leaves a point a signed distance `e` behind
its triangle plane, a same-plane ray hit occurs at:

```text
t = e / dot(N, L)
```

As the light becomes grazing, `dot(N, L)` approaches zero and the required
`TMin` becomes arbitrarily large. That is why even `0.1` could fail in the
reported Sponza view.

UVSR now stores two deliberately distinct G-buffer normals. The shading normal
remains smooth or normal-mapped for the BSDF. The geometric normal is derived
from `cross(ddx(worldPosition), ddy(worldPosition))`, faced toward the camera,
and packed in the existing field. It therefore follows the raster triangle
plane represented by BLAS/TLAS instead of an interpolated vertex normal.

For each receiver, UVSR:

1. reconstructs the surface point and the distance represented by one safer
   depth code;
2. takes the maximum of that internal distance and nonnegative **Ray Bias**;
3. displaces the point once along the view-facing triangle normal;
4. applies a Ray Tracing Gems representable-position nudge along the same
   normal; and
5. traces with `TMin = 0` and the full scene ray distance.

Using the maximum avoids paying the depth floor and user clearance twice.
Keeping `TMin` at zero avoids a second contact-shadow dead zone. The factory
default is `0.002` world units and the diagnostic range is `0` through `0.1`.
Larger values can miss a blocker within that normal gap or detach contact
shadows.

Changing **Ray Bias** does not change dispatch dimensions, sample count, or ray
count. Its fixed arithmetic is negligible relative to tracing. It can alter
which extremely near candidate the hardware accepts, so tiny scene-dependent
timing differences are possible, but bias is a correctness/quality control and
not a meaningful performance setting.

## Producer Composition

Screen-Space Directional Shadows and Ratio-Estimator Ray-Traced Shadows have
independent **Enabled** controls. Both can be off, either can run alone, or both
can run together. Missing or invalid inputs resolve to white, and both-on
composition takes the componentwise minimum:

```text
directional visibility = min(screen-space visibility, ray-traced visibility)
```

The minimum preserves the strongest occlusion without multiplying two
estimates of the same event. Each texture is applied only to the exact
directional light that produced it.

## World-Space Representation

The Representation drawer owns a consumer-neutral triangle acceleration
structure rather than placing it inside the shadow pass:

- **Bounding Volume Hierarchy** selects Fast Trace, Balanced, or Fast Build.
- **Bottom-Level Acceleration Structures** selects Rebuild or Refit for changed
  dynamic mesh geometry.
- **Top-Level Acceleration Structure** selects Rebuild or Refit for changed
  instance transforms.

One BLAS is built per unique instanced triangle mesh. Initial construction is
staged one BLAS per loading frame, followed by a coherent TLAS. Runtime skinned
geometry updates BLAS first and TLAS second. Topology, buffer identity, instance
identity, and transform changes invalidate only the required level. The shadow
pass consumes a ready TLAS but never owns it, leaving the representation
available to later ray-query features.

## Controls and Commands

The Shadows drawer contains two independent groups. Ratio-Estimator Ray-Traced
Shadows exposes **Enabled**, **Hard Shadows**, **Animate Samples**, **Samples Per
Pixel**, **Noise Pattern**, **Max Distance**, and **Ray Bias**. Soft sampling
controls are disabled when explicit hard mode or a zero-extent emitter makes
them irrelevant; Max Distance remains available because it applies to both
hard and soft queries.

Factory defaults keep both shadow producers disabled, set the ratio estimator
to two samples, keep hard mode off, enable sample animation, select Void Cluster
Blue Noise, use a `0.002` triangle-normal bias, and give a zero-extent primary
sun a `0.53` degree diameter. Max preserves the established
`max(sceneDiagonal * 2, 1)` reference reach. The `32m`, `16m`, `8m`, `4m`, and
`2m` modes intentionally ignore farther blockers, so they are bounded
visibility experiments rather than exact sun visibility. Commands use the prefixes
`shadows.screen-space-directional.` and `shadows.ratio-estimator.`. Changing a
shadow sampling setting resets only the renderer's ordinary final-color TAA
state and deterministic sample phase; there is no pass-private history.

## Statistics

The Statistics drawer's **Directional Shadows** effect includes the retained
screen-space breakdown and a **Ratio-Estimator Ray Dispatch** row. The same row
appears in **Complete Renderer**. Its GPU query encloses the complete remaining
Heitz compute dispatch. Newly enabled or inactive work reports unavailable
rather than a fabricated zero, and timing epochs reject stale delayed queries.

## Runtime Limits

- DirectX Raytracing 1.1 acceleration structures and inline ray queries are
  required. Unsupported adapters keep the producer unavailable.
- The material-dependent RGB result currently requires single-sample deferred
  rendering. Screen-space shadows and MSAA remain independently available.
- Triangle geometry is supported. The first query conservatively treats
  alpha-tested, blended, and transmissive domains as solid occluders.
- Uniform light-only sampling is valid but has higher glossy-penumbra variance
  than the paper's rectangular-light LTC/MIS implementation.
- Animated one-sample output flickers without TAA by design. Disabling animation
  produces stable but spatially fixed Monte Carlo error.

## Validation

CPU reference tests cover correlated-ratio endpoints, current-frame matched
accumulation, integer sample-rate mapping, geometric bias at grazing angles,
hard-path eligibility, blue-noise sampling, and settings bounds. Source tests
protect the true raster triangle normal, single-application origin bias,
absence of fractional/private-history/hashed-noise surfaces, compact bindings,
statistics timing, and UI command domains. Release verification additionally
compiles every G-buffer and Heitz shader permutation, checks both shader bundles,
runs the full CTest suite, and exercises the exact renderer artifact.
