# SMAA

## Record

- Relationship: Incorporated Upstream Material and Adapted Implementation
- Status: Historical; removed from the current renderer
- Confidence: Confirmed
- Upstream: [SMAA Repository](https://github.com/iryoku/smaa) and [Project Page](http://www.iryoku.com/smaa/)
- Revision: Upstream revision not recorded; UVSR's vendored snapshot is preserved in Git history
- Governing Terms: MIT-like permission notice by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, and Diego Gutierrez

## UVSR Relationship

UVSR historically vendored `SMAA.hlsl`, generated lookup headers, and the
upstream license, then built its own NVRHI integration and debugging paths
around them. This was direct upstream source and data reuse. The SMAA path and
lookup assets are absent from the current product.

## Evidence

- Vendored snapshot added in `58813cb94054738fc25ff2493444fbdb3dce7d98`; inspect with `git show 58813cb:src/third_party/smaa/SMAA.hlsl`
- Removed in `644a521fe0afb867c412bc12f9f1a364636b0e84`

## Commercial Clearance

No current distribution obligation arises from removed files. Restoration must
retain the complete historical copyright and permission notice in every source
copy or substantial portion and should pin the corresponding upstream release.
