# Screen-Space Visibility

## Product Contract

UVSR uses one current-frame screen-space pass for ambient occlusion and
one-bounce indirect diffuse. The two consumers share depth/normal traversal,
sample scheduling, reconstruction, and composition, but retain independent
enable and intensity controls.

Visibility does not enable, disable, or reconfigure PBR, sky, lights, shadows,
or anti-aliasing. When neither AO nor indirect diffuse is active, the pass and
its optional resources are skipped.

## Current Pipeline

The retained pipeline has three measured stages:

1. **First Trace** reads the closest visible surface and evaluates the selected
   estimator for the exact runtime sample count.
2. **Reconstruction** copies full-resolution results directly or performs the
   selected guide-aware reconstruction for reduced-resolution input.
3. **Composition** applies AO and/or indirect diffuse to deferred lighting.

There is no visibility-owned temporal stage, depth hierarchy, recursive bounce
frontier, indirect dispatch planner, or fused AO-only application route.
Renderer TAA is the sole long-term temporal reconstruction system.

## Quality Recipes

All recipes enable AO and indirect diffuse, use a radius of 3, thickness of
0.5, distribution exponent of 2, and Void Cluster Blue Noise.

| Recipe | Resolution | Estimator | Samples | Spatial Reconstruction | Precision |
| --- | --- | --- | ---: | --- | --- |
| Low | Quarter | Projected Angle | 8 | Joint Bilateral | 16-bit |
| Medium | Half | Solid Angle | 8 | Joint Bilateral | 16-bit |
| High | Full | Solid Angle | 20 | Off | 16-bit |
| Ultra | Full | Solid Angle | 48 | Off | 32-bit |

Changing any recipe-owned control preserves its origin and appends
**(Custom)** while the reset icon indicates the change. Each owned control can
return to its originating recipe value, while the profile's circular reset
restores the complete High recipe. A custom setting does not create a different
runtime planner or shader profile; it directly configures the same operational
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

## Noise Patterns

- **Permutated White Noise** uses an ordinary per-pixel pseudorandom sequence
  passed through a deterministic output permutation.
- **Hashed White Noise** uses the retained independent hash scheduler.
- **Void Cluster Blue Noise** uses UVSR's prepared toroidal void-and-cluster
  rank field.

These names describe user-visible intent. Renaming the two existing options did
not change their algorithms. Noise is current-frame sample placement, not a
history system.

## Sampling Controls

The sample count is an exact runtime value from 1 through 64. Radius controls
the maximum screen-space reach, thickness controls depth acceptance, and the
distribution exponent shifts steps toward or away from the receiver. Counts
are not rounded to a fixed shader family.

All supported combinations use the same guarded trace implementation with a
small parity specialization where loop structure materially differs.

## Reconstruction

One direct-or-guide-aware mode composes full-resolution input directly and uses
depth/normal guides for half- or quarter-resolution input. Its visible label is
**Full Resolution** or **Guide-Aware Upsampling** according to the selected
sampling resolution. Three packed alternatives remain: **Packed
Depth-Normal**, **Packed Slope-Aware**, and **Packed Leak-Controlled**. The old
Packed Depth mode was removed. The Reconstruction group starts collapsed for a
full-resolution trace and expanded for reduced-resolution tracing, then
preserves a user's manual disclosure choice.

Joint Bilateral and Gaussian Bilateral are the two spatial filters. The
old packed/fused AO-only profiles were removed; reconstruction always serves
the active AO-plus-indirect route rather than maintaining a second planner.

## Buffers and Lifetime

Resources are allocated only for active consumers and the selected resolution,
precision, and reconstruction mode. The current pass owns raw/final AO and
raw/final indirect textures, optional reconstruction metadata, its constant
buffer, scheduler data, and the active binding/pipeline cache.

It does not own motion vectors, previous-frame color/depth, temporal moments,
bounce history, activity flags, indirect-dispatch arguments, or a depth
pyramid. Removing those surfaces also removed their clears, resize paths,
bindings, statistics, shader keys, and per-pixel history cost.

## Debug and Statistics

The expanded-by-default Debug drawer's Visibility group selects **Default**, **Ambient
Visibility**, **Traced Indirect**, or **Applied Indirect** while retaining the
selected World appearance. A Physically Based Lighting filter keeps Visibility
executing but suppresses ordinary Visibility composition; an explicit
Visibility view takes precedence when both selectors are active. No debug
choice silently enables or disables material or lighting modes.

Statistics reports First Trace, Reconstruction, Composition, their named-stage
sum, the unattributed timer difference, the complete effect envelope, logical
texture payloads, and active resource/dispatch counts in a labeled table only
after a completed timer query. It reports an explicit unavailable state while
Visibility is dormant or its first query is pending. It does not expose
benchmark runs, planner identities, avoided-profile estimates, or export
schemas.

## Validation Boundary

The retained automated boundary includes:

- deterministic estimator and projection fixtures;
- deterministic noise and scheduler fixtures;
- source contracts for exact current names and removed temporal/bounce paths;
- shader-manifest expansion and runtime package checks;
- quality-recipe and resource-lifetime tests; and
- a Release renderer build plus runtime scene smoke.

Performance claims require a separate controlled comparison. A smaller shader
catalog or a faster isolated stage is not by itself proof of a faster complete
frame.

## Restoration Boundary

Do not restore visibility history, a depth hierarchy, recursive diffuse
bounces, packed/fused AO-only profiles, or planner/benchmark infrastructure
without a current product need and equal-quality complete-frame evidence.
Restoration must include the full CPU/HLSL ABI, resources, packaging,
validation, and memory cost rather than only a shader file.
