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
