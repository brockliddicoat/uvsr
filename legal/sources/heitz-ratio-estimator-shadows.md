# Heitz Ratio Estimator Shadows

## Record

- Relationship: Independent Implementation and Design Influence
- Status: Current
- Confidence: Confirmed
- Upstream: [A Fast and Stable Visibility Ratio Estimator for Correlated Spherical Integrals](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018Shadow.pdf)
- Revision: Publication and [SIGGRAPH talk](https://casual-effects.com/research/Heitz2018Shadow/Heitz2018SIGGRAPHTalk.pdf)
- Governing Terms: Publication rights; no upstream implementation code is incorporated

## UVSR Relationship

UVSR independently implements the correlated numerator-and-denominator ratio
estimator described by Eric Heitz, Stephen Hill, and Morgan McGuire. Filtering
the correlated terms before division is the conceptual foundation; UVSR's DXR,
resource, sampling, and UI implementation is first-party.

## Evidence

- [Design Documentation](../../docs/heitz-ratio-estimator-shadows.md)
- [Implementation](../../src/heitz_ratio_estimator_shadows.cpp)
- [Compute Shader](../../src/heitz_ratio_estimator_shadows_cs.hlsl)
- Commit `ca4bd62bf4c052791103dbd49e0dc48e301fdddf`

## Commercial Clearance

No copied source code was established. Citation does not grant rights to reuse
publication figures or text beyond applicable law.
