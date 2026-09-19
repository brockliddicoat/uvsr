# Legal

this directory is the entry point for third-party ownership, provenance, and
distribution records. records describe the evidence available to this project.
they do not grant rights, replace the controlling terms, or establish legal
clearance for an unreviewed use.

## Project Material

the [project license](../LICENSE.md) applies only to first-party material.
[notices](../NOTICES.md) identifies retained third-party assets and inspected
research references. third-party material retains its own terms.

## Fonts

- [Dear ImGui default fonts](imgui-fonts.md), retained ProggyClean and
  ProggyForever assets with complete MIT notices and exact source identities.
- [Microsoft Segoe UI](segoe-ui.md), local
  Windows-sourced fonts with no recorded redistribution license.

## Scene Assets

- [Amazon Lumberyard Bistro](bistro.md),
  attribution and the immediate source GLB's unresolved chain of title.
- [San Miguel](san-miguel.md), supplied research and
  educational use restrictions.
- [San Miguel Camera Data](san-miguel-camera.md),
  the separate provenance of the initial camera.

## Distribution Review

before including third-party files in a public repository or package, retain
the controlling license and any separate permission, identify the exact files
and modifications, and verify that the intended use is covered. a notice,
attribution, noncommercial label, or absence of a fee does not itself supply
permission. unresolved items remain unresolved until supporting evidence is
recorded.

## Compiler Patches

the [prerequisite patch record](../patches/rustgpu-prerequisites/README.md) identifies rspirv source and local modifications, with its complete [Apache-2.0 license](licenses/rspirv-Apache-2.0.txt). source pins, patch hashes and existing upstream work remain explicit.

the same record retains full MIT and Apache-2.0 texts for SPIR-T and RustGPU source patches, separately from the project's own license.

the SPIRV-Tools wrapper source patch retains its full [MIT](licenses/SPIRV-Tools-rs-MIT.txt) and [Apache-2.0](licenses/SPIRV-Tools-rs-APACHE.txt) texts. the separate native optimizer patch retains [SPIRV-Tools' Apache-2.0 license](licenses/SPIRV-Tools-APACHE.txt). the patch manifest records its exact base and modification independently of the wrapper and header pins. generated tables remain outside this repository.

the separate [NGAPI host prerequisite](../patches/ngapi-physical-readback/README.md) retains NoGraphicsAPI's complete [MIT notice](licenses/NoGraphicsAPI-MIT.txt), original source pin and local modification. NGAPI remains an external native consumer, outside the generic RustGPU contribution.
