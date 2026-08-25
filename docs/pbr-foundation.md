# PBR Foundation

## Product Boundary

UVSR has one physically based material contract and two lighting solutions.
Ray Marching uses deferred raster lighting plus protected visibility effects.
Path Tracing reconstructs the same supported material data at committed DXR
triangle hits. Generic forward, legacy, and alternate deferred renderer modes
are not product choices.

The supported domain is opaque and alpha-tested triangle geometry. Blended and
transmissive materials remain outside binary ray visibility and complete path
transport. Adding a material domain requires an explicit renderer contract,
shared CPU/HLSL representation, resource evidence, and focused rendered tests.

## Materials and Geometry

The retained material model consumes base color, opacity/mask state, metalness
or reconstructed specular/gloss parameters, roughness, tangent-space normal,
emissive radiance, material ambient occlusion, and double-sided state. Material
AO affects ambient Ray Marching response; it is not multiplied into complete
path transport.

The packed G-buffer stores the attributes needed by deferred lighting, motion,
AO/GI, picking, and debug views. Its shading normal drives the BSDF. Its flat,
view-facing geometric normal comes from the triangle plane and owns hemisphere
gates and ray-origin offset. Smooth or mapped normals must not weaken separation
from the actual acceleration-structure triangle.

MSAA preserves every covered G-buffer sample through material decode, direct
lighting, and HDR resolve. Motion and visibility guides exist only when an
active consumer requires them.

## Direct Lighting and Visibility

Directional, point, spot, and camera-flashlight lights share one analytical
lighting contract. Diffuse and specular terms use the same prepared surface and
visibility result for that source. The flashlight remains one analytical spot
light with its retained two-lobe beam, finite spherical emitter, camera-relative
motion, collision handling, and independent ray-traced shadows; it is not
duplicated by a private light or raster-shadow system.

Directional shadows use direct binary ray-query visibility. Their only product
controls are enable, maximum distance, and ray bias. At 1x the pass traces the
active receiver. At 2x, 4x, 8x, and 16x MSAA it must trace and apply visibility
independently for every valid covered sample. Broadcasting one pixel result is
not correct at mixed-primitive or shadow-boundary coverage.

Alpha-tested candidates commit only when sampled base opacity passes the
material cutoff. Single-sided backfaces are rejected; double-sided geometry
remains visible from either side. An unavailable producer must fail visibly or
use the explicitly defined neutral result, never stale history from another
light or surface.

Directional, flashlight, and sky signals keep their supported Raw,
first-party spatial, and eligible NRD denoising choices. A denoiser may consume
only a signal and guides with matching extent, ownership, and hit-distance
semantics. Missing prerequisites leave the raw signal valid. MSAA validation
must cover every sample count with raw and each eligible filtering route.

## Environment, AO, and GI

One selected HDR environment supplies Lambert-convolved SH9 diffuse irradiance,
GGX-prefiltered specular radiance, the split-sum environment BRDF input, and the
optional visible background. Diffuse/specular IBL toggles and strengths are
independent; exposure scales the common source. UVSR retains all six packaged
HDR sources and has no hidden procedural or hemispherical ambient fallback.

Ray-traced sky visibility can affect diffuse and specular IBL independently and
retains its enable, hit-distance, sample, noise, distance, bias, and denoising
controls. A missing environment resolves to zero environment contribution; it
must not reuse an earlier cube.

Screen-space visibility retains every user-facing AO/GI combination, quality,
resolution, estimator, sampling, precision, noise, and filter choice. AO
modulates ambient response. One-bounce GI adds current-frame diffuse transport
from its defined source radiance. Their raw signals and eligible denoiser
histories remain separate from TAA and from the fixed path-tracer mean.

## Standard Path Tracer

The standard tracer bypasses the raster G-buffer, screen-space AO/GI
composition, selective directional/sky/flashlight passes, TAA, and MSAA. It
reconstructs material and geometric data directly at committed hits, evaluates
emission, uniformly selected analytical lights, environment misses, Lambert
diffuse, and GGX reflection, then advances one cumulative mean/count history
while its count is below terminal `UINT32_MAX` overflow safety.

Its fixed recipe is four maximum bounces, at most one fresh sample per pixel per
frame until that terminal count, and Russian roulette beginning at continuation
three. The terminal case performs no fresh sample or advance and is not a
selectable history cap. No configurable transport or accumulation policy
exists. See [Path Tracing Transport](path-tracing-transport.md).

## Tone Mapping and Debugging

AgX maps scene-linear HDR radiance for display. Optional automatic exposure
meters a GPU luminance histogram and changes only the exposure multiplier before
the same display transform. Tone mapping, output transfer, and dithering must
not enter AO/GI or path-tracer histories.

Debug views separate world appearance from information. Retained visibility
views show final, ambient visibility, traced indirect, or applied indirect.
Retained PBR filters show final output, normals, environment terms, reflectance,
specular visibility, environment level, or sky visibility. Debug selection may
change presentation but must not change estimator settings or history ownership.

## Validation

CPU and shader-contract tests cover material decode, geometric/shading-normal
roles, BSDF equations, light preparation, environment response, and shared
buffer layouts. Alpha-tested and direct-visibility semantics still require
production-bound shader or rendered evidence. Scene and provenance tests protect
Bistro, San Miguel, every HDR environment, and the retained noise set.

Runtime validation must bind exact source, configuration, settings hash/version,
executable SHA-256, adapter, scene, camera, resolution, warmup, and capture
window. Exercise AO/GI combinations; directional shadows at 1x/2x/4x/8x/16x;
sky and flashlight visibility; both retained scenes; all HDR sources; static,
motion, disocclusion, resize, material, and lighting changes; and every relevant
history reset. Compilation, source spelling, or a non-black image is not visual,
performance, or package proof.

The developer-only `-debug --verify-retained-runtime` diagnostic does not prove
the production executable. Exact-package proof must install and launch the
Release package through the trusted launcher, exercise normal product
interfaces, and preserve the package path, manifest source/configuration,
engine SHA-256, settings identity, commands, and captures. That local DXR
hardware matrix remains open until those artifacts exist.

Production exposes interactive ImGui and command-field controls, keyboard
clipboard screenshots, persistent snapshot codes, and narrow early identity
and settings JSON diagnostics. Exact-package evidence must independently hash
and identity-check the engine, use ordinary scene/size arguments and normal
product settings, and retain settings, camera, captures, and timings. Focused
external [`pixtool.exe`](https://devblogs.microsoft.com/pix/pixtool/) capture
and debug-layer replay are additionally required for barrier, lifetime,
ray-tracing, and Donut-detachment changes; other provenance-bound rendered
capture routes remain valid. Settings without a normal CLI require an
authorized interactive local DXR-hardware run.
