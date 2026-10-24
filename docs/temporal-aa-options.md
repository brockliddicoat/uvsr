# Anti-Aliasing Options

## Normal Menu

The **Aliasing** drawer exposes:

- **Enabled**
- **Method**
- **Quality**
- **Temporal Cost**
- **Developer Options** for Temporal Reconstructive

**Enabled** is a true execution bypass. Method, Quality, and retained settings
remain selectable while AA is off, so disabling AA does not erase or strand the
configuration that will run when it is enabled again.

The **Statistics** drawer places **Run Current With Motion** immediately below
**Run Current** so the static and motion benchmark actions stay together. Its
**Anti-Aliasing** effect selection owns the history, permutation, timing,
CMAA2, and MSAA statistic lines; the Aliasing drawer does not duplicate them.
The Cancel control animates into the drawer only while a test is active.

Run Current With Motion is intentionally uncapped. It renders 180 warm-up
frames, turns 45 degrees right over 120 frames at `0.375` degrees per rendered
frame, holds for 16 frames, and returns over 120 frames. The 256 turn, hold, and
return samples are therefore identical at any renderer speed; only elapsed
wall-clock time changes. No 40 Hz sleep or target frame rate remains.

The available methods are:

- **Temporal Reconstructive**: long-term Temporal AA with optional
  presentation morphology
- **Conservative Morphological**: Intel CMAA2
- **Multisample Reference**: diagnostic deferred MSAA with optional CMAA2

The retired SMAA method, pass, benchmark telemetry, shaders, lookup assets, and
third-party source bundle are not built or staged.

## Quality Mapping

| Method | Low | Medium | High | Ultra |
| --- | --- | --- | --- | --- |
| Temporal Reconstructive | 3 prior frames | 6 prior frames | 9 prior frames | 12 prior frames |
| Conservative Morphological | CMAA2 Low | CMAA2 Medium | CMAA2 High | CMAA2 Ultra |
| Multisample Reference | 2x | 4x | 8x | 16x |

The Temporal presets resolve as follows:

| Quality | Reconstruction | Dejitter | Rectification | Morphology |
| --- | --- | --- | --- | --- |
| Low | 1x Bilinear | Off | Pair Tristimulus | Off |
| Medium | 1x Bilinear | Off | Pair Tristimulus | Off |
| High | 1x Bicubic | Off | Variance Chroma | Off |
| Ultra | 5x Bicubic | On | Variance Chroma | Off |

Temporal reconstruction exposes a cost baseline independently from
image-quality selection:

| Temporal Cost | Default History and Dispatch Contract | Intended Use |
| --- | --- | --- |
| Full Quality | Established robust RGBA16F color and R32 depth histories; separate output pass by default | Drop-in default and maximum robustness |
| Reduced | Robust algorithm with split-and-packed LDS, adjacent-pixel work reuse, pair-coherent early rejection, and fused output | Lower compute cost without changing the history formats |
| Minimum | Compact color/depth histories and one 16-by-8 compute dispatch using direct current color, center motion, bilinear history, and paired RGB bounds | Lowest-cost explicit quality trade |

