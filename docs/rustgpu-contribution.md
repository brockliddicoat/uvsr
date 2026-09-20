# local RustGPU contribution draft

the bounded Windows Vulkan prototype and local contribution draft are prepared. this is not an opened upstream PR or a full upstream CI pass. [the crosswalk](specs/001-theta-prototype/upstream-tests.md) records all 40 feature, review and CI rows, including the proposed API scope that still needs maintainer agreement.

RustGPU base: `e6394e08eb3356083b12f732a01906f8e49f7c4a`. candidate: `a83098321509b3d514aa5315a414f6af4204b163`. the [patch manifest](../patches/rustgpu-prerequisites/sources.json) and [application recipes](../patches/rustgpu-prerequisites/README.md) reconstruct the candidate and its separate prerequisites. local dependency overrides and assembly probes are excluded from the proposed RustGPU commit tree.

## proposed title

added Vulkan physical pointers and native descriptor heaps

## draft body

Rust shaders can now pass explicit 64-bit device addresses through a Vulkan root, load and store through them, and use native resource/sampler descriptor heaps. the combined path executes compute, textured cube, storage-image and task/mesh fixtures in actual NoGraphicsAPI on Windows Vulkan. no NGAPI or AGFX host dependency is added to RustGPU.

the new Vulkan `-physical64` target gives Rust pointers and usize an explicit eight-byte ABI. existing targets retain their four-byte ABI. storage-class inference determines valid conversions, physical loads/stores retain required alignment, and qptr preserves memory operands. ordinary u32 arithmetic and signed/truncating conversions keep their original widths. effectful Function accesses survive promotion and optimization.

`spirv_std::PhysicalPtr<T>` transports a u64 address without borrowing or owning its allocation. address construction, comparison, casts and wrapping element arithmetic are safe. `read` and `write` are unsafe and require a live aligned complete range, valid values, compatible aliases, visibility and lifetime through completion. the initial API has no Deref, physical references, restriction marker, atomics, volatile or copy-range API. the operation table in `docs/src/platform-support.md` distinguishes tested raw operations, observed rejections and untested operations. full `*mut` parity is not proposed as completed.

`Image::from_resource_heap` and `Sampler::from_sampler_heap` use native heap declarations, untyped accesses and descriptor-size/stride IDs. their unsafe contracts require valid typed indices, descriptors, resources and synchronization. the existing image operations consume the returned values. combining heap access with physical loads/stores requires the separate physical64 target. no descriptor-array substitute, arbitrary integer widening or new restriction marker is introduced.

