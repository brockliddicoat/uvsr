# `stb`

## Record

- Relationship: Dependency Integration and Indirect Lineage
- Status: Current
- Confidence: Confirmed
- Upstream: [stb](https://github.com/nothings/stb)
- Revision: `2e2bef463a5b53ddf8bb788e25da6b8506314c08`
- Governing Terms: Upstream choice of MIT or public-domain dedication; copyright Sean Barrett

## UVSR Relationship

Donut uses stb image readers and writers in its texture pipeline. UVSR does not
claim the stb implementation or modify its pinned submodule.

## Evidence

- [Texture Loading Override](../../overrides/donut-loading.patch)
- [Donut Submodule Declarations](../../donut/.gitmodules)
- [Donut Third-Party License Inventory](../../donut/ThirdPartyLicenses.txt) at Donut revision `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`

## Commercial Clearance

Use the MIT option for predictable notice preservation unless a distributor
deliberately documents another valid upstream option.
