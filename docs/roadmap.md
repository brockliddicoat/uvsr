# roadmap and current state

status: prepared research baseline. implementation, builds, GPU execution, and upstream pull requests have not started.

## source baseline

the inspected revisions are listed in [notices](../NOTICES.md). refresh them once when implementation starts, record relevant changes, then pin the chosen baseline.

## stages

| id | outcome | completion evidence |
| --- | --- | --- |
| S0 | native references, device/tool inventory, AGFX and ShaderToHuman inventories, nine shader-language/backend probes | actual reference runs and first scoped compatibility failures |
| S1 | useful Rust graphics core slice with ordinary shaders and report output | source/candidate buffer and image agreement with native backend logic in Rust |
| S2 | correct rust-gpu physical-pointer representation and operations | upstream compiletests and difftests, full-width ABI and overflow regressions |
| S3 | reusable native descriptor-heap helpers and compiler support | real heap instructions, optimizer/validator survival, deterministic resource access |
| S4 | Rust cube/readback through the framework and Rust shaders in NoGraphicsAPI | exact shader identity, combined pointer/heap behavior, reference agreement |
| S5 | complete required API and backend behavior | per-feature reference mapping and executed backend reports |
| S6 | focused rust-gpu contribution packet | complete promised checklist, reviewable commits, documentation, and full relevant CI |
| S7 | Rust, Slang, and HLSL support across applicable backends | resource/layout/stage probes and feature parity by cell |
| S8 | portable ShaderToHuman coverage and human/agent test access | real results, trustworthy oracles, separate report tabs, seeded usability evaluation |

S2 and S3 can proceed independently of the complete framework. S7 and S8 begin when the first useful S1 slice exists. a working S4 prototype does not complete S5, S7, or S8.

## first checkpoint

1. choose isolated implementation and dependency directories.
2. inventory available D3D12, Vulkan, and Metal tools and test devices.
3. run pinned native AGFX references and freeze report/readback behavior.
4. build the nine minimal shader-language/backend probes.
5. reconcile the source test inventories with actual runner enumeration.
6. select the first small AGFX slice only after those results identify the viable compiler paths.

## current uncertainties

- available devices and required Vulkan extensions;
- complete physical-pointer semantics and target layout in rust-gpu;
- native descriptor-heap support through compilation and optimization;
- Rust shader translation and ABI compatibility on D3D12 and Metal;
- Slang binding adaptation for AGFX's layouts;
- portable reconstruction of ShaderToHuman's Gigi fixtures and color handling.

report source inspection, compile success, GPU execution, image agreement, backend parity, and upstream status separately. no weighted completion percentage is defined.
