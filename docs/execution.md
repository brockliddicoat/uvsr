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
