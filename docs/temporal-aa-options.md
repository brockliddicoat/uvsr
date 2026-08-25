# Temporal Aliasing Options

## Independent Techniques

The **Aliasing** drawer exposes three animated, independently collapsible,
default-off techniques:

- **Temporal Reconstructive** performs long-term temporal reconstruction in
  scene-linear RGB.
- **Fast Approximate** adapts Google Filament's FXAA implementation for
  current-frame edge smoothing after tone mapping.
- **Multisample Adaptive** preserves 2x, 4x, 8x, or 16x G-buffer samples
  through deferred material decode and lighting before the high-dynamic-range
  resolve.

The controls are not a mutually exclusive method dropdown. Any combination is
valid. The deterministic order is MSAA resolve, TAA, tone mapping, then Fast
Approximate AA. All three disabled is also a supported configuration.
Every technique shows a Low, Medium, High, or Ultra **Quality** selector while
enabled, followed by an animated **Advanced** disclosure that starts collapsed.
Disabling a technique preserves its stored values.

## Main Temporal Controls

The normal Temporal Reconstructive surface contains:

- **Enable**;
- **Quality**: Low, Medium, High, or Ultra; and
- a default-closed **Advanced** disclosure.

Lower-level policies comparable to Wicked Engine's implementation live directly
under Advanced.
Its final default-closed **Cost** submenu contains **Mode** with Full Quality,
Reduced, and Minimum choices plus the cost-owned overrides.

## Jitter Sequence Contract

The Jitter Sequence selector is the first control under Advanced. It contains
every pattern exposed by Google Filament at
pinned revision `47c86eec22e56d75897e16651eb4d2abd64fc29a`:

- **Rotated Grid 4**;
- **Uniform Helix 4**;
- **Halton 8**;
- **Halton 16**, the UVSR factory default; and
- **Halton 32**.

The Halton choices use Filament's exact shared sequence, including its 409-entry
radical-inverse skip. They are not the older UVSR sequence that began at index
zero. The stored samples are centered to the half-pixel footprint and passed
directly to Donut's DirectX 12 planar view without Filament's non-DX12 Y flip.

**Sobol 32** is one additional experimental fixed pattern. It comes from
Helmer, Christensen, and Kensler's stochastic Sobol (0,2) construction. The
seed produces the initial point directly. For each subsequent point, the
`--bn2d` best-candidate path evaluates 100 candidates in the required Sobol
stratum and chooses the one with the greatest minimum toroidal distance from
the points already selected. The 2-, 4-, 8-, 16-, and 32-sample prefixes
therefore retain their base-2 net stratification. In the checked table, those
prefixes have greater toroidal minimum pair separation than the matching pinned
Filament Halton prefixes. That is the supported meaning of the optimized
construction; it is not a claim that Sobol 32 produces a better final image in
every scene.

