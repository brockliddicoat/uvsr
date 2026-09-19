# theta execution record

this tracked record preserves meaningful work, decisions, failures, and handoffs for reviewers and future agents. it is not a transcript or a second task ledger. [tasks](specs/001-theta-prototype/tasks.md) owns implementation completion, [research](specs/001-theta-prototype/research.md) owns source pins and design decisions, and [lessons](lessons.md) owns reusable findings.

## recording contract

append a checkpoint after a milestone, important experiment or failed approach, scope change, unsafe review, or handoff. also record a blocker that materially changes the next action. update the ignored `work/theta/STATE.md` continuously and keep raw commands/logs/captures in `work/theta/NOTES.md` and `evidence/`. promote the concise conclusion here before leaving the checkpoint.

each entry records date and stable ID, task/requirement IDs, exact source and tool/configuration identities or their pinned manifest, objective, change, decisive command or reproduction reference, expected/observed results, uncertainty and unrun checks, unsafe-record changes, lessons added or reconsidered, and next action. label documentation, source inspection, compilation, device execution, image comparison, and upstream outcomes separately.

retain failed hypotheses with the smallest reproducer and what would justify retrying. correct earlier conclusions in a new entry with a link to the superseded entry. do not erase a failure to make the narrative appear linear. summarize evidence, not private reasoning. redact machine-specific secrets from durable records.

ignored local evidence is not automatically available to another clone. include enough commands, pins, observations, and limitations here to understand or reproduce the result. if a conclusion relies on a missing local artifact, label that availability limit. move reviewed source fixtures and mappings into their normal tracked owners when implemented, without checking in generated logs or captures.

```text
## E-nnn. date, checkpoint title
tasks / requirements / milestone:
source and configuration:
objective and change:
command or reproducible evidence reference:
expected and observed result, evidence layer:
unsafe audit changes:
lessons added, revised, or none:
limitations and unrun checks:
next action:
```

## E-001. 2026-09-19, refreshed planning baseline

scope: documentation preparation and source inspection only, before T001 activation.

the plan was rebased conceptually onto UVSR main `6f8b5084aa51962f4d5d6b09081fb991b4a13c8f`, with the core source pins in [research](specs/001-theta-prototype/research.md). a live GitHub tree query found 326 blobs, zero Rust files, and zero Cargo manifests. the earlier S0-S8 prompt system was archived locally and replaced by a Spec Kit style audit, contracts, task list, and bounded resume prompt. the user restored close AGFX and ShaderToHuman source parity.

evidence: `git ls-remote origin refs/heads/main`, GitHub commit/tree API records, and source inventories under ignored `work/theta/evidence/`. local copies preserve the old prompt archive and hash manifest. source counts are not runtime-discovered test counts.

result: reviewable planning documents. no implementation, build, GPU result, reference capture, or PR was produced. prior documentation checks apply only to their recorded document revision.

## E-002. 2026-09-19, Vulkan and RustGPU priorities

authority: the user's scope clarification. affected requirements: FR-002 through FR-005, FR-008, FR-018, FR-020 through FR-023.

the primary endpoint is a local RustGPU PR draft with real NGAPI consumer proof. native Windows Vulkan is the first local path on the development machine, with Linux Vulkan portability retained. Metal is the second backend direction and DirectX the lowest priority. additional-language implementation and the former nine-cell gate are removed from active scope. full AGFX and ShaderToHuman parity remains a separately reported supporting outcome.

