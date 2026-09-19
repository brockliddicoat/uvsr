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

## L-007. probe the compiler's representations and both tool configurations

**observed, pinned compiler probes.** at [E-008](execution.md#e-008-2026-09-19-installed-rust-and-located-compiler-pipeline-failures), SDK SPIRV-Tools accepted the native-heap fixture, but RustGPU's loader rejected its untyped globals and SPIR-T rejected the untyped capability. the bundled C++ tools rejected the heap capability even earlier. rspirv already knew the instruction vocabulary despite its older version label.

application: separate vocabulary, loader placement, IR representation, linking and validation probes. compare installed and compiled tools before changing compiler semantics. limitation: these assembly-input failures locate compatibility work, not Rust source behavior or GPU correctness. a successful parser round trip cannot substitute for the remaining pipeline or consumer gates.

## L-008. verify case selection when adding a target variant

**observed, pinned RustGPU harness.** at [E-016](execution.md#e-016-2026-09-19-proved-explicit-rust-pointer-width-layouts), `compiletest_rs` 0.11.2 split stage IDs at the first hyphen. the new `vulkan1.3-physical64` target therefore ran the 32-bit layout case and skipped its own case. escaping the stage ID preserved the distinct target identity. a separate forward-slash filename filter on Windows matched zero cases despite exit 0.

application: check stable case IDs and the executed denominator for each configuration before accepting a green command or blessing output. limit: these are observed selector rules in this harness/version, not evidence that all hyphenated targets or Windows runners have the same problem. keep the distinct-ABI matrix and revisit its selectors when the harness changes.

## L-009. separate valid instructions from correct Rust conversions

**observed, pinned RustGPU cast probes.** at [E-017](execution.md#e-017-2026-09-19-lowered-physical-address-conversions), a shader passed SPIR-V validation while zero-extending a signed integer before conversion to a pointer. the same rustc's native backend and its SSA caller contract required sign extension. the original lowering chose conversion from destination signedness instead of rustc's source-signedness argument. correcting that also required an unsigned intermediate for `OpUConvert` when the final Rust type was signed.

application: test signed and unsigned sources, widening and truncation, and inspect emitted operations against the language's bit-level result. use structural assertions for the compiler's alias policy. the current specification permits absent alias decorations, so validation alone cannot establish that policy. limit: these checks establish compiler behavior for the selected cases, not GPU execution or correctness of every pointer operation.

## L-010. check memory effects after optimization

**observed, pinned compiler/tool regression.** at [E-019](execution.md#e-019-2026-09-19-preserved-qptr-memory-effects), qptr and linking retained both volatile loads, but SPIRV-Tools aggressive dead-code elimination removed the one with an unused result. the optimized module still validated. preserving operands during serialization did not establish their later effects.

application: assert required effect counts, flags and scope identities after optimization. include an ordinary removable access as a control so disabling optimization cannot satisfy the test. limit: this result covers explicit Volatile OpLoad and the tested memory forms. it does not prove all native optimizer semantics, Rust volatile intrinsic support or GPU behavior.

## L-011. follow aggregate accesses through copy lowering

**observed, pinned RustGPU regression.** at [E-022](execution.md#e-022-2026-09-19-tested-the-physical-pointer-library), scalar accesses carried valid alignment but an array read/write failed validation. rustc routed the aggregate through memcpy, whose lowering discarded both alignments. preserving alignment also required transferring it when the linker split a copy into a load/store, then cleaning newly introduced logical accesses.

application: cover a whole aggregate and mixed physical/logical temporaries when changing memory access metadata. inspect the final accesses after legalization and optimization. a single SPIR-V copy mask applies to both endpoints, so the weaker known alignment is valid for both. limit: the regression proves the tested array compiler path with pure Aligned metadata. it does not establish effectful/scoped-copy semantics or aggregate GPU execution.

[E-027](execution.md#e-027-2026-09-19-verified-the-native-heap-textured-cube) adds bounded aggregate-read execution: a 24-byte vertex containing two float arrays works as one aligned aggregate load at opt0 and six aligned scalar loads at opt3. both actual cube image/depth oracles pass in Debug/Release. this extends the evidence for that vertex layout, without proving the separate u32-array copy fixture or arbitrary aggregate operations.

## L-012. preserve declaration order across assembly placement

**observed, pinned RustGPU native-heap source probe.** at [E-023](execution.md#e-023-2026-09-19-compiled-native-heap-rust-shaders), the assembly loader accepted heap instructions but source compilation failed on a forward ID. inline assembly registered types immediately while deferring constants to function bodies. later global placement therefore put a descriptor-size constant after the array whose ID decoration used it.

application: inspect the complete source-to-module order when adding instructions with type/annotation dependencies. place constants in the global section at declaration time and test the full source path, alongside assembly-input probes. limit: the tested default, optimized and qptr shaders establish these declarations and accesses, not arbitrary malformed assembly handling or GPU behavior.

[E-024](execution.md#e-024-2026-09-19-executed-native-heap-rust-shaders) adds an observed follow-on limit: debug stripping invokes a separate dead-constant pass that discarded constants referenced only by ID decorations. the existing debug-enabled fixtures did not exercise it. use the production metadata settings and inspect the final module selected by the output manifest. RustGPU's `--disassemble` diagnostic precedes those final native passes, so it is not a final-artifact snapshot. the generic optimizer regression must also prove that ordinary unused constants still disappear.

## L-013. match descriptor indexing to both shader and device requirements

**observed, ordinary AGFX source translation.** at [E-028](execution.md#e-028-2026-09-19-executed-the-direct-rust-buffer-copy), the validator accepted a dynamically uniform storage-buffer array access without StorageBufferArrayDynamicIndexing. the [Vulkan interface contract](https://docs.vulkan.org/spec/latest/chapters/interfaces.html) still requires that capability and shaderStorageBufferArrayDynamicIndexing. source review caught the gap before dispatch, and the compiler profile plus host query/enable rules were corrected.

application: derive required capabilities and host features from the actual indexing operation, then assert them on the final artifact. successful SPIR-V validation alone is insufficient feature accounting. limit: this is one observed validation gap in the pinned tools, not a claim about every validator version or descriptor access mode. the shader was compile-only when the gap was found. E-029 subsequently executes the corrected profile at both optimization levels in the direct Rust host.
