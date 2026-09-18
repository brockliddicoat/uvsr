# Roadmap and Current State

status: prepared research baseline. implementation, builds, GPU execution, and upstream pull requests have not started.

## Inspected Baselines

the inspected revisions are listed in [notices](../NOTICES.md). refresh them once when implementation starts, record relevant changes, then pin the revisions used for each experiment. AGFX and ShaderToHuman remain inspirations. this project owns its API, test inventory, expected results, and thresholds.

## Stages

| id | outcome | completion evidence |
| --- | --- | --- |
| S0 | device and tool inventory, inspiration study, project contracts, and nine shader-language and backend probes | actual probe runs, explicit capability limits, and first scoped failures |
| S1 | one useful Rust graphics vertical slice with ordinary shaders and structured report output | device, shader, resource, submission, readback, retirement, and image evidence from native Rust backend logic |
| S2 | correct Rust-GPU physical-pointer representation and operations | upstream compiletests and difftests, full-width ABI and overflow regressions |
| S3 | reusable native descriptor-heap helpers and compiler support | real heap instructions, optimizer and validator survival, deterministic resource access |
| S4 | Rust cube and readback through the framework and Rust shaders in NoGraphicsAPI | exact shader identity, combined pointer and heap behavior, project-owned expected-result agreement |
| S5 | complete required project API and backend behavior | per-feature contract mapping and executed backend reports |
| S6 | focused Rust-GPU contribution packet | complete promised checklist, reviewable commits, documentation, and full relevant CI |
| S7 | Rust, Slang, and HLSL support across applicable backends | resource, layout, stage, execution, and result proof by cell |
| S8 | selected shader regression coverage and human and agent test access | real results, trustworthy oracles, queryable reports, and seeded usability evaluation |

S2 and S3 can proceed independently of the complete framework. S7 and S8 begin when the first useful S1 slice exists. a working S4 prototype does not complete S5, S7, or S8.

## First Checkpoint

1. choose isolated implementation, dependency, build, and result directories.
2. inventory available DirectX 12, Vulkan 1.4, and Metal tools and test devices.
3. study the pinned inspirations and write the first project-owned device, shader, lifetime, and result contracts.
4. run the nine minimal shader-language and backend feasibility probes.
5. implement one native vertical slice and emit a stable machine-readable result.
6. select the next API slice only after those results identify viable compiler and backend paths.

## Current Uncertainties

- available devices and required Vulkan 1.4 extensions.
- complete physical-pointer semantics and target layout in Rust-GPU.
- native descriptor-heap support through compilation and optimization.
- Rust shader translation and ABI compatibility on DirectX 12 and Metal.
- Slang binding adaptation across the selected project layouts.
- portable shader-regression fixtures and consistent color handling.

report source inspection, compile success, GPU execution, image agreement, backend parity, and upstream status separately. no weighted completion percentage is defined.
