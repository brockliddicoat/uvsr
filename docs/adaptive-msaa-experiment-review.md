# Adaptive MSAA Experiment Review and Handoff

## Executive Decision

This branch proves that all three selectable MSAA implementations render, but it does not prove a performance benefit. The user visually confirmed Reference, Adaptive G-buffer, and Compact Visibility. In the first same-window comparison, Adaptive G-buffer was materially slower than Reference and Compact Visibility was within noise of Reference. The final bounded rescue exposed a much worse Compact result: substantial CPU preparation overhead plus a GPU regression.

Stop expanding this branch after the bounded 4x rescue tests. Preserve it as the complete experiment, archive Adaptive G-buffer as a negative result, and restart any follow-up Compact Visibility proof from the exact base with a much smaller performance-first scope.

Reference production behavior remains the selectable default and visual oracle; diagnostics-only source wiring was added around it. No adaptive mode is ready to become the default.

## Exact Snapshot

| Item | Value |
|---|---|
| Exact base | `578496047c4753077e36f39a68df8c2c323cee58` |
| Current `HEAD` | `578496047c4753077e36f39a68df8c2c323cee58` |
| Branch | `codex/adaptive-msaa-visibility` |
| Worktree | `C:\Users\brock\OneDrive\Documents\uvsr\work\adaptive-msaa-visibility` |
| State | Entire experiment is uncommitted and recoverable in the isolated worktree |
| Product observation | All three modes render; Adaptive is slower; final Compact candidate is slower on both CPU cadence and GPU time |
| Default | Reference MSAA |
| Current build option | `UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC=OFF` |
| Dependency rule | No files under `donut/` were edited |
| Current conservative executable SHA-256 | `48D8E3E11F5E733967F1428B3A634513B0584A7D54FDF36BD3ECCB4425290668` |
| GPU fixture SHA-256 | `8442F5FB41B71269CE56E244AAC63B843AB2ABADC336EB5F5AE7D4A5FF3FAD54` |
| Staged `dxil.dll` SHA-256 | `058F2F52A680C38B223A5615B7DF969DEF21F77E577562E5099D971DEDE992DE` |

The final code-volume audit counted 93,113 added and 801 deleted lines across 104 primary text files, excluding `build/`, `work/`, `outputs/`, and binaries. The user's approximately 65,000-line estimate is close to the 56,419 production `src/` additions; tests add another 28,867 lines.

## Measured Result Before the Bounded Rescue

This was a same-window, directional diagnostic bracket rather than an accepted cold benchmark. It is sufficient to reject the current performance hypothesis, but not to publish a formal score.

| Mode | Total GPU Time | Frame Rate | Throughput |
|---|---:|---:|---:|
| Reference, Start | 5.0 ms | 198.6 fps | 43.3 TFLOPS |
| Adaptive G-buffer, Rejected Lazy Grouping | 6.6 ms | 150.4 fps | 44.3 TFLOPS |
| Compact Visibility | 5.1 ms | 196.7 fps | 42.4 TFLOPS |
| Reference, End | 5.2 ms | 194.2 fps | 42.9 TFLOPS |

The lazy-grouping shortcut was visually unsafe and fully removed. The restored strict Adaptive path was still approximately 6.3-6.4 ms and materially slower than Reference. The GPU had 8-12 C of loaded thermal headroom, no live thermal limiter, and comparable clocks. These numbers remain diagnostic because the thermal and process protocol was intentionally relaxed at the user's direction.

## Two Bounded Rescue Fixes

### Fix One: Remove False 4x Texture Ineligibility

The default-off 4x proof flag now admits Sponza's exact RGBA8 metal/roughness and RGBA8 normal formats and accepts render-target-capable textures only when UVSR can prove that their load-time mip chain is finalized. Non-4x textured draws and every failed proof retain the local full-G-buffer fallback.

Before this fix, Compact could reconstruct only 22 of 436 Sponza geometry identities. Static scene analysis predicts up to 302 of 436 identities after the fix, but only 36.46% of opaque triangles because double-sided curtains and ivy dominate the remaining fallback.

Supporting correctness work explicitly zero-initializes UVSR-owned material constants before fingerprinting, byte-copies candidate constants, and stages the exact `dxil.dll` paired with DXC. This removes padding and module-identity ambiguity from qualification. It did not make the widened renderer faster.

