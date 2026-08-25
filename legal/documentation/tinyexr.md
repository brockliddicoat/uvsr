# `TinyEXR`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [TinyEXR](https://github.com/syoyo/tinyexr)
- Revision: `58a81c36caad469aed86441cc91080f23b496ffb`
- Archive SHA-256: `c745ae7f336760014509f900779187825b12e61e699ae8a49679a546cd5b8147`
- Header SHA-256: `6d744b9efdcfa18d201d28b21386e99dfeae622e0d03e11fea4d8684fa714c4c`
- Governing Terms: BSD-style TinyEXR terms plus embedded OpenEXR notices

## UVSR Relationship

UVSR fetches the immutable upstream archive directly and owns the `tinyexr`
interface target. It replaces Donut's unpinned vendored ownership; it is not
byte-identical to that former header. Donut's transitional texture loader is
the only current consumer. No retained EXR caller is proven, so remove the pin
and notice with that loader rather than keeping a second image decoder.

## Evidence

- [Direct Pin and Header Check](../../cmake/DirectThirdParty.cmake)
- [Current Transitional Loader](../../donut/src/engine/TextureCache.cpp)

## Commercial Clearance

Both notices are installed as
`bin/licenses/TinyEXR-and-OpenEXR-BSD.txt` (SHA-256
`0d3e165809f0c704b67e8cd860e5e0e148122f70a9d2ac90400d3df9482c67f4`).
