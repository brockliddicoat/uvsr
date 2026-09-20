# agent handoff: 001-theta-prototype

## feature context

- feature: `001-theta-prototype`.
- implementation checkout: `C:/Users/brock/.codex/worktrees/0cd5/uvsr`.
- remote: [UVSR on GitHub](https://github.com/brockliddicoat/uvsr).
- starting main revision: [6f8b5084aa51962f4d5d6b09081fb991b4a13c8f](https://github.com/brockliddicoat/uvsr/commit/6f8b5084aa51962f4d5d6b09081fb991b4a13c8f).
- workflow: Spec Kit constitution → specification → plan → tasks → implementation.

work in the implementation checkout, where the verified specification files were imported. the prepared source at `C:/Users/brock/.codex/worktrees/theta-spec-audit/uvsr` remains read-only. compare live main at activation as instructed below. read the resources relevant to the selected task.

## objective

finish the close ordinary Rust AGFX and ShaderToHuman ports for the declared Vulkan scope. the local RustGPU contribution draft is already prepared in E-035. complete the reusable testbed so a more advanced scene and renderer can test the modified RustGPU path with actual NoGraphicsAPI (NGAPI). use native Windows Vulkan as the primary development and test path on the current Windows machine. give direct Vulkan and Vulkan through NGAPI the most design attention and avoid compromises imposed by future backends.

use a close, lightweight, ordinary Rust AGFX port as the reusable testbed. preserve AGFX and ShaderToHuman source behavior through explicit mappings and tests. complete declared-scope testbed parity is now the active outcome. retain the independent compiler patches and actual NGAPI evidence.

## constitution

read [agent contract](../../../AGENTS.md) and [constitution](../../../.specify/memory/constitution.md) in the intended Theta checkout. follow Spec Kit's specification, plan, tasks, and implementation sequence using the existing files below.

use safe Rust by default. read [unsafe policy and registry](../../../UNSAFE.md) before changing native or raw-address boundaries. document every necessary exception there and beside the code, with its reason, insufficient safe alternatives, complete obligations, owner, verification, and review status. expose a safe API only when it enforces its memory-safety preconditions.

preserve source attribution, licenses, protected inputs, unrelated work, and the Git index. keep the user's model, reasoning, and execution settings. Git actions and external publication follow actual task authority.

## specification

use [specification](spec.md) for user stories, requirements, and acceptance. continue the active US2 and US4 port outcomes, retaining US1 actual NGAPI integration and the completed local US6 draft.

the active shader route is RustGPU to SPIR-V to Vulkan. prove physical GPU pointers and real native resource/sampler descriptor heaps in actual NGAPI on Windows. query required device features and tool support. a version string, another host, ordinary descriptor arrays, or another backend cannot replace this proof.

retain Linux Vulkan portability as separately reported coverage. Metal is the second backend direction, DirectX the lowest priority. keep artifact and native boundaries adaptable for future APIs and shader languages without implementing those future routes in this plan.

## plan

follow [implementation plan](plan.md), its completed local M0-M3 outcome, and active M4-M6 port milestones. use [source pins and research](research.md) for source pins and [RustGPU upstream test crosswalk](upstream-tests.md) for the RustGPU contribution obligations. read only the contracts and source owners needed by the selected task.

compare live main with the recorded baseline at activation, inspect relevant changes, and pin the chosen starting revision. keep RustGPU, the actual NGAPI host, and read-only source references separately owned. keep generic compiler patches independent of testbed dependencies.

expand the proven Windows Vulkan AGFX slice owner by owner against the complete source inventory, followed by the ShaderToHuman library and portable fixtures. preserve explicit ownership, native capabilities, shader ABI, and completion-based retirement. keep the port as small as equivalent source behavior allows. measure matched scope and retain readable code, meaningful tests, diagnostics, and safety documentation.

## tasks

use [tasks](tasks.md) as the sole completion ledger. continue T024-T029 and the next eligible active port task from `work/theta/STATE.md`, using the [work-card instructions](quickstart.md#current-work-card). follow dependencies and record partial or blocked evidence honestly.

before editing, identify the task and requirements, source owners and callers, required tool/device capability, expected result, and smallest decisive check. use ordinary tools for mechanical work. do not start agents, automations, or an automatic goal merely because this prompt exists.

## implementation

complete authorized work through the active port milestones. the old M3 compiler checkpoint is not a stopping condition for this request. run focused checks while editing and the relevant gate at a coherent checkpoint. use upstream compiler regressions, deterministic readback, native validation where applicable, and exact image/state oracles. preserve optimized/unoptimized and ordinary logical-pointer regression coverage.

keep current position in `work/theta/STATE.md` and detailed experiments in `work/theta/NOTES.md`, following the [work-card and note instructions](quickstart.md#current-work-card). append meaningful checkpoints, failed approaches, evidence, limits, and next actions to [execution record](../../execution.md). promote reusable findings to [reusable lessons](../../lessons.md), labeled observed, inferred, or proposed. reconcile the unsafe registry at every affected checkpoint.

after two materially identical failures without new evidence, change the investigation. checkpoint after six experiments on one design decision. preserve the smallest reproducer and what would justify retrying. follow the recovery details in [start, resume, and recovery instructions](quickstart.md).

## completion

prepare the minimal generic RustGPU diff, upstream tests, safety/ABI documentation, exact Windows NGAPI consumer recipe, and local PR title/body. account for every promised feature and all current CI obligations. keep unavailable checks explicitly pending. distinguish a prepared draft, fully passing CI, publication, and maintainer acceptance.

close port tasks only with their required source mappings, implementations and evidence. the declared Windows Vulkan scope must account for every required AGFX behavior and ShaderToHuman library, regression, documentation and example behavior. report Linux and future backends separately. preserve compiler CI and upstream acceptance as distinct outcomes. do not call a reduced fixture or documentation inventory a completed port.
