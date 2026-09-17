# cross-platform Rust GPU framework

this repository is the working home for a lightweight Rust graphics framework with explicit D3D12, Vulkan, and Metal 4 backends. the finished system is intended to make Rust, Slang, and HLSL equally usable shader languages, expose modern bindless and GPU-pointer capabilities without hiding native behavior, and provide one conformance system for interactive review and automated diagnosis.

[AGFX](https://github.com/AmelieHeinrich/agfx) supplies the behavioral and architectural reference for the graphics API. [ShaderToHuman](https://github.com/electronicarts/ShaderToHuman) supplies additional shader regression material. the product itself is a native Rust system with explicit ownership, small reviewed native API boundaries, and a shared result model across every supported backend and shader language.

the first end-to-end proof will run a Rust-authored shader through the real [NoGraphicsAPI](https://github.com/sebbbi/NoGraphicsAPI) host, exercising physical GPU pointers and native descriptor heaps. reusable compiler and shader-library work will be designed for contribution to [rust-gpu](https://github.com/Rust-GPU/rust-gpu).

## target matrix

the project plans to support shaders authored in Rust, Slang, and HLSL across each applicable native backend:

| platform | backend | Rust | Slang | HLSL |
| --- | --- | :---: | :---: | :---: |
| Windows | D3D12 | planned | planned | planned |
| Linux | Vulkan | planned | planned | planned |
| macOS | Metal 4 | planned | planned | planned |

compiled bytes, shader stage, entry point, source language, and required backend metadata remain explicit. a Rust host that executes only HLSL does not count as Rust shader support.

## design direction

- provide a small explicit API whose behavior and coverage can be compared directly with AGFX;
- use explicit Rust ownership with small reviewed `unsafe` boundaries around native graphics APIs;
- retain backend-specific capabilities and failure modes where flattening them would hide real behavior;
- keep generic rust-gpu changes separate from AGFX host code and narrow NoGraphicsAPI integration;
- use one test truth for human-readable reports and machine-readable failure analysis.

the detailed contracts live in [architecture](docs/architecture.md), [testing](docs/testing.md), and [upstream contribution boundaries](docs/upstream.md).

## planned stages

1. freeze the pinned AGFX, rust-gpu, NoGraphicsAPI, ShaderToHuman, and SPIRV-Cross baselines;
2. reproduce the authoritative AGFX test inventory and expected behavior;
3. establish the Rust workspace and shared API contracts;
4. bring up Vulkan, D3D12, and Metal 4 backends in explicit capability slices;
5. run Rust-authored shaders through NoGraphicsAPI's physical-pointer and descriptor-heap paths;
6. integrate useful ShaderToHuman coverage into the AGFX-style runner with separate **AGFX** and **ShaderToHuman** report tabs;
7. prepare generic rust-gpu compiler and library changes as focused upstream contributions.

the complete sequence and exit criteria are in the [roadmap](docs/roadmap.md).

## repository contents

- `.github/workflows` validates the repository baseline and automatically runs Rust formatting and workspace tests once `Cargo.toml` exists;
- `assets/scenes` retains Bistro Interior and San Miguel as future graphics fixtures, together with their provenance, conversion reports, and controlling notices;
- `docs` defines the new architecture, roadmap, testing contract, and upstream boundary;
- `AGENTS.md` and `CONTRIBUTING.md` define the direct-to-`main` pull request and checkpoint commit workflow;
- `NOTICES.md` records pinned research sources and retained scene restrictions.

the prior C++ UVSR implementation is preserved on [`uvsr-delta-recovery`](https://github.com/brockliddicoat/uvsr/tree/uvsr-delta-recovery). it is not duplicated in the new `main` source tree.

## current status

the planning baseline is ready for implementation. there is not yet a Rust workspace, compiler patch, successful build, GPU result, backend parity result, or upstream pull request. retained scene files are source material, not evidence that the new project builds or renders them.

## contributing

read [AGENTS.md](AGENTS.md) and [CONTRIBUTING.md](CONTRIBUTING.md) before changing the repository. use one purpose-named task branch, commit coherent verified checkpoints, and open the pull request directly into `main`. do not create merge-only branches.

first-party material remains under the [PolyForm Noncommercial License](LICENSE.md). upstream projects and retained assets keep their own licenses and notices. review [notices](NOTICES.md) before importing code, translating shaders, or redistributing scene data.
