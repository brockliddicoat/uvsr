# NVIDIA FXAA

## Record

- Relationship: Indirect Lineage
- Status: Current
- Confidence: Confirmed as Lineage; Direct UVSR Study Not Established
- Upstream: NVIDIA FXAA 3.11 by Timothy Lottes
- Revision: Version 3.11; no separate UVSR source revision
- Governing Terms: NVIDIA notice reproduced in [Google Filament Anti-Aliasing Attribution](google-filament-fxaa.md#nvidia-fxaa-notice)

## UVSR Relationship

UVSR adapted Filament's FXAA path. Filament carried an NVIDIA FXAA 3.11 lineage
through a G3D-modified implementation. This record preserves that indirect
lineage and does not claim that UVSR independently copied an NVIDIA source file.

## Evidence

- [Filament and FXAA Attribution](google-filament-fxaa.md)
- [Current FXAA Shader](../../src/fast_approximate_aa_ps.hlsl)
- Commit `a9a3dd10d7c8cf21e23c6642f1f93f4a7142192f`

## Commercial Clearance

Preserve the complete NVIDIA disclaimer already carried with the adaptation.
It is a notice obligation, not a license grant for unrelated NVIDIA material.
