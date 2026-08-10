# AgX Display Transform

## Record

- Relationship: Adapted Implementation and Indirect Lineage
- Status: Current
- Confidence: Confirmed implementation match; uncertain historical route
- Upstream: [Minimal AgX Implementation](https://iolite-engine.com/blog_posts/minimal_agx_implementation) and [Troy Sobotka's AgX](https://github.com/sobotka/AgX)
- Revision: Immediate source and revision not established by UVSR history
- Governing Terms: Benjamin Wrensch/Missing Deadlines publishes the matching implementation under MIT; the governing terms for Troy Sobotka's upstream AgX data lineage still require confirmation

## UVSR Relationship

The current shader's distinctive inset/outset matrices, exposure bounds, and
contrast polynomial exactly match Benjamin Wrensch's minimal implementation,
whose values trace to Troy Sobotka's AgX work. UVSR reorganizes and integrates
that math. Because the immediate historical route and revision were not
recorded, UVSR conservatively treats this as an adapted implementation without
claiming which copy or intermediary was consulted.

## Evidence

- [Current AgX Shader](../../src/agx_tonemapping_ps.hlsl)
- [Preserved Missing Deadlines MIT Notice](../licenses/IOLITE-AgX-MIT.txt)
- Introduction commit `c90274a01f21db1f4c23e3629d3004e9160fbeb6`
- [Matching MIT Gist](https://gist.github.com/nxrighthere/eb208dae8b66dbe452af223f276e46cc)

## Commercial Clearance

Preserve the Missing Deadlines MIT notice. Before commercial licensing,
confirm the immediate historical source and revision and determine the terms
governing Troy Sobotka's upstream matrices and curve data. If that lineage
cannot be cleared, replace it with a documented clean implementation.
