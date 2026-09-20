# theta specification audit

status: the bounded Windows Vulkan prototype and [local RustGPU draft](../../rustgpu-contribution.md) are prepared. complete AGFX and ShaderToHuman Vulkan ports are now the active objective. full upstream CI and maintainer acceptance remain separate. the [task ledger](tasks.md) owns completion.

**agent handoff: [prompt.md](prompt.md).** copy that file when handing work to an implementing agent. this page contains human review context, not the handoff prompt.

**unsafe policy and registry: [UNSAFE.md](../../../UNSAFE.md).** **execution and learning: [execution](../../execution.md), [lessons](../../lessons.md).**

## audit in this order

| document | question it answers |
| --- | --- |
| [specification](spec.md) | what must work, which priority does it have, and what counts as completion? |
| [implementation plan](plan.md) | how does the Windows Vulkan and RustGPU critical path stay small? |
| [tasks](tasks.md) | what is eligible next and what evidence closes it? |
| [requirements checklist](checklists/requirements.md) | where does each requirement map to work and acceptance? |
| [baseline and decisions](research.md) | what source was inspected and which earlier decisions changed? |
| [start/resume details](quickstart.md) | how does an agent keep state, recover, and record findings? |

load technical contracts as needed: [source parity](contracts/source-parity.md), [shader ABI](contracts/shader-abi.md), [results](contracts/test-results.md), [data](data-model.md), [upstream crosswalk](upstream-tests.md), and [constitution](../../../.specify/memory/constitution.md).

## revised priorities

| subject | controlling direction |
| --- | --- |
| current deliverable | complete the declared Vulkan AGFX and ShaderToHuman Rust ports after the prepared local RustGPU draft |
| primary API and host | Vulkan directly and through NGAPI, with native Windows Vulkan first on the current machine and Linux portability retained |
| AGFX port | close ordinary Rust implementation, high-quality lightweight testbed, complete declared Vulkan parity now active |
| ShaderToHuman | source-faithful Rust library, regressions, and examples in the active port track |
| Metal and DirectX | Metal second, DirectX last with the most acceptable documented compromises. future work, not primary gates |
| additional shader languages | adaptable boundaries only. former adapter tasks and the nine-cell requirement are retired |
| unsafe | safe default, local contracts, complete prominent registry, revision-specific review and evidence |
| execution and learning | tracked checkpoint history and reusable evidence-qualified lessons, with raw logs ignored |
| size | prefer fewer lines through simpler code, measured against equivalent source behavior |

M3 is complete for its bounded prototype scope. the latest user instruction activates M4-M6 and T024-T029. those ports do not acquire parity claims from the compiler draft. report polish and deferred backends remain separate.

## baseline and evidence

the starting main is [`6f8b5084`](https://github.com/brockliddicoat/uvsr/commit/6f8b5084aa51962f4d5d6b09081fb991b4a13c8f), a documentation/asset tree with 326 blobs, zero Rust files, and zero Cargo manifests. [research](research.md) owns the full pins and source observations. that starting snapshot was preparation only. current bounded compiler and GPU evidence is in [execution](../../execution.md). full parity, usability gains and upstream acceptance remain unclaimed.

the user restored close AGFX and ShaderToHuman source parity over main's inspiration-only wording, then prioritized the RustGPU contribution and Windows Vulkan. accurate source attribution remains required. the prior broad plan is preserved locally under ignored `work/theta/archive/spec-v1.1-before-vulkan-focus/`.

this follows [GitHub Spec Kit's document structure](https://github.com/github/spec-kit/tree/d4229c071c7ea3885b43e8a7739847300f618f13), adapted under `docs/specs`. it does not install a CLI or imply slash commands are available. current implementation and pending outcomes are recorded in the task ledger.
