# data model

these are information boundaries, not a database, generic plugin system, or mandatory crate hierarchy. begin with small records near their consumers.

| entity | required information | owner |
| --- | --- | --- |
| source baseline | project, remote, full revision, relevant paths, license, inspected role | research and activation evidence |
| source mapping | source ID, file/symbol/revision, behavior, Rust owner, primary/supporting/future scope, disposition, case IDs, reason | `tests/parity/` |
| shader artifact | authored source/hash, compiler/options, target/payload/hash, stage/entry, profile/layout, capabilities, diagnostics | build module |
| case definition | stable ID, suite, source IDs, risk, fixture, oracle, required features and variants | test source |
| variant | case ID, host OS, backend, authored language, stage/profile/configuration and capability predicate | test source |
| run | ID, source/diff identity, build identity, OS/loader/adapter/driver, tools, selection, required/discovered/selected counts, events, completion | runner |
| case result | run/variant IDs, phase/outcome, assertions, thresholds, first failure, expected/actual, artifacts, exit status, reason | runner |
| fixture/oracle | content identity, source provenance, format/color/alpha, seed/camera/state, reference, comparison rule/version | reviewed inputs |
| reproduction | executable identity, argument array, working directory, inputs, tool/environment/device requirements | result |
| unsafe boundary | U ID, source sites, necessity, obligations, invariant owners, checks, review, assumptions and change history | [UNSAFE.md](../../../UNSAFE.md) |
| work card | selected task, owners, hypothesis, changed paths/processes, evidence, next action, retry counters | ignored `work/theta/STATE.md` |
| execution checkpoint | E ID, task/requirements, exact source/configuration, change, result, evidence layer, limits, lessons and next action | [execution](../../execution.md) |
| reusable lesson | L ID, observed/inferred/proposed, evidence and scope, application, limits, counterevidence/corrections | [lessons](../../lessons.md) |
| size measurement | matched behavior/file manifests, tool/version/options, separated LOC/dependencies, deferred behavior, limits | execution summary and ignored raw evidence |

source mappings and cases are many-to-many. a case may have several variants, but no Cartesian expansion is required. **host OS and graphics API are separate fields**, so Windows Vulkan cannot be confused with Windows DirectX or Linux Vulkan. the active authored route is RustGPU to SPIR-V. metadata supports future routes without implementing them.

a run retains its acceptance denominator while selecting diagnostic variants. logs, images, and disassembly are evidence, not independent result databases. state phases are compile, validate, pipeline, execute, readback, and compare where applicable. terminal outcomes use [the result contract](contracts/test-results.md). missing terminal completion is incomplete.

use source/diff fingerprints, not branch names or timestamps alone. record durations as observations, not completion evidence. keep portable fixtures free of live GPU addresses. non-dereferencing representations and scoped runtime diagnostics have different purposes.
