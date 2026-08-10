# Microsoft MiniEngine Temporal Anti-Aliasing

## Record

- Relationship: Adapted Implementation
- Status: Current
- Confidence: Confirmed
- Upstream: [Microsoft DirectX Graphics Samples](https://github.com/microsoft/DirectX-Graphics-Samples)
- Revision: `357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a`
- Governing Terms: [Microsoft MIT License](../licenses/Microsoft-DirectX-Graphics-Samples-MIT.txt)

## UVSR Relationship

UVSR adapted MiniEngine's temporal blend, resolve, and sharpening approach to
its own resource layouts, motion contract, UI, history rules, and shader build.
The current HLSL files retain Microsoft's copyright and license notice; the
surrounding renderer integration is first-party.

## Evidence

- [Temporal Anti-Aliasing Host](../../src/temporal_aa.cpp)
- [Blend Shader](../../src/temporal_aa_blend_cs.hlsl)
- [Resolve Shader](../../src/temporal_aa_resolve_cs.hlsl)
- [Sharpen Shader](../../src/temporal_aa_sharpen_cs.hlsl)
- Commit `d27517538c1693b134157b93dbb612fbac493368`

## Commercial Clearance

The Microsoft MIT notice must remain with source and substantial binary
distributions. UVSR's license applies only to its separable additions.
