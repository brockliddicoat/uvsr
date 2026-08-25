# Intel XeGTAO

## Record

- Relationship: Adapted Implementation and Indirect Lineage
- Status: Mostly Historical
- Confidence: Confirmed
- Upstream: [Intel XeGTAO](https://github.com/GameTechDev/XeGTAO)
- Revision: `a5b1686c7ea37788eeb3576b5be47f7c03db532c`, version 1.30
- Governing Terms: [MIT License](https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/LICENSE)
- Packaged License Size: 1,081 bytes
- License SHA-256: `1f3bfd6b628535f0f0e779e70c260cfdc1bb45bc8644d7108e65f83e24a05cba`

## UVSR Relationship

UVSR historically maintained a narrow HLSL source port for controlled AO
comparison. Those shaders were removed, but the current visibility shader still
labels a compact fast-acos expression as the “XeGTAO / Lagarde approximation.”
The remainder of the current estimator is not an XeGTAO port.

## Evidence

- [Current Visibility Shader](../../src/screen_space_visibility_cs.hlsl)
- Historical `src/screen_space_visibility_xegtao*`; removed by `16d8fc88901ad2aab7ca5f8e99d617294d3ba6f1`

## Commercial Clearance

Retain Intel's MIT notice for any recognizable surviving implementation. A
future clean-room replacement may permit this record to become purely historical.
Production packages install the notice as
`bin/licenses/Intel-XeGTAO-MIT.txt`.
