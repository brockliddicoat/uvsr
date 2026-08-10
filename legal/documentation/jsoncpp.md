# `JsonCpp`

## Record

- Relationship: Dependency Integration and Indirect Lineage
- Status: Current
- Confidence: Confirmed
- Upstream: [JsonCpp](https://github.com/open-source-parsers/jsoncpp)
- Revision: Amalgamated copy vendored by pinned Donut
- Governing Terms: Upstream public-domain/MIT choice; copyright Baptiste Lepilleur and the JsonCpp Authors where applicable

## UVSR Relationship

Donut uses JsonCpp for JSON scene and configuration data. UVSR consumes that
functionality through Donut and does not identify JsonCpp code as first-party.

## Evidence

- [Donut Third-Party License Inventory](../../donut/ThirdPartyLicenses.txt) at Donut revision `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`
- [Scene Catalog](../../src/scene_catalog.cpp)

## Commercial Clearance

Retaining Donut's full JsonCpp notice is the conservative redistribution path,
including the MIT text for portions not treated as public domain.
