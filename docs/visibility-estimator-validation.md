# Visibility Estimator Validation

UVSR compiles three runtime estimators: `UniformProjectedAngle`,
`UniformSolidAngle`, and `CosineWeightedSolidAngle`. Their UI labels are
**Uniform Projected Angle**, **Uniform Solid Angle**, and **Cosine-Weighted
Solid Angle**. Uniform Projected Angle remains the default; all three are
selectable.

The default projected-angle measure follows
[Screen Space Indirect Lighting with Visibility Bitmask](https://arxiv.org/abs/2301.11376).
Uniform Solid Angle and Cosine-Weighted Solid Angle are UVSR alternatives
derived and tested against explicit slice integrals. Activision's
[Practical Realtime Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf)
informs traversal and reconstruction choices, but GTAO does not define UVSR's
finite-thickness bitmask sectors.

## Shared Estimator Contract

`src/visibility_estimator_shared.h` is compiled as both C++ and HLSL. CPU
quadrature and the renderer therefore share the slice basis, projected-normal
sign convention, finite-thickness back direction, CDF mapping, endpoint
interval, AO resolve, GI sector weight, and irradiance normalization.

For receiver-to-camera unit vector `V`, positive slice direction `S`, and a
normal projected into the slice,

```text
Nslice = p * (cosGamma * V - sinGamma * S).
```

The uniform-solid-angle no-`acos` CDF is

```text
u(D) = 0.5 * (1 + sinGamma + side * (1 - dot(D,V))).
```

Within the receiver hemisphere,

```text
du = 0.5 * abs(sin(alpha)) d(alpha),
```

so each of 32 sectors owns `1/32` of the conditional uniform measure.
Uniform-solid-angle GI keeps the front sample's receiver cosine explicit and
normalizes irradiance by `2*pi`.

The complete joint-cosine CDF instead integrates

```text
cos(alpha + gamma) * abs(sin(alpha))
```

over `[-pi/2-gamma, pi/2-gamma]`. Its projected slice mass is

```text
p * (cos(gamma) + gamma*sin(gamma)).
```

The receiver cosine is already present in the CDF and mass. Cosine-weighted GI
therefore multiplies newly covered sector fraction by slice mass and source-
facing cosine, but not receiver cosine again, and uses the outer `pi`
normalization. Averaging complete slice mass over uniformly selected azimuths
recovers one, which is the normalized cosine hemisphere.

Uniform Solid Angle and Cosine-Weighted Solid Angle intentionally estimate
different per-slice approximations. Uniform sectors hold the front sample's
receiver cosine constant over its finite angular interval. Cosine sectors
integrate receiver cosine across that interval. A cosine result should be
compared with joint-cosine quadrature, not with the uniform-sector reference.

Perspective thickness extends each sampled point along its own camera ray:

```text
sampleBackVS = sampleVS + normalize(sampleVS) * thickness.
```

Orthographic projection uses the constant camera-away direction.
`src/visibility_projection_shared.h` analytically clips projected radius
endpoints against positive homogeneous `w` and the active D3D near plane before
the one perspective divide.

## Deterministic Reference Suite

`uvsr_visibility_estimator_tests` uses 131,072-direction quadrature and 2,048
coherent sector phases per fixture. It covers:

1. Infinite floor and wall.
2. Thin vertical card.
3. Two overlapping thin cards.
4. Fence or evenly spaced bars.
5. Wide-FOV off-axis receiver.
6. Near-plane-crossing geometry.
7. Orthographic projection.
8. Small bright emissive source.
9. Double-sided emissive card.
10. High-frequency normal-map tilt, including out-of-plane normals.
11. Fully metallic and black diffuse receivers.
12. Diffuse-furnace finite multibounce energy.
13. Screen-edge emitters.
14. Foreground/background depth-layer ambiguity.

The current deterministic summary is:

| Estimator/reference pair | Signed mean AO bias | AO RMSE | GI luminance RMSE | Mean GI chroma error | AO P95 | AO P99 | Mean AO phase variance |
|---|---:|---:|---:|---:|---:|---:|---:|
| Uniform Projected Angle / uniform fixture reference | 0.0272703 | 0.0693457 | 0.6425340 | 0.0092881 | 0.1653101 | 0.1764001 | 0.0002806 |
| Uniform Solid Angle / uniform quadrature | -0.0000012 | 0.0000142 | 0.0002069 | 0.0000673 | 0.0000216 | 0.0000245 | 0.0002442 |
| Cosine-Weighted Solid Angle / joint-cosine quadrature | -0.0000032 | 0.0000178 | 0.0000539 | 0.0000406 | 0.0000333 | 0.0000396 | 0.0003131 |

These are estimator/quantization checks, not renderer image-quality or runtime
performance evidence. In particular, the projected-angle row is not directly
comparable to the cosine row as a ranking because the references differ.

## Additional Checks

The estimator target also proves:

- the analytic cosine antiderivative against numerical integration across
  normal tilts and out-of-plane components;
- complete projected slice mass and azimuth normalization;
- coherent stochastic endpoint ordering and sector quantization;
- invalid, degenerate, and non-finite inputs fail deterministically;
- source-sidedness, chroma, and finite GI output;
- `2*pi` uniform and `pi` cosine irradiance normalizations; and
- no receiver-cosine double weighting in the cosine path.

`uvsr_visibility_projection_tests` independently covers forward/reversed depth,
perspective and orthographic projection, camera-plane and near-plane crossings,
near-plane receivers, large finite radii, both-side symmetry, and invalid
inputs.

ShaderMake compiles AO-only, GI-only, combined AO/GI, first-bounce metadata,
higher-bounce reinjection, temporal reconstruction, and both spatial-filter
permutations. On 2026-07-14, all 69 configured DXIL tasks compiled with DXC
1.9.2602, and the Release C++ renderer linked with MSVC 19.44.

## Runtime Validation Still Required

Automated evidence is sufficient to make Cosine-Weighted Solid Angle
selectable, but not to make it the default. Human testing should compare thin
geometry, normal-mapped surfaces, off-axis views, near-plane geometry, bright
small emitters, disocclusions, and full/half/quarter resolution. Controlled
hardware records should include GPU timestamps, register count, occupancy,
cache/traffic measurements, memory, and matched captures. No performance win
is claimed without those measurements.
