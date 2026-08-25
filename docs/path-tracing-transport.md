# Path Tracing Transport

## Product Contract

UVSR has one conventional DX12 ray-query path tracer. Its transport recipe is
fixed in `src/path_tracing_settings.h` and mirrored in the shader:

- four maximum bounces;
- at most one fresh sample per pixel per submitted frame, stopping only at the
  terminal `UINT32_MAX` count used to prevent overflow;
- Russian roulette beginning at continuation three;
- uniform selection among submitted analytical lights; and
- one fixed balance-heuristic mixture of diffuse and GGX BSDF proposals.

These constants are not UI settings, commands, snapshot fields, presets,
permutations, or compatibility aliases. `lighting.solution=path-tracing` is the
only user-facing selection. There is one main compute-shader task and one
pipeline; an unavailable ray-query pipeline does not select an alternate
tracer.

## Supported Domain

The tracer consumes the same scene representation and bindless material data as
Ray Marching. It supports opaque and alpha-tested triangles, including
single/double-sided rules, base color, opacity cutoff, metalness or reconstructed
specular/gloss parameters, roughness, tangent-space normals, emissive radiance,
and instance motion. Material-aware candidate handling rejects single-sided
backfaces and alpha-test failures before committing the triangle.

Blended, transmissive, subsurface, hair, curve, and participating-media
transport are outside the product boundary. They must not be treated silently
as opaque. Bistro Interior and San Miguel are the retained representative
scenes; their packaged geometry and material bytes remain protected.

## Fixed Integration

Each pixel generates one jittered camera ray from its successful-sample index
and the selected retained noise texture. A committed hit reconstructs position,
previous position, geometric and shading normals, tangent frame, UV, and
material state from bounded scene buffers. Geometric normals own ray-origin
offset and hemisphere validity; shading normals own BSDF response.

At every hit the integrator adds non-negative emissive radiance and one
analytical-light next-event sample. Uniform light selection has probability
`1 / lightCount`; the selected light's own directional density completes the
Monte Carlo weight. Directional, point, spot, and the camera flashlight share
the analytical-light contract. Finite emitters sample a real endpoint and the
visibility ray terminates at that endpoint after the robust origin offset.

The continuation sampler uses Lambert diffuse and GGX reflection. Invalid,
non-finite, or negative throughput rejects the sample. Environment misses add
the selected HDR radiance when the background/transport boundary allows it.
The fixed recipe does not expose alternate light distributions, solver modes,
sample counts, bounce controls, reuse policies, denoisers, or debug reservoirs.

Path Tracing bypasses the raster G-buffer, screen-space AO/GI composition,
selective directional/sky/flashlight visibility passes, TAA, and MSAA. Those
features remain protected in Ray Marching; their controls are not path-tracer
transport options. The shared PBR boundary is described in
[PBR Foundation](pbr-foundation.md).

## History and Resources

The path tracer always owns one cumulative scene-linear RGB mean and one
successful-sample count. A finite accepted contribution, including black or an
environment miss, advances the count exactly once while it is below
`UINT32_MAX`. That terminal value is overflow safety, not a selectable history
cap; it performs no fresh sample or advance. An invalid attempt leaves a valid
prior mean/count unchanged. A non-finite stored mean is instead repaired to
empty history and published even when the new attempt is rejected. The mean
uses the direct cumulative update `mean += (sample - mean) / count`; there is no
exponential, adaptive, capped, or selectable history policy.

The pass allocates five full-resolution resources:

| Resource | Format | Bytes per Pixel |
| --- | --- | ---: |
| Cumulative mean | `RGBA32_FLOAT` | 16 |
| Successful count | `R32_UINT` | 4 |
| First-hit motion | `RGBA16_FLOAT` | 8 |
| First-hit depth | `R32_FLOAT` | 4 |
| Retry generation | `R32_UINT` | 4 |
| **Total** |  | **36** |

That format calculation is about 71.2 MiB at 1920x1080 and 284.8 MiB at 4K,
excluding the light/constant buffers and driver allocation overhead. It is a
resource-model calculation, not measured residency. Motion and depth are
current first-hit outputs. Retry generation changes the phase and seed after a
rejected attempt, clears on acceptance or history reset, and is neither a
second product count nor a radiance history.

External/invalidation clears occur on first use, output-extent or binding
re-creation, an explicit reset, or a changed renderer lighting-history epoch.
The epoch must change for camera motion/cuts, scene, geometry, instance
transform, material, lights, environment/exposure affecting radiance,
resolution, noise, lighting solution, or any other image-defining setting.
Separately, a non-finite stored mean is repaired to empty history as numerical
safety. Changing presentation-only tone mapping must not corrupt scene-linear
ownership.

## Extension Boundary

Keep one baseline before considering any transport experiment. A candidate
belongs on an isolated branch and receives no UI, commands, snapshots, saved
compatibility, or shader matrix until reproducible equal-time comparison beats
this tracer. Define image-error/noise targets, high-sample ground truth,
frame-time and memory budgets, motion/reset cases, and a retirement date first.
The [engine cutdown archive](postmortem/engine-cutdowns/README.md) preserves
historical recovery evidence without production hooks.

## Validation

Focused CPU/source tests must cover fixed constants, buffer layouts, bounded hit
reconstruction, alpha testing, BSDF evaluation/sampling, light PDFs, finite
emitter endpoints, cumulative arithmetic, count truthfulness, and invalid
sample behavior. Shader compilation proves only that the contract is wired.

Runtime validation must use the exact developer or production artifact and bind
source identity, settings hash/version, executable SHA-256, adapter, scene,
camera, resolution, HDR environment, warmup, and capture window. Compare
representative Bistro and San Miguel static views against high-sample output;
exercise motion, disocclusion, camera cuts, scene changes, resize, material,
lighting, environment, and settings resets. Record frame time and resource
residency. A non-black frame or a source-spelling test is not convergence,
quality, performance, or package proof.
