# resume the Rust graphics ports

the user requested a deliberate stop after E-049 so another agent can continue. the next agent should finish the complete declared AGFX and ShaderToHuman Vulkan ports, then use them for an advanced scene and renderer on actual NGAPI with the RustGPU modifications. the local compiler draft is a completed milestone, not the end of this task. do not close full parity from a reduced fixture or a successful build.

## read first and locate the checkpoint

read current [AGENTS.md](../AGENTS.md), [UNSAFE.md](../UNSAFE.md), the [start/resume workflow](specs/001-theta-prototype/quickstart.md) and [task ledger](specs/001-theta-prototype/tasks.md). [E-049](execution.md#e-049-2026-09-20-ported-active-gaussian-programs-and-checkpointed-for-handoff) owns the checkpoint evidence. the [Gaussian mapping](../tests/parity/shader-to-human-gaussian.md) owns source behavior, exact failures and inherited candidates. [AGFX port notes](agfx-port-notes.md) remain the ongoing record of changes worth considering. consult the relevant Delta postmortem before architecture, visual-test, dependency or workflow changes.

the working checkout is `C:\Users\brock\.codex\worktrees\0cd5\uvsr`. inspect ignored `work/theta/STATE.md` first for the final publication SHA, current branch and process state. `work/theta/evidence/full-port/gaussian-publication.json` records the PR, exact checked head, matching main tree and completed CI. verify those against live Git before editing. a newer main revision may supersede this guide.

```powershell
git status --short --branch
git rev-parse HEAD
git log -5 --oneline
git remote get-url origin
gh api repos/brockliddicoat/uvsr/commits/main --jq .sha
```

the standing user authority covers ordinary implementation, local tests and verified UVSR publication through checked PRs into main. it does not authorize publishing the RustGPU or NGAPI forks upstream. do not request routine permissions again. use one coordinator for edits, Cargo and GPU work. do not start subagents, automations or new tasks merely to resume. keep lowercase prose, source attribution and honest evidence boundaries.

## what is ready and what remains

the active Gaussian compute, bounded PLY, raster and eight-sample resolve paths are implemented. both native hosts pass numeric checks, but strict HLSL image failures remain. the complete counts and limits live in the mapping and execution record. prior ShaderToHuman strict fixture, documentation and Features differences also remain open. safe arbitrary shader loading, complete AGFX API/Ez behavior and the advanced integrated renderer are not finished.

the next useful work is to isolate the remaining Gaussian near-camera coverage difference with the preserved original-HLSL control, before changing source algorithms. the corrected conic cross coefficient is already covered by a rotated-ellipse oracle. degenerate source eigenvectors are a demonstrated CPU defect and a candidate explanation for the remaining difference, not a proved GPU diagnosis. retain failing captures, strict thresholds and the distinction between a translation repair and a deliberate source-behavior improvement.

then reconcile all ShaderToHuman required behavior against T026-T029, including inactive legacy Gaussian material, shared-library coverage and earlier image differences. the old Splat_example has stale includes and an incompatible drawArrow call. do not silently invent its missing argument or count it as ported. document explicit equivalents and unresolved source ambiguity.

continue AGFX T024/T025 owner by owner from the frozen API/test/example inventory. remaining areas include general handle/slot allocation, texture shapes/subresources, heap and queue semantics, indirect/task/mesh behavior, Ez/window/input and full examples. the source inventory is broader than the currently passing fixtures. reuse the existing owners and preserve safe Rust contracts. do not introduce a second RHI, render graph or ECS to accelerate apparent coverage.

T018 joins the growing Rust host to the existing actual-NGAPI shader contract. use that integration for the requested advanced scene/renderer, preserving payload, stage, entry, target, profile and compiler identity. native Windows Vulkan remains primary. T023 Linux evidence, T030-T032 report/query work, T034 complete upstream CI and upstream acceptance stay separate. the task ledger remains authoritative.

## toolchain and reproducible checks

the formerly blocked Rust installation is resolved. the pinned nightly-2026-07-03 with rustc-dev, rust-src and llvm-tools is installed. use the task-local environment and target directories, not another checkout's build products.

```powershell
. ./work/theta/rust-env.ps1
$env:CARGO_HOME=Join-Path $ThetaWorkRoot 'tools/cargo-host'
$env:CARGO_TARGET_DIR=Join-Path $ThetaWorkRoot 'build/agfx-host'
$thetaPython='C:/Users/brock/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo fmt --all --check
& $thetaPython -m unittest discover -s tools/theta -p 'test_*.py'
```

the bundled Python has NumPy and Pillow. the system Python does not. Vulkan SDK is `work/theta/tools/vulkan-sdk-1.4.357.0`, with `Bin/vulkaninfoSDK.exe`. RustGPU's backend is `work/theta/build/rust-gpu/release/rustc_codegen_spirv.dll`. shader compile arguments and native commands are in the [Gaussian mapping](../tests/parity/shader-to-human-gaussian.md#reproduction) and [tool guide](../tools/theta/README.md). do not repeat unchanged broad suites without a changed input or unresolved risk.

| local file or evidence directory | use and limits |
| --- | --- |
| work/theta/gaussian_gates.py | sequential formatting, strict Debug/Release Clippy, 69 Rust tests, both all-target builds and 125 Python controls at E-049 |
| work/theta/review_gaussian.py | exact current source, host and complete shader interface/call-graph review. run separately for debug/release, and require success before dispatch |
| full-port/gaussian-conic-compile | final opt0/opt3 modules and metadata. private caller hashes must match, not merely filenames |
| full-port/gaussian-conic-debug and gaussian-conic-release | final numeric captures. Debug additionally has a final-runner CPU recheck record |
| work/theta/compare_gaussian.py conic | strict source-image and float diagnostics, with Debug/Release equality. process success means the report was produced, not source parity |
| full-port/gaussian-reference-strict | frozen original-HLSL control, pre-GPU review, compiler recipes and captures. it predates the Rust conic fix but executes unchanged source HLSL with identical inputs |
| work/theta/gaussian_regressions.py | eleven prior native families, serialized. verifies fresh source/executable identity and reviewed owners before GPU. retains known strict failures |
| full-port/gaussian-local-gates.json, gaussian-regression-verification.json, gaussian-preservation.json | exact commands, results and preserved inputs for the checkpoint |

`full-port` above means `work/theta/evidence/full-port`. raw records and helper scripts are intentionally ignored, so an online-only clone does not contain this evidence. the tracked mappings, source manifests, callers, runners and fixtures remain available online. retrieve the local checkpoint material before claiming to reproduce its recorded results. missing evidence is not a pass.

## preserve these boundaries

never run `isolate_mesh_stages.py`. a prior run crashed the machine. do not rerun `prepare_gaussian_host.py` or old preparation generators over the corrected caller, ABI or payload allowlist. the final Gaussian buffer is 51,520 bytes including two zero padding words. older 51,512-byte and pre-conic captures are failed historical attempts, not the current ABI.

preserve the original source checkouts and indexes, old goldens, protected scenes/workflows/metadata and dirty upstream work. `full-port/source-inventory.json` gives exact local source paths, pins and index hashes. ShaderToHuman's source path is under a temporary research directory, so verify availability rather than silently substituting a newer checkout. RustGPU is at a83098321509b3d514aa5315a414f6af4204b163 with pre-existing tracked/untracked probe changes. actual NGAPI is at 289864d773ae83f8ae14ab8f30982928ce628864. inspect their live state and the contribution crosswalk before modifying either.

persist source/module/caller review before GPU execution. after a code or shader change, rebuild the affected host, renew identities and use a new evidence directory. do not overwrite the frozen original-HLSL control merely because unrelated Rust hashes changed. original HLSL, portable Vulkan hosting, native execution, image agreement, Linux support and upstream acceptance are distinct claims.

keep the active work card current. append meaningful checkpoints and failures to execution, and update inherited candidates with source pins, disposition and required evidence. publish coherent verified changes from one task branch through a PR to protected main, checking the exact head and resulting tree. stop at the requested checkpoint without leaving builds or GPU sessions running.
