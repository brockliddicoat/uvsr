# NVIDIA Falcor SVGF

- relationship: adapted HLSL filtering implementation.
- status: active source and compiled runtime shaders.
- upstream: [Falcor SVGFPass](https://github.com/NVIDIAGameWorks/Falcor/tree/eb540f6748774680ce0039aaf3ac9279266ec521/Source/RenderPasses/SVGFPass).
- revision: `eb540f6748774680ce0039aaf3ac9279266ec521`.
- terms: the BSD 3-Clause notice carried by the source shaders, retained
  [verbatim](../licenses/falcor-svgf.txt).

`svgf_common.hlsli`, `svgf_reproject_cs.hlsl`, `svgf_moments_cs.hlsl`,
`svgf_atrous_cs.hlsl`, and `svgf_output_cs.hlsl` adapt the corresponding
SVGFCommon, SVGFReproject, SVGFFilterMoments, SVGFAtrous, and SVGFModulate
shader algorithms. each retains the original notice. the C++ pass owner and
guide preparation translate engine resources and dispatch into UVSR.
[denoising](../../docs/denoising.md) owns the algorithm differences.

source distributions retain the copyright notice, conditions, and disclaimer.
binary distributions reproduce them as
`bin/licenses/NVIDIA-Falcor-SVGF-BSD-3-Clause.txt`. NVIDIA's and contributors'
names cannot endorse derived products without prior written permission.
Falcor is not linked or fetched as a build dependency.
