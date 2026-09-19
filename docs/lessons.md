# theta lessons for future agents

this tracked page collects reusable findings from Theta's actual work. it complements the [Delta postmortems](postmortems/README.md). read only the entries relevant to the current boundary. [execution](execution.md) preserves the originating checkpoint and [research](specs/001-theta-prototype/research.md) preserves source evidence.

## contribution rule

at each meaningful checkpoint, promote a useful finding from local notes, revise one contradicted by evidence, or record that there is no new general lesson. do not manufacture a lesson to fill a quota.

each entry needs a stable L ID, evidence classification, source/checkpoint and exact scope, finding, practical consequence, applicability limits, counterevidence or what could overturn it, and linked task/contract if useful. **observed** means direct evidence, **inferred** means an explanation not isolated experimentally, and **proposed** means an unverified practice or hypothesis. a policy choice is not a performance or soundness result.

keep entries short enough to search and use. promote recurring lessons into the owning contract when justified, then link to that contract instead of copying the rule everywhere. preserve corrections and supersession history.

## L-001. classify evidence before counting progress

**observed, source inspection.** the pinned Theta main contains documentation and assets but no Rust files or Cargo manifest. a complete source tree and green baseline workflow cannot establish a Rust build or GPU result. see [E-001](execution.md#e-001-2026-09-19-refreshed-planning-baseline).

application: inspect what a check actually runs and name its evidence layer. useful for planning handoffs and newly reset repositories. limit: this describes the pinned baseline, not future implementation revisions. revise with exact build/device evidence when it exists.

## L-002. similar graphics concepts can have different shader ABIs

**observed, source inspection.** pinned AGFX's ordinary Vulkan path uses descriptor bindings, while pinned NGAPI requires a native-heap and physical-address contract. [source references](specs/001-theta-prototype/research.md#decisive-source-findings) and [ABI contracts](specs/001-theta-prototype/contracts/shader-abi.md) identify the difference.

application: retain explicit profiles and test the real consumer. a generic “bindless” label is insufficient compatibility evidence. limit: no runtime interoperability experiment has run. future upstream changes could alter these profiles and require a new comparison.

## L-003. keep the contribution independent of the testbed's size

**proposed, scope decision.** a focused compiler contribution should depend on its compiler regressions and actual consumer proof, not exhaustive framework, report, or secondary-backend completion. [E-002](execution.md#e-002-2026-09-19-vulkan-and-rustgpu-priorities) records why this project adopted that dependency order.

application: build a small reusable Vulkan testbed slice and keep broader parity visible in its own track. limit: faster delivery and fewer defects have not been measured. if a particular source behavior is necessary to reproduce a compiler issue, bring that behavior into the primary slice explicitly.

## L-004. line counts require matched behavior

**proposed, measurement rule.** compare the port and source over an explicit shared behavior manifest using the same counting tool and exclusions. report omitted/deferred behavior, generated code, dependencies, tests, and documentation separately. see [size and complexity](specs/001-theta-prototype/plan.md#size-and-complexity).

application: evaluate whether ordinary Rust ownership and equivalent mappings actually reduce implementation size. limit: the port does not exist, so no reduction is measured or promised. a smaller subset cannot establish a smaller equivalent implementation.

## L-005. make unsafe contracts visible at both review levels

**proposed, coding standard.** local safety explanations and a conspicuous central registry serve different review needs. [UNSAFE.md](../UNSAFE.md) owns the required record and audit process.

application: link each operation to its invariant owner and keep the summary discoverable from the repository entry. limit: documentation and tests do not establish soundness by themselves. revise the process if actual review finds missing sites, stale contracts, or unenforced assumptions.

## L-006. isolate implicit layers when a Vulkan inventory stalls

**observed, Windows probe.** with loader 1.4.341.0 and NVIDIA 616.56, full `vulkaninfo` timed out at 45 seconds. setting `VK_LOADER_LAYERS_DISABLE=~implicit~` only for the probe process produced a complete inventory in 2.2 seconds. see [E-006](execution.md#e-006-2026-09-19-activated-implementation-and-queried-windows-vulkan).

application: preserve the failed command, then isolate incidental layers with a process-local setting. limitation: this comparison does not identify the responsible layer or prove the same cause for other stalls. required explicit validation layers remain a separate gate. reported extensions/features establish a candidate path, not successful device creation or shader execution.
