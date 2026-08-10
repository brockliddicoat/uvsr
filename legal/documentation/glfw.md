# `GLFW`

## Record

- Relationship: Dependency Integration and Indirect Lineage
- Status: Current
- Confidence: Confirmed
- Upstream: [GLFW](https://github.com/glfw/glfw)
- Revision: `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`
- Governing Terms: zlib/libpng-style license; copyright Marcus Geelnard and Camilla Löwy

## UVSR Relationship

Donut uses GLFW for window and input integration, and UVSR directly includes
its native Win32 interface where needed. UVSR has no independently vendored
GLFW fork.

## Evidence

- [Application Source](../../src/uvsr.cpp)
- [Donut Submodule Declarations](../../donut/.gitmodules)
- [Donut Third-Party License Inventory](../../donut/ThirdPartyLicenses.txt) at Donut revision `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`

## Commercial Clearance

Preserve the GLFW license notice in source and binary distributions as required
by its permissive terms.
