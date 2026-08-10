# NVIDIA Donut

## Record

- Relationship: Dependency Integration, Adapted Implementation, and Design Influence
- Status: Current
- Confidence: Confirmed
- Upstream: [NVIDIA Donut](https://github.com/NVIDIAGameWorks/donut)
- Revision: `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`
- Governing Terms: [MIT License](https://github.com/NVIDIAGameWorks/donut/blob/bc1ea24b0486f1c00d89327fe16c0b4dd11c5937/LICENSE.txt)

## UVSR Relationship

Donut is UVSR's pinned engine foundation. UVSR directly uses its application,
scene, material, view, render-pass, shader, and loading infrastructure. UVSR
also stages reviewed patches over selected Donut files at build time. Current
NVIDIA/MIT headers and recognizable deferred-lighting structure preserve this
lineage; UVSR-specific behavior outside those boundaries is first-party.

## Evidence

- [Submodule Declaration](../../.gitmodules)
- [Build Integration](../../CMakeLists.txt)
- [Engine Override](../../overrides/donut-engine.patch)
- [Application Source](../../src/uvsr.cpp)
- [Deferred Lighting Adapter](../../src/pbr_deferred_lighting_pass.cpp)
- Initial repository commit `bcf097a7015f95e901b29561ae7027e5aaa60c15`; PBR foundation commit `1bb1b989f8601b4470468868dbe305ac13f3d596`

## Commercial Clearance

Redistribution must preserve Donut's MIT notice and the licenses of Donut's
own dependencies. UVSR's project license cannot replace those terms.