The widened 4x POC activated expensive full-screen reconstruction, guide, and lighting work while mixed fallback retained the full G-buffer. It produced 74-83 ms top-line frame cadence and approximately 5.99 ms GPU time against a 5.05 ms Reference. The POC is therefore rejected and disabled in the handed-off executable.

### Fix Two: Skip Reconstruction Proof Work for Fallback Draws

`DescribeDraw` performed a view-dependent triangle-conditioning walk before it knew whether the immutable scene record was reconstructable or already classified as `FullGBufferFallback`. The conservative build therefore walked Sponza triangles for reconstruction-only proof fields that classification would never consume.

The fix preserves handle, range, buffer, generation, and content validation for every draw, but evaluates view-dependent triangle conditioning only for records whose payload class is `Reconstruct`. The surrounding scene, raster-classification, and texture-qualification contracts pass; no focused unit test directly instruments the internal gate. Its practical result was evaluated through runtime cadence.

The runtime result did not restore viable cadence: Compact still measured 64.5 ms top-line frame cadence. This disproves the hypothesis that the triangle-conditioning walk was the sole CPU bottleneck. Per-frame fingerprints, snapshot validation/publication, and other prepared-submission proof work remain likely suspects, but another unprofiled fix would exceed the bounded rescue.

## Two Bounded Rescue Tests

### Test One: Exact 4x Reconstruction Matrix

Failed as a test harness. The exact GPU fixture ran for 1,283 seconds, consumed approximately 1,262 CPU-seconds, grew beyond 4.3 GB of memory, left the GPU idle and cool, and produced no qualification certificate. The exact task-owned process was stopped. No tolerance was widened and no pass is claimed.

The failure is itself useful evidence: the 9,993-line "micro-test" is effectively a second renderer and certification application, not a bounded capability test suitable for iteration or handoff.

### Test Two: Pass Cost and Reference Bracket

Completed at Sponza Position 1, 1920 x 1080, 4x MSAA, diagnostics off, and the widened POC disabled. This was a same-window directional bracket rather than an accepted cold benchmark.

| Measurement | Reference, Start | Compact | Reference, End |
|---|---:|---:|---:|
| Top-line frame cadence | 4.9 ms | 64.5 ms | 5.1 ms |
| Complete Renderer GPU | approximately 4.9 ms | 5.648 ms | 4.882 ms |
| Geometry GPU | not retained | 1.956 ms | 2.152 ms |
| Closest Surface Resolve GPU | not retained | 0.720 ms | 0.326 ms |
| Direct Lighting GPU | not retained | 1.123 ms | 0.971 ms |
| Screen-Space Visibility GPU | not retained | 1.594 ms | 1.311 ms |

The static image remained visually consistent at inspection scale, but no formal image-diff acceptance is claimed. Compact saves approximately 0.20 ms in geometry yet adds approximately 0.39 ms in closest-surface resolve, 0.15 ms in direct lighting, and 0.28 ms in screen-space visibility. It is approximately 0.77 ms slower on the GPU even before accounting for the roughly 60 ms CPU-side preparation regression.

## Why the Current Modes Do Not Save Time

Adaptive G-buffer adds an `RG32_UINT` multisample key load, grouping, pairwise coherence checks, extra world-position reconstruction, and sizeable lane-local arrays. Conservative sharing usually keeps sample-dependent lighting separate, so the mode mostly shares cheaper decode and material preparation while adding approximately 1.4-1.6 ms of traffic and control work.

Compact Visibility initially fell back for 414 of 436 Sponza geometry identities. Mixed fallback retains approximately 198-261 MiB of 4x full-G-buffer attachments, so Compact pays visibility, reconstruction, scene lookup, and fallback costs without eliminating the dominant G-buffer lifetime. Widening common texture support improves draw coverage, but the predicted result still reconstructs only 36.46% of opaque triangles.

The final conservative build also proves that CPU validation architecture is on the render path. Deferring reconstruction-only triangle conditioning did not remove the roughly 60 ms overhead, so at least one other per-frame proof, fingerprint, snapshot, prepared-submission, or unrelated CPU operation remains. A new proof should profile this before editing, publish immutable scene data on revision changes, and keep steady-state draw submission O(draws), not O(triangles) or O(scene payload bytes).

