# testing contract

the first testing priority is a reproducible RustGPU contribution with Rust-authored shaders in actual NGAPI on **native Windows Vulkan**. a small direct Windows Vulkan testbed fixture helps isolate compiler and host failures. record Linux Vulkan portability independently.

## primary evidence

use upstream compile-pass/fail/disassembly tests, optimizer/layout regressions, deterministic GPU readback, and exact image/state oracles. cover physical addresses, native resource/sampler heaps, required untyped instructions, root layout, enabled features, and valid resource/descriptor lifetime.

record source/diff and build identity, host OS, Vulkan loader, adapter/driver, enabled features, compiler/options, shader hashes, authored language, profile, fixtures, seed, oracle, expected/actual values, first failure, and reproduction arguments. distinguish offscreen readback from native Windows presentation.

unsupported local extension hardware blocks that device proof. another backend, a Linux-only run, ordinary arrays, or a replacement host cannot establish Windows NGAPI support. upstream platform CI remains a separate compiler regression obligation under the [crosswalk](specs/001-theta-prototype/upstream-tests.md).

## source-parity evidence

[the parity contract](specs/001-theta-prototype/contracts/source-parity.md) accounts for all AGFX and ShaderToHuman source items while separating the primary slice, supporting Vulkan parity, and deferred future variants. preserve meaningful source tests, API/Ez assertions, algorithms, fixtures, and notices.

the supporting ShaderToHuman work includes its library, five golden groups, documentation branches, and distinct examples. replace Gigi hosting without silently deleting its behavior. exact source inventories and candidate comparisons are required for parity claims, not for starting a useful compiler experiment.

the [documentation demo mapping](../tests/parity/shader-to-human-demos.md) describes the implemented source branches and deterministic UI frames. its native runner checks image structure and independent state words. exact source-image comparison remains a separate gate, with failed cases retained explicitly.

confirm source oracle defects with focused reproducers and correct them separately. missing goldens must fail, not become candidate-generated references. preserve source thresholds. use alpha, local masks/regions, exact structural buffers, numeric rules, and perceptual scores as appropriate. an LLM's visual opinion is not an oracle.

the [AGFX sampling mapping](../tests/parity/sampling.md) separates exact bytes, the original FLIP threshold, independent sampling arithmetic and alpha. its CPU reference uses the unchanged pinned source header. no added rounding assertion may silently replace the source contract.

the [AGFX raster mapping](../tests/parity/raster.md) covers the source classic draw, depth, blend and load/store groups. source FLIP scores remain separate from exact-byte diagnostics and complete depth, alpha, sparse coverage and region checks. discarded attachment contents never become readback evidence.

## results and reports

the [Hello example mapping](../tests/parity/shader-to-human-hello.md) covers screen, compute and quad rendering, including the equivalent Slang source flavor. its runner checks color space, projected coverage, numeric depth/W displays and retained frame contents. a separate exact comparison consumes independently executed original HLSL captures.

the [Zoom2D mapping](../tests/parity/shader-to-human-zoom.md) preserves pan/zoom formulas and explicit pre/image/post ordering across29 input frames. independent state endpoints, image structure and grid/gamma arithmetic supplement exact original-HLSL image and state comparisons. its documented single-writer adaptation removes the source reset race.

use one canonical result model for minimal primary records and the supporting report/query views. [result contracts](specs/001-theta-prototype/contracts/test-results.md) and [data](specs/001-theta-prototype/data-model.md) own the schema and acceptance. stable IDs and bounded first-failure records matter more than verbose logs.

required empty/all-skipped selections, missing output, stale identity, interruption, and missing cases cannot pass. keep the acceptance denominator separate from diagnostic selection and report deferred variants honestly.

the full AGFX-style report has separate AGFX and ShaderToHuman tabs, expected/actual/difference artifacts, suite-local counts/filters, deep links, and keyboard access. report polish and measured human/agent usability are supporting tasks, not RustGPU draft prerequisites.

## checks and learning

run pure ABI and compiler checks before device tests, structural readback before image comparison, and still cases before temporal sequences. set duration budgets. after a failure, rerun the decisive case instead of the unchanged broad suite. run the applicable full gate once at a coherent checkpoint.

[UNSAFE.md](../UNSAFE.md) defines lint, documentation, inventory, and review checks. test unsafe obligations where meaningful, while preserving the distinction between tests and soundness proof. no undefined GPU dereference is a valid negative-test oracle.

append meaningful outcomes, failures, source/configuration, limits, and next actions to [execution](execution.md). promote evidence-qualified reusable findings to [lessons](lessons.md). raw logs stay ignored.

retained Bistro Interior and San Miguel assets are future inputs with protected provenance. their presence is not new-project build or render evidence, and they are not prerequisites for the minimal compiler contribution.
