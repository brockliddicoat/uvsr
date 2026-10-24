# Specular Reflections

UVSR's Specular drawer contains two opt-in reflection techniques: **Stochastic
Screen Space Reflections** and **Hybrid Reflections**. Both are off by default.
When Hybrid is enabled it reuses the stochastic screen-space proposal and owns
one final reflection result; UVSR never adds the standalone and hybrid results
together.

The implementation is informed by AMD FidelityFX SSSR and Hybrid Reflections,
but is adapted to UVSR's deferred G-buffer, light probes, shared TLAS, and
single final-image composition. It deliberately does not import AMD's denoiser.

## Frame Integration

The active technique builds depth and HDR scene-color pyramids after ordinary
lighting and the environment background have completed. Deferred PBR and
Screen Space Visibility temporarily defer only their inherited environment
specular term. The Specular pass then resolves one replacement containing
screen-space hits, optional ray-query hits, and the same environment fallback,
and adds it to HDR color before temporal or presentation anti-aliasing.

Direct-light specular remains in deferred PBR. Diffuse IBL, direct diffuse,
emissive lighting, and Screen Space Visibility are not transferred to the
Specular pass. This ownership boundary prevents the environment response from
being counted twice.

## Stochastic Screen Space Reflections

The screen-space path prepares a hierarchical depth pyramid, generates
GGX-oriented proposals, traces the pyramid, and retains hit UV, source PDF,
distance, direction, footprint, metadata, and confidence. The resolver rejects
invalid depth, backface, and roughness cases and uses the environment probe as
the native fallback.

**Samples Per Quad** selects one, two, or four sparse proposals per 2 by 2
pixel quad. Sparse ownership rotates with the frame index. **Max Traversal
Iterations**, **Min Traversal Occupancy**, **Most Detailed Mip**, and **Depth
Thickness** expose the traversal controls most useful for quality and cost.
Long world-space proposals are clipped to the D3D camera-near plane before
projection. A distant endpoint that passes behind the eye therefore does not
discard a valid nearer screen-space segment.

## Ratio Estimator

With **Ratio Estimator** enabled, every reusable hit keeps its source proposal
PDF. At destination pixel `x`, hit `y_i` receives the local BRDF weight divided
by that source PDF:

```text
w_i(x) = BRDF(x, y_i) / sourcePdf_i
L_reused(x) = sum(L(y_i) * w_i(x)) / sum(w_i(x))
reflection(x) = L_reused(x) * preintegratedBRDF(x)
```

Invalid or negligible weight sums fall back to the environment response. The
preintegrated BRDF is applied once, after normalization. Disabling the toggle
uses the direct stochastic contribution route closer to AMD's reference
method; it does not change the proposal population merely to make the two modes
look different.

This is the variance-reduction construction described by Stachowiak as
BRDF-weighted image contributions, normalization of BRDF weights, and a
pre-integrated BRDF, and later identified by Heitz as a ratio estimator.

## Deferred Color Lookup

With **Deferred Color Lookup** enabled, tracing stores geometry rather than
scene color. Resolve samples the HDR pyramid with a footprint derived from
roughness, hit distance, and specular elongation. One **Trace And Filter Bias**
narrows the ray footprint while shifting the color-pyramid mip in the opposite
direction, so roughness is not applied once during tracing and again during
filtering.

Disabling the toggle captures source radiance during tracing and uses that
stored value during resolve. Both routes preserve the elongated footprint near
contacts instead of collapsing every reflection to an isotropic blur.

## Hybrid Reflections

Hybrid starts with the same stochastic screen-space proposal. Hits at or above
**Screen Space Confidence** resolve in screen space. Lower-confidence work can
be compacted and sent through DirectX Raytracing 1.1 inline ray queries when
both **Ray Tracing** and the global **Allow Ray Traversal** permission are on.
The dense dispatch remains available as a reference configuration.

UVSR does not re-shade arbitrary ray-traced materials in this first integration.
A ray hit that can be projected into the current view reuses the HDR pyramid;
an off-screen or otherwise unshadeable hit falls back to the environment probe.
This preserves one material system and avoids a second bindless shading path.

Turning **Ray Tracing** off, denying global traversal, or moving the confidence
threshold to accept only screen-space results leaves a screen-space plus
environment technique. **Ray Max Distance**, **Ray Bias**, **Backface
Rejection**, **Depth Rejection**, and **Compact Dispatch** live in the Hybrid
Advanced drawer. Backface Rejection compares the hit normal with the ray
direction and rejects values above its signed threshold; higher values are more
permissive.

## Sky Estimator Composition

Ray Traced Sky Visibility can provide scalar visibility, paired RGB ratio
modulation, or paired full stochastic radiance. The Specular resolver consumes
only the specular lobe:

- Scalar and RGB ratio outputs modulate the analytic environment fallback.
- Full stochastic output replaces that fallback and receives environment scale
  and specular occlusion once.
- Full stochastic radiance does not pass through the preintegrated environment
  BRDF again.

## Availability and Diagnostics

The techniques require a single-sample resolved G-buffer, an HDR environment
probe, and its preintegrated BRDF texture. Hybrid ray traversal additionally
requires a coherent shared TLAS. Existing PBR or Screen Space Visibility debug
views take final-image ownership and temporarily suppress Specular without
changing stored settings.

Each subdrawer has **Ray Confidence**, **Hit Distance**, **Source PDF**,
**Pyramid Mip**, and **Ray Route** diagnostic views. The Statistics drawer
reports the complete Specular Reflections graphics stage.

## Source Provenance

- AMD's [FidelityFX SDK 1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4)
  is the final complete 1.x source used for SSSR and Hybrid Reflections design
  reference. It is MIT licensed; UVSR records the pinned revision and license
  in `THIRD_PARTY_NOTICES.md`.
- Stachowiak's *Stochastic Screen-Space Reflections* SIGGRAPH 2015 course talk
  supplies the reuse normalization and filtered-importance-sampling model.
- Heitz, Hill, and McGuire's
  [2018 presentation](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018SIGGRAPHTalk.pdf)
  supplies the ratio-estimator terminology and comparison.
- AMD's
  [Far Cry 6 Hybrid Reflections presentation](https://gpuopen.com/download/GDC_Performant_Reflective_Beauty_Hybrid_Ray_Traced_Reflections_In_Far_Cry_6.pdf)
  supplies the screen-space confidence, compaction, and ray-query routing
  context.

## Validation

Focused reference tests cover setting sanitization, technique ownership,
proposal density, ratio normalization, deferred mip selection, confidence
routing, and fallback selection. Source-contract tests protect the explicit hit
and PDF records, one-time BRDF application, coupled trace/filter bias,
roughness-distance-elongation mip logic, compact and dense ray-query routes,
camera-near segment clipping, absence of an imported denoiser, and single
additive composition.
