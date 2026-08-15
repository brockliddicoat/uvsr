# Screen Space Visibility

## Product Contract

UVSR uses one current frame screen space pass for ambient occlusion and one
bounce indirect diffuse. The two consumers share depth and normal traversal,
sample scheduling, automatic reduced-resolution upsampling, and composition,
but retain independent
enable, intensity, and **Output Hit Distance** controls.

Visibility does not enable, disable, or reconfigure PBR, sky, lights, shadows,
or anti aliasing. When neither AO nor indirect diffuse is active, the pass and
its optional resources are skipped.

## Current Pipeline

The retained pipeline has three measured stages:

1. **First Trace** reads the closest visible surface and evaluates the selected
   estimator for the exact runtime sample count. Requested AO and GI physical
   hit distances are written by the same samples.
2. **Upsample** bypasses full-resolution results or applies the one automatic
   depth- and normal-guided four-tap upsample to reduced-resolution input.
3. **Composition** applies AO and/or indirect diffuse to deferred lighting.

There is no visibility-owned temporal stage, depth hierarchy, recursive bounce
frontier, indirect dispatch planner, or fused AO-only application route.
Renderer TAA remains the complete image temporal reconstruction system. An
optional NRD backend may instead own a dedicated AO or GI denoising history
after the raw trace; it does not restore history inside this pass.

## Quality Recipes

All recipes enable AO and indirect diffuse and use a radius of 3, thickness of
0.5, and distribution exponent of 2. Noise is configured independently and is
neither changed by a recipe nor part of its **(Custom)** identity.

| Recipe | Resolution | Estimator | Samples | Automatic Upsample | Precision |
| --- | --- | --- | ---: | --- | --- |
| Low | Quarter | Projected Angle | 8 | Guide Aware | 16-bit |
| Medium | Half | Solid Angle | 8 | Guide Aware | 16-bit |
| High | Full | Solid Angle | 16 | Bypassed | 16-bit |
| Ultra | Full | Solid Angle | 48 | Bypassed | 32-bit |

Changing any recipe-owned control preserves its origin and appends
**(Custom)** while the reset icon indicates the change. Each owned control can
return to its originating recipe value, while the profile's circular reset
restores the complete High recipe. A custom setting does not create a different
runtime planner; it directly configures the same operational
pass. Ambient occlusion retains one Strength control and has no separate
contrast or power axis.

## Estimators

The three estimators differ only in how a sample represents its angular
measure:

- **Projected Angle** uses uniform projected angular sectors.
- **Solid Angle** uses uniform solid-angle weighting.
- **Cosine Weighted** applies the cosine-weighted solid-angle estimator.

The C++ reference functions and HLSL use the same clipping, reverse-Z,
degenerate-segment, and normalization contracts. See
[Visibility Estimator Validation](visibility-estimator-validation.md).

## Noise Sampling

Ambient occlusion and diffuse illumination share one **Specify Noise** override
because they share one trace dispatch. With the override off, the pass inherits
the global Noise drawer. With it on, private Pattern, Resolution, and Animate
Samples controls select **Spatial White**, **Spatial Blue**, or
**Spatiotemporal Blue** at 64x64, 128x128, 256x256, or 512x512.

The selected precomputed `R8_UNORM` texture is point-loaded with coordinates
centered in the local sampling-resolution dispatch. Animated spatial modes
translate their complete tile; Spatiotemporal Blue keeps XY fixed and advances
through 64 array layers. The pass advances its own phase only after a successful
animated dispatch. Noise changes reset that phase and downstream image history,
but do not add visibility-owned temporal accumulation. See [Noise](noise.md).

## Sampling Controls

The sample count is an exact runtime value from 1 through 64 and defaults to 16
through the High recipe. Radius controls the maximum screen space reach,
thickness controls depth acceptance, and the distribution exponent shifts
steps toward or away from the receiver over `0.25` through `8`. AO Strength
covers `0` through `8`, and diffuse illumination intensity defaults to `1`.
Counts are not rounded to a fixed shader family.

All supported combinations use the same guarded trace implementation with a
small parity specialization where loop structure materially differs.

## Automatic Upsampling