On the GPU, Compact's small geometry saving is outweighed by closest-surface reconstruction and higher direct-lighting and screen-space-visibility costs. Because mixed fallback preserves the full multisampled G-buffer, the renderer never tests the intended upper-bound architecture in which compact visibility replaces the dominant bandwidth and lifetime rather than supplementing it.

The central mistake was measuring implementation coverage rather than expensive work eliminated. Equal `SurfaceKey` ownership is useful, but it does not remove shadow visibility, sharp specular, local-light attenuation, screen-space visibility, or other sample-dependent lighting often enough to amortize the added machinery.

## Code Volume and Disposition

| Section | Added Lines | Bloat Likelihood | Handoff Quality | Recommended Disposition And Important Thought |
|---|---:|---|---|---|
| Validation, Certificates, And Image Diff | 24,119 | Very high | Poor | Quarantine. A 180-job/720-file authenticated pipeline, 64-field receipts, hashing, leases, and a 9,993-line GPU test were built before a performance win existed. Retain only fixed error limits and two test recipes. |
| Compact Scene, Reconstruction, And Shading | 18,927 | High | Mixed | Salvage the reconstruction HLSL and exact gradient/alpha formulas. Rewrite the host wrapper; CPU interval proofs, fingerprints, and repeated publication validation overwhelm the essential renderer. |
| Renderer And Build Integration | 17,438 | Very high | Poor | Rebuild minimally. `uvsr.cpp` alone adds 8,937 lines and combines rendering, validation, capture, qualification, diagnostics, commands, and UI. The change adds 139 compiled shader permutations and too many structural contracts. |
| Raster And G-buffer | 13,889 | High | Mixed | Salvage only the Compact visibility pass and VS/PS. Archive Adaptive G-buffer as a measured negative. Much of the 6,642-line Compact raster host is proof and prepared-submission machinery rather than raster work; do not port the ineffective conditioning cache or the failed per-frame preflight architecture. |
| Diagnostics | 11,065 | Very high | Poor For A New Proof | Quarantine until performance acceptance. The requested final drawer became a large ABI, producer, readback, inspector, and dual-render system before the render path was viable. Start with counters and timestamps. |
| Core Key And Grouping | 3,447 | Lowest | Good After Trimming | Retain `SurfaceKey`, lossless packing, generation reuse, raster/DXR identity tests, deterministic grouping, and weighting. Remove future cache capability composition until it has a caller. |
| Cross-Cutting Docs And Assets | 4,228 | Medium | Mixed | Retain exact license notices and scene provenance. Replace the 1,215-line active plan with this review. Treat the 2,011-line embedded glTF as generated data with provenance, not architectural guidance. |

These subsystem rows total the 93,113 audited additions. The largest individual files are the 9,993-line GPU fixture, the 8,937-line `uvsr.cpp` change, the 6,642-line Compact raster host, and the 4,673-line Compact shade host.

## Material That Should Not Enter a New Agent's Initial Context

- The 1,215-line active execution plan, which predates the decisive performance result.
- The 34 raw output files, old DXIL, disassembly, transient certificates, and stale executable hashes.
- The certificate wire format, process-global lease authority, module hashing, and hundreds of certificate references.
- The 180-job validation runner, 720-file evidence model, and 64-field receipts.
- Hundreds of source-string assertions that preserve accidental structure rather than runtime behavior.
- Claims of broad 2x/4x/8x/16x production readiness or earlier scoped reviews that did not include Position 1 performance.
- The monolithic GPU fixture and `uvsr.cpp` diff as required reading. They should be optional evidence, not an onboarding sequence.

## Introspective Findings

- I treated the original comprehensive acceptance list as one implementation phase instead of sequencing it behind a performance proof.
- I built production certification, diagnostics, future DXR/cache identity, four sample counts, and unattended image testing before proving that Compact could avoid the full G-buffer on a favorable 4x workload.
- I optimized for formal correctness evidence and recovery from every theoretical stale state. That created a large public API, transaction machinery, and brittle source-contract tests while the core cost model remained untested.
- I allowed host integration to accrete in `uvsr.cpp` instead of enforcing one small pass/controller boundary.
- I should have measured reconstructed triangle coverage, full-G-buffer allocation/touches, and actual full-lighting evaluations immediately after the first working frame.
- Preserving Reference, using exact stable surface identity, retaining conservative fallback, reconstructing explicit gradients, and keeping exact third-party provenance were sound decisions and are worth carrying forward.

## Cleanup Plan Before Reassignment

