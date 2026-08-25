# `GLFW`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [GLFW](https://github.com/glfw/glfw)
- Revision: `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`
- Archive SHA-256: `699bf0b3d0bd422c0212263f30c4b6fc1ab4f67320824b27854f1e5c6949a2a0`
- Governing Terms: zlib/libpng-style license; copyright Marcus Geelnard and Camilla Löwy
- License SHA-256: `149704059b5d0bf551637e50042dd4de9c2cae921021f6636298911e3a5f9462`

## UVSR Relationship

UVSR fetches and builds the immutable upstream GLFW archive directly as a
static Win32 target. Donut's application shell remains a transitional caller;
UVSR has no GLFW fork. This is not a final dependency endpoint: remove the pin
and notice when the direct Win32 shell replaces every GLFW caller.

## Evidence

- [Direct Pin and Target](../../cmake/DirectThirdParty.cmake)
- [Application Source](../../src/uvsr.cpp)

## Commercial Clearance

The fetched notice is installed as `bin/licenses/GLFW.txt`.
