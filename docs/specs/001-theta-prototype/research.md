# baseline, decisions, and research

checked on 2026-09-19 using GitHub's API, `git ls-remote`, pinned source, and the earlier local plan. this is source research. no compiler, GPU, or reference-image experiment ran during specification preparation.

## project starting point

remote: `https://github.com/brockliddicoat/uvsr.git`.

main commit: `6f8b5084aa51962f4d5d6b09081fb991b4a13c8f`.

tree: `c7c2fe29387a41232de4ad8e4a33a4662ba57732`.

the [pinned README](https://github.com/brockliddicoat/uvsr/blob/6f8b5084aa51962f4d5d6b09081fb991b4a13c8f/README.md), [architecture](https://github.com/brockliddicoat/uvsr/blob/6f8b5084aa51962f4d5d6b09081fb991b4a13c8f/docs/architecture.md), and [workflow](https://github.com/brockliddicoat/uvsr/blob/6f8b5084aa51962f4d5d6b09081fb991b4a13c8f/.github/workflows/rust-project.yml) establish a planning-only Rust project. the complete tree query reported 326 blobs, zero `.rs` files, zero `Cargo.toml` files, and no submodule implementation. the workflow validates repository files/links and runs formatting/workspace tests only when a Cargo manifest exists. it is not a completed Rust build or GPU check.

the older local prompt system was `work/rust-gpu-nographicsapi-plan-20260916/START.md`, with nine S0-S8 stage packets and topic articles. it remains recovery evidence. its instruction to implement outside uvsr, old stage ledger, and recovery threshold are superseded here. new implementation belongs in UVSR Theta. the preserved Delta branch supplies lessons, not inherited C++ settings identity, launcher/package architecture, or an implementation prerequisite.

## explicit decisions

| id | decision and reason | authority/status |
| --- | --- | --- |
| D001 | use the exact live main above as the new repository baseline | current user request, verified remotely |
| D002 | restore close AGFX Rust translation and source-test parity | user selected this over main's inspiration-only contract |
| D003 | require ShaderToHuman source parity, including library and distinct examples | user's subsequent clarification, supersedes the old “as much as feasible” scope |
| D004 | keep “inspirations” in the overview while accurately identifying source translations and notices | user wording preference, licenses remain controlling |
| D005 | former nine-cell language/backend implementation requirement | superseded by D011-D013. retained here as decision history only |
| D006 | former prototype gate required native-heap proofs in both NGAPI and AGFX | superseded by D011. actual NGAPI is decisive, a small direct Vulkan slice is required, and adding the full heap profile to AGFX is supporting work |
| D007 | retain the new repository workflow, assets, legal layout, RustGPU naming, and direct-to-main PR process | current main, with no publication authorized by this planning task |
| D008 | use two materially identical failures without new evidence as the loop stop, plus a six-experiment design checkpoint | current-main agent-workflow lesson replaces the older three-experiment rule |
| D009 | use Spec Kit style documents under `docs/specs`, with only temporary run state in `work/theta` | auditability plus current repository document conventions |
| D010 | safe Rust is the default, with necessary exceptions justified and reviewed | user's coding-standard concern. [UNSAFE.md](../../../UNSAFE.md) now owns the conspicuous complete audit required by the latest clarification |
| D011 | make a focused local RustGPU PR draft enabling actual NGAPI the primary endpoint. the close AGFX Rust port and ShaderToHuman parity support it | user priority revision. full parity and report polish must not block the contribution |
| D012 | prioritize Vulkan directly and through NGAPI, with native Windows Vulkan first on the current Windows machine and Linux Vulkan portability retained | explicit user clarification. the earlier OS/backend table incorrectly treated Vulkan as Linux-only |
| D013 | Metal is second backend priority, DirectX last with the most acceptable documented compromises. additional shader languages remain possible future pivots, with no adapter implementation in this plan | user scope revision. do not weaken Vulkan or build unused abstractions for deferred paths |
| D014 | publish execution checkpoints and reusable evidence-qualified lessons, while raw logs and the active work card stay ignored | user request for execution documentation and useful takeaways for future agents |
| D015 | prefer a lightweight Rust port, ideally fewer lines than matched source behavior | user size preference. [measurement rules](plan.md#size-and-complexity) prohibit credit for omitted behavior or hidden implementation |
| D016 | keep the agent handoff in a clean [Spec Kit style prompt](prompt.md), with audit/change-history text outside it | user's handoff-format clarification |

AGFX's [MIT license](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/LICENSE) and ShaderToHuman's [BSD-3-Clause text](https://github.com/electronicarts/ShaderToHuman/blob/d6f98b7d67da802053cd9c702082fa741dec42e7/LICENSE.txt) permit modification subject to their conditions. retain controlling copyright/license notices and accurate provenance for translated material, including binary notices and the BSD non-endorsement condition where applicable. changing language or descriptive labeling is not treated as removing those obligations. first-party licensing does not replace third-party terms.

## upstream refresh

the full four core revisions below still matched live upstream heads. their existing research checkouts also matched these revisions with no tracked changes.

| project | inspected baseline | current observation |
| --- | --- | --- |
| AGFX | `f91b108a111d2ca3ca4b6586b6cb5dd750064fd7` | unchanged |
| RustGPU | `e6394e08eb3356083b12f732a01906f8e49f7c4a` | unchanged |
| NoGraphicsAPI | `d60b10bdfe15c0f350d6d291d8e06afef3fe7d38` | unchanged |
| ShaderToHuman | `d6f98b7d67da802053cd9c702082fa741dec42e7` | unchanged |
| SPIRV-Cross | `a9193e76134de63810ee1a57b5343f236d6ff102` | current head `d029329bd164a3f38338d95ab56c74f27128a029`, 15 commits ahead |
| GitHub Spec Kit | `d4229c071c7ea3885b43e8a7739847300f618f13` | structure reference only, not installed |

SPIRV-Cross's [comparison](https://github.com/KhronosGroup/SPIRV-Cross/compare/a9193e76134de63810ee1a57b5343f236d6ff102...d029329bd164a3f38338d95ab56c74f27128a029) includes matrix/pointer fixes, new GLSL descriptor-heap handling, and dependency changes. inspected patches modify shared/GLSL paths and MSL, with no `spirv_hlsl.cpp` change in this comparison. this does not prove Rust SPIR-V can produce AGFX-compatible DXIL/Metal binding behavior. this is retained research for future pivots. SPIRV-Cross adoption and cross-API translation are outside the active RustGPU-to-Vulkan plan.

RustGPU [PR 237](https://github.com/Rust-GPU/rust-gpu/pull/237) remains open and draft at `e14a70d9260c7df7fab1542810f2ca0276233331`. [issue 524](https://github.com/Rust-GPU/rust-gpu/issues/524) remains open. [PR 534](https://github.com/Rust-GPU/rust-gpu/pull/534) is merged as `00f5d28f6b1e64ea054539a345ff2ef9b91bed85`. these statuses do not imply approval of this project's design.

## decisive source findings

| finding | source | consequence |
| --- | --- | --- |
| AGFX's ordinary Vulkan path uses descriptor arrays | [backend](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx/agfx_vulkan.cpp), [shader compiler](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx_shader/agfx_shader_compiler_linux.cpp) | preserve this ABI and expose NGAPI native heaps separately |
| NoGraphicsAPI has a concrete SPIR-V/root/heap contract | [shader contract](https://github.com/sebbbi/NoGraphicsAPI/blob/d60b10bdfe15c0f350d6d291d8e06afef3fe7d38/docs/slang.md), [host](https://github.com/sebbbi/NoGraphicsAPI/blob/d60b10bdfe15c0f350d6d291d8e06afef3fe7d38/src/NoGraphicsAPI.cpp) | actual-host execution with its entry points and root rules is mandatory |
| RustGPU target pointer width is 32 and physical layout has an explicit gap | [target](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/crates/rustc_codegen_spirv/src/target.rs#L659), [types](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/crates/rustc_codegen_spirv/src/spirv_type.rs#L400-L403) | decide representation/layout with tests before pointer APIs |
| pointer handling crosses SPIR-T passes | [linker passes](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/crates/rustc_codegen_spirv/src/linker/spirt_passes/mod.rs) | code emission alone is insufficient |
| native heap/untyped-pointer semantics have explicit extension rules | [heap specification](https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_descriptor_heap.html), [untyped pointers](https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_untyped_pointers.html) | validate real instructions, interfaces, and capabilities |
| source reports and goldens already exist | [AGFX report](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/tools/test_report/index.html), [ShaderToHuman runner](https://github.com/electronicarts/ShaderToHuman/blob/d6f98b7d67da802053cd9c702082fa741dec42e7/unittests/GigiTest.py) | translate fixtures and reuse the report structure with better failure propagation |

RustGPU pins [`nightly-2026-07-03`](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/rust-toolchain.toml). its [CI](https://github.com/Rust-GPU/rust-gpu/blob/e6394e08eb3356083b12f732a01906f8e49f7c4a/.github/workflows/ci.yaml) uses Vulkan SDK `1.4.321.0`, while the pinned [NoGraphicsAPI requirements](https://github.com/sebbbi/NoGraphicsAPI/blob/d60b10bdfe15c0f350d6d291d8e06afef3fe7d38/README.md#L115-L128) call for Vulkan SDK `1.4.357+`, Slang `2026.14.1+`, and SPIRV-Tools `2026.3+`. its CMake example check admits an older Slang version. these are inspected upstream host/reference requirements, not a task to add that language to the Rust port. record the actual tools needed by the selected host recipe. extension support needs full-pipeline probes, not version-string inference.

the source AGFX port and native NoGraphicsAPI proof are distinct from native Metal support in NoGraphicsAPI. this plan makes no such portability claim. existing RustGPU EXT task/mesh facilities are integration foundations, not features to invent again.

## open engineering questions

| question | next decisive action | owner task |
| --- | --- | --- |
| does the current Windows Vulkan device support the exact NGAPI heap/profile requirements? | query the native loader, feature/property chains, driver, and actual host path. distinguish offscreen and presentation results | T001, T004, T016 |
| which physical-address representation preserves Rust integer and layout semantics? | mixed-width, aggregate, overflow, conversion and alias regressions | T010-T013 |
| do all actual tool paths preserve heap/untyped instructions? | minimal full-pipeline modules in installed/compiled-tools configurations | T005, T014-T015 |
| which source platform assumptions must change for Windows Vulkan? | inspect native build/loader/window ownership and prove the direct Vulkan slice on Windows | T004, T006-T008 |
| does the same Vulkan slice remain portable to Linux? | preserve shared ABI and execute separately on a Linux Vulkan device | T023 |
| can portable ShaderToHuman fixtures reproduce all Gigi defaults? | inspect source/generated bindings and capture exact reference state | T003, T027 |
| does the query/report format help humans and agents? | matched seeded faults and independently checked diagnoses/repairs | T032 |

these questions have explicit experiments. a missing Windows capability remains a primary runtime blocker. future API/language research no longer blocks the contribution, and scoped parity claims retain the full source inventory. raw API snapshots, original inventories, source checks, and document verification are retained in ignored `work/theta/evidence/` in the prepared checkout.

## format reference

the [Spec Kit specification](https://github.com/github/spec-kit/blob/d4229c071c7ea3885b43e8a7739847300f618f13/templates/spec-template.md), [plan](https://github.com/github/spec-kit/blob/d4229c071c7ea3885b43e8a7739847300f618f13/templates/plan-template.md), and [tasks](https://github.com/github/spec-kit/blob/d4229c071c7ea3885b43e8a7739847300f618f13/templates/tasks-template.md) informed the separation of user stories, requirements, success criteria, technical context, constitution checks, and dependency-ordered tasks. this is an authored adaptation, with explicit audit and source-parity contracts for this project.
