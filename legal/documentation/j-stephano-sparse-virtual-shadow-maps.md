# J. Stephano Sparse Virtual Shadow Maps

## Record

- Relationship: Design Influence and Implementation Study
- Status: Historical; the UVSR SVSM experiment was removed
- Confidence: Confirmed influence; no copied-code claim
- Upstream: [Sparse Virtual Shadow Maps](https://ktstephano.github.io/rendering/stratusgfx/svsm)
- Revision: Article and linked materials reviewed during the July 2026 experiment; no source revision was pinned
- Governing Terms: The article and code-sample terms were not recorded in UVSR

## UVSR Relationship

The article supplied the directional-clipmap architecture for UVSR's retired
sparse virtual shadow map experiment. UVSR translated concepts to its own
reverse-Z, NVRHI, page-allocation, and cache contracts. The record does not
establish that article code was copied; the lack of a recorded sample license
is why this classification stops at design and implementation study.

## Evidence

- [Abandoned SVSM Plan](../../docs/exec-plans/abandoned/sparse-virtual-shadow-maps.md)
- First published implementation commit `ea566bc67f744059e6f62e33c541c5b25bde9bd8`
- Final removal commit `b9287b04874cbdf0a3b805a37a6952104df77c29`

## Commercial Clearance

No historical implementation is shipped. Before restoring it, identify the
exact article/source revisions and their terms, then either document any code
lineage precisely or perform and record an independent implementation.
