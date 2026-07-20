# Anti-Aliasing Options

## Normal Menu

The **Aliasing** drawer exposes:

- **Enabled**
- **Method**
- **Quality**
- **History Frames**
- **History Strength**
- **Run 45-Degree Motion Test**

The available methods are:

- **Temporal**: long-term MiniEngine TAA with optional presentation morphology
- **Conservative Morphological**: Intel CMAA2
- **Multisample**: diagnostic deferred MSAA followed by CMAA2 by default
- **Subpixel Morphological**: diagnostic full-screen SMAA 1x

SMAA T2x was removed. It no longer owns a menu method, shader bundle,
phase-history allocation, benchmark mode, or developer performance control.
Spatial SMAA remains only as a fixed diagnostic comparison with CMAA2.

## Quality Mapping

| Method | Low | Medium | High | Ultra |
| --- | --- | --- | --- | --- |
| Temporal | 3 prior frames | 7 prior frames | 15 prior frames | 31 prior frames |
| Conservative Morphological | CMAA2 Low | CMAA2 Medium | CMAA2 High | Mutex |
| Multisample | 2x + CMAA2 | 4x + CMAA2 | 8x + CMAA2 | 16x + CMAA2 |
| Subpixel Morphological | SMAA Low | SMAA Medium | SMAA High | SMAA Ultra |

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
can still compare no morphology or full-screen SMAA where the selected method
supports those diagnostic combinations.

Spatial SMAA preserves the pinned official thresholds and search behavior for
Low, Medium, High, and Ultra. It uses one fixed pixel edge pass, dense weight
pass, and neighborhood pass. SMAA-specific compute, stencil, compact-tile,
indirect-dispatch, and fusion experiments are not compiled or exposed.

## Developer Algorithm Configuration

The default-open **Aliasing Algorithm Configuration** drawer exposes resolved
algorithm controls. A resolved preset value and **(Preset)** are displayed as
one entry. Mutually exclusive entries display **(Mutex)**.

Temporal controls include motion source, current reconstruction, history
filter, rectification, stable-interior weighting, sample resurrection, and
presentation morphology. SMAA quality is selected only through the normal
Low, Medium, High, and Ultra presets.

## Developer Performance Overrides

The collapsed performance drawer contains only options that operate on the
MiniEngine temporal path:

- Execution Path
- Compute Kernel
- LDS Layout
- Shared-Work Reuse
- Early History Rejection
- Pass Fusion
- Cache Blocking

Spatial SMAA has no optional performance overrides.

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

Combo popups use a 120 ms fade. A selection made during that fade is queued and
commits on the first rendered frame after the fade reached completion. This
prevents a long frame from both skipping the visible animation and applying a
topology-changing selection while stale popup pixels are still presented.

## Center Crosshair

A two-pixel-radius white dot with 50% alpha is drawn at the exact main viewport
center after scene anti-aliasing. It is presentation-only and cannot enter TAA,
CMAA2, SMAA, or MSAA history.

## Benchmark

The in-app motion-test button and benchmark CLI use Benchmark Position 1,
turn right 45 degrees at 15 degrees per second, hold, and return at the same
rate. Reports include warm median, worst-case GPU time, and spatial SMAA stage
timings when SMAA is active. Removed T2x phase and SMAA execution-experiment
telemetry is not emitted.
