# UVSR Theta

![host: Rust](https://img.shields.io/badge/host-Rust-000000?logo=rust&logoColor=white&style=flat)
![shaders: Rust | Slang | HLSL](https://img.shields.io/badge/shaders-Rust%20%7C%20Slang%20%7C%20HLSL-C62828?style=flat)
![backends: DirectX 12 | Vulkan 1.4 | Metal 4](https://img.shields.io/badge/backends-DirectX%2012%20%7C%20Vulkan%201.4%20%7C%20Metal%204-4C8F20?style=flat)
[![license: Polyfrom Noncommercial](https://img.shields.io/badge/license-Polyfrom%20Noncommercial-8250DF?style=flat)](LICENSE.md)

this repository is the working home for a lightweight Rust graphics framework with explicit DirectX 12, Vulkan 1.4, and Metal 4 backends. the finished system is intended to make Rust, Slang, and HLSL equally usable shader languages, expose modern bindless and GPU-pointer capabilities without hiding native behavior, and provide one conformance system for interactive review and automated diagnosis.

[AGFX](https://github.com/AmelieHeinrich/agfx) and [ShaderToHuman](https://github.com/electronicarts/ShaderToHuman) are design and testing inspirations. their explicit APIs, shader examples, golden-image tests, structured results, and report presentation inform the project, but they are not direct implementation sources or compatibility specifications. this project defines and tests its own Rust contracts.

the first end-to-end proof will run a Rust-authored shader through the real [NoGraphicsAPI](https://github.com/sebbbi/NoGraphicsAPI) host, exercising physical GPU pointers and native descriptor heaps. reusable compiler and shader-library work will be designed for contribution to [RustGPU](https://github.com/Rust-GPU/rust-gpu).

## Target Matrix

the project plans to support shaders authored in Rust, Slang, and HLSL across each applicable native backend:

| platform | backend | Rust | Slang | HLSL |
| --- | --- | :---: | :---: | :---: |
| Windows | DirectX 12 | planned | planned | planned |
| Linux | Vulkan 1.4 | planned | planned | planned |
| MacOS | Metal 4 | planned | planned | planned |

compiled bytes, shader stage, entry point, source language, and required backend metadata remain explicit. a Rust host that executes only HLSL does not count as Rust shader support.

## Design Direction

- provide a small explicit API with project-owned behavior and native escape hatches.
- use explicit Rust ownership with small reviewed `unsafe` boundaries around native graphics APIs.
- retain backend-specific capabilities and failure modes where flattening them would hide real behavior.
- keep generic RustGPU changes separate from framework integration and narrow NoGraphicsAPI work.
- use one result model for human reports and concise machine-readable failure analysis.
- prove one vertical slice before expanding API coverage, settings, or backend variants.

the detailed contracts live in [architecture](docs/architecture.md), [testing](docs/testing.md), and [upstream contribution boundaries](docs/upstream.md).

## Lessons from UVSR Delta

the repository also preserves what people and LLMs can learn from UVSR Delta. its experiments exposed recurring problems in rendering strategy, visual verification, dependency ownership, agent competence, test duration, product evidence, and feature growth. the [postmortem index](docs/postmortems/README.md) reorganizes every historical postmortem into general guidance, while the [experiment verdict catalog](docs/postmortems/experiment-catalog.md) separates fundamentally flawed formulations from work that could be retried with a better scope, harness, or owner.

the prior C++ implementation and exact historical records remain on [`uvsr-delta-recovery`](https://github.com/brockliddicoat/uvsr/tree/uvsr-delta-recovery). that branch is evidence and recovery material. it is not duplicated into the new product source tree.

## Planned Stages

1. pin inspected inspirations, compiler targets, consumer baselines, and available platform capabilities.
2. choose a small project-owned API slice and a structured test-result contract.
3. prove device creation, one shader, one resource, one submission, readback, and retirement on each native backend.
4. prove the risky Rust shader, physical-pointer, descriptor-heap, and translation paths with bounded probes.
5. expand only the API and shader-language cells supported by executed evidence.
6. contribute reusable RustGPU changes as focused upstream patches.

the complete sequence and exit criteria are in the [roadmap](docs/roadmap.md).

## Repository Contents

- `.github/workflows` validates the repository baseline and automatically runs Rust formatting and workspace tests once `Cargo.toml` exists.
- `assets/scenes` retains Bistro Interior and San Miguel as future graphics fixtures, together with their provenance, conversion reports, and controlling notices.
- [`assets/fonts`](assets/fonts/README.md) retains MIT-licensed ProggyClean and ProggyForever assets and local Windows Segoe UI copies excluded from Git. the [legal index](legal/README.md) documents fonts and scene assets.
- `docs` defines the architecture, roadmap, testing contract, upstream boundary, and lessons from prior experiments.
- `AGENTS.md` and `CONTRIBUTING.md` define the direct-to-`main` pull request and checkpoint commit workflow.
- `NOTICES.md` records pinned inspirations, research references, and retained scene restrictions.

## Current Status

the planning baseline is ready for implementation. there is not yet a Rust workspace, compiler patch, successful build, GPU result, backend parity result, or upstream pull request. retained scene files are source material, not evidence that the new project builds or renders them.

## Contributing

read [AGENTS.md](AGENTS.md) and [CONTRIBUTING.md](CONTRIBUTING.md) before changing the repository. use one purpose-named task branch, commit coherent verified checkpoints, and open the pull request directly into `main`. do not create merge-only branches.

first-party material remains under the [Polyfrom Noncommercial License](LICENSE.md). upstream projects and retained assets keep their own licenses and notices. review [notices](NOTICES.md) before importing code, translating shaders, or redistributing scene data.
