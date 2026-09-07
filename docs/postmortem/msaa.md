# MSAA removal

on 2026-09-05 the user authorized removing MSAA and described it as outdated for
UVSR's direction. the user also reported that the adaptive MSAA / DCAA-competitor
effort did not succeed. this records that product decision and reported outcome;
it does not invent a benchmark or assign an unproved technical cause.

## history and limits

the earlier adaptive work sought to spend coverage and shading work selectively
while retaining image quality. related sparse-edge and per-sample records are
recoverable at `e29a41245dbd0e6fd7a819d2341646419ab76e72` and
`c4dbcb31c2a241f5864cb08ae2f80551ef2615fb`. those records describe pooled or
closest-receiver ownership problems and a rejected excess-GI image despite
passing tests. they show why receiver identity and visual acceptance matter.
they do not establish which exact candidate the user called DCAA. the precise
DCAA implementation remains unidentified in the inspected history. CMAA2 was a
separate removal and must not be substituted for it.

the relevant paths are
`docs/exec-plans/completed/ratio-shadow-msaa-sparse-edge-repair.md` at the
first commit and
`docs/postmortem/engine-cutdowns/2026-08-23-msaa-per-sample-visibility.md`
at the second.

the immediate pre-removal implementation had already narrowed policy to direct
2x, 4x, 8x, or 16x samples. preserving each receiver still required multisample
G buffers, per-sample material/lighting and visibility paths, receiver layers,
coverage handling, denoiser preparation, resolve work, adapter format checks,
and permutations. that is a concrete maintenance and resource cost even when
the sampling policy has one control. this postmortem provides no fresh timing
or memory measurement of that cost.

Brian Karis called MSAA “an antiquated technique” in his
[2020-11-14 post](https://x.com/BrianKaris/status/1327712610364522496).
the matching date and text were readable in the
[reproduction](https://threadreaderapp.com/user/BrianKaris); direct X retrieval
was unavailable. his earlier [Epic forum explanation](https://forums.unrealengine.com/t/it-would-be-really-nice-to-see-support-for-different-anti-aliasing-types/388/2)
distinguishes geometric edge treatment from other aliasing sources. the quoted
judgment supports the user's chosen direction; it is not universal experimental
proof that MSAA is unsuitable for every renderer.

## removal and recovery

six dedicated raster-topology, visibility-resolve, and deferred MSAA owners
totaled 1,242 lines and 42,991 bytes at the removal baseline. these are gross
owner sizes, not the total or net impact: much of the simplification is inside
shared targets, PBR, visibility, denoising, and frame orchestration. the combined
AA change reduces configured shader tasks from 134 to 81 and the runtime shader
inventory from 46 to 40. those counts are not GPU performance measurements.

one raster surface now supplies the visible material and depth receiver at each
pixel. MSAA allocations, receiver layers, coverage resolves, pipeline choices,
fallback controls, and AA-only proof are removed. ray/path/noise sample counts
and denoiser histories remain independent. the retained all-signal diagnostic
uses both scenes at single-sample rasterization; it keeps resize, camera,
scene-transition, and causal lighting checks. bounded snapshot migration is
defined in [settings](../settings.md).

the exact dirty source recovery boundary and archive hash are recorded once in
the [TAA recovery section](taa.md#removal-and-recovery). recover all consumers
selectively when researching an old algorithm. a restored sample-count control
without its resource and receiver contracts would not recover MSAA behavior.

## lessons and acceptance

prove receiver ownership against dense/reference math and order invariance;
darker output alone is not better visibility. compare an adaptive candidate at
equal measured time and reject it when visual evidence contradicts a passing
test. keep quality claims tied to exact source, scenes, captures, and hardware.

the removal aligns the current renderer with the user's chosen future
reconstruction direction. [architecture](../architecture.md#future-reconstruction)
keeps that path explicit without implementing DLAA or a bespoke TSR now. fresh
single-sample visuals, retained denoiser/reset checks, GPU validation, and exact
package proof remain acceptance requirements. the current source edits are not
a substitute for those results.
