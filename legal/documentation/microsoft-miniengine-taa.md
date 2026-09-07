# Microsoft MiniEngine Temporal Anti-Aliasing

## Record

- Relationship: Adapted Implementation
- Status: Retired adaptation, notice retained for recovery
- Confidence: Confirmed
- Upstream: [Microsoft DirectX Graphics Samples](https://github.com/microsoft/DirectX-Graphics-Samples)
- Revision: `357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a`
- Governing Terms: [Microsoft MIT License](../licenses/Microsoft-DirectX-Graphics-Samples-MIT.txt)

## UVSR Relationship

UVSR adapted MiniEngine's temporal blend, resolve, and sharpening approach to
its own resource layouts, motion contract, UI, history rules, and shader build.
the removed HLSL files retain Microsoft's copyright and license notice in
historical source. the surrounding renderer integration was first-party.

## Evidence

- former owners: `src/temporal_aa.cpp`, `temporal_aa_blend_cs.hlsl`,
  `temporal_aa_resolve_cs.hlsl`, and `temporal_aa_sharpen_cs.hlsl`.
- [removal and dirty-state recovery](../../docs/postmortem/taa.md)
- Commit `d27517538c1693b134157b93dbb612fbac493368`

## Commercial Clearance

The Microsoft MIT notice must remain with source and substantial binary
distributions. UVSR's license applies only to its separable additions.