The fixed table is exactly reproducible from pinned source
`f90b115806675035c8c727bab4575ca5ba1760b6`: change the generator declaration
to `RNG rng(43);`, run
`./generate_samples --seq=ssobol --n=32 --nd=2 --bn2d`, subtract 0.5 from each
coordinate, and store the results as floats. Seed 43 removes the upstream
nondeterministic default. The source method and implementation are recorded in
the
[EGSR 2021 paper](https://diglib.eg.org/items/cd326c49-1f97-437b-8280-f989181e52e4)
and [pinned author code](https://github.com/Andrew-Helmer/stochastic-generation/tree/f90b115806675035c8c727bab4575ca5ba1760b6).

Jitter Sequence remains independent from the Quality and Cost recipes. Its
reset returns to Halton 16, restoring sixteen unique subpixel locations before
repetition for longer edge diversity. Halton 8 remains available when a shorter
cycle is preferred, with the tradeoff of more periodic correlation on difficult
stationary outlines. A live sequence change invalidates temporal history,
clears the previous-view jitter basis, and restarts phase zero so samples from
two distributions never share one accumulated history.

## Advanced Temporal Controls

Advanced begins directly with:

- **Jitter Sequence**, with all six patterns described above;
- **Depth Validation**, with Stationary Bypass and Legacy Four-Texel Footprint
  choices;
- history frames and strength.

Motion-source selection, current-sample reconstruction, history filtering, and
rectification algorithms are recipe implementation details, not separate UI
rows.

Stationary Bypass is the factory default. It keeps center-owned stationary
history on the phase-invariant resolved-color grid instead of testing the raw
previous-depth grid at a different projection-jitter phase. Center and selected
XY motion must both be at most 0.01 pixel and the device-depth delta must be at
most `1e-6`. Center-First Edge Dilation normally borrows only the foreground XY
reprojection and retains center depth, Z delta, validity, and output ownership.
When projection jitter clears the center of a thin stationary feature exactly
to background, it may instead hand coverage to the nearest stationary cross
neighbor. That handoff never receives the unconditional stationary bypass: it
must point-validate the neighbor's expected depth at the neighbor's own prior
raw-depth coordinate, while current color and output depth remain center-owned.

Moving and coverage-handoff samples reject a stale nearer prior surface with a
one-sided linear-view-depth comparison. A farther sample is conservatively
accepted so a harmless slope or subpixel ownership transition does not toggle
history at the stationary threshold. Legacy Four-Texel Footprint remains an
explicit comparison mode that validates the complete bilinear/Gather footprint
and can reject stationary silhouette history as that raw footprint changes with
the jitter phase.

The default-closed Cost submenu comes last and contains:

- **Mode**: Full Quality, Reduced, or Minimum;
- robust or compact history storage;
- history weighting and motion trust;
- rectification clipping and blend domain, with Linear RGB as the factory
  blend domain; and
- preset/output sharpening.

Internal inheritance delegates a row to the selected Quality or Cost
recipe. The UI previews the effective value and lists each concrete choice once
instead of exposing an internal sentinel or owner suffix. The adjacent reset
icon reattaches that row to its recipe. Preset Sharpening alone keeps an
**(Automatic)** choice. History Frames displays 1 through 32 and History
Strength displays 0 through 200 percent. Explicit values override only their
row. Changing a Quality-owned history value appends **(Custom)** to the selected
Quality preview; Jitter Sequence and Depth Validation remain independent.
Changing a Cost control appends **(Custom)** to the selected Cost Mode preview.
Each marker disappears when every control in its group returns to its recipe.
The adjacent Quality and Cost resets restore their factory recipe values.
Disabling Temporal Reconstructive preserves the stored configuration.

## History Contract

TAA owns long-term image history only when Ray Marching **Accumulate Samples**
is off. With that accumulator enabled, raw scene-linear samples are resolved
before TAA and the TAA history/blend stage is bypassed, preventing one temporal
estimator from averaging another. Raster TAA and its camera jitter both resolve
inactive in this mode, so a camera-driven accumulation reset cannot present a
different raw subpixel phase on each frame. The stored TAA controls are restored
when Ray Marching accumulation is disabled. Path Tracing always resolves TAA
off and owns its progressive sample history; raster depth and motion are not
synthesized to reenable TAA.

The default TAA blend operates directly in scene-linear RGB. The optional
luminance-compressed domain remains an Advanced cost policy, but it is not the
factory choice because averaging in a nonlinear compression domain can darken
a long history and reduce contrast after inversion.

Effective image-policy, format, render-size, sample-topology, or
camera-discontinuity changes reset TAA history. A live Jitter Sequence change
also resets history and its phase. Fast-Approximate-only changes do not. MSAA
topology changes rebuild the relevant render targets and
invalidate history through the same image-key contract.

Robust history uses RGBA16F color and R32 depth. Compact history uses
R11G11B10 color and R16 depth when the device and selected policy permit it.
There is no Sample Resurrection path or older-frame cache; the resident history
is the active temporal history plus the minimum reconstruction surfaces.

## Coordinate Convention

Motion is current-to-previous in full-resolution pixels. Planar-view jitter is
already present in the projection, and reprojection applies the current-to-
previous jitter delta exactly once. UVSR uses reverse-Z, so larger valid raw
depth is closer. Background, non-finite motion, out-of-bounds reprojection, and
incoherent depth footprints reject history before its color is trusted.

## Fast Approximate Contract

Fast Approximate consumes AgX's undithered, tone-mapped display-linear RGBA16F
output and writes a separate matching texture. It is a modified HLSL adaptation
of Google Filament's G3D-patched NVIDIA FXAA 3.11 PC-console path at commit
`47c86eec22e56d75897e16651eb4d2abd64fc29a`. For each sample, the shader
reconstructs perceptual luminance as the square root of Rec. 601 luminance while
filtering the original display-linear RGB. It preserves the center sample's
alpha; final display transfer and dithering remain downstream.

Fast Approximate Quality owns three source-backed controls:

- **Low**: Edge Sharpness 2, Relative Edge Threshold 0.25, Minimum Edge
  Threshold 0.06;
- **Medium**: 4, 0.1875, and 0.055;
- **High**: 8, 0.125, and 0.05; and
- **Ultra**: Filament's 8, 0.08, and 0.04 defaults.

Advanced exposes Edge Sharpness from 2 through 8, Relative Edge Threshold from
0.08 through 0.25, and Minimum Edge Threshold from 0.04 through 0.06. Editing
one appends **(Custom)** to the selected Quality recipe; its reset returns to the
selected recipe value.

The pinned source, UVSR modifications, G3D BSD terms, and NVIDIA notice are
recorded in `legal/documentation/google-filament-fxaa.md`. The runtime
package includes that attribution plus the shared Apache 2.0 and BSD 2-Clause
license texts.

## MSAA Contract

MSAA is part of deferred PBR rather than a separate forward path. Every sample
retains material and lighting identity until final HDR resolution. UVSR queries
the active adapter and reports a fallback when a requested sample count is not
supported by all required formats. Multisample Adaptive Quality maps Low,
Medium, High, and Ultra to 2x, 4x, 8x, and 16x respectively. Direct Samples
selection remains under the technique's default-collapsed Advanced disclosure.

Ray-traced directional, sky, and flashlight visibility automatically evaluates
every valid covered receiver sample at 2x, 4x, 8x, and 16x. No UI control,
setting, command, or closest-receiver approximation can disable that contract;
mixed visible/occluded coverage must retain independent evidence per sample.

## Command Interface

The command surface mirrors the independent controls. Representative paths are:

```text
anti-aliasing.taa.enabled
anti-aliasing.taa.quality
anti-aliasing.taa.temporal-cost
anti-aliasing.taa.jitter-sequence
anti-aliasing.taa.previous-depth
anti-aliasing.taa.history.storage
anti-aliasing.fxaa.enabled
anti-aliasing.fxaa.quality
anti-aliasing.fxaa.edge-sharpness
anti-aliasing.fxaa.edge-threshold
anti-aliasing.fxaa.minimum-edge-threshold
anti-aliasing.msaa.enabled
anti-aliasing.msaa.quality
anti-aliasing.msaa.samples
```

Use Tab completion for the complete current catalog. Accepted image-changing
mutations normalize the aggregate AA state and cross the renderer boundary at
the same safe post-ImGui point as visible controls. The previous-depth command
accepts `nearest-texel` or `four-texel-footprint`; the retained
`nearest-texel` token selects the Stationary Bypass plus moving-point policy for
command compatibility.

## Validation Boundary

Reference tests cover quality resolution, every Filament jitter sample,
Sobol 32 prefix stratification and spacing, C++17 settings comparison,
history keys and reset rules, all eight independent enable combinations,
reverse-Z footprints, center-owned edge dilation, all sixteen default Halton
phases at an interior and viewport-edge stationary silhouette, exact-background
coverage handoff in all four cross directions, conservative moving-point depth,
point-versus-Gather bounds, accumulation-owned raster-TAA suppression, motion
validity, linear-RGB defaults, the Fast Approximate
source/resource/provenance contract, retained shader axes, and the absence of
resurrection paths. Directional, sky, and flashlight visibility shaders compile
for 1x, 2x, 4x, 8x, and 16x receiver samples and write each covered sample
directly; exact-candidate rendered validation remains required.
Product validation still requires viewing each technique alone and the
supported combined order on the exact candidate executable.
