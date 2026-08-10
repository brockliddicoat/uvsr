# Demofox Blue-Noise Soft Shadows

## Record

- Relationship: Design Influence
- Status: Current
- Confidence: Confirmed
- Upstream: [Using Blue Noise for Raytraced Soft Shadows](https://blog.demofox.org/2020/05/16/using-blue-noise-for-raytraced-soft-shadows/)
- Revision: Article published May 16, 2020
- Governing Terms: Publication reference; no source-code license relied upon

## UVSR Relationship

UVSR adapts the article's high-level spatial blue-noise rotation and
low-discrepancy temporal idea while retaining its independently implemented
uniform spherical-cap proposal and correlated ratio estimator. No article code,
image, or numeric preset is identified as copied.

## Evidence

- [Ratio Estimator Documentation](../../docs/heitz-ratio-estimator-shadows.md)
- [Ratio Estimator Shader](../../src/heitz_ratio_estimator_shadows_cs.hlsl)

## Commercial Clearance

Keep the relationship framed as conceptual unless a future audit identifies a
specific code sample and its license.
