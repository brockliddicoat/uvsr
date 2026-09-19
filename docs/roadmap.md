# roadmap and current state

status: specification prepared. no Rust implementation, build, device probe, GPU execution, or upstream PR exists.

the [audit index](specs/001-theta-prototype/README.md) is the plan and prompt entry. [research](specs/001-theta-prototype/research.md) owns the exact main and source pins. the latest scope makes the RustGPU contribution primary and native Windows Vulkan the first local test path.

## primary delivery

| milestone | exit evidence | tasks |
| --- | --- | --- |
| M0, activate Vulkan baseline | pinned sources, Windows tool/device facts, minimal ABI and instruction probes, explicit blockers | [baseline tasks](specs/001-theta-prototype/tasks.md#m0-vulkan-baseline) |
| M1, compiler and small testbed slice | tested generic RustGPU lowering and a minimal native Windows Vulkan Rust AGFX slice | [compiler and slice tasks](specs/001-theta-prototype/tasks.md#m1-compiler-and-small-vulkan-slice) |
| M2, actual consumer | combined Rust pointer/heap readback and cube in actual NGAPI on Windows Vulkan, with exact provenance | [consumer tasks](specs/001-theta-prototype/tasks.md#m2-actual-ngapi-on-windows-vulkan) |
| M3, local RustGPU PR draft | focused diff, upstream tests, safety and ABI docs, reproducible consumer, local PR text and honest CI status | [contribution tasks](specs/001-theta-prototype/tasks.md#m3-rustgpu-contribution-draft) |

direct Vulkan and NGAPI receive most design and test effort. Linux Vulkan portability remains explicit, but does not substitute for Windows proof. unavailable native-heap hardware blocks that runtime claim, not useful compiler diagnostics.

M3 is the main local deliverable. complete framework parity, report polish, Metal, DirectX, and additional shader languages are not prerequisites. drafting, publishing, successful public CI, and maintainer acceptance are separate states.

## supporting testbed work

[the supporting track](specs/001-theta-prototype/tasks.md#supporting-testbed-parity) expands the close AGFX Rust port and ShaderToHuman source parity after, or where directly useful to, the primary path. preserve the complete source inventory and label deferred variants. do not claim full upstream parity while they remain unproved.

Metal is the second backend priority. DirectX comes last and may accept greater documented compromises. these are future implementation directions, not gates for this RustGPU plan. adaptable shader/artifact boundaries preserve future choices without adding unused adapters.

## execution and learning

use the [start/resume prompt](specs/001-theta-prototype/quickstart.md). keep current position and detailed experiments in ignored `work/theta/STATE.md` and `NOTES.md`. append durable milestone and blocker summaries to [execution](execution.md). promote reusable, evidence-qualified findings to [lessons](lessons.md). maintain [the unsafe audit](../UNSAFE.md) with every boundary change.

[tasks](specs/001-theta-prototype/tasks.md) owns implementation completion. [requirements](specs/001-theta-prototype/checklists/requirements.md) owns traceability. these documents do not keep competing completion percentages.
