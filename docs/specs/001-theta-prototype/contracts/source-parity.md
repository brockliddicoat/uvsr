# source-parity contract

## scope and meaning

AGFX and ShaderToHuman are pinned semantic references in [research](../research.md). preserve supported behavior, algorithms, layouts, limits, failure semantics, fixtures, and meaningful assertions. use ordinary Rust modules, types, and explicit ownership. equivalent behavior does not require binary C++ ABI compatibility, duplicate C/C++ wrappers, or unsafe pointer idioms.

the port is a high-quality reusable testbed. full parity is a separate outcome from the RustGPU contribution. **the active backend scope is Vulkan, with Windows first and Linux portability retained.** source platform assumptions may need a faithful Windows Vulkan adaptation. Metal is the next backend direction, and DirectX is lowest priority. those native implementations and additional-language adapters are deferred from this plan.

AGFX inventory includes public/native APIs, backend behavior, shader helpers, meaningful Ez behavior, registrations, and examples. ShaderToHuman inventory includes all library/header functionality, five regressions, documentation branches, examples, state, and formatting/rendering behavior. Gigi's editor is not a product to rewrite, but hosted behavior needs equivalent fixtures for parity claims.

## inventory and dispositions

each item records project and full revision, file/symbol or registration, purpose, source API/language flavor, capabilities, target Rust owner, case IDs, oracle, notices, scope class, disposition, and evidence. generated enumeration stays ignored. reviewed mappings live in `tests/parity/` once implemented.

scope classes are **primary contribution slice**, **supporting Vulkan parity**, and **future compatibility**. future compatibility must cite the explicit scope decision in research, rather than being used to discard a difficult Vulkan requirement.

| disposition | meaning |
| --- | --- |
| direct port | implemented source behavior with preserved assertions |
| equivalent mapping | several source entry points share a named Rust behavior with the meaningful assertions intact |
| host adaptation | behavior retained through different hosting/platform machinery and compared with the source contract |
| blocked/unimplemented | still required in the declared scope, with missing work or evidence |
| deferred by scope | future API/language variant explicitly deferred by the user, with the source item and reason retained |
| nonsemantic tooling | build/editor machinery with no omitted observable library, test, or example behavior, with justification |

missing hardware is blocked evidence. unsupported means a real optional source/device capability rule, not a compiler gap. deferred does not mean unsupported, implemented, or passed. needed Vulkan behavior cannot be deferred merely because it is difficult.

## denominators and honest parity claims

keep three denominators separate: the finite primary contribution slice, the declared Vulkan testbed parity scope, and the complete source inventory including future variants. each run reports required, discovered, selected, executed, and outcome counts. narrowing a diagnostic selection does not shrink the acceptance denominator.

the existing source-only AGFX inventory has 401 registrations, 149 distinct names, and 109 test-directory C++ files. these are not unique-behavior or passing-test counts. reconcile with runtime discovery when available. public API and complete example enumeration remain open.

ShaderToHuman's existing inventory has five golden groups, 35 documentation branches, seven example families, and Intro. categories overlap and cannot be added into a fictional test count. its complete library/function inventory remains open. preserve semantic helper behavior without creating additional source-language implementation obligations.

use labels such as “primary slice passed” or “Vulkan source parity at this revision” only when their declared scope passes. full source parity across all upstream backends cannot be claimed while deferred variants remain. M3's RustGPU draft does not require that full outcome.

## reference and correction policy

freeze source/tool/device/fixture/shader and golden identities. compare matched inputs and preserve expected values. ordinary AGFX descriptor bindings remain distinct from the optional native-heap profile. readback oracles may isolate a compiler issue before any full application exists.

source inspection identified possible oracle gaps: AGFX can report success with no executed cases and its FLIP comparison omits alpha. ShaderToHuman missing-golden/readback propagation also needs explicit checks. confirm defects with focused reproducers before correcting them. retain the source assertion plus a separately documented correction. do not relax thresholds, rewrite goldens, or change algorithms merely to obtain a pass.

where source behavior is defective or undefined, record the case, supported contract, actual source result, proposed correction, and affected mappings. keep the correction separate from mechanical translation and retain unresolved intent as a visible decision.

## lightweight translation and attribution

combine duplicate wrappers only when meaningful ownership and failure behavior remains covered. measure the port against matched source functionality under [size accounting](../plan.md#size-and-complexity). clarity, tests, and safety documentation take precedence over an arbitrary LOC target. [UNSAFE.md](../../../../UNSAFE.md) governs every necessary low-level exception.

translations remain accurately attributed under [NOTICES.md](../../../../NOTICES.md). “inspirations” wording does not establish independent authorship or waive upstream terms. no translated implementation exists at this planning checkpoint.
