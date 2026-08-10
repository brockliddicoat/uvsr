# Activision Practical Real-Time Indirect Occlusion

## Record

- Relationship: Design Influence and Implementation Study
- Status: Current Reference; Historical Adaptation Retired
- Confidence: Confirmed
- Upstream: [Practical Real-Time Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf)
- Revision: Published technical report and associated SIGGRAPH course material
- Governing Terms: Publication rights; no current incorporated code is established

## UVSR Relationship

The publication informs UVSR's traversal and reconstruction reasoning. A
historical PS4-style approximation pipeline was built for comparison and later
removed. GTAO does not define UVSR's current finite-interval estimator.

## Evidence

- [Estimator Validation](../../docs/visibility-estimator-validation.md)
- [Completed AO Plan](../../docs/exec-plans/completed/ao-performance-optimization.md)
- Historical implementation removed by commit `16d8fc88901ad2aab7ca5f8e99d617294d3ba6f1`

## Commercial Clearance

Keep this classified as publication-derived design work unless exact licensed
sample code is later identified.
