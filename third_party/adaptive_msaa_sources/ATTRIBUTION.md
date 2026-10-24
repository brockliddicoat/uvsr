# Adaptive MSAA Source Attribution

## The Forge Visibility Buffer

UVSR's adaptive-MSAA resource flow, multisample descriptor loading, and
depth-classified simple/complex architecture comparison are adapted from the
Apache-2.0-licensed
`Visibility_Buffer` example in The Forge:

- Repository: `https://github.com/ConfettiFX/The-Forge`
- Commit: `a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b`
- Verified tree: `e300153c60c611a05d878d22035c703a81724fb6`
- Primary paths:
  `Examples_3/Visibility_Buffer/src/Shaders/FSL/MsaaEdgeDetect.frag.fsl` and
  `Examples_3/Visibility_Buffer/src/Shaders/FSL/VisibilityBufferShade.frag.fsl`
- License: [Apache-2.0.txt](../licenses/Apache-2.0.txt)

The Forge's transient filtered-primitive encoding, mean-depth ownership
heuristic, and 1x/2x/4x-only assumptions are not retained. UVSR replaces them
with a generation-aware exact `SurfaceKey`, conservative shading-coherence
checks, explicit reference fallbacks, and 2x/4x/8x/16x permutations. The
upstream repository contains no separate root `NOTICE` file at the pinned
commit.

UVSR's retained compact shader compacts exact-key invariant preparation into a
bounded wave-local queue. It does not retain The Forge's transient filtered-
primitive work lists or imply cross-pixel radiance reuse. The separated
simple/complex candidate remains an explicit matched full-frame comparison,
not a provenance claim about the retained implementation.

## Burns and Hunt Visibility Buffer

UVSR adapts the deterministic ascending representative, exact unique-surface
grouping, and `group sample count / total sample count` weighting concepts from
the official supplemental code for Christopher A. Burns and Warren A. Hunt,
"The Visibility Buffer: A Cache-Friendly Approach to Deferred Shading":

- Article: `https://jcgt.org/published/0002/02/04/`
- Official archive: `https://jcgt.org/published/0002/02/04/code.zip`
- Verified archive SHA-256:
  `6C9A2410ED37270B6CB5F8FED34CE5A330A229ECD426C16E573646FBBE0C76E2`
- Primary archive path: `supplementalmaterial/DeferredPass.hlsl`, especially
  lines 424-508
- Required embedded license and article notice:
  [BURNS_HUNT_NOTICE.txt](BURNS_HUNT_NOTICE.txt)

UVSR does not retain the upstream assumption that equal primitive identity
makes final radiance constant. `SameSurface` and `CanShareShading` remain
separate, and sample-dependent lighting stays per sample unless equivalence is
proven conservatively.

## UVSR and Donut Raster Paths

The UVSR-owned adaptive raster pass is a sibling adaptation of the pinned
Donut G-buffer and geometry-pass implementation. Each adapted source file
retains Donut's complete MIT notice and records exact provenance:

- Donut commit: `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`
- UVSR source base: `578496047c4753077e36f39a68df8c2c323cee58`

No file under `donut/` is modified.

## Supplementary Design References

The following sources informed representation and validation design only; no
source text or implementation code was copied from them:

- AMD Capsaicin at commit
  `914b91596cd119eda85fbc1d3c7ee6ac391b1452`, used to cross-check shared
  raster/DXR surface-hit concepts and representation-specific cache addressing.
- Epic Games' Lumen Technical Details documentation, used to distinguish
  logical surface hits from surface-cache parameterization and addresses.
