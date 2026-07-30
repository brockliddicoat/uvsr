# Anti-Aliasing Options

## Normal Menu

The **Aliasing** drawer exposes:

- **Enabled**
- **Method**
- **Quality**
- **History Frames**
- **History Strength**
- **Dejitter**
- **Sharpness**

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

- **Temporal Reconstructive**: long-term MiniEngine TAA with optional
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

| Quality | Reconstruction | Dejitter | Rectification | Subpixel Morphology |
| --- | --- | --- | --- | --- |
| Low | 1x Bilinear | Off | Pair Tristimulus | Off |
| Medium | 1x Bilinear | Off | Pair Tristimulus | Off |
| High | 1x Bicubic | Off | Variance YCoCg | Off |
| Ultra | 5x Bicubic | On | Variance YCoCg | Off |

MSAA uses static shader permutations for 2x, 4x, 8x, and 16x. At runtime,
UVSR checks every multisampled render-target format and falls back to the
highest supported sample count rather than creating an invalid resource.

## Temporal History

MiniEngine TAA owns the only anti-aliasing temporal history. History Frames is
a 1-32 prior-frame horizon. History Strength ranges from 0% to 200% and scales
only history that already passed invalid-motion, reprojection-bounds,
reverse-Z depth, disocclusion, and rectification gates.
Strength above 100% reinforces accepted partial history before the
horizon-derived cap; it cannot revive a rejected sample.

History Strength is not Sample Resurrection. Resurrection owns older validated
history resources and remains a separate developer-only experiment.

Effective temporal image changes reset history exactly once. Presentation-only
morphology and image-equivalent performance changes do not reset it.

## Morphology

Temporal and Multisample presets default Subpixel Morphology to **Off**, so no
hidden CMAA2 pass is charged to either complete method. **Conservative
Morphological** remains the explicit standalone CMAA2 method. The algorithm
drawer can select **Off**, **Conservative Low**, **Conservative Medium**,
**Conservative High**, or **Conservative Ultra** after a Temporal or
Multisample resolve. Its quality is independent from the main Temporal or
Multisample quality: choosing Conservative Ultra while Temporal Low is active
changes only the CMAA2 presentation pass. Changing only presentation morphology
preserves temporal history.

## Algorithm Configuration

The default-open **Aliasing Algorithm Configuration** drawer shows the concrete
resolved selection rather than a generic **Preset** row. Cost-ranked dropdown
choices are ordered from least expensive to most expensive in every state.
Mutually exclusive entries display **(Mutex)**.

Temporal controls include **Subpixel Morphology**, **Motion Source**,
**Reconstruction**, and **Rectification**. Reconstruction offers **1x
Bilinear**, **1x Bicubic**, **5x Bicubic**, and **9x Bicubic**. The last option
performs the complete nine-bilinear-tap Catmull-Rom reconstruction, including
all four corners.

## Dejitter and Sharpness

**Dejitter** appears above **Sharpness** in the normal temporal controls. It is
off for Low, Medium, and High and on for Ultra. Sharpness starts disabled for
every preset while retaining its stored strength when toggled off.

Stable Interior and its moment-history resource were retired. Execution path,
compute kernel, LDS layout, shared-work reuse, early rejection, pass fusion,
cache blocking, Sample Resurrection, and developer debug dropdowns are not
exposed in production.

## Rectification

Rectification is primarily a history-quality policy, not a blanket performance
optimization. Pair Tristimulus uses paired neighborhood bounds; Variance YCoCg
uses variance-aware bounds. The per-pixel RGB and YCoCg variants were retired
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
edges from expanding to RGBA16F white while preserving MiniEngine's original
history sharpening behavior.

## Motion and Jitter Convention

MiniEngine motion is current-to-previous in full-resolution pixels. Positive
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