1. Freeze this worktree after the two bounded rescue tests conclude or fail; do not add double-sided, alpha, more sample counts, or another work scheduler here.
2. Preserve the complete dirty worktree as the full failed experiment. Do not delete or rewrite it while creating the replacement.
3. Put raw logs, patches, binaries, and disassembly in an ignored failed-experiment archive. Link that archive as optional evidence rather than copying its content into a handoff prompt.
4. Move the long execution plan to `docs/exec-plans/abandoned/` and make this concise review the only required reading.
5. Create a fresh isolated worktree from `578496047c4753077e36f39a68df8c2c323cee58`.
6. Port only the license notices, minimal key packing and tests, Compact visibility VS/PS, essential reconstruction HLSL, minimum scene lookup tables, one Compact pass, and one focused GPU test.
7. Leave behind Adaptive G-buffer, the diagnostics drawer, certificate/lease system, authenticated image-matrix runner, broad source-contract additions, adjacent screen-space/IBL/TAA lifetime changes, separated work topology, and 2x/8x/16x permutations.
8. Run a fully eligible 4x upper-bound scene where the full multisampled G-buffer is neither allocated nor touched. Require at least a 5% matched full-frame improvement or a margin clearly above bracket noise.
9. If the upper bound wins, add Sponza common textures, then validate the existing double-sided/alpha reconstruction path, then run two focused static/animated error comparisons.
10. Stop after two bounded optimization iterations if the upper bound still fails. Preserve the postmortem and identity/provenance lessons instead of rebuilding the discarded infrastructure.

Do not execute destructive cleanup in the original worktree. The clean replacement should be selective porting from an intact archive, not subtractive surgery on 93,000 lines.

## Revised Scope for a New Agent

Build a default-off DirectX 12 Compact Visibility 4x proof of concept from exact commit `578496047c4753077e36f39a68df8c2c323cee58`. Preserve Reference unchanged and default. Support one controlled, fully eligible textured and normal-mapped scene first, with no full multisampled G-buffer allocated or touched. Reconstruct exact surface data and sample-dependent lighting without cross-pixel reuse. Measure Reference, Compact, and Reference full-frame and pass times at the same settings.

Stop unless Compact improves matched full-frame time by at least 5% or another margin clearly above bracket noise. Only after that win, add Sponza's common textures, then double-sided and alpha materials, and run two focused static and animated image comparisons. Do not implement diagnostics UI, runtime certificates, DXR or cache integrations, 2x/8x/16x modes, or exhaustive validation until performance and visual quality are accepted.

Adaptive G-buffer remains an archived working negative experiment rather than part of the new optimization scope.

## Selective Port Map

| Disposition | Files Or Areas | Reason |
|---|---|---|
| Retain | `third_party/adaptive_msaa_sources/`, `src/adaptive_msaa.h`, `src/adaptive_msaa.cpp`, focused identity and generation tests | Exact provenance and the stable identity/grouping contract are the most durable results. |
| Salvage Selectively | `src/adaptive_msaa_compact_visibility_vs.hlsl`, `src/adaptive_msaa_compact_visibility_ps.hlsl`, `src/adaptive_msaa_compact_shade_common.hlsli`, the essential lighting and resolve HLSL | These contain the useful visibility encoding, barycentric reconstruction, explicit gradients, alpha handling, normal reconstruction, motion, and sample-dependent shading work. Do not port their host wrappers wholesale. |
| Quarantine | `src/adaptive_msaa_texture_qualification*`, `src/adaptive_msaa_diagnostics*`, `src/adaptive_msaa_validation*`, `tests/adaptive_msaa_gpu_micro_tests.cpp`, `tools/adaptive_msaa_image_diff.*` | Certification, diagnostics, and exhaustive validation were implemented before viability and will anchor a new agent on obsolete mechanisms. |
| Do Not Port | Adaptive G-buffer passes and shader, broad `src/uvsr.cpp` integration, and task-specific changes to screen-space visibility, directional shadows, IBL, and TAA | These are either measured negative work or adjacent lifetime plumbing that expands the proof without improving its cost model. |

## Minimal Handoff Package

Give the new agent only this review, the exact base SHA, one small timing table, one coverage table, 8-12 source-file pointers, two test commands, and the stop conditions. Link the full experiment, raw evidence, and long plan as optional material. This keeps the failed mechanisms from consuming the new context before the agent can test the cost model.
