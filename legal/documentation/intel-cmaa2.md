# Intel Conservative Morphological Anti-Aliasing 2

## Record

- Relationship: Incorporated Upstream Material and Adapted Implementation
- Status: Current
- Confidence: Confirmed
- Upstream: [Intel CMAA2](https://github.com/GameTechDev/CMAA2)
- Revision: Upstream shader lineage recorded in the vendored file; no repository pin is presently documented
- Governing Terms: [Apache License 2.0](../licenses/Intel-CMAA2-Apache-2.0.txt)

## UVSR Relationship

UVSR vendors Intel's substantial `CMAA2.hlsl`, keeps modifications identified
in that file, and supplies a first-party NVRHI host adapter and wrapper shader.
The upstream shader also records portions derived from Microsoft samples; those
notices remain part of the incorporated material.

## Evidence

- [Vendored CMAA2 Shader](../samples/intel-cmaa2/CMAA2.hlsl)
- [Host Adapter](../../src/cmaa2.cpp)
- [Wrapper Shader](../../src/cmaa2.hlsl)
- Commits `58813cb94054738fc25ff2493444fbdb3dce7d98` and `a9a3dd10d7c8cf21e23c6642f1f93f4a7142192f`

## Commercial Clearance

Retain Intel's Apache notice, prominent modification notices, and every nested
Microsoft notice. The absent exact upstream revision should be repaired before
a formal commercial clearance claim.
