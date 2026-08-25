# Visibility Estimator Validation

UVSR compiles three runtime estimators: `UniformProjectedAngle`,
`UniformSolidAngle`, and `CosineWeightedSolidAngle`. Their UI labels are
**Projected Angle**, **Solid Angle**, and **Cosine Weighted**. Solid Angle is the
factory default; all three remain selectable.

The projected-angle measure follows
[Screen Space Indirect Lighting with Visibility Bitmask](https://arxiv.org/abs/2301.11376).
Solid Angle and Cosine Weighted are UVSR alternatives tested against explicit
slice integrals. Activision's
[Practical Realtime Strategies for Accurate Indirect Occlusion](https://research.activision.com/publications/2020-03/practical-real-time-strategies-for-accurate-indirect-occlusion)
informs traversal and reconstruction, but GTAO does not define UVSR's finite-
thickness bitmask sectors.

## Shared Estimator Contract

`src/visibility_estimator_shared.h` compiles as both C++ and HLSL. CPU quadrature
and the renderer therefore share the slice basis, projected-normal sign,
finite-thickness back direction, CDF mapping, endpoint interval, AO resolve, GI
sector weight, and irradiance normalization.

For receiver-to-camera unit vector `V`, positive slice direction `S`, and a
normal projected into the slice:

```text
Nslice = p * (cosGamma * V - sinGamma * S)
```

The solid-angle no-`acos` CDF is:

```text
u(D) = 0.5 * (1 + sinGamma + side * (1 - dot(D,V)))
```

Within the receiver hemisphere, `du = 0.5 * abs(sin(alpha)) d(alpha)`, so each
of 32 sectors owns `1/32` of the conditional uniform measure. Solid-angle GI
keeps the front sample's receiver cosine explicit and normalizes irradiance by
`2*pi`.

The joint-cosine CDF integrates:

```text
cos(alpha + gamma) * abs(sin(alpha))
```

Its projected slice mass is
`p * (cos(gamma) + gamma*sin(gamma))`. Receiver cosine is already present in
that CDF and mass, so Cosine Weighted GI multiplies new sector fraction by slice
mass and source-facing cosine, does not apply receiver cosine again, and uses
the outer `pi` normalization.

Perspective thickness extends each sample along its camera ray:

```text
sampleBackVS = sampleVS + normalize(sampleVS) * thickness
```

Orthographic projection uses the constant camera-away direction.
`src/visibility_projection_shared.h` clips projected radius endpoints against
positive homogeneous `w` and the active D3D near plane before the one
perspective divide.

## Deterministic Reference Suite

`uvsr_visibility_estimator_tests` uses 131,072-direction quadrature and 2,048
coherent sector phases per fixture. It covers:

1. Infinite floor and wall.
2. Thin and overlapping cards.
3. Fence-like repeated bars.
4. Wide-FOV off-axis receivers.
5. Near-plane-crossing geometry.
6. Orthographic projection.
7. Small bright and double-sided transported sources.
8. High-frequency normal-map tilt.
9. Metallic and black diffuse receivers.
10. Screen-edge emitters.
11. Foreground/background depth ambiguity.

The current deterministic summary is:

| Estimator and Reference | Signed Mean AO Bias | AO RMSE | GI Luminance RMSE | Mean GI Chroma Error | AO P95 | AO P99 | Mean AO Phase Variance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Projected Angle / uniform fixture | 0.0272703 | 0.0693457 | 0.6425340 | 0.0092881 | 0.1653101 | 0.1764001 | 0.0002806 |
| Solid Angle / uniform quadrature | -0.0000012 | 0.0000142 | 0.0002069 | 0.0000673 | 0.0000216 | 0.0000245 | 0.0002442 |
| Cosine Weighted / joint-cosine quadrature | -0.0000032 | 0.0000178 | 0.0000539 | 0.0000406 | 0.0000333 | 0.0000396 | 0.0003131 |

These are estimator/quantization checks, not renderer image-quality or runtime
performance evidence. The references differ, so the rows are not a direct
visual ranking.

## Additional Checks

The estimator target also proves:

- the analytic cosine antiderivative against numerical integration;
- complete projected slice mass and azimuth normalization;
- coherent stochastic endpoint ordering and sector quantization;
- deterministic failure for invalid, degenerate, and non-finite inputs;
- source sidedness, chroma, and finite one-bounce GI output;
- `2*pi` solid-angle and `pi` cosine normalization; and
- no receiver-cosine double weighting.

`uvsr_visibility_projection_tests` independently covers forward/reversed depth,
perspective/orthographic projection, camera-plane and near-plane crossings,
large finite radii, symmetry, and invalid inputs.

Shader packaging covers AO-only, GI-only, and combined AO/GI for the retained
current-frame trace and both spatial filters. It does not compile bounce
reinjection or visibility temporal reconstruction.

## Runtime Validation Still Required

Automated evidence makes all three estimators selectable but does not make a
visual or performance claim. Compare thin geometry, normal-mapped surfaces,
off-axis views, near-plane geometry, bright small sources, disocclusions, and
full/half/quarter resolution on the exact candidate. A controlled performance
record must fix the executable, adapter, driver, scene, camera, resolution,
settings, warmup, and sample window and report complete-frame time alongside
the visibility stages.
