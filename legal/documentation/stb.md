# `stb`

## Record

- Relationship: Dependency Integration
- Status: Current
- Confidence: Confirmed
- Upstream: [stb](https://github.com/nothings/stb)
- Revision: `2e2bef463a5b53ddf8bb788e25da6b8506314c08`
- Archive SHA-256: `f6a4669309a29dd8634c3c2c7a955da72469c2dc61471f68d9c499e517ab823f`
- Governing Terms: Upstream choice of MIT or public-domain dedication; copyright Sean Barrett
- License SHA-256: `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`

## UVSR Relationship

UVSR fetches the immutable upstream archive directly and owns the `stb`
interface target. First-party environment loading and Donut's transitional
texture pipeline consume the same unmodified target.

## Evidence

- [Direct Pin and Target](../../cmake/DirectThirdParty.cmake)
- [First-Party Environment Loader](../../src/image_based_lighting_environment.cpp)

## Commercial Clearance

The complete choice-of-terms file is installed as
`bin/licenses/stb-MIT-or-Public-Domain.txt`.
