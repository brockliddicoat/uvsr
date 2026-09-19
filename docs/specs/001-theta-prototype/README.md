# theta specification audit

status: revision 2.0 prepared on 2026-09-19. implementation has not started.

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
| main deliverable | a focused local RustGPU PR draft enabling actual NGAPI |
| primary API and host | Vulkan directly and through NGAPI, with native Windows Vulkan first on the current machine and Linux portability retained |
| AGFX port | close ordinary Rust implementation, high-quality lightweight testbed, separate complete-parity outcome |
| ShaderToHuman | source-faithful Rust library, regressions, and examples in the supporting parity track |
| Metal and DirectX | Metal second, DirectX last with the most acceptable documented compromises. future work, not primary gates |
| additional shader languages | adaptable boundaries only. former adapter tasks and the nine-cell requirement are retired |
| unsafe | safe default, local contracts, complete prominent registry, revision-specific review and evidence |
| execution and learning | tracked checkpoint history and reusable evidence-qualified lessons, with raw logs ignored |
| size | prefer fewer lines through simpler code, measured against equivalent source behavior |

complete testbed parity, report polish, future APIs, and future shader languages do not delay a proven M3 contribution draft. they also do not acquire success claims from that draft.

## baseline and evidence

the starting main is [`6f8b5084`](https://github.com/brockliddicoat/uvsr/commit/6f8b5084aa51962f4d5d6b09081fb991b4a13c8f), a documentation/asset tree with 326 blobs, zero Rust files, and zero Cargo manifests. [research](research.md) owns the full pins and source observations. no device support, build, GPU result, parity, usability gain, or upstream acceptance is claimed.

the user restored close AGFX and ShaderToHuman source parity over main's inspiration-only wording, then prioritized the RustGPU contribution and Windows Vulkan. accurate source attribution remains required. the prior broad plan is preserved locally under ignored `work/theta/archive/spec-v1.1-before-vulkan-focus/`.

this follows [GitHub Spec Kit's document structure](https://github.com/github/spec-kit/tree/d4229c071c7ea3885b43e8a7739847300f618f13), adapted under `docs/specs`. it does not install a CLI or imply slash commands are available. all active implementation tasks remain open.
