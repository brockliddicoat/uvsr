# theta constitution

version: 2.0.0. priorities and execution documentation revised on 2026-09-19.

the current [agent contract](../../AGENTS.md) governs execution. this is the Spec Kit entry to project principles, not an independent authorization source.

1. **RustGPU contribution first.** the main deliverable is a reviewable RustGPU PR enabling actual NoGraphicsAPI. complete AGFX or ShaderToHuman parity must not delay a proven compiler contribution.
2. **Vulkan first, including Windows.** native Windows Vulkan is the primary development and consumer proof path. direct Vulkan and Vulkan through NGAPI receive the most design attention. retain Linux portability. future Metal and DirectX limits must not force avoidable compromises into Vulkan.
3. **source-faithful testbed.** make a close ordinary Rust AGFX port and preserve ShaderToHuman behavior under the [parity contract](../../docs/specs/001-theta-prototype/contracts/source-parity.md). distinguish primary, supporting, and deferred coverage. never describe pending parity as complete.
4. **small explicit owners.** retain native capabilities, shader ABI, and completion-based retirement. no second RHI, render graph, ECS, mandatory shared ownership, or speculative adapters. pursue fewer lines through simpler ownership and equivalent mappings, with matched-scope measurements.
5. **safe Rust by default.** [UNSAFE.md](../../UNSAFE.md) is the prominent policy and audit registry. necessary exceptions need local safety explanations, concrete obligations, review, and tests. source parity does not require copying unsafe C/C++ idioms.
6. **evidence before claims.** separate source, compile, runtime, image, parity, local draft, public CI, and upstream acceptance. missing or empty required runs cannot pass. human and agent views use one result model.
7. **recoverable execution and learning.** keep a short ignored work card and detailed experiment notes. publish concise checkpoints in [execution](../../docs/execution.md) and evidence-qualified reusable findings in [lessons](../../docs/lessons.md). follow [bounded recovery](../../docs/specs/001-theta-prototype/quickstart.md#recovery).
8. **accurate provenance.** “inspiration” wording does not replace notices for translations. follow [NOTICES.md](../../NOTICES.md), preserve protected inputs, and keep source mappings reviewable.

amend the specification, task dependencies, and affected contracts together when scope changes. record superseded decisions. future API or language flexibility means clear boundaries, not implementation commitments in this plan.