Minimum's cost contract was designed against the official
[Wicked Engine temporal AA shader at pinned commit `3a800b7`](https://github.com/turanszkij/WickedEngine/blob/3a800b7134aafe58461093c8abb2e274d4e64033/WickedEngine/shaders/temporalaaCS.hlsl)
as a cost-class reference. UVSR does not copy that shader or claim measured
performance parity. It adds packed FP16 group memory, pair-coherent rejection,
compact depth history, conditional depth validation, validity-authoritative
first writes without reset clears, and direct one-dispatch output.

Temporal Cost now supplies defaults instead of masking the controls in
**Developer Options**. The visible image-and-stability baseline is:

| Policy | Full Quality | Reduced | Minimum |
| --- | --- | --- | --- |
| Motion Source | Quality preset | Quality preset | Center |
| Dejitter | Quality preset | Quality preset | Off / Direct |
| Reconstruction | Quality preset | Quality preset | 1x Bilinear |
| Rectification | Quality preset | Quality preset | Pair Tristimulus |
| History Storage | Robust RGBA16 + R32 | Robust RGBA16 + R32 | Compact R11/R16 Preferred |
| Previous-Depth Validation | Four-Texel Footprint | Four-Texel Footprint | Stationary Bypass |
| History Weight | Confidence Recurrence | Confidence Recurrence | Immediate Horizon |
| Motion Trust | Linear Speed | Linear Speed | Squared Speed |
| Rectification Clip | Velocity-Dilated Line | Velocity-Dilated Line | Tight Component |
| Blend Domain | Luminance-Compressed | Luminance-Compressed | Linear RGB |
| Sharpness Policy | On | On | Off |
| Sample Resurrection | Preset or explicit override | Off | Off |

The quality preset supplies Center motion for Low and Edge Dilation for Medium,
High, and Ultra. It also supplies the reconstruction, Dejitter, and
Rectification values in the quality table above. Selecting an explicit
Developer Options value overrides that row's selected Temporal Cost default
without modifying the stored cost selection. Resetting that row returns it to
**Temporal Cost**. Sample Resurrection is the deliberate exception:
production and developer builds can edit it only at Full Quality. Its row fades
and collapses under Reduced and Minimum while retaining the stored Full Quality
choice. The factory-startup shader experiment omits resurrection.

Minimum prefers `R11G11B10_FLOAT` color and `R16_FLOAT` depth histories, with
`RGBA16_FLOAT` and `R32_FLOAT` fallbacks when typed UAV support is unavailable.
Compact storage uses the dedicated one-dispatch path only with Center motion,
Direct current color, 1x Bilinear history, Pair Tristimulus rectification,
Immediate Horizon weighting, and Sample Resurrection Off. An incompatible
Minimum algorithm or history-weight override produces an explicit
robust-storage fallback and reports **Effective Temporal Cost: Reduced** in
Statistics. A compatible explicit compact-storage request under Full Quality or
Reduced can also activate the dedicated path and reports **Effective Temporal
Cost: Minimum** there. An incompatible request retains the selected profile's
robust path. Diagnostics and selective export also force a robust fallback.
CMAA2 forces it when compact color is `R11G11B10_FLOAT`, because CMAA2 requires
an `RGBA16_FLOAT` source; a hardware fallback that already uses
`RGBA16_FLOAT` remains compatible.

The robust and compact history pools are both resident so changing cost mode
does not recreate the pass or textures. Production and developer builds also
keep the older-frame resurrection snapshots resident so enabling the Full
Quality option does not rebuild resource bindings. **Active History Memory** is
the logical read/write footprint of the effective path. **Resident History
Memory** includes every allocated history pool. Neither figure includes fused
output, selective-output, or debug textures.

The quality path contains code adapted from Microsoft DirectX Graphics Samples.
Its full MIT notice is stored in
`third_party/microsoft_directx_graphics_samples/LICENSE.txt` and copied beside
the executable in `licenses/Microsoft-DirectX-Graphics-Samples.txt`.

MSAA uses static shader permutations for 2x, 4x, 8x, and 16x. At runtime,
UVSR checks every multisampled render-target format and falls back to the
highest supported sample count rather than creating an invalid resource.

## Temporal History

Temporal AA owns the only anti-aliasing temporal history. History Frames is
a 1-32 prior-frame horizon. History Strength ranges from 0% to 200% and scales
only history that already passed invalid-motion, reprojection-bounds,
reverse-Z depth, disocclusion, and rectification gates.
Strength above 100% reinforces accepted partial history before the
horizon-derived cap; it cannot revive a rejected sample. The same closed
0%-200% range applies to Full Quality, Reduced, and Minimum.

History Strength is not Sample Resurrection. Resurrection owns older validated
history resources and remains a separate Full Quality option.

Effective temporal image changes reset history exactly once. Full Quality and
Reduced share the robust image policies and preserve history when their
effective image and resurrection settings match. Minimum's default
Previous-Depth Validation, History Weight, Motion Trust, Rectification Clip,
and Blend Domain differ even when Low already matches its visible algorithm
choices, so entering the unmodified Minimum baseline invalidates image history
for every quality. A separate runtime layout transition invalidates history
when execution actually changes between compact and robust storage.

Overriding only History Storage does not change the image key, but an actual
compact/robust transition still resets the physical history. Changing
Previous-Depth Validation, History Weight, Motion Trust, Rectification Clip,
Blend Domain, motion ownership, reconstruction, rectification, or resurrection
changes the effective temporal image and resets it. Presentation-only
Morphology, Sharpness, and image-equivalent execution-topology changes preserve
history.

## Morphology

Temporal and Multisample presets default Morphology to **Off**, so no hidden
CMAA2 pass is charged to either complete method. **Conservative
Morphological** remains the explicit standalone CMAA2 method. The algorithm
drawer can select **Off**, **Conservative Low**, **Conservative Medium**,
**Conservative High**, or **Conservative Ultra** after a Temporal or
Multisample resolve. Its quality is independent from the main Temporal or
Multisample quality: choosing Conservative Ultra while Temporal Low is active
changes only the CMAA2 presentation pass. Changing only presentation morphology
preserves temporal history.

## TAA Developer Options

The default-open **Developer Options** segment appears for Temporal
Reconstructive. It shows the concrete resolved value beside each inherited
choice; an explicit choice overrides the selected Temporal Cost, and the row's
reset returns control to that cost baseline. Cost-ranked choices remain ordered
from least expensive to most expensive. Multisample and standalone CMAA2 retain
the narrower **Aliasing Algorithm Configuration** segment.

The available Dejitter and Sharpness toggles appear first. Sharpness fades and
collapses when Sharpness Policy disables it. History tuning follows those
toggles inside Developer Options, ahead of the remaining TAA controls:

| Control | Available Policies |
| --- | --- |
| Dejitter | Direct or De-Jittered current color |
| Sharpness | Stored enable and strength, shown only when Sharpness Policy permits it |
| History Frames | 1 through 32 prior frames |
| History Strength | 0% through 200% accepted-history scaling |
| Morphology | Off or Conservative Low, Medium, High, or Ultra CMAA2 presentation |
| Motion Source | Center, Closest Cross, or Edge Dilation |
| Reconstruction | 1x Bilinear, 1x Bicubic, 5x Bicubic, or complete 9x Bicubic Catmull-Rom |
| Rectification | Pair Tristimulus or Variance Chroma |
| History Storage | Robust RGBA16 + R32 or Compact R11/R16 Preferred |
| Previous-Depth Validation | Four-Texel Footprint or Stationary Bypass |
| History Weight | Confidence Recurrence or Immediate Horizon |
| Motion Trust | Linear Speed or Squared Speed |
| Rectification Clip | Velocity-Dilated Line or Tight Component |
| Blend Domain | Luminance-Compressed or Linear RGB |
| Sharpness Policy | Temporal Cost, Off, or On |
| Sample Resurrection | No Resurrection, 1x Frame, or 2x Frames; shown only at Full Quality in production and developer builds |

The 9x Bicubic option performs the complete nine-bilinear-tap Catmull-Rom
reconstruction, including all four corners. Enabling sharpness with compact
history adds one presentation-sharpen dispatch. Sample Resurrection requires
Full Quality, robust storage, and its validated legacy topology.

Execution topology is intentionally absent from Settings because the
cost-profile rows were noninteractive in production and did not help choose an
image result. Statistics retains effective cost, dispatch count, memory, and
timing. Developer builds retain command-line topology experiments.

## Small-Edge Shimmer Isolation

The controlled user comparison found **Stationary Bypass** was the only exposed
policy that massively stabilized the observed small edges. On a still view, the
shader does not enter its moving-point branch: it bypasses
previous-depth sampling and accepts otherwise-valid stationary history. That
localizes the observed failure to the four-texel previous-depth gate. Its
jittered footprint can alternate between geometry and background at a thin
silhouette, reject the whole sample even when a background lane has no
meaningful filter weight, and repeatedly expose the raw jittered current edge.

This does not show that history weighting, rectification, storage, or blending
are broadly broken; those unchanged stages stabilize the image once history
survives the gate. It does expose an over-rejecting robust validation policy.
The exact combined setting is not yet a safe universal default: stationary
bypass can retain stale foreground history on a newly revealed zero-motion
background, while the moving branch validates only one depth texel for a
multi-texel color filter and uses a loose far-distance reverse-Z tolerance.

Use this sequence to isolate the effect:

1. Start from Reduced and change only **Motion Source** from Edge Dilation to
   Center.
2. Select Minimum, force **History Storage** to Robust, and leave the other
   Minimum image-and-stability defaults unchanged. This keeps Minimum's behavior
   policies while removing compact storage and its dedicated dispatch.
3. Toggle only **Previous-Depth Validation** between Four-Texel Footprint and
   Stationary Bypass.
4. Restore the winning depth policy, then toggle only **History Weight** between
   Confidence Recurrence and Immediate Horizon.
5. Restore the winning weight policy, then compare **Rectification Clip**;
   **Motion Trust** and **Blend Domain** are lower-priority follow-ups.
6. After each history reset has warmed up, compare Robust and Compact History
   Storage last while retaining a compact-compatible algorithm and Immediate
   Horizon weighting.

Keep the scene, camera, quality, frame horizon, history strength, morphology,
sharpness, and every untested row fixed. The comparison identifies correlation
in the live renderer; it is not a matched performance benchmark.

## Dejitter and Sharpness

**Dejitter** and the available **Sharpness** control are the first two rows in
Developer Options. Dejitter is off for Low, Medium, and High and on for Ultra.
Sharpness starts disabled for every preset while retaining its stored strength
when toggled off. Minimum defaults Dejitter and Sharpness Policy off, but each
policy can be overridden independently. The Sharpness row fades and collapses
while its policy is Off; enabling the policy reveals the stored toggle. Compact
sharpening runs as one presentation dispatch.

Stable Interior and its moment-history resource were retired. Temporal Cost
exposes the supported production topology profiles. Runtime-uniform
image-and-stability policies are available in production. Individual robust
execution path, compute kernel, LDS layout, shared-work reuse, early rejection,
pass fusion, cache blocking, and debug experiments remain developer command-line
controls rather than Settings rows. Sample Resurrection is additionally limited
to Full Quality.

## Rectification

Rectification is primarily a history-quality policy, not a blanket performance
optimization. Pair Tristimulus uses paired neighborhood bounds; Variance Chroma
uses variance-aware YCoCg bounds. The per-pixel RGB and YCoCg variants were retired
because they duplicated the same policy space while multiplying every other
TAA compile-time axis. Relative GPU cost still depends on the active temporal
permutation and adapter, while the visible tradeoff is how aggressively valid
history is constrained.

## Presentation Sharpening Contract

Temporal history stores premultiplied RGB and confidence in alpha, so its
sharpen permutation divides by valid history alpha. CMAA2 emits resolved RGB
and uses alpha as an unused output channel; processed edge pixels can therefore
contain zero alpha. Post-CMAA2 sharpening selects a separate resolved-input
permutation that never divides RGB by this alpha. This prevents finite HDR
edges from expanding to RGBA16F white while preserving Temporal AA's original
history sharpening behavior.

## Motion and Jitter Convention

Temporal AA motion is current-to-previous in full-resolution pixels. Positive
motion moves the current sample toward its previous-frame location. PlanarView
jitter is expressed in pixel offsets and is already present in the view
projection. Reprojection applies the current-to-previous jitter difference
exactly once.

UVSR uses reverse-Z. Greater valid raw depth is closer. Background, non-finite
motion, out-of-bounds reprojection, and incoherent depth footprints reject
history before history color is trusted.

## Dropdown Commit Timing

Dropdown selection closes without a fade. Topology-changing Aliasing choices
are queued for the next rendered UI frame so dependent rows can disappear only
after the renderer consumes the selection.

## Center Crosshair

A two-pixel-radius white dot with 50% alpha is drawn at the exact main viewport
center only while pixel zoom is active. It is presentation-only and cannot
enter TAA, CMAA2, or MSAA history.

## Benchmark

The in-app motion-test button and benchmark CLI use Benchmark Position 1,
turn right 45 degrees at `0.375` degrees per rendered frame, hold for 16
frames, and return at the same per-frame step. Reports include warm median and
worst-case GPU time plus `wall_clock_pacing_enabled: false`. Retired morphology
stage telemetry is not emitted.
