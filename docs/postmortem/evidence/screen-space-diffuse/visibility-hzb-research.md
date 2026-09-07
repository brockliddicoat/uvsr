# visibility and shadow research

this candidate continues the preserved `bed36f9` visibility research build on
`codex/visibility-hzb-research`. it is local work, not a canonical checkpoint.

## implementation

| feature | implementation | boundary |
| --- | --- | --- |
| aggressive distance mips | SSRT3 LOD sequence `min((j + 1) / 2, 4)` applied to depth, normal, and radiance together | lossy first-trace option, default off; later bounce frontier stays at exact source pixels |
| directional fallback | visible angular bins sample the raw UVSR environment with the estimator's cosine mass | first trace only; replaces diffuse environment composition once; GI intensity also scales it |
| shared HZB | full-resolution `RG32_FLOAT` positive view-depth min/max pyramid | rebuilt once from resolved G-buffer depth, shared by active consumers |
| visibility rejection | cached empty-cell skipping for all estimators, plus conservative depth-radius rejection for Horizon | mask estimators retain projected reach without a new world-distance cutoff; aggressive footprints bypass leaf rejection |
| angular shadows | binary screen-depth intersections over a uniform spherical light cap | eight cosine-weighted rays, one for zero angular size; no sample-distance softening |
| optional wave tracer | retained Bend CPU planner and GPU tracer selected together | default off; nonlinear thickness differs from the independent tracer |

ordinary mips use arithmetic averages of valid samples. they are not min/max
bounds, and can blur thin geometry or mix neighboring surfaces. depth, normals,
and radiance stay at the same LOD and footprint. odd dimensions absorb the tail
in the last mip cell. averaged normal vectors are normalized before shading.

HZB mip zero linearizes finite foreground hardware depth. empty cells use an
empty min/max interval. each reduction conservatively includes every child,
including odd trailing rows and columns. explicit per-mip SRV/UAV subresources
allow normal NVRHI transitions and UAV ordering. positive depth keeps interval
tests independent of the hardware forward/reverse-Z convention.

the shadow ray endpoint is solved in projected space for its requested pixel
reach, bounded by the vanishing point for away-facing rays. traversal skips the
receiver cell, descends intersecting bounds to a source-pixel slab, and advances
to the next cell boundary after empty intervals. the loop has no arbitrary
512-step lit fallback. both paths retain the same producer-neutral R8 output.
the optional wave path accumulates in R32 before the final R8 write.

the visibility estimator integrates angular occlusion across scheduled radial
samples. treating it as a binary reflection ray would change the result. its
HZB test therefore rejects empty depth regions for every estimator. Horizon
also rejects a necessary depth-radius condition because its original integral
has a world-distance cutoff. the mask estimators do not have that cutoff and
must retain potentially contributing distant depth. the test caches the rejected
screen region. this can save geometry/radiance work, but cells with
wide depth ranges may require several hierarchy loads before the same leaf read.

## source comparison and licenses

- [SSRT3 source](https://github.com/cdrinmatane/SSRT3/blob/b38d82140acf646ee485f9ab16a5fa84906f2231/HDRP/Shaders/Resources/SSRTCS.compute)
  supplies the aggressive LOD sequence and visible-bin fallback pattern.
  [its MIT license](https://github.com/cdrinmatane/SSRT3/blob/b38d82140acf646ee485f9ab16a5fa84906f2231/LICENSE) is
  retained verbatim at `third_party/licenses/CDRIN-SSRT3.txt` and copied into
  the runtime package. copyright 2024 CDRIN. UVSR adapts the bin weights to its
  existing estimator CDF and cosine measure. Unity APV/reflection helpers are
  not imported; they depend on HDRP and carry separate provenance concerns.
- [AMD FidelityFX SSSR traversal](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)
  describes coarse-cell skipping and refinement at potential intersections.
  [the pinned SSSR header](https://github.com/GPUOpen-Effects/FidelityFX-SSSR/blob/34dcacd1feefcfab2855b82e76c7d711f2020a75/ffx-sssr/ffx_sssr.h)
  was cross-checked for the traversal design. UVSR's implementation uses its own
  min/max slab test and does not copy the SSSR software.
- [McGuire and Mara, 2014](https://jcgt.org/published/0003/04/04/)
  supplies the perspective-correct screen DDA reference: view depth must use
  reciprocal interpolation across a perspective-projected segment.
- [Widmer et al., 2015](https://research.nvidia.com/index.php/publication/2015-08_adaptive-acceleration-structure-screen-space-ray-tracing)
  supports adaptive screen-space traversal and highlights the benefit of
  conservative refinement. this is an algorithm reference, not imported code.
- [Bend's released implementation](https://www.bendstudio.com/assets/cms/downloads/code_final_candidate.zip)
  and [article](https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/)
  identify the retained CPU planner and GPU tracer. both upstream headers stay
  byte-identical. the optional path compiles five hard-shadow reach variants,
  with fading and soft sample buckets removed from the active permutation.
  `licenses/Bend-Screen-Space-Shadows.txt` identifies the retained code and UVSR
  adaptations beside the complete `licenses/Apache-2.0.txt` in the package.

turning **Wave Optimizations** off prevents execution of the Bend planner and
tracer. it does not remove those sources or compiled shaders from the package.
Apache-2.0 redistribution still requires the applicable license and notices for
retained material. the off path's HZB traversal is independently implemented,
but this distributed build still includes Bend code. removing that dependency
from a future distribution requires removing the optional implementation and
its packaged shaders, then auditing the remaining adapter/provenance. a runtime
toggle alone is not a basis for dropping the credit.

## verification limits

the independent CPU reference compares hierarchy traversal with dense leaf DDA,
including odd sizes, empty and invalid depth, reciprocal depth, shifted origins,
long rays, and 2,000 seeded randomized cases. it is a double-precision model,
not a GPU execution test. shader compilation and DX12 runtime validation are
separate requirements.

shared HZB construction uses serial compute dispatches per mip. a fused LDS/SPD
builder, tile classification/compaction, and asynchronous scheduling are not
claimed as implemented optimizations. they need measured evidence, especially
because hierarchy construction and divergent traversal can outweigh skipped
loads. include hierarchy construction in total-frame comparisons; the old
visibility depth-preparation timer now measures only the optional sampling
mips. the separate Shared Hierarchical Depth timer and resource row expose the
shared cost. the visibility table includes it in its combined value. do not
label this the fastest HZB without matched measurements.

screen depth cannot represent hidden or off-screen blockers. fallback supplies
environment radiance, not missing geometry or local probes. eight fixed shadow
directions can show penumbra steps without temporal reconstruction. the wave
and default tracers can differ at depth edges because their slab tests differ.
no image-equivalent performance claim is justified by their toggle alone.

Horizon fallback restores the environment share left by each radius-weighted
occlusion wedge, then adds the remaining open directions. its inherited hard
horizon still owns wedges in sample order; this task does not make that weighted
estimator order-independent.

current build/check results are recorded in the owning execution plan and the
candidate verification artifacts under `work/visibility-hzb-start/`.
