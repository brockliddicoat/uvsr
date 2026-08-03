# Temporal Aliasing Options

## Independent Techniques

The **Aliasing** drawer exposes three animated, independently collapsible,
default-off techniques:

- **Temporal Reconstructive** performs long-term temporal reconstruction in
  scene-linear space.
- **Conservative Morphological** performs conservative morphological
  anti-aliasing after tone mapping.
- **Multisample Reference** preserves 2x, 4x, 8x, or 16x G-buffer samples
  through deferred material decode and lighting before the high-dynamic-range
  resolve.

The controls are not a mutually exclusive method dropdown. Any combination is
valid. The deterministic order is MSAA resolve, TAA, tone mapping, then CMAA2.
All three disabled is also a supported configuration.

## Main Temporal Controls

The normal Temporal Reconstructive surface contains:

- **Enable**;
- **Quality**: Low, Medium, High, or Ultra; and
- **Temporal Cost**: Full Quality, Reduced, or Minimum.

The lower-level Wicked Engine-derived policies live under a default-closed,
animated **Advanced** tree.

## Advanced Temporal Controls

Advanced separates the retained policies into **Algorithm** and **Cost**.
Algorithm begins with:

- **Previous-Depth Validation**, with Stationary Bypass and Four-Texel
  Footprint choices;
- motion source;
- current-sample reconstruction;
- history filter and rectification;
- history frames and strength.

Cost contains:

- robust or compact history storage;
- history weighting and motion trust;
- rectification clipping and blend domain; and
- preset/output sharpening.

Internal inheritance delegates a row to the selected quality or Temporal Cost
recipe. The UI previews the effective value and lists each concrete choice once
instead of exposing an internal sentinel or owner suffix. The adjacent reset
icon reattaches that row to its recipe. Preset Sharpening alone keeps an
**(Automatic)** choice. History Frames displays 1 through 32 and History
Strength displays 0 through 200 percent. Explicit values override only their
row. Changing an Algorithm control appends **(Custom)** to the selected Quality
preview; changing a Cost control appends **(Custom)** to the selected Temporal
Cost preview. Each marker disappears when every control in its group returns to
its recipe. The adjacent top-level reset arrow restores the complete factory
Quality-and-Algorithm or Temporal-Cost-and-Cost group. Selecting any named
preset, including the currently named Custom preset, reapplies it and clears its
owned overrides. Selecting a preset-equivalent Advanced value likewise
reattaches that row unless it has a distinct **(Automatic)** choice. Disabling
Temporal Reconstructive preserves the stored configuration.

## History Contract

TAA owns the renderer's only long-term image history. Effective image-policy,
format, render-size, sample-topology, or camera-discontinuity changes reset that
history. CMAA2-only changes do not. MSAA topology changes rebuild the relevant
render targets and invalidate history through the same image-key contract.

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

## CMAA2 Contract

CMAA2 consumes the display-linear, single-sample output. The compiled path fixes
HDR range support off; the retired HDR-CMAA2 axis and its permutations are not
part of the runtime package. Quality remains selectable at runtime.

## MSAA Contract

MSAA is part of deferred PBR rather than a separate forward path. Every sample
retains material and lighting identity until final HDR resolution. UVSR queries
the active adapter and reports a fallback when a requested sample count is not
supported by all required formats.

## Command Interface

The command surface mirrors the independent controls. Representative paths are:

```text
anti-aliasing.taa.enabled
anti-aliasing.taa.quality
anti-aliasing.taa.temporal-cost
anti-aliasing.taa.previous-depth
anti-aliasing.taa.history.storage
anti-aliasing.cmaa2.enabled
anti-aliasing.cmaa2.quality
anti-aliasing.msaa.enabled
anti-aliasing.msaa.samples
```

Use Tab completion for the complete current catalog. Accepted image-changing
mutations normalize the aggregate AA state and cross the renderer boundary at
the same safe post-ImGui point as visible controls.

## Validation Boundary

Reference tests cover quality resolution, C++17 settings comparison, history
keys and reset rules, reverse-Z footprints, motion validity, retained shader
axes, and the absence of resurrection/HDR-CMAA2 paths. Product validation still
requires viewing each technique alone and the supported combined order on the
exact candidate executable.
