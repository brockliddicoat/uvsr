# UVSR Theta

![host: Rust](https://img.shields.io/badge/host-Rust-000000?logo=rust&logoColor=white&style=flat)
![primary: Vulkan + NGAPI](https://img.shields.io/badge/primary-Vulkan%20%2B%20NGAPI-C62828?style=flat)
[![license: Polyfrom Noncommercial](https://img.shields.io/badge/license-Polyfrom%20Noncommercial-8250DF?style=flat)](LICENSE.md)

the main goal is a focused, reviewable **RustGPU PR that lets Rust-authored shaders work in actual [NoGraphicsAPI](https://github.com/sebbbi/NoGraphicsAPI)**. Vulkan, both directly and through NGAPI, drives the design. **native Windows Vulkan is the primary local test path on the current development machine.** Linux Vulkan is also a portability target. neither path is replaced by DirectX or a translation layer.

> **unsafe Rust:** read [UNSAFE.md](UNSAFE.md) for the policy, current inventory, necessary exceptions, safety contracts, and review evidence. every implemented exception must be visible there and documented beside its code.
>
> **audit and progress:** [specification and prompt](docs/specs/001-theta-prototype/README.md), [execution record](docs/execution.md), [lessons for future agents](docs/lessons.md).

[AGFX](https://github.com/AmelieHeinrich/agfx) provides the base for a close, ordinary Rust port used as a high-quality reusable testbed. [ShaderToHuman](https://github.com/electronicarts/ShaderToHuman) supplies shader-library, regression, and example behavior to preserve. both are inspirations and source-parity references. translation retains exact attribution and license notices. complete testbed parity is a separate outcome, not a prerequisite for drafting the RustGPU contribution.

## priorities

| priority | direction | effect on this plan |
| --- | --- | --- |
| primary | RustGPU to SPIR-V to Vulkan, directly and through actual NGAPI | most design and testing effort. Windows first, Linux portability retained. prove physical pointers and native descriptor heaps |
| supporting | lightweight Rust AGFX testbed and ShaderToHuman source parity | build the smallest useful Vulkan slice first, then extend matched source coverage |
| second backend | Metal | preserve room for a later native implementation without diluting Vulkan semantics |
| lowest | DirectX | later work may accept the largest explicitly documented compromises |

additional shader languages are future pivots. this plan does not implement Slang or HLSL adapters or a nine-cell compatibility matrix. keep shader payload, stage, entry point, target, profile, and compiler identity explicit so future routes can be added without a new framework.

## design direction

use safe Rust and explicit ownership. keep Vulkan capabilities and native escape hatches visible. isolate necessary unsafe operations and asynchronous resource retirement. generic RustGPU changes belong upstream, while AGFX and NGAPI integration remain separate consumer evidence.

keep the port as small as the preserved behavior permits, ideally smaller than equivalent source material. compare matched functionality with a fixed counting method. do not remove tests, safety documentation, or behavior to reduce a number. do not add a render graph, ECS, second RHI, speculative backend skeletons, or a general compiler-plugin system.

the [architecture](docs/architecture.md), [testing contract](docs/testing.md), and [contribution boundary](docs/upstream.md) define the project rules. the [roadmap](docs/roadmap.md) leads with the compiler contribution and separates later parity work.

## retained history and inputs

the [UVSR Delta postmortems](docs/postmortems/README.md) contain prior lessons. the [experiment verdict catalog](docs/postmortems/experiment-catalog.md) distinguishes flawed formulations from work worth retrying. exact historical source remains on [`uvsr-delta-recovery`](https://github.com/brockliddicoat/uvsr/tree/uvsr-delta-recovery), as recovery evidence.

`assets/scenes` retains Bistro Interior and San Miguel with their provenance and controlling notices. [`assets/fonts`](assets/fonts/README.md) retains MIT-licensed ProggyClean and ProggyForever assets and local Windows Segoe UI copies excluded from Git. the [legal index](legal/README.md) records font and scene restrictions. these inputs do not prove the new project builds or renders them.

## current status

implementation is active. the [execution record](docs/execution.md) includes actual NGAPI scalar, uniform/divergent native heap and textured-cube Rust shader results on Windows Vulkan. the [direct Rust slice](tools/theta/README.md) also passes the frozen AGFX buffer-copy case. its compute shader compiles, while direct Rust dispatch, full testbed parity and the upstream PR remain open.

`.github/workflows` validates the baseline and runs Rust formatting and CPU workspace tests. it does not replace RustGPU's upstream CI or actual NGAPI execution.

## contributing

read [AGENTS.md](AGENTS.md), [UNSAFE.md](UNSAFE.md), and [CONTRIBUTING.md](CONTRIBUTING.md). use the [start/resume prompt](docs/specs/001-theta-prototype/quickstart.md), preserve unrelated work, and record meaningful checkpoints and lessons. publication and Git actions follow actual task authority.

first-party material remains under the [Polyfrom Noncommercial License](LICENSE.md). upstream code and retained assets keep their own terms. review [NOTICES.md](NOTICES.md) before importing or translating material.