the previous specification is preserved in ignored `work/theta/archive/spec-v1.1-before-vulkan-focus/` with a hash manifest. stable task IDs are retained where useful and retired IDs remain documented. the new [unsafe audit](../UNSAFE.md), this record, and [lessons](lessons.md) make safety decisions and reusable findings visible outside local scratch notes. [size accounting](specs/001-theta-prototype/plan.md#size-and-complexity) compares matched behavior.

evidence layer: user direction and document changes. first-party Rust remains unimplemented, so no unsafe exception has been verified. this Windows device's extension support is unknown until T001/T016. no runtime or compiler claim follows from the revised priorities.

next action: finish the documentation audit. implementation activation remains T001 when requested. preserve all unrelated work and Git indexes.

## E-003. 2026-09-19, documentation handoff verified

scope: plan revision and agent handoff only. main still resolves to `6f8b5084aa51962f4d5d6b09081fb991b4a13c8f`. the clean `docs/specs/001-theta-prototype/prompt.md` follows the Spec Kit sequence and names the prepared checkout. human review history remains outside that prompt.

document checks covered 27 changed/new Markdown files, 240 Markdown links including 174 local links and 15 local anchors, 23 functional requirements, 11 success criteria, and 33 unique unchecked implementation tasks. T020-T022 are retired by scope, not completed. AGENTS.md is 750 words and README.md is 621, within their caps.

preservation checks found all 17 workflow-required files and 295 tracked scene files present. the 23-file prior-spec archive matches its manifest. the original checkout and isolated worktree index hashes are unchanged. no protected workflow, asset, LICENSE.md, legal asset record, or CONTRIBUTING.md diff exists. whitespace checks passed.

evidence: ignored `work/theta/evidence/priority-update-document-check.json`, using local Markdown target/anchor checks, requirement/task ID comparisons, workflow baseline validation, `git diff --check`, `git ls-remote`, and SHA-256 preservation checks. this is documentation/source evidence only. all compiler, device, GPU, parity, usability, unsafe implementation, and upstream CI checks remain unrun.

unsafe status: the policy and registry are prepared, with no implemented exceptions to review. lessons L-001/L-002 are source observations, and L-003-L-005 are proposed practices, not measured runtime conclusions.

next action: hand off prompt.md. T001 remains the first implementation task when implementation is requested. no commit, branch change, push, or public PR was performed.

## E-004. 2026-09-19, restored linked handoff resources

scope: resource navigation in the agent prompt. converted its project-document references to clickable Markdown links and added a resource key covering current documents, exact upstream source revisions, compiler tests/CI, review discussions, and safety/extension references. the ignored local work card and notes retain explicit paths with linked instructions, so the repository prompt does not depend on untracked link targets. a local resource key at `work/theta/RESOURCE-LINKS.md` also provides absolute clickable paths to every current resource, including those live work files.

the six referenced project heads were rechecked through GitHub's API and still match the pinned UVSR, RustGPU, NGAPI, AGFX, ShaderToHuman, and Spec Kit revisions. GitHub tree queries confirmed all 25 directly linked source file/directory paths. local documents remain the current prepared working copies, including unpublished edits. live upstream links and immutable revision links are labeled separately.

validation and exact link counts are recorded in ignored `work/theta/evidence/prompt-resource-link-check.json`. this is document navigation and source-link evidence only. implementation priorities, requirements, source pins, and task completion are unchanged. no new general lesson or runtime conclusion is asserted.

next action: use the linked prompt and resource key. T001 remains the first implementation task.

## E-005. 2026-09-19, shortened linked prompt

at the user's request, removed the resource-key appendix and its jump link from the agent prompt. retained the inline links and implementation instructions. the separate local resource key remains available. this is a presentation edit with no implementation or scope change.

## E-006. 2026-09-19, activated implementation and queried Windows Vulkan

tasks / requirements: T001, M0. FR-001, FR-004, FR-013, FR-018, FR-022. implementation is authorized by the task handoff. local commits are authorized, external publication is not.

ownership: this coordinator owns `C:/Users/brock/.codex/worktrees/0cd5/uvsr`, branch `codex/theta-prototype`, its ignored tools/builds, and separate `work/theta/upstream/rust-gpu` and `work/theta/upstream/NoGraphicsAPI` clones. the prepared checkout `theta-spec-audit/uvsr`, the original OneDrive checkout, and existing AGFX/ShaderToHuman research references remain read-only. RustGPU and NGAPI clones retain their [research pins](specs/001-theta-prototype/research.md). the task's recorded turn context verifies `gpt-6-astra`, reasoning `max`, approval `never`, and `danger-full-access`. no model or execution setting was changed. no agents, goals, or automations were started.

bootstrap: all 27 source Markdown files matched `astra-max-handoff-documents.json` before copying and again at the destination. both Git index SHA-256 values remained unchanged during import. copied the work card and notes, retained prior evidence by read-only reference, and changed the copied prompt/card to the actual implementation checkout. no resource-key appendix was restored. `work/theta/evidence/bootstrap-import.json` records every source/destination hash.

source comparison: `git ls-remote origin refs/heads/main` returned `6e210cdfde61992f69b156690c28b650bdf6d972`. its diff from `6f8b5084aa51962f4d5d6b09081fb991b4a13c8f` contains independent font assets, attributes and legal records, with README/NOTICES overlap. inspected that diff and retained the approved `6f8b5084` implementation baseline. future integration must reconcile the font notices. no workflow, scene, or legal record was changed by activation.

first decisive device result: native `C:/Windows/System32/vulkaninfo.exe` with process-local `VK_LOADER_LAYERS_DISABLE=~implicit~` completed enumeration. loader/tool version is 1.4.341.0. the NVIDIA RTX 4090 Laptop GPU reports driver 616.56, Vulkan 1.4.351, `VK_EXT_descriptor_heap`, `VK_KHR_device_address_commands`, `VK_KHR_shader_untyped_pointers`, `descriptorHeap=true`, `shaderUntypedPointers=true`, `bufferDeviceAddress=true`, and `shaderInt64=true`. Intel Arc reports driver 101.8724 and Vulkan 1.4.344. the NVIDIA extension path is a viable candidate, not yet an enabled-device or shader execution pass. this older inventory tool does not report the device-address-commands feature struct. T016 must query it directly.

failures and tools: full enumeration with implicit layers timed out after 45 seconds. the process-local disabled-layer run completed in 2.2 seconds, without identifying the responsible layer. MSVC 19.44 and Windows SDK 10.0.26100 are available. NGAPI's exact CMake configuration first fails at missing Vulkan headers/import library >=1.4.357. Rust was absent from PATH. isolated rustup bootstrap succeeded, but the official pinned-nightly download failed with connection resets in both backends. stop repeating those requests. SDK setup and a separately recorded mirror attempt continue under T004/T005.

reproduction: run `vulkaninfo --summary`, then `vulkaninfo` with the process-local layer setting. configure pinned NGAPI with `cmake -S work/theta/upstream/NoGraphicsAPI -B work/theta/build/ngapi -G "Visual Studio 17 2022" -A x64 -DNOGRAPHICSAPI_BUILD_EXAMPLES=OFF -DNOGRAPHICSAPI_BUILD_TESTS=OFF`. ignored `work/theta/evidence/` contains `vulkan-full-no-implicit.*`, `ngapi-baseline-configure.*`, and `rust-toolchain-curl.*` with commands, exits, hashes and logs.

unsafe audit: no first-party Rust implementation or exception exists yet. no unsafe review is claimed. lesson L-006 records the probe finding. compiler, shader execution, actual NGAPI, images, Linux portability, parity, and upstream CI remain unrun. next: finish isolated tool setup, probe NGAPI and the actual RustGPU pipeline, then eligible M1 work.

## E-007. 2026-09-19, built NGAPI and froze the first source slice

tasks / requirements: T004, T002, T003 completed. T005 remains partial. FR-002, FR-004, FR-005, FR-010, FR-013, FR-017, FR-021, FR-022.

native host: extracted the published Vulkan SDK 1.4.357.0 installer into ignored task-local tools, after its SHA-256 matched LunarG's file manifest. system loader/driver and user environment were unchanged. MSVC 19.44.35228 and Windows SDK 10.0.26100 built the unchanged pinned NGAPI library with one worker. [the native probe recipe](../tools/theta/ngapi-probe/README.md) builds a small capability consumer plus the unmodified upstream command-context test. NGAPI already supports Windows headless creation and Win32 surfaces. no NGAPI patch was needed.

result: the actual host created a device on the NVIDIA RTX 4090 Laptop GPU with max push data 256, texture descriptor size 32 and sampler descriptor size 32. this establishes its required device feature query/enablement path, including device-address commands. Release and Debug command-context tests returned 0, not the unsupported code 77. Debug's loader trace confirms insertion of `VK_LAYER_KHRONOS_validation` 1.4.357 into both instance and device calls. the command test produced no validation diagnostics. it exercised real allocations, descriptor heaps, timestamp readback, completion and context reuse, with zero Rust shader cases. presentation, textures rendered by shaders, and Linux remain unrun.

artifact boundary: NGAPI's `create_compute_pso` accepts a word span and fixes entry `computeMain`; graphics expects the stage-specific source entries. root bytes pass through `vkCmdPushDataEXT`. AGFX's stock xmake selects D3D12 on Windows and Vulkan only on Linux. its Vulkan shader module accepts explicit SPIR-V words. the Rust port must choose native Windows Vulkan explicitly rather than inherit that OS routing.

source slice: [reviewed mapping](../tests/parity/primary-slice.md) and [hash manifest](../tests/parity/primary-slice-sources.json) freeze 11 source files, two behavior groups and six C/Cpp/Ez registrations. copied two unchanged 256-byte upstream goldens with the exact MIT license. independently evaluated source formulas match both binaries: offset-copy pattern and four dependent dispatches producing `15*i+11`. this is source/oracle verification, not AGFX runtime parity. AGFX/ShaderToHuman references remain unchanged. complete source inventories and all Rust mappings remain unimplemented outside this freeze.

T005 tool result: SPIRV-Tools `v2026.3.rc1-0-gb707790a` passed all six [installed-tool probe cases](../tests/compiler-probes/README.md). logical, physical-store, untyped-store and combined real resource/sampler heap modules survived assembly, validation, link, `-O`, disassembly, reassembly and validation. expected alignment and heap-capability omissions were rejected. generated artifacts were not dispatched. RustGPU parser, SPIR-T qptr, compiler linker, compiled-tools configuration and Rust shader execution remain pending. these tests do not close T005 or H07.

reproduction/evidence: the tracked native and SPIR-V recipes above are sufficient to rerun with the pinned inputs. ignored evidence includes `ngapi-debug-device.*` (layer trace), `ngapi-debug-commands.*` (exit 0), `spirv-tools/results.json` (fixed 6-case denominator, tool/fixture hashes and every command), SDK manifests and download hash. the earlier failed guessed SDK filename returned 404; querying LunarG's manifest supplied the correct filename. retain that failure rather than treating guessed version names as a tool identity.

unsafe review: no first-party Rust exceptions exist. inspected the C++ diagnostic's create/check/caps/wait/destroy lifetime and the exact upstream test entry. no independent review is claimed. assembly fixtures remain non-executed with explicit raw-address preconditions. `UNSAFE.md` records the scope and dependency distinction. no new general lesson beyond L-006 was needed.

Rust setup: after official and RsProxy resets, direct USTC and SJTUG downloads supplied identical nightly manifests with SHA-256 `3fc1dad39caa648c42f716de8cba4e2846d2ce5ff5da75ed10df2bdf6d53f5bf`, matching the published checksum. the compiler commit matches the pinned `c397dae808f70caebab1fc4e11b3edf7e59f58c7`. isolated rustup is now downloading the eight requested components from USTC. earlier failures remain evidence, not a claim of a general server outage. the user's separate default Rust installation does not share this task's `RUSTUP_HOME` or `CARGO_HOME`.

next action: complete the pinned toolchain installation and T005's actual RustGPU pipeline probes, then choose T010/T014 from decisive compiler evidence. no compiler contribution, local PR draft, source parity or M1-M3 completion is claimed at this checkpoint.

## E-008. 2026-09-19, installed Rust and located compiler pipeline failures

tasks / requirements: T005 compatibility inventory, M0. FR-001, FR-013, FR-015, FR-021, FR-022. this checkpoint records diagnostic sources and failures, not completed compiler features.

toolchain: USTC successfully installed `nightly-2026-07-03` in the task-local Rust homes. `rustc -vV` reports 1.98.0-nightly, commit `c397dae808f70caebab1fc4e11b3edf7e59f58c7`, host `x86_64-pc-windows-msvc`, and LLVM 22.1.8. Cargo, rustc-dev, rust-src, llvm-tools, rustfmt and clippy are installed. the pinned RustGPU release build with `--locked --no-default-features --features use-installed-tools -j 1` passed in 12m 10s. the user's default installation remains separate. earlier download failures remain recorded, without asserting a general server outage.

reproduction: [the pipeline recipe](../tests/compiler-probes/rustgpu.md) installs the authored diagnostic module into RustGPU `e6394e08eb3356083b12f732a01906f8e49f7c4a`. compiler semantics and dependencies were unchanged for these baselines. each tools configuration required and executed 16 cases with zero ignored tests. both test binaries built successfully, then returned 101 with **4 passed and 12 failed**. installed-tools test compilation took 2m 23s. compiled-tools took 13m 17s with one worker.

| fixture | installed-tools observation | compiled-tools observation |
| --- | --- | --- |
| logical store | parser and SPIR-T round trips pass. default and qptr linker modes panic during storage-class inference on a fully typed interface variable with no specialization instance | same |
| physical store | parser and SPIR-T round trips preserve the physical conversion and `Aligned 4`. both linker modes encounter the same inference panic | same |
| untyped store | rspirv's loader rejects a detached `OpUntypedVariableKHR`. direct SPIR-T lowering rejects capability 4473 (`UntypedPointersKHR`). both linker modes stop at the loader | same |
| combined native heaps | rspirv recognizes the vocabulary but rejects the untyped globals. direct SPIR-T lowering rejects capability 4473 | all four cases stop earlier, because the bundled assembler rejects `DescriptorHeapEXT` |

dependency identity: `rspirv` 0.13.0+sdk-1.4.341.0 comes from `8afc3d0ac8e158128cd1410bb2e4b4c26ab11bb4`. its grammar contains heap instructions, so its version label alone did not explain the failure. `spirt` 0.4.0 comes from `6d89471bb8c810d9d749c7a4009ba47d51811866`, with header pin `2acb319af38d43be3ea76bfabf3998e5281d8d12`. `spirv-tools-sys` 0.13.3 comes from `39c1ec2dee67ee4c8541cfbd8e32cf018320cb73`; its generated build identity is SPIRV-Tools v2025.3, `33e02568`. installed SDK tools are the newer E-007 v2026.3 build. each path needs its own compatibility evidence.

evidence: ignored `rustgpu-baseline-build.json`, `rust-toolchain-ustc.json`, and `rustgpu-{installed,compiled}-probes.{json,stdout.txt,stderr.txt}` retain commands, exits, timings and output hashes. the tracked recipe and module permit reproduction without those logs. no failed result was blessed as a passing expectation.

unsafe audit: the one diagnostic Rust module has `forbid(unsafe_code)` and no unsafe operations or GPU dispatch. coordinator self-review covered fixture preconditions, fixed case IDs, required instruction checks, optimizer/validator calls and nonzero exits. upstream dependencies retain their own unsafe code. no first-party U record is needed. no independent review or runtime soundness result is claimed.

publication: the user authorized frequent verified progress on GitHub main. the E-007 baseline preserved main's independent font changes and merged through [PR #66](https://github.com/brockliddicoat/uvsr/pull/66) as `3191e94ff9879dd324562dffa89c3f6b82799393`. both the PR check and [the resulting main check](https://github.com/brockliddicoat/uvsr/actions/runs/35438429529) passed, then its transient branch was deleted. this authority applies to the owned UVSR repository, not publication to upstream compiler repositories.

next action: fix the demonstrated loader and inference failures, probe the narrowly updated SPIR-T grammar, and implement missing native-heap representation with regressions. T005 and H07 remain open because the full compiler pipeline cannot yet process every required fixture. Rust pointer semantics, native heap shader execution, source parity and M1-M3 remain unproved. lesson L-007 records the dependency distinction.

## E-009. 2026-09-19, repaired untyped global loading

tasks / requirements: T005 prerequisite work. FR-001, FR-013, FR-015, FR-021, FR-022. the [tracked patch and recipe](../patches/rustgpu-prerequisites/README.md) preserve the tested rspirv change, its source/patch identity and full Apache-2.0 license. the loader now places module-scope `OpUntypedVariableKHR` alongside ordinary globals. the new regression checks globals with and without Data Type, function-local placement and exact serialized words.

verification: rspirv base `8afc3d0ac8e158128cd1410bb2e4b4c26ab11bb4`, local commit `7e01958be8984892e56af92e7359f897dcaef175`, nightly `c397dae808f70caebab1fc4e11b3edf7e59f58c7`. `cargo test -p rspirv --locked -j 1` passed 82 unit tests, one binary-fixture test and six documentation tests, with zero failures or ignored cases. this repository omits Cargo.lock, so the initial locked invocation failed, then a local lockfile was generated and retained with the evidence. this change adds no unsafe operation or lint exception. self-review only.

integration: the same 16 RustGPU installed-tools cases compiled in 6m 04s with local rspirv/spirv overrides, a candidate concrete-interface inference guard and SPIR-T's candidate header update. all four parser cases pass. twelve later cases fail, so this is not pipeline completion. untyped linker cases now reach the reserved-opcode inference check. direct SPIR-T fails on the new grammar's aliases field, poisoning later grammar accesses. that run does not establish the inference guard. evidence: ignored `rspirv-untyped-loader-full.*` and `rustgpu-prerequisite-probes.*`, retaining commands, exits and output hashes.

upstream coordination: pre-existing [rspirv PR #264](https://github.com/gfx-rs/rspirv/pull/264), head `4ccca9665f650e13bcbb04bc2c6e7f83d413a253`, contains the identical placement fix alongside other helpers. found after the local implementation. acknowledge that work and coordinate a useful regression rather than submit a duplicate fix. no upstream PR was opened.

failed grammar experiments: simply updating SPIR-T headers to SDK 1.4.357.0 (`29981f65241605e08b0ede4cfeb999fe3b723c6a`) is insufficient. bounded tests exposed aliases/provisional fields, 69 operand kinds beyond six-bit packing, three optional operands and ComponentType values above u16. the overflow diagnostic re-enters lazy grammar initialization and stalls; only the identified owned test process was stopped. the current local alias test passes, but the full extended-grammar test fails on version 1000001 parsed as u8. those unverified changes remain outside the published patch. six bounded grammar iterations are checkpointed in ignored notes and `spirt-grammar-tests*` evidence before further work. no new reusable lesson beyond L-007 is needed.

publication: the diagnostic checkpoint merged through [PR #67](https://github.com/brockliddicoat/uvsr/pull/67) as `6277950d7c130018251a42f017774a9d1379de97`. its PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35438891024) passed. the branch was deleted after verifying the merged tree.

next action: finish grammar metadata compatibility, rerun the concrete-interface control cases, then address the demonstrated untyped/heap IR gaps. T005 and H07 stay open. no Rust shader execution, physical-pointer API, native-heap shader behavior, source parity or M1-M3 completion is claimed.

## E-010. 2026-09-19, restored logical and physical pipeline controls

tasks / requirements: T005 prerequisite work. FR-001, FR-013, FR-015, FR-021, FR-022. [the patch record](../patches/rustgpu-prerequisites/README.md#modern-grammar-and-concrete-interfaces) now preserves SPIR-T's modern grammar support and RustGPU's grammar API adaptation plus concrete-interface specialization fix. patch hashes, exact bases and local commits are in its manifest. applying each patch to an isolated index at its base reproduces the verified commit tree exactly. licenses remain upstream MIT OR Apache-2.0.

source identities: SPIR-T `94fbc6dd7357ce99f69af525d77e48af5fc5c56d` over published 0.4.0, with SDK header `29981f65241605e08b0ede4cfeb999fe3b723c6a`. RustGPU `9c8a26f8a4290d66b63e67c33c7959492374badc` over `e6394e08eb3356083b12f732a01906f8e49f7c4a`, with the already tracked diagnostic module installed locally and the E-009 rspirv override. ordinary generic specialization is retained. already-concrete entry-point globals keep their existing IDs.

result: all three SPIR-T grammar unit tests pass, including loading every extended grammar, alias equivalence, full-word enum values, optional operand counts and old/new operand-name spelling. there are zero documentation tests, which is not additional coverage. the actual RustGPU release unit run took 2m 54s to compile, then executed 32 of 36 registered cases: **26 passed, six failed, four existing macOS-only cases ignored**. all 16 applicable pre-existing unit tests passed. none of the 16 required diagnostic cases was skipped.

| diagnostic fixture | parser | SPIR-T | default linker and optimizer | qptr linker and optimizer |
| --- | --- | --- | --- | --- |
| logical store | pass | pass | pass | pass |
| physical store | pass | pass | pass | pass |
| untyped store | pass | fails on module-scope untyped variable | fails at reserved untyped-pointer opcode | same |
| native resource/sampler heaps | pass | fails on ID decoration | fails at reserved untyped-pointer opcode | same |

this is 10/16 diagnostic passes. physical conversion and `Aligned 4` survive both actual linker modes and performance optimization. it does not establish Rust raw-pointer code generation, alignment for other operations or GPU memory correctness. the remaining six failures are retained as failures. T005 and H07 stay open.

format/lint limits: RustGPU `cargo fmt --all -- --check` passes. direct rustfmt checks of the four changed SPIR-T files pass. SPIR-T's full formatter reports unchanged files such as `src/cfg.rs`; strict Clippy stops at unchanged `build.rs:10` (`unnecessary_map_or`). no lint waiver or unrelated formatting churn was introduced. full upstream CI remains unproved.

evidence: ignored `spirt-grammar-tests-final.*`, `rustgpu-grammar-controls.*`, `spirt-grammar-format*`, `spirt-grammar-clippy.*` and `rustgpu-grammar-format.*` contain exact commands, diagnostics and hashes. the earlier aliases, packing, wide-enum, extension-version and test-compilation failures remain in n010 and their separate records. safe-Rust self-review found no new unsafe boundary or U record. no independent review. no additional reusable lesson beyond L-007.

publication: the loader checkpoint merged through [PR #68](https://github.com/brockliddicoat/uvsr/pull/68) as `3c0f7e98e6810d2371c907cc445fd51ec74e61af`, with passing PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35440146344). its transient branch was deleted after merged-tree verification.

next action: add the demonstrated untyped/ID-bearing IR support and verify the compiled SPIRV-Tools path at its own exact source revision. the latter has been prepared in a separate owned checkout and is being built with one worker. no Rust shader execution, native-heap shader behavior, source parity or M1-M3 completion is claimed.

## E-011. 2026-09-19, enabled compiled descriptor-heap tools

tasks / requirements: T005 prerequisite work. FR-001, FR-013, FR-015, FR-021, FR-022. the [source patch and reproduction recipe](../patches/rustgpu-prerequisites/README.md#compiled-descriptor-heap-tools) update spirv-tools-rs from `39c1ec2dee67ee4c8541cfbd8e32cf018320cb73` to local verified commit `1f283e5106cb5eb7c2cf486b540e4af03fca53ed`. its native library is SPIRV-Tools `9a49b0883b9b635689a85b5647dbfcb223268151`, and headers are `29981f65241605e08b0ede4cfeb999fe3b723c6a`. the generator now includes the four extension grammars required by that source. upstream-generated tables stay outside this repository, with hashes retained for reproduction.

verification: generation passed, then `cargo test -p spirv-tools --all-features --locked -j 1` compiled in 12m 12s and passed all eight integration tests, with zero failed or ignored. four new cases cover native resource/sampler heap round-trips and rejection without the required capability, separately for compiled and installed tools. both paths preserve heap decorations, untyped variables/access chains, size constants, image sampling and physical-store alignment through optimization and serialization. the four existing integration cases also pass. zero unit/doc cases are not additional coverage. `cargo fmt --all -- --check` passed.

identities differ intentionally: compiled tools are `v2026.3` at the full source pin above; installed SDK 1.4.357.0 tools are `v2026.3rc1` at `b707790a`. this result establishes the standalone wrapper, not RustGPU's compiled-tools feature or shader execution. T005 and H07 remain open. ignored `spirv-tools-sdk357-generate.*`, `spirv-tools-sdk357-tests.*` and `spirv-tools-sdk357-format.*` retain exact commands, exits and output hashes. the test record reports 735.37 seconds total.

safety and provenance: source patch plus recorded generated outputs reconstruct the verified commit tree in an isolated index. full wrapper MIT/Apache texts are retained. no FFI, unsafe site or lint exception changed. UNSAFE.md records the updated dependency and coordinator self-review, with no independent or complete native safety audit. no additional lesson beyond L-007.

publication: the grammar/control checkpoint merged through [PR #69](https://github.com/brockliddicoat/uvsr/pull/69) as `a6e284d3c38a568c898de914f8b45a8e62fa8750`. its PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35440867018) passed. the transient branch was deleted after merged-tree verification.

next action: test the untyped-variable/type-operand IR repair through the actual RustGPU linker, then implement the remaining heap ID-decoration and size-constant support. compiled-tools integration follows the same required diagnostic matrix. no Rust shader execution, source parity or M1-M3 completion is claimed.

## E-012. 2026-09-19, preserved untyped-pointer pipeline controls

tasks / requirements: T005 prerequisite work. FR-001, FR-013, FR-015, FR-021, FR-022. [the incremental source patches](../patches/rustgpu-prerequisites/README.md#untyped-pointers-and-static-type-operands) preserve SPIR-T `2ac3a9b7d5805794e687c55bf3ccf1a3ff3f6404` and RustGPU `70c8a22fdcd96004cdaad3a8577add5521a8bd61` over their E-010 commits. optional untyped-global Data Type and static instruction type operands remain distinct from value IDs. RustGPU inference recognizes untyped pointer storage classes, and interface collection retains referenced untyped globals. qptr leaves untyped memory operations explicit.

verification: SPIR-T passes three existing grammar tests and five new structural tests, zero failures/skips. positive cases cover global/local variables, optional type and initializer, operand order, printing, fixed-point serialization, untyped loads/stores through qptr and a typed Private access-chain base. the negative case asserts the existing qptr diagnostic for a typed whole-buffer input. that optional-pass restriction remains unsupported. these structural fixtures are not GPU dispatch or complete extension validation.

the actual RustGPU release run reports **29 passed, three failed, four existing macOS-only skips**. all 16 applicable pre-existing cases pass. the required diagnostic matrix has 13 passes, three failures and no skips: every logical, physical and untyped-store stage passes, including optimization after default and qptr linking. native heaps parse, but SPIR-T and both linker modes still fail on `OpDecorateId`. the full command retains exit 101. T005 and H07 remain open. exact commands and hashes are in ignored `spirt-untyped-ir-final.*`, `rustgpu-untyped-ir-final.*` and `rustgpu-untyped-ir-format.*`. RustGPU full formatting passes. SPIR-T formatting was restricted to changed lines and new tests; the unchanged whole-repository formatting/Clippy failures recorded in E-010 are not claimed resolved.

failed attempts: the first compiler run omitted an untyped interface global and then terminated during qptr's incorrect conversion of an untyped store. it returned `0xc0000409`, with no complete suite result. the next run after the two repairs completed normally. an initial new-test lifetime error was fixed. a mixed typed whole-buffer fixture exposed qptr's existing ambiguous buffer diagnostic and was retained as a negative case alongside a supported typed Private positive case. no failed case was silently removed or counted as a pass. the final explicit-layout handling is restricted to the named untyped variable/access-chain instructions and diagnoses other unsupported static-type operand changes.

safety / provenance: each patch reproduces its exact verified tree in an isolated index. no unsafe operation, FFI declaration or lint exception was added. UNSAFE.md records coordinator self-review and the absence of independent review. no new reusable lesson beyond L-007.

publication: compiled tools merged through [PR #70](https://github.com/brockliddicoat/uvsr/pull/70) as `b9ef1b7d313ae8552d58aa928429d4677fdead93`, with passing PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35441726778). its transient branch was deleted after matching the merged tree.

next action: preserve heap stride ID decorations and size-of type operands, then repeat the remaining failing stages and compiled-tools integration. no Rust-authored shader execution, native heap runtime, source parity or M1-M3 completion is claimed.

## E-013. 2026-09-19, recorded AGFX inheritance candidates

at the user's request, [AGFX port notes](agfx-port-notes.md) now retain source behaviors that may deserve later optimization or simpler Rust. five initial entries cover blocking transfer helpers, per-submission allocation, descriptor-index widths, mapped-buffer interfaces and whole-buffer barriers. each separates observed source behavior, the parity/safety contract, a candidate change and the evidence needed. the pinned AGFX checkout was reverified and read only. the Rust slice is still unimplemented, so inheritance and performance benefits are not claimed. contributor instructions and the primary source mapping link this single record. no unsafe boundary or implementation changed. compiler work continues from E-012.

## E-014. 2026-09-19, preserved native heap metadata through the linker

tasks / requirements: T005 prerequisite work, H07. [incremental source patches](../patches/rustgpu-prerequisites/README.md#heap-metadata-and-dependency-retention) preserve SPIR-T `2259d009f9a70d16cebc0cd5b18256b815a8514b` and RustGPU `aecf38db5856fa153bc311e3530bb1f20c9d6b15`. SPIR-T now represents ID-bearing annotations and descriptor size constants explicitly, preserving their dependencies and order. RustGPU retains annotation-only constants, removes annotations of dead targets, and distinguishes descriptor structs with different member offsets during deduplication.

verification: SPIR-T passes three grammar and eight structural cases, zero failures/skips. the installed-tools RustGPU release gate reports **35 passed, zero failed, four pre-existing macOS-only ignores**, including three new dependency/deduplication regressions. all 16 required diagnostic cases pass without skips. real resource/sampler heap declarations, nonzero-slot accesses, descriptor-size/stride metadata, sampling and physical-store alignment survive default/qptr linking, validation, performance optimization and reparsing. this is assembly-input compiler evidence, not Rust source code generation or execution. compiled-tools integration remains pending, so T005 and H07 remain open.

failed approaches and recovery: two initial SPIR-T compile attempts exposed misplaced or incomplete enum handling. after correction, direct SPIR-T passed but both RustGPU heap linker variants rejected an undefined stride constant. inspection traced it to dead-code elimination retaining the annotation but dropping its extra ID operands. a targeted dependency repair passed all 16 probes. review then found the missing member ID decoration in deduplication and added its regression. six heap-representation experiments are checkpointed here before changing tool configurations. no identical failed command was repeated without new evidence.

evidence: ignored `spirt-heap-ir-final.*` and `rustgpu-heap-ir-final.*` retain commands, output hashes and exit 0. the final compiler run took 87.69 seconds, including 1m 26s compilation. RustGPU full formatting passes. SPIR-T used changed-range formatting, with prior unrelated full-format/Clippy failures unwaived. each exported patch reconstructs its committed tree in an isolated index. licenses and source pins remain in the manifest. UNSAFE.md records safe-Rust self-review, no new unsafe boundary and no independent review. no new reusable lesson beyond L-007.

publication: the untyped-pointer and AGFX-note checkpoint merged through [PR #71](https://github.com/brockliddicoat/uvsr/pull/71) as `b712f35fa4adb432cc86e83c6366d268a6146989`. PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35442359466) passed. its branch was deleted after matching the merged tree.

next action: run this exact compiler candidate with the E-011 compiled SPIRV-Tools wrapper, then begin the Rust pointer-width/code-generation probes. Rust shader GPU results, native heap runtime behavior, source parity and M1-M3 remain unproved.

## E-015. 2026-09-19, completed the two-configuration pipeline gate

tasks / requirements: T005 and H07's assembly-input tool gate are complete. the exact E-014 RustGPU/SPIR-T/rspirv candidate passes with both installed SDK tools and the E-011 compiled spirv-tools-rs candidate. [the reproduction recipe](../patches/rustgpu-prerequisites/README.md#two-configuration-compiler-gate) records the two additional local wrapper overrides and full command. native source and generated-output identities remain in the prerequisite manifest.

| tool configuration | required diagnostic cases | full compiler unit gate | result |
| --- | --- | --- | --- |
| installed SDK `v2026.3rc1`, `b707790a` | 16 passed, zero failed/skipped | 35 passed, zero failed, four existing macOS-only ignores | E-014, exit 0 |
| compiled `v2026.3`, `9a49b0883b9b635689a85b5647dbfcb223268151` | 16 passed, zero failed/skipped | 35 passed, zero failed, four existing macOS-only ignores | exit 0, 667.77 seconds including 11m 07s compilation |

all four fixtures pass parser, SPIR-T, default linker and qptr linker stages. both linker variants also validate optimized and unoptimized output and reparse serialized words. these are actual compiler-pipeline probes with assembly inputs. they do not establish Rust raw casts, Rust-authored heap declarations, GPU memory behavior, images or full upstream CI.

evidence: ignored `rustgpu-compiled-heap-final.*`, `compiled-overrides.toml` and the retained installed/compiled Cargo.lock files. the targeted Cargo update proposed unrelated dependency-edge rewrites. those were discarded, retaining only the two intended source/checksum changes, and the locked offline build accepted that resolution. no package version or unrelated dependency edge changed. no new unsafe site, FFI or waiver. E-015's audit retains coordinator self-review and no independent or runtime safety claim. no additional reusable lesson beyond L-007.

inherited behavior: [A-006 and A-007](agfx-port-notes.md) add source-observed coherent-memory selection and the concurrent-sharing comment versus exclusive-buffer creation mismatch. the pinned AGFX checkout remains clean and read only. no performance cost or GPU failure was measured. the initial Rust contract must make memory and queue-family ownership explicit. these notes remain candidates with no implemented Rust owner.

publication: E-014 merged through [PR #72](https://github.com/brockliddicoat/uvsr/pull/72) as `aab83c9442ea46f65dd5400c85b816f3d338b05b`. its PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35443351551) passed. the transient branch was deleted after exact merged-tree verification.

next action: T010's Rust layout/cast baseline, then an explicit address/target-width decision with unchanged u32 semantics. M1-M3, runtime parity and the contribution draft remain open.

## E-016. 2026-09-19, proved explicit Rust pointer-width layouts

tasks / requirements: partial T010, R05 and the layout/integer portion of A01/A02. the chosen foundation is an opt-in Vulkan `-physical64` target, applied before rustc layout and MIR cast decisions. default targets retain 32-bit pointers/usize. fixed-width integers retain their existing semantics. the [ABI contract](specs/001-theta-prototype/contracts/shader-abi.md#physical-addresses) records the decision and [patch recipe](../patches/rustgpu-prerequisites/README.md#explicit-rust-pointer-width-foundation) owns exact reproduction. T010 and all physical memory/runtime gates remain open.

source identity: RustGPU `f9f3e575ddc95a47ace76fcce6d328967177873a`, including baseline commit `604f441f9a84f2400a987144e55542d65025830a`, on E-014's compiler candidate. SPIR-T, rspirv and compiled tools retain E-015's pins and five task-local overrides. the exported patch reconstructs the committed tree in an isolated index. user Rust homes, source-reference checkouts, repository workflows and protected assets remain unchanged.

observed results:

- 37 compiler and three shared-type unit tests passed, zero failed. four pre-existing macOS-only tests remain ignored. all 16 required assembly-input probes passed. new unit cases cover every target's exact client/backend JSON, explicit target identity and invalid variants, and numeric/string pointer-width encodings across supported historical target-spec formats.
- the selected Rust source matrix passed four case/target pairs. `vulkan1.3` passed the four-byte layout and raw-cast rejection. `vulkan1.3-physical64` passed the eight-byte layout and the same cast rejection. each configuration excludes the opposite ABI's layout case and filters 331 unrelated cases. no unsupported-hardware skip is counted as a pass.
- const assertions cover pointer/usize size, pointer alignment, nested pointer-containing aggregates and offsets. reviewed SPIR-V retains 32-bit add/carry and wrapping operations, explicit truncation/extension, 64-bit high-word shifts through `usize`, mixed-width comparison, root offsets and output stride. memory model remains Logical Vulkan at this foundation checkpoint. source tests forbid unsafe and perform no pointer dereference.
- RustGPU full formatting and direct formatting of the new UI sources passed. expected diagnostics/disassembly were manually reviewed before blessing, then the selected matrix passed without blessing in 2.93 seconds. unit compilation/checks took 86.69 seconds. ignored `rustgpu-physical64-target-unit-final.*` and `rustgpu-physical64-layout-final.*` preserve exact commands, output hashes and exit 0.

failed attempts and corrections: the first source-baseline build ran zero cases after the official glam 0.33.3 download reset. an alternate archive was verified against the unchanged Cargo.lock SHA-256 before populating the isolated cache. the first target compile exposed rustc's `u16` pointer-width field, corrected before cases ran. the first two-target source run exposed stage-ID truncation at hyphens, so the generic harness now escapes hyphens to underscores and documents its selectors. an initial Windows blessing filter matched zero cases and was rejected as evidence. a unique filename filter ran the intended case. missing expectation files were filled only after semantic review, with a final non-blessing pass. lesson L-008 records the selector finding.

unsafe audit: safe target/compiler transformations and safe non-dereferencing probes only. no new boundary, lint waiver, FFI or GPU execution. UNSAFE.md records self-review and no independent safety review. existing full-SPIR-T format/Clippy limitations remain unwaived. all inherited AGFX candidates continue in the single port-notes document, with no new measured optimization claim.

publication: E-015 merged through [PR #73](https://github.com/brockliddicoat/uvsr/pull/73) as `aac2b2e94484ae3c2e5a363c35c224420a988d79`. PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35444062002) passed. its branch was deleted after matching the merged tree.

next action: implement physical addressing, storage-class-constrained casts and defined operation diagnostics on the explicit 64-bit foundation. prove alignment/alias behavior before dereferencing real allocated addresses. shader-library API, actual Windows Vulkan/NGAPI execution and M1-M3 remain pending.

## E-017. 2026-09-19, lowered physical address conversions

tasks / requirements: partial T010/T011, P01-P04 and P08 compiler evidence. RustGPU `0983e4e07ca382804c9f0084b36e049f8613d211` adds physical addressing and constrained raw casts on the explicit 64-bit ABI. SPIR-T `c41f85a5c7320c8718d046d2072315acbb739dcb` preserves physical types and function arguments through qptr. the [patch recipe](../patches/rustgpu-prerequisites/README.md#physical-address-conversions) and manifest own exact reproduction, source bases and licenses. both patches reconstruct their committed trees in isolated indexes. rspirv and compiled tools retain the preceding pins.

verification: **42 compiler tests and three shared-type tests passed**, zero failed, with four pre-existing macOS-only ignores. all 16 required instruction probes pass without skips. new linker cases cover typed/untyped conversions with and without qptr, physical function boundaries, exact alias decoration placement, preservation of explicit restrictions and idempotence. SPIR-T passes three grammar and nine structural tests, zero failures/skips. its new structural test retains physical parameters and both conversions while ordinary logical memory still goes through qptr.

the Rust source matrix passes six required case/target pairs, three per ABI. each target deliberately excludes three opposite-ABI cases and filters 331 unrelated cases. reviewed output includes 64-bit pointer conversions, explicit truncation to u32, signed i32 extension, unsigned extension, unchanged integer/layout controls and a clean PushConstant/PhysicalStorageBuffer conflict diagnostic. another **18 existing storage-class and integer-cast cases pass**, zero failures/skips, 319 filtered. the final source run took 180.08 seconds including rebuilds, and adjacent cases took 2.55 seconds. expected output was reviewed before acceptance, followed by non-blessing checks. final diagnostic formatting was separately checked after normalizing its trailing blank line and retaining LF bytes.

failed approaches and corrections: initial alias decorations were applied before inlining and moved onto non-declaration values, so they now apply after transformations. Vulkan rejected direct pointer-to-u32 conversion, which now converts to u64 before truncation. an initially valid module zero-extended signed input because the old compiler chose integer conversion by destination signedness. the same rustc's native CPU transport oracle and SSA caller contract established the correction. the first correction exposed OpUConvert's unsigned-result restriction, handled with an unsigned intermediate and bitcast. the six cast implementation attempts were checkpointed before broader verification. lesson [L-009](lessons.md#l-009-separate-valid-instructions-from-correct-rust-conversions) records the semantic distinction.

broader checks exposed an incorrect test assumption that validation would reject missing alias metadata. exact structural assertions now cover that requirement in addition to placement validation. typed physical arguments also reached qptr's unimplemented logical-pointer call path. their explicit physical representation now survives that pass. an empty source map and empty dump path in the assembly-only test harness initially obscured this diagnostic and caused a process abort. the harness now has a source-map entry and a valid temporary output path. no GPU or machine crash occurred. standalone SPIR-T initially rejected RustGPU's unrelated local override configuration under `--locked`; isolating that run preserved its lockfile and passed. the optional probe registration patch was adjusted and checked against the exported compiler tree.

evidence: ignored `spirt-physical-parameters-isolated.*`, `rustgpu-physical64-full-unit-gate.*`, `rustgpu-physical64-source-gate.*`, `rustgpu-physical64-adjacent-gate.*` and `rustgpu-physical64-negative-lf.*` retain exact commands, exit 0 and output hashes. full RustGPU formatting and direct UI formatting pass. SPIR-T changed ranges were formatted, with the prior unrelated whole-repository formatting/Clippy failures unwaived. UNSAFE.md records coordinator self-review, no unsafe addition and no independent review. AGFX A-001 through A-007 retain their source-observed candidate status, with no new measured optimization claim.

publication: E-016 merged through [PR #74](https://github.com/brockliddicoat/uvsr/pull/74) as `68e32943bc1b705c3248c2af42b643b2aa819901`. PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35446226306) passed, and the task branch was deleted after exact tree comparison.

limits / next action: there is no Rust shader GPU dispatch, real-address read/write result, complete aggregate-pointer contract or raw-pointer parity. array-valued physical function parameters and untyped pointer-storage variables need separate coverage. preserving explicit physical instructions through qptr does not prove P07's memory-operand lowering/lifting. implement aligned operations and the supported-operation inventory, then a narrowly documented shader-library boundary and real Vulkan/NGAPI allocation tests. T010-T013 and M1-M3 remain open.

## E-018. 2026-09-19, compiled aligned physical accesses

tasks / requirements: partial T011, P05/P06 and ordinary logical-pointer regression evidence. RustGPU `92eea1c02f80a8b99c5209ce1630c828d2294487` carries rustc's alignment into physical64 load/store instructions, then removes only Aligned from known logical accesses after storage-class inference. other flags and following scope IDs retain their order. the [aligned-access patch](../patches/rustgpu-prerequisites/README.md#aligned-physical-accesses) reconstructs its exact committed tree in an isolated index. its source pin, hash and license are in the manifest. SPIR-T, rspirv and compiled tools retain the E-017 pins.

verification: **42 compiler tests and three shared-type tests passed**, zero failed, four pre-existing macOS-only ignores. all 16 required instruction probes pass. four mixed typed/untyped linker cases, with and without qptr, now check physical Aligned 4 plus Nontemporal and removal of logical Aligned 8 while preserving MakePointerAvailable/Visible, NonPrivatePointer and Workgroup scope IDs.

the two-target Rust matrix passes **eight required case/target pairs**. ordinary Vulkan runs three cases and excludes five physical64 cases. physical64 runs five and excludes three ordinary cases. each target filters 331 unrelated cases. the new scalar source emits two physical u32 loads and one store with Aligned 4, unchanged u32 wrapping addition, and logical accesses without alignment. the safe panic/array fixture preserves a dynamic u32 message, bounds control flow and consistent 64-bit index arithmetic. expected instructions were manually reviewed before acceptance, followed by a non-blessing run. **24 existing storage-class, cast and panic cases pass**, with one opposite-ABI exclusion and 314 filtered cases. full RustGPU formatting and direct UI formatting pass.

failed approaches and corrections: the baseline scalar source failed validation for missing alignment. adding alignment exposed 28 core formatting warnings because the panic decompiler expected only ID operands. it now accepts exactly alignment-only memory metadata, retaining rejection of volatile/scoped forms. the stronger memory test found a storage-inference crash on scope IDs, corrected by allowing trailing memory operand IDs in load/store signatures. the safe bounds fixture then found a u32 index merely retagged as u64 during access-chain merging. the merge now converts signed SPIR-V indices from their real types to the wider operand width before addition. all six implementation/debug attempts were preserved before the final gates. the 64-bit debug formatter still emits explicit unprintable placeholders for usize bounds arguments. this limitation is documented, not normalized away.

evidence: ignored `rustgpu-physical-access-source-gate.*`, `rustgpu-physical-access-unit-gate.*` and `rustgpu-physical-access-adjacent-gate.*` retain commands, exits and output hashes. elapsed times were 75.38, 71.07 and 2.96 seconds respectively, including the applicable rebuilds. earlier baseline, alignment, scoped-memory and index-width failures remain separately recorded. all twelve exported patch hashes and the optional diagnostic installation were verified. the current alias-specification clarification is in the patch recipe and L-009. absence of alias metadata is valid SPIR-V, so the structural test checks the compiler policy rather than a supposed validator defect.

unsafe audit: [U-001](../UNSAFE.md#u-001-compile-only-physical-u32-access) owns the compile-only unsafe entry and three raw accesses. deny lints, the source contract and emitted instructions passed coordinator self-review. no independent review, new native boundary, safe memory-access API or GPU soundness result is claimed. compiler transformations remain safe Rust. AGFX A-001 through A-007 remain source-observed candidates, with no newly encountered AGFX behavior or measured optimization in this compiler checkpoint.

publication: E-017 merged through [PR #75](https://github.com/brockliddicoat/uvsr/pull/75) as `363e91c9b3960499a9d20723af0bd9562af600ae`. PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35448990286) passed. the merged tree matched before both transient branch references were deleted.

limits / next action: no Rust shader has executed on the GPU. scalar compilation does not establish aggregate access, arbitrary pointer-operation parity, real-address validity or synchronization. qptr still preserves flagged instructions as raw SPIR-V, so P07's actual load/store lowering/lifting remains open. implement that representation and regression next, then the operation inventory, shader-library contract and real Vulkan/NGAPI consumer. T010-T013 and M1-M3 remain open.

## E-019. 2026-09-19, preserved qptr memory effects

tasks / requirements: partial T011 and P07 compiler evidence. SPIR-T `53ace0c85f9659f2a62c2e9234c84cfe53bad13a` lowers flagged logical loads/stores into actual QPtr operations, retaining memory masks, literals and scope IDs through analysis, printing and lifting. physical-pointer instructions retain their explicit representation. RustGPU `133bbb542b4ade422e96b3134fef5fc242233a46` adds ordinary/qptr optimizer regressions. the separate native SPIRV-Tools prerequisite `cb001ee6bf925867e98f5846e2806d8677c46415`, based on `9a49b0883b9b635689a85b5647dbfcb223268151`, prevents explicit volatile loads from being classified as effect-free. the wrapper remains `1f283e5106cb5eb7c2cf486b540e4af03fca53ed`, with its native submodule changed by the separately exported patch. the [patch recipe](../patches/rustgpu-prerequisites/README.md#qptr-memory-operands-and-volatile-loads) owns application order, commands and licenses.

verification: SPIR-T passes **three grammar and 14 structural tests**, zero failures/skips. five new cases cover absent operands, explicit None, Aligned, Volatile with Nontemporal, and scoped availability/visibility. they require actual qptr loads/stores, exact lifted operands, scope dependencies, preserved physical access and repeat serialization. the compiler gate passes **44 compiler tests and three shared-type tests**, zero failures, with four pre-existing macOS-only ignores. all 16 diagnostic probes pass. two new cases each exercise aggressive dead-code elimination and performance optimization after ordinary or qptr linking. they require both volatile loads, including an unused result, the scoped logical store and aligned physical volatile store to survive. an ordinary unused load must disappear, and all outputs validate. Nontemporal remains a hint rather than a required optimized effect.

the two-target Rust source gate passes all **eight required case/target pairs**, with three ordinary and five physical64 cases. opposite-ABI exclusions remain five/three, and each target filters 331 unrelated cases. another **24 existing storage-class, integer-cast and panic cases pass**, with one opposite-ABI exclusion and 314 filtered cases. source expectations were unchanged. RustGPU full formatting passes. SPIR-T changed-range formatting and its existing full-format/Clippy limitations retain the earlier scope. no additional broad native C++ or upstream CI pass is claimed.

failed controls and correction: before the SPIR-T change, only the absent-operand case passed. the other four failed because their instructions never became QPtr operations. the first full compiler gate then passed 42 cases but failed both new volatile cases, with four existing ignores. shared-type tests did not run after that failure. linking preserved the accesses, but compiled-tools performance optimization removed one volatile load. a separate installed SDK reproducer confirmed that aggressive dead-code elimination alone caused the loss and still produced a valid module. the native classification fix passed both focused cases before the stronger full gate. unpatched installed tools still have this failure. the new regression remains required, with a patched CLI build pending.

evidence: ignored `spirt-memory-operands-baseline.*`, `spirt-memory-operands-lowering.*`, `rustgpu-qptr-memory-gate.*`, `volatile-optimizer/`, `rustgpu-volatile-tools-candidate.*` and `rustgpu-qptr-memory-{unit,source,adjacent}-final.*` retain the commands, failed outputs and hashes. the SPIR-T candidate took 30.98 seconds, focused native rebuild/test 480.25 seconds, full unit gate 56.47 seconds, source gate 177.36 seconds and adjacent gate 2.87 seconds. all fifteen exported patch hashes and the three newly reconstructed commit trees were verified, including optional diagnostic registration. generated native version metadata still names the base revision, so the source manifest also records the native patch. [L-010](lessons.md#l-010-check-memory-effects-after-optimization) records the effect-oracle lesson.

unsafe audit: new Rust transformations and regressions remain safe, with no new lint exception, FFI or public memory-access API. the native dependency change checks opcode and operand count before reading the mask. coordinator self-review is recorded in UNSAFE.md, with no independent review or complete native safety claim. U-001 remains compile-only. AGFX A-001 through A-007 remain observed, unmeasured candidates. this compiler checkpoint encountered no additional AGFX behavior.

publication: E-018 merged through [PR #76](https://github.com/brockliddicoat/uvsr/pull/76) as `e1b707c1eef8e25cf349eab95273d191a01440ab`. PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35450643032) passed. the merged tree matched before the previous task branch was deleted locally and remotely.

limits / next action: no Rust shader GPU dispatch has run. P07's compiler representation is implemented for the tested operands, while its full-pipeline GPU execution requirement remains open. Rust volatile intrinsics, aggregate memory access, complete pointer operations, the shader-library contract and actual Vulkan/NGAPI consumer remain pending. inventory and probe those source operations next. T010-T013 and M1-M3 remain open.

## E-020. 2026-09-19, lowered typed pointer operations

tasks / requirements: partial T010/T011 and P09/P11 operation evidence. RustGPU `ebc1f2727e958c06dbf3c5fa4b22b014b88fb384` adds physical64 address comparisons and a byte-arithmetic fallback after existing logical pointer-offset legalization. checked constant multiplication avoids overflowing the preliminary offset calculation. SPIR-T, rspirv and patched compiled tools retain E-019's pins. the [patch recipe](../patches/rustgpu-prerequisites/README.md#typed-pointer-operations) and manifest own exact application, source identity and license. the exported patch reconstructs its committed tree and retains the optional diagnostic installation.

verification: all **14 required source/target pairs pass**. ordinary Vulkan runs three cases and excludes 11 physical64 cases. physical64 runs 11 and excludes three ordinary cases. each target filters 331 unrelated cases. the six new cases include valid u32-pointer equality/inequality/order, dynamic wrapping add/sub/offset and a negative offset with rustc optimization enabled. disassembly retains 64-bit conversions, integer comparisons, byte stride four, modular multiplication/addition and subtraction for wrapping_sub. three negative cases retain clean logical-storage and standard null-helper diagnostics. expectations were manually reviewed before acceptance, followed by a non-blessing full matrix run.

the full selected unit gate passes **44 compiler tests and three shared-type tests**, zero failures, with four existing macOS-only ignores. all 16 instruction probes still pass. **31 existing adjacent source cases pass**, with one opposite-ABI exclusion and 313 filtered cases, covering prior storage/casts/panic plus raw read/write/copy, null allocation and glam offset regressions. full RustGPU and direct new-UI formatting pass. the final source, unit and adjacent commands took 16.33, 36.23 and 3.18 seconds. prior SPIR-T format/Clippy and full upstream/native CI limits remain open.

independent transport reference: the same nightly's native x64 backend passed 198 assertions in both unoptimized and optimized builds. six addresses include zero, high-bit values and the u64 boundary. signed offsets span isize::MIN through isize::MAX, and wrapping counts include usize::MAX. native raw-pointer wrapping/comparisons are checked against full-width address arithmetic, without dereferences. this checks the intended Rust bit-level behavior separately from SPIR-V validity. it does not execute the shader or discharge any allocation contract.

failed controls: the baseline rejected raw equality and arbitrary offsets. enabling rustc optimization initially reached the backend's unimplemented ThinLTO method and caused a compiler process failure. `-C lto=off` isolated the actual pointer test without changing backend LTO support. the first candidate compiled both offset cases, but exposed the standard is_null helper's u32-to-u8 pointer cast. null_mut separately requires an unsupported void-to-unit cast. these methods now have explicit rejection cases and an upstream-facing inventory, not a parity claim. an initial logical-array comparison hit pointer casting before inference, so its control now uses scalar logical interfaces and verifies the intended PushConstant/PhysicalStorageBuffer conflict. the six debug runs and reviewed candidate were preserved before the final gates.

evidence: ignored `rustgpu-pointer-operations-*`, `rustgpu-pointer-null-constant-baseline.*`, `rustgpu-pointer-constant-no-lto-baseline.*`, `pointer-transport-cpu-{debug,optimized}.*`, `pointer_transport_oracle.rs` and `pointer-operations-reviewed-outputs.json` retain commands, source, results and hashes. all sixteen exported patch hashes passed verification. repository checks preserve the separate source-reference checkout/index, protected scenes and original parity fixtures.

unsafe audit: compiler changes are safe Rust and every added source fixture forbids unsafe. U-001's access source and compile-only contract are unchanged. UNSAFE.md records coordinator self-review, no new boundary and no independent review. no AGFX behavior was newly examined, so its seven source-observed candidates remain unchanged and unmeasured.

publication: E-019 merged through [PR #77](https://github.com/brockliddicoat/uvsr/pull/77) as `3457306128264056e516f95ca87ee4706bbc7506`. PR and [main checks](https://github.com/brockliddicoat/uvsr/actions/runs/35452562948) passed. the checked and merged trees matched before both task-branch references were deleted.

limits / next action: these are address-transport and compiler results, with no Rust shader GPU dispatch. arbitrary pointee casts, standard null helpers, aggregate accesses, volatile intrinsics, copy/atomic/reference contracts and allocation-dependent methods remain incomplete. continue the bounded physical-memory operation inventory and shader-library contract, then the native heap and real Vulkan/NGAPI consumer. T010-T013, P09/P11 and M1-M3 remain open.
