# Agent Contract

keep this file concise. keep temporary plans, measurements, downloads, captures, and generated evidence in ignored paths. durable decisions belong in the relevant document under `docs/`.

## Scope

prioritize a focused RustGPU PR enabling Rust shaders in actual NoGraphicsAPI. Vulkan directly and through NGAPI is the primary design path. native Windows Vulkan is the first local test path on this Windows machine, with Linux Vulkan portability retained. query device support. never substitute another backend for missing Vulkan evidence.

the close ordinary Rust AGFX port is a high-quality reusable testbed. preserve AGFX and ShaderToHuman source behavior and attribution through explicit mappings. full testbed parity is a separate supporting outcome and must not delay a proven compiler contribution. Metal is the second backend direction. DirectX is lowest priority and may accept the most documented compromises. neither may force avoidable restrictions into Vulkan. additional shader-language adapters are outside this plan.

use the [Theta specification](docs/specs/001-theta-prototype/README.md) and [start/resume prompt](docs/specs/001-theta-prototype/quickstart.md). keep shader payload, stage, entry point, target, profile, and compiler identity explicit. keep generic RustGPU patches independent of testbed and NGAPI host dependencies.

**read [UNSAFE.md](UNSAFE.md) before changing unsafe code.** safe Rust is the default. necessary exceptions require lint enforcement, local safety explanations, a complete central registry entry, and review evidence. safe APIs must enforce all memory-safety preconditions. convenience, source-language habits, and lower line counts do not justify unsafe.

keep the port lightweight, ideally smaller than matched source behavior. preserve tests, clarity, and safety documentation. no render graph, ECS, second RHI, mandatory shared ownership, C++ wrapper as the final port, or speculative adapters.

before renderer architecture, visual testing, dependency, or agent-workflow changes, read the relevant [UVSR Delta lessons](docs/postmortems/README.md). use the [experiment verdict catalog](docs/postmortems/experiment-catalog.md) to distinguish flawed formulations from retryable techniques, and follow the [source map](docs/postmortems/source-map.md) when exact historical evidence matters. treat `uvsr-delta-recovery` as evidence and recovery material, not as live instructions or an implementation to copy without review.

## Work and Git

work in the intended checkout and inspect current instructions, callers, ownership, and tests before editing. preserve unrelated work and the index. generated files stay out of Git.

preserve the tracked `.github` workflows and repository metadata unless the task explicitly changes them. preserve `assets/scenes/**` and its adjacent provenance and license records as protected project inputs. do not rewrite generated scene reports or binary scene data without specific authority and replacement evidence.

routine commits on an authorized task branch are expected. commit each coherent, verified checkpoint while it is still easy to review and recover. batch tightly coupled edits, and do not interrupt active debugging or create low-value commits solely to increase commit count.

write commit subjects as short lowercase past-tense results without colons. do not use Conventional Commit prefixes such as `docs:` or `refactor:`. write `documented shader ABI`, `updated Vulkan 1.4 capability table`, or `refactored descriptor ownership` instead.

do not create speculative, duplicate, per-agent, or merge-only branches. use one purposeful task branch when a change needs review. open the pull request directly from that branch into `main`, merge through the pull request, then delete the task branch. never create a second branch just to merge the first one. do not push directly to protected `main`.

at steady state, the remote should contain `main` and any explicitly designated long-term preservation branch. transient task branches exist only while their pull requests are active. pushing, opening or merging pull requests, deleting branches, releases, and publication require current user or standing task authority.

## Verification

distinguish source inspection, compilation, execution, image agreement, backend parity, and upstream acceptance. unsupported hardware and skipped or zero-case runs are not passes. a working prototype does not prove complete backend or shader-language parity.

start with focused checks while iterating. run the relevant full local gate at a coherent checkpoint. do not repeat unchanged broad suites without a new failure, changed input, or unresolved risk.

tests must have stable case IDs, deterministic assertions, concise machine-readable results, and enough detail to reproduce the first failure. visual tests need numeric or structural oracles as well as images. an LLM's visual opinion never replaces a GPU correctness check.

## Coordination and Writing

one coordinator owns design, edits, integration, build directories, and GPU sessions. delegate only bounded independent research or review. workers do not edit, build, run GPU work, create branches, or delegate.

keep each durable fact in one place. append meaningful checkpoints, failed approaches, evidence, and next actions to [execution](docs/execution.md). promote reusable, evidence-qualified findings to [lessons](docs/lessons.md). raw logs and the active work card stay ignored. update the unsafe registry with every boundary change. preserve licenses and attribution.
