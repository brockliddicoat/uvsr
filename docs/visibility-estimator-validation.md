# Visibility Estimator Validation

UVSR keeps `PaperAngular` as the shipping estimator while `GTUniform` is an
explicit comparison path. `GTCosine` is reserved but is not implemented until
the uniform formulation clears the renderer and hardware promotion gates.

The formulation follows the GT-VBAO family described in
[Ground-Truth Ambient Occlusion](https://arxiv.org/abs/2301.11376) and the
optimized uniformly weighted reference published as
[GT-VBAO (uniformly weighted)](https://www.shadertoy.com/view/4cdfzf).

## Shared estimator contract

`src/visibility_estimator_shared.h` is compiled as both C++ and HLSL. The CPU
reference and renderer therefore execute one source definition for the slice
basis, projected-normal convention, perspective thickness direction, GT CDF,
endpoint interval, AO resolve, GI weight, and normalization.

For a receiver-to-camera unit vector `V`, positive slice direction `S`, and
receiver normal projected into the slice, UVSR uses

```text
Nslice = cosGamma * V - sinGamma * S
```

For a unit direction `D`, let `side` be the sign of `dot(D, S)` and let
`horizonCos = dot(D, V)`. The no-`acos` uniform-solid-angle slice CDF is

```text
u(D) = 0.5 * (1 + sinGamma + side * (1 - horizonCos))
```

Within the receiver hemisphere, `du = 0.5 * abs(sin(theta)) dtheta`, so the
CDF integrates to one and each of the 32 bits represents exactly `1/32` of the
uniform hemisphere measure. Front and back values are quantized with one
coherent stochastic phase for the slice. Sorting remains defensive release
behavior; the debug result separately checks the expected same-side endpoint
order so a sign error is observable rather than hidden.

AO is the open-sector fraction. GTUniform GI samples the front-surface source
kernel and uses

```text
weight = newlyCoveredMass * receiverCosine * sourceFacingCosine
irradiance = 2 * pi * mean(weightedSource)
```

The factor is `2*pi` because the sectors represent a uniform hemisphere PDF of
`1/(2*pi)`. The receiver cosine remains explicit for this uniform PDF. A future
cosine-weighted CDF must remove that factor from the sample weight and derive
its own normalization; combining its CDF with this equation would count the
receiver cosine twice.

Perspective thickness extends the sampled point along its own camera ray:

```text
sampleBackVS = sampleVS + normalize(sampleVS) * thickness
```

The orthographic path instead uses the camera's constant away direction. The
CPU tests prove that perspective front and back points retain identical `xy/w`,
that thickness moves away from the camera, and that forward/reversed depth do
not alter this view-space construction.

## Deterministic reference suite

`uvsr_visibility_estimator_tests` prepares the same receiver, radial source
samples, slice, projection model, thickness, source radiance, sidedness, screen
bounds, and sample order for both estimators. A 131,072-direction integration
determines interval ownership from geometric signed angles, independently of
either estimator's CDF. Near-to-far ownership is preserved.

UVSR has one source-radiance/normal G-buffer sample at each front point. The
reference consequently holds that sampled source kernel constant over its
virtual thickness interval while explicitly integrating the interval's solid
angle. It does not treat the synthetic back point as a second emitting surface.

The asset-free fixtures are:

1. Infinite floor and wall.
2. Thin vertical card.
3. Two overlapping thin cards.
4. Fence or evenly spaced bars.
5. Wide-FOV off-axis receiver.
6. Near-plane-crossing geometry.
7. Orthographic camera reference.
8. Small bright emissive source.
9. Double-sided emissive card.
10. Flat geometry with a high-frequency normal map.
11. Fully metallic receiver.
12. Black diffuse receiver.
13. Diffuse furnace and finite multibounce energy.
14. Screen-edge emitter entering and leaving the viewport.
15. Foreground/background depth-layer ambiguity.

Across 2,048 coherent sector phases per fixture, the initial CPU baseline is:

| Estimator | Signed mean AO bias | AO RMSE | GI luminance RMSE | Mean GI chroma error | AO P95 | AO P99 | Mean AO phase variance |
|---|---:|---:|---:|---:|---:|---:|---:|
| PaperAngular | 0.0272703 | 0.0693457 | 0.6425340 | 0.0092881 | 0.1653101 | 0.1764001 | 0.0002806 |
| GTUniform | -0.0000012 | 0.0000142 | 0.0002069 | 0.0000673 | 0.0000216 | 0.0000245 | 0.0002442 |

These values establish estimator bias only. They are not renderer image-quality
or performance evidence.

## Promotion status

GTUniform remains non-default until all required runtime permutations compile
and the benchmark record includes HDR-FLIP, NRA-RTAA disocclusion behavior,
GPU timestamps, register count, occupancy, measured traffic, and memory on the
available adapters. The benchmark JSON schema deliberately requires those
fields so an incomplete CPU-only record cannot be mistaken for promotion data.

### Local Integration Check — 2026-07-13

- Release renderer and all four CTest targets passed with MSVC 19.44.
- ShaderMake compiled every affected PaperAngular and GTUniform AO, GI,
  combined, debug, metadata, and reinjection permutation.
- An unattended default-frame smoke test remained healthy for 35 seconds at
  1920x1080 on an NVIDIA GeForce RTX 4090 Laptop GPU. The captured configuration
  was deferred AO+GI, PaperAngular, 32 SPP, radius 3, thickness 0.5, one bounce,
  exact depth, perspective projection, and Medium/Balanced NRA-RTAA.
- The Windows input-automation helper's activation step reproducibly faults in
  pinned GLFW's `_glfwPollEventsWin32`; passive frame capture works. Therefore
  this check does not claim Paper/GT screenshots, per-stage timings, HDR-FLIP,
  disocclusion, register, occupancy, or traffic evidence.

The estimator remains non-default and unpromoted.