Diffuse exposes no reconstruction mode, packed metadata, spatial-filter toggle,
filter selector, or filter-radius control. A full-resolution trace proceeds
directly to composition. A half- or quarter-resolution trace uses the same
four-tap depth- and normal-guided upsample automatically. A denoiser result that
already has the valid full output extent bypasses this upsample independently
for AO and GI.

Joint Bilateral and Gaussian Bilateral now belong to each signal in the
Denoising drawer. They are executable first-party spatial methods rather than
Diffuse reconstruction choices.

## Buffers and Lifetime

Resources are allocated only for active consumers and the selected resolution
and precision. The current pass owns raw/final AO and raw/final indirect
textures, its constant buffer, and the active binding/pipeline cache. It owns no
packed reconstruction metadata. The shared Noise texture belongs to the central
library and is reported once rather than duplicated as pass working memory.

It does not own motion vectors, previous-frame color/depth, temporal moments,
bounce history, activity flags, indirect-dispatch arguments, or a depth
pyramid. Removing those surfaces also removed their clears, resize paths,
bindings, statistics, shader keys, and per-pixel history cost.

## Physical Hit Distance Outputs

AO and GI each expose an independent **Output Hit Distance** switch. Both
default off. When enabled for an active consumer, the trace writes an R16
physical view space distance matched to the aggregate signal. AO uses the
expected first bounce distance over the same 32 equal measure sectors that form
its visibility mask: a blocked sector contributes its first sampled blocker
distance and a visible sector is censored at the configured trace radius. GI
uses the first bounce distance weighted by the NRD luminance of each exact RGB
term that contributes to raw GI. Invalid AO receivers and GI pixels without a
positive finite contribution use zero. Neither guide changes the raw AO or GI
equation.

Hit output does not add samples or traversal work. It selects a shader variant,
allocates one raw distance texture per requested signal, and adds an output
write and memory traffic. The feature therefore has a real but usually smaller
cost than increasing samples; measure the complete frame before choosing it for
anything other than a denoiser that needs the data.

The Denoising drawer starts AO and GI at Raw. Both also support the first-party
Joint Bilateral and Gaussian Bilateral methods without hit distance or NRD. AO
optionally adds ReBLUR; GI optionally adds ReBLUR or ReLAX. Selecting any method
does not enable Output Hit Distance, change the Diffuse recipe, or alter
sampling. Missing distance data or an unavailable optional NRD backend leaves a
selected third-party route on the raw signal.

## Debug and Performance

The expanded-by-default Debug drawer's Visibility group selects **Default**, **Ambient
Visibility**, **Traced Indirect**, or **Applied Indirect** while retaining the
selected World appearance. A Physically Based Lighting filter keeps Visibility
executing but suppresses ordinary Visibility composition; an explicit
Visibility view takes precedence when both selectors are active. No debug
choice silently enables or disables material or lighting modes.

Performance reports First Trace, Upsample, Composition, their exclusive
sum as Complete Effect, logical texture payloads, and active resource/dispatch
counts in a labeled table only after every submitted stage query for one
latency slot has completed. The named values publish as one snapshot; a stage
that was not submitted for that frame, such as reconstruction when NRD owns it,
publishes zero instead of retaining an older value. The inclusive
callback envelope is not presented as the effect cost. **Ambient Occlusion
Denoise** and **Diffuse Illumination Denoise** are separate rows rather than
being folded into the visibility total. The table reports an explicit
unavailable state while Visibility is
dormant or its first query is pending. It does not expose
benchmark runs, planner identities, avoided-profile estimates, or export
schemas.

## Validation Boundary

The retained automated boundary includes:

- deterministic estimator and projection fixtures;
- deterministic settings-inheritance, centered-noise, phase, and asset fixtures;
- source contracts for exact current names, hit output variants, and removed
  temporal/bounce paths;
- shader-manifest expansion and runtime package checks;
- quality-recipe and resource-lifetime tests; and
- a Release renderer build plus runtime scene smoke.

Performance claims require a separate controlled comparison. A smaller shader
catalog or a faster isolated stage is not by itself proof of a faster complete
frame.

## Restoration Boundary

Do not restore selectable Diffuse reconstruction, private visibility history, a
depth hierarchy, recursive diffuse bounces, packed/fused AO only profiles, or planner/benchmark infrastructure
without a current product need and equal-quality complete-frame evidence.
Restoration must include the full CPU/HLSL ABI, resources, packaging,
validation, and memory cost rather than only a shader file.
