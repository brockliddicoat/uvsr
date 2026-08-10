# Screen-Space Indirect Lighting with Visibility Bitmask

## Record

- Relationship: Independent Implementation and Design Influence
- Status: Current
- Confidence: Confirmed
- Upstream: [Paper by Olivier Therrien, Yannick Levesque, and Guillaume Gilet](https://arxiv.org/abs/2301.11376)
- Revision: 2023 publication
- Governing Terms: Publication rights; no upstream implementation code is identified

## UVSR Relationship

UVSR's projected-angle estimator follows the paper's finite-thickness visibility
bitmask concept. UVSR's solid-angle and cosine-weighted estimators are separate
first-party alternatives, and the current traversal, validation, and integration
are not represented as copied paper source code.

## Evidence

- [Estimator Validation](../../docs/visibility-estimator-validation.md)
- [Estimator Shader](../../src/visibility_estimator_shared.h)
- [Screen-Space Visibility Shader](../../src/screen_space_visibility_cs.hlsl)

## Commercial Clearance

Cite the paper for the algorithmic foundation. Do not reproduce publication
figures or substantial text without separate permission.
