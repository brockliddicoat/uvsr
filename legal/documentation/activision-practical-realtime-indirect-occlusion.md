# activision practical real time indirect occlusion

## record

- relationship: publication influence and implementation study
- status: current reference; historical approximation retired
- confidence: confirmed
- upstream: [Practical Real-Time Strategies for Accurate Indirect Occlusion](https://research.activision.com/publications/2020-03/practical-real-time-strategies-for-accurate-indirect-occlusion)
- revision: published technical report and associated SIGGRAPH course material
- terms: publication rights; no incorporated upstream code identified

## UVSR relationship

the publication informed traversal and reconstruction comparisons. UVSR's
current finite interval estimators are first party implementations and are not
represented as GTAO or copied publication source. a historical PS4 style
approximation was removed in commit
`16d8fc88901ad2aab7ca5f8e99d617294d3ba6f1`.

## evidence

- [user guide](../../docs/user-guide.md)
- [validation contract](../../docs/validation.md)
- [estimator implementation](../../src/visibility_estimator_shared.h)
- [screen space trace](../../src/screen_space_visibility_cs.hlsl)

preserve the publication citation. review any later sample code separately
before incorporating it.
