# agent contract

keep this file concise. keep temporary plans, measurements, downloads, captures, and generated evidence in ignored paths. durable decisions belong in the relevant document under `docs/`.

## scope

build a lightweight cross-platform Rust graphics framework with native D3D12, Vulkan, and Metal 4 backends and first-class Rust, Slang, and HLSL shaders. use AGFX as the concrete behavior and architecture reference for public concepts, resource states, handles, limits, optional capabilities, shader ABI, and test coverage. use Rust modules, structs, enums, explicit ownership, and small unsafe native API boundaries.

support Rust, Slang, and HLSL shader sources across every applicable D3D12, Vulkan, and Metal 4 cell. keep compiled bytes, stage, entry point, source language, and required metadata explicit and separate. a Rust host running only HLSL is not Rust shader support.

the NoGraphicsAPI prototype must exercise physical GPU pointers and native descriptor heaps with Rust shaders in the actual host. keep reusable rust-gpu compiler and shader-library work generic. keep AGFX host changes and narrow NoGraphicsAPI integration outside rust-gpu patches.

do not add a render graph, ECS, a second abstraction layer, mandatory shared ownership, or a C++ wrapper presented as the finished product. add complexity only when a requirement or measured result needs it.

## work and Git

work in the intended checkout and inspect current instructions, callers, ownership, and tests before editing. preserve unrelated work and the index. generated files stay out of Git.

preserve the tracked `.github` workflows and repository metadata unless the task explicitly changes them. preserve `assets/scenes/**` and its adjacent provenance and license records as protected project inputs. do not rewrite generated scene reports or binary scene data without specific authority and replacement evidence.

routine commits on an authorized task branch are expected. commit each coherent, verified checkpoint while it is still easy to review and recover. batch tightly coupled edits, and do not interrupt active debugging or create low-value commits solely to increase commit count.

write commit subjects as short lowercase past-tense results without colons. do not use Conventional Commit prefixes such as `docs:` or `refactor:`. write `documented shader ABI`, `updated Vulkan capability table`, or `refactored descriptor ownership` instead.

do not create speculative, duplicate, per-agent, or merge-only branches. use one purposeful task branch when a change needs review. open the pull request directly from that branch into `main`, merge through the pull request, then delete the task branch. never create a second branch just to merge the first one. do not push directly to protected `main`.

at steady state, the remote should contain `main` and any explicitly designated long-term preservation branch. transient task branches exist only while their pull requests are active. pushing, opening or merging pull requests, deleting branches, releases, and publication require current user or standing task authority.

## verification

distinguish source inspection, compilation, execution, image agreement, backend parity, and upstream acceptance. unsupported hardware and skipped or zero-case runs are not passes. a working prototype does not prove complete backend or shader-language parity.

start with focused checks while iterating. run the relevant full local gate at a coherent checkpoint. do not repeat unchanged broad suites without a new failure, changed input, or unresolved risk.

tests must have stable case IDs, deterministic assertions, concise machine-readable results, and enough detail to reproduce the first failure. visual tests need numeric or structural oracles as well as images. an LLM's visual opinion never replaces a GPU correctness check.

## coordination and writing

one coordinator owns design, edits, integration, build directories, and GPU sessions. delegate only bounded independent research or review. workers do not edit, build, run GPU work, create branches, or delegate.

keep each durable fact in one place. update only affected documents. record source pins, commands, expected and observed results, and uncertainty. preserve licenses and attribution for every imported or translated dependency, shader, fixture, or test.
