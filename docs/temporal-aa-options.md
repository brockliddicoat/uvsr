# Temporal Anti-Aliasing Options

## Normal Menu

The **Aliasing** drawer keeps **Enabled**, **Method**, **Quality**, and
**Temporal Cost** available while anti-aliasing is disabled, so a temporary
bypass does not discard the selected configuration. **Temporal Reconstructive**
opens **Developer Options**; CMAA2 and MSAA keep their smaller method-specific
configuration surfaces.

The available methods are:

- **Temporal Reconstructive** for long-term temporal reconstruction.
- **Conservative Morphological** for Intel CMAA2.
- **Multisample Reference** for deferred MSAA with 2x, 4x, 8x, or 16x quality.

The Statistics drawer owns history, timing, CMAA2, and MSAA telemetry. Its
**Run Current With Motion** action warms for 180 frames, turns right 45 degrees
over 120 frames at `0.375` degrees per rendered frame, holds for 16 frames,
and returns over 120 frames without a frame-rate cap.

## Quality and Cost

| Quality | Reconstruction | Dejitter | Rectification |
| --- | --- | --- | --- |
| Low | 1x Bilinear | Off | Pair Tristimulus |
| Medium | 1x Bilinear | Off | Pair Tristimulus |
| High | 1x Bicubic | Off | Variance Chroma |
| Ultra | 5x Bicubic | On | Variance Chroma |

**Temporal Cost** selects the default policy for the temporal path:

| Cost | Default History Layout | Intended Tradeoff |
| --- | --- | --- |
| Full Quality | Robust RGBA16F color and R32 depth | Maximum robustness and complete feature set |
| Reduced | Robust RGBA16F color and R32 depth | Default lower-compute profile with Stationary Bypass |
| Minimum | Compact history when compatible | Lowest-cost explicit quality tradeoff |

UVSR starts in **Medium** Temporal Reconstructive with the **Reduced** cost
profile. Reduced retains robust history while using **Stationary Bypass** for
previous-depth validation; explicit Developer Options can still replace it.

Minimum prefers compact `R11G11B10_FLOAT` color and `R16_FLOAT` depth history,
with robust fallbacks where typed UAV support or the chosen image policy requires
them. Statistics reports the effective cost when a requested compact path falls
back to robust history.

## History and Overrides

**History Frames** spans 1 through 32 prior frames. **History Strength** spans
0% through 200% and applies only to history that already passes motion,
reprojection, reverse-Z depth, disocclusion, and rectification gates.

Developer Options can override the selected cost defaults for **History
Storage**, **Previous-Depth Validation**, **History Weight**, **Motion Trust**,
**Rectification Clip**, **Blend Domain**, and **Sharpness Policy**. Resetting
one of those rows returns it to the current Temporal Cost default.

**Sample Resurrection** is a Full Quality-only setting. Reduced and Minimum
retain its stored Full Quality choice but do not apply it; the factory shader
configuration does not expose the option. Its command value `preset` restores
the selected quality preset's behavior.

Effective image-policy and compact-versus-robust layout changes reset temporal
history once. Presentation-only CMAA2 and image-equivalent sharpening changes
preserve it.

## Command Interface

The slash command interface exposes the same temporal policies through:

```text
anti-aliasing.temporal-cost
anti-aliasing.history.storage
anti-aliasing.previous-depth-validation
anti-aliasing.history.weight
anti-aliasing.motion-trust
anti-aliasing.rectification-clip
anti-aliasing.blend-domain
anti-aliasing.sharpen.policy
anti-aliasing.sample-resurrection
```

Each accepted temporal mutation normalizes the complete anti-aliasing setting
set and resets temporal history at the same safe post-ImGui mutation barrier as
the visible controls.

## Motion and Jitter Convention

Temporal motion is current-to-previous in full-resolution pixels. Planar-view
jitter is already represented in the view projection, and reprojection applies
the current-to-previous jitter difference exactly once. UVSR uses reverse-Z:
larger valid raw depth is closer. Background, non-finite motion, out-of-bounds
reprojection, and incoherent depth footprints reject history before its color
is trusted.