the dependency work is separate and necessary. rspirv needs correct untyped-global placement. SPIR-T needs modern grammar, untyped pointers, ID metadata and qptr memory-operand preservation. the SPIRV-Tools wrapper needs current generated grammar/native sources, while native optimizer fixes preserve volatile accesses and constants used by ID decorations. these local dependency revisions must be accepted/released or explicitly pinned before ordinary upstream CI can test this complete stack. the [source manifest](https://github.com/brockliddicoat/uvsr/blob/89345f47932673e7695ac9a233a00db6915c822a/patches/rustgpu-prerequisites/sources.json) and [application recipes](https://github.com/brockliddicoat/uvsr/blob/89345f47932673e7695ac9a233a00db6915c822a/patches/rustgpu-prerequisites/README.md) include exact bases, patches, licenses and generated-output identities.

local validation is recorded at the relevant source revisions:

| evidence | observed result |
| --- | --- |
| compiler and shared types | E-030: 32 exported compiler tests and three shared-type tests pass. 16 additional local assembly probes pass but are excluded from this feature diff. four existing macOS-only ignores remain |
| source compiletests | E-033: all 51 required physical-pointer/native-heap pairs pass, seven logical and 44 physical64. complete expected/final instruction streams were reviewed |
| generic difftest | E-034: the existing harness passes one CPU/Vulkan comparison with both 768-byte outputs present. four default/qptr dev/release variants each run three seeds |
| high device addresses | E-034: all 12 independently captured Vulkan cases pass on Intel Arc with real nonzero upper address bits and zero core/synchronization validation diagnostics |
| actual NoGraphicsAPI | scalar, uniform/divergent sampled heaps, aggregate aliases, textured cube, rgba32ui storage reads/writes and task/mesh fixtures pass their bounded Debug/Release gates on NVIDIA. records retain source, tool, device, shader and host identities |
| lint and formatting | compiler and new-fixture focused checks pass. dependency-inclusive fixture Clippy fails seven findings in unchanged upstream test helpers. the adjacent source gate retains a pre-existing read_subpass failure |

production compiler/library code has not changed since E-030. subsequent commits add reviewed source/runtime regressions and documentation. these results are local subsets, with the complete upstream CI matrix explicitly pending. stock installed tools still lose the tested volatile Function operations without the native prerequisite. native C++ GTest, Linux/macOS/Android, cargo-gpu compatibility, publish dry-run and the complete lint/dependency checks are not claimed passing.

with the recorded patched dependencies and `nightly-2026-07-03`, run from the RustGPU checkout:

```text
cargo run --release --locked -p compiletests --no-default-features --features use-compiled-tools -j1 -- --target-env vulkan1.3,vulkan1.3-physical64 physical_storage descriptor_heap
cargo test --release --locked -p difftests --no-default-features --features use-compiled-tools --test difftests -j1 -- physical_storage --test-threads=1 --nocapture
```

the ordinary CI target matrix still needs an explicit physical64 source step alongside its unchanged targets. a hardware skip in the difftest is not execution evidence. retain the complete workflow gates and report extension support separately.

the actual consumer uses NoGraphicsAPI `289864d773ae83f8ae14ab8f30982928ce628864`, reconstructed from the [separate host patches](https://github.com/brockliddicoat/uvsr/blob/89345f47932673e7695ac9a233a00db6915c822a/patches/ngapi-physical-readback/README.md). its added feature queries/enables match the declared shader profile. the [Windows consumer recipe](https://github.com/brockliddicoat/uvsr/blob/89345f47932673e7695ac9a233a00db6915c822a/tools/theta/ngapi-probe/README.md) gives the exact shader compiler, Visual Studio/CMake and per-fixture runner commands. apply all current prerequisite manifests before using those commands. use SDK 1.4.357.0 for the independently checked validator/layer, and the patched compiled optimizer described above. the runners require complete counts, exact artifact identity, numeric/structural oracles, completion and validation insertion.

the actual NGAPI proof remains NVIDIA-specific. its allocations were below 4 GiB, and both queried native descriptor strides were 32 bytes. the independent Intel high-address difftest does not prove Intel native-heap support. broader image parameters/formats, image atomics, concurrent aliases, physical references and allocation-dependent pointer methods remain outside the tested contract. the task/mesh fixture uses fixed output slots, since a preserved dynamic-u64 Output-index variant triggered a NVIDIA pipeline-compiler fault. no compiler workaround or edited post-output module is used to claim that behavior works.

the implementation was reviewed against the pinned source and final instructions by its coordinator. complete safety contracts accompany the upstream code and fixtures. there is no independent safety audit or maintainer approval. prior work and design concerns from [PR #237](https://github.com/Rust-GPU/rust-gpu/pull/237) are attributed without treating that consumer experiment as an accepted implementation. RustGPU/SPIR-T/wrapper terms remain MIT OR Apache-2.0, rspirv Apache-2.0, and translated NGAPI fixture ABI material retains its MIT notice. AGFX and ShaderToHuman are outside this generic compiler diff.

## review size and sequence

the final RustGPU diff is **+7,639/-142 across 150 files**. dedicated test paths account for 6,282 additions, separate documentation 204, and implementation files 1,153. the latter include inline tests and safety comments, so 1,153 is not a pure production-code count. about 85 percent of additions are in dedicated test paths and docs. these counts exclude dependency repositories, NGAPI patches and UVSR hosts.

prefer two or three focused RustGPU reviews, with dependency fixes submitted separately. the useful boundaries are generic instruction/optimizer fixes, physical addressing with its CPU/Vulkan test, and native heaps with their source regressions. the 29 exported checkpoints are recovery increments, not a request for 29 PRs. regrouping overlapping files into an independently applicable series needs a fresh per-series validation before publication. the combined candidate is the presently tested tree. reducing test or safety coverage solely to reduce line count would make review harder to justify. no acceptance probability is inferred from LOC.

## delivered testbed size

the fixed behavior manifest remains [the two-case slice mapping](../tests/parity/primary-slice.md). [cloc 2.10](https://github.com/AlDanial/cloc/releases/tag/v2.10) counted nonblank, noncomment code with the same options for source and port. inline `cfg(test)` suffixes were counted separately. raw file/hash/command and dependency manifests are retained under ignored `work/theta/evidence/slice-size`.

| counted port scope | code lines |
| --- | ---: |
| Rust device/buffer/compute owners | 925 |
| inline Rust CPU tests | 136 |
| Rust compute shader, including ABI checks and lints | 36 |
| two Rust fixture runners | 483 |
| seven AGFX Python build/result/control tools | 508 |
| two shared Python build/log tools | 76 |

four shared Markdown documents contain 365 nonblank text/code lines. this documentation context covers safety, recipes, source mapping and port notes, including other contributions. it is recorded separately from implementation LOC.

the matched source shader file has 20 code lines and delegates shared behavior to AGFX shader headers. its two source test files have 341 code lines across C/Cpp/Ez registrations, whose distinct semantics are not all ported. seven frozen shared source files have 6,493 code lines covering substantial unported behavior. those totals are context, not a denominator for a claimed port reduction. the Rust slice has three direct dependencies, ash, serde_json and sha2, with 24 external packages in its lockfile. the counted port groups contain no generated/vendor files. Vulkan bindings, spirv-std, the compiler stack and separate NGAPI diagnostic hosts remain explicit external work.

the simplicity review found one native owner per resource, explicit completion and bounded fixture contracts, with no render graph, ECS, second RHI, mandatory shared ownership or generic adapter framework. the shader count includes explicit ABI assertions and lint declarations, while safety comments are counted separately from code. no full-port size reduction or performance gain is claimed. [port notes](agfx-port-notes.md) remain the record for inherited behavior and unmeasured optimization candidates.

## primary outcome audit

| criterion | disposition |
| --- | --- |
| SC-001 | bounded actual Windows NGAPI pointer/heap/cube result established in E-021 through E-033 |
| primary SC-003 | direct Rust Windows Vulkan slice executed in E-028/E-029. Linux remains pending |
| primary SC-005 | complete case records, stale/missing/corrupt output controls and interruption propagation tested. supporting report/query work remains pending |
| SC-006 | generic diff, source tests, independent difftest, local title/body, ABI/safety contracts, dependency explanation and actual consumer recipe are present. all CI rows have explicit status |
| SC-007 | current task, owners, pins, failed approaches and next action are recoverable from the work card and execution record |
| SC-009 | U-001 through U-015 distinguish boundaries, site inventories, caller contracts and self-review. full dependency soundness and independent review are unclaimed |
| SC-010 | execution records meaningful checkpoints and failed approaches. reusable findings are qualified in lessons |
| SC-011 | fixed scope, source/port counts, dependency identities and simplicity review are recorded without an unmatched reduction claim |

this completes the local bounded prototype/draft outcome. the broader P09/P11 raw-pointer scope and additional H05/A01 cases remain partial in the crosswalk. T034 remains open until the full applicable upstream evidence exists. full AGFX/ShaderToHuman parity, Linux portability, report evaluation, Metal and DirectX remain separate work. no upstream publication is authorized or claimed by this document.
