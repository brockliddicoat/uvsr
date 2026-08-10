# `TinyEXR`

## Record

- Relationship: Dependency Integration and Indirect Lineage
- Status: Current
- Confidence: Confirmed
- Upstream: [TinyEXR](https://github.com/syoyo/tinyexr)
- Revision: Vendored by pinned Donut rather than a standalone UVSR pin
- Governing Terms: BSD-style TinyEXR terms plus embedded OpenEXR notices

## UVSR Relationship

Donut uses TinyEXR for EXR image loading. UVSR depends on that loader through
Donut but does not copy or adapt TinyEXR into first-party source.

## Evidence

- [Donut Third-Party License Inventory](../../donut/ThirdPartyLicenses.txt) at Donut revision `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`
- [Image-Based Lighting Environment](../../src/image_based_lighting_environment.cpp)

## Commercial Clearance

Both the TinyEXR and embedded OpenEXR copyright and disclaimer text must be
preserved in applicable distributions.
