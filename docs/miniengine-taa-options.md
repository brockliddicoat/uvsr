# Anti-Aliasing Options

## Normal Menu

The **Aliasing** drawer exposes:

- **Enabled**
- **Method**
- **Quality**
- **History Frames**
- **History Strength**

The **Statistics** drawer places **Run Current With Motion** immediately below
**Run Current** so the static and motion benchmark actions stay together. Its
**Anti-Aliasing** effect selection owns the history, permutation, timing,
CMAA2, and MSAA statistic lines; the Aliasing drawer does not duplicate them.
The Cancel control animates into the drawer only while a test is active.

The available methods are:

- **Temporal Reconstructive**: long-term MiniEngine TAA with optional presentation morphology
- **Conservative Morphological**: Intel CMAA2
- **Multisample Reference**: diagnostic deferred MSAA followed by CMAA2 by default

The retired SMAA method, pass, benchmark telemetry, shaders, lookup assets, and
third-party source bundle are not built or staged.

## Quality Mapping

| Method | Low | Medium | High | Ultra |
| --- | --- | --- | --- | --- |
| Temporal Reconstructive | 3 prior frames | 6 prior frames | 9 prior frames | 12 prior frames |
| Conservative Morphological | CMAA2 Low | CMAA2 Medium | CMAA2 High | CMAA2 Ultra |
| Multisample Reference | 2x + CMAA2 | 4x + CMAA2 | 8x + CMAA2 | 16x + CMAA2 |

MSAA uses static shader permutations for 2x, 4x, 8x, and 16x. At runtime,
UVSR checks every multisampled render-target format and falls back to the
highest supported sample count rather than creating an invalid resource.

## Temporal History

MiniEngine TAA owns the only anti-aliasing temporal history. History Frames is
a 1–31 prior-frame horizon, while History Strength can only reduce accepted
history. It cannot restore history rejected by invalid motion, reprojection
bounds, reverse-Z depth validation, disocclusion, or rectification.

Effective temporal image changes reset history exactly once. Presentation-only
morphology and image-equivalent performance changes do not reset it.

## Morphology

Every MSAA quality defaults to **Conservative Morphological**, so Intel CMAA2
runs after the multisample lighting resolve. The developer algorithm drawer
can select **Off**, **Conservative Low**, **Conservative Medium**,
**Conservative High**, or **Conservative Ultra** after a Temporal or
Multisample resolve. Its quality is independent from the main Temporal or
Multisample quality: choosing Conservative Ultra while Temporal Low is active
changes only the CMAA2 presentation pass. Changing only presentation morphology
preserves temporal history.

## Developer Algorithm Configuration

The default-open **Aliasing Algorithm Configuration** drawer exposes resolved
algorithm controls without adding **(Preset)** to inherited values. Mutually
exclusive entries display **(Mutex)**.

Temporal controls include motion source, current reconstruction, history
filter, rectification, sample resurrection, and presentation morphology.

## Stable Interior

**Stable Interior** appears immediately above **Sharpness** in the temporal
algorithm controls and remains off in every preset. Sharpness also starts
disabled for every preset while retaining its stored strength when the user
toggles it off. The separate developer performance drawer is removed.
Execution path, compute kernel, LDS layout, shared-work reuse, early rejection,
pass fusion, cache blocking, and developer debug dropdowns are not exposed.

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
turn right 45 degrees at 15 degrees per second, hold, and return at the same
rate. Reports include warm median and worst-case GPU time. Retired morphology
stage telemetry is not emitted.
