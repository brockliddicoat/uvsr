# denoising

`SvgfPass` is the only denoiser. it owns independent history for AO, GI, sun
shadows, flashlight shadows, sky visibility, and path tracing. Raw returns the
producer output. path tracing retains progressive averaging in Raw. ray marching
sample accumulation bypasses SVGF and owns its raw lighting average.

the global Denoising drawer selects Raw or SVGF. **Specify Noise and Denoising**
enables private settings. AO and GI share the noise gate but have separate
filters. sun shadows expose **Specify Denoising**. the canonical settings live
in `denoising_fields.def`. older filtered methods migrate to default SVGF;
Raw remains Raw. discarded filter controls do not affect the replacement.

## reference and pass order

the implementation adapts NVIDIA's
[Falcor SVGFPass](https://github.com/NVIDIAGameWorks/Falcor/tree/eb540f6748774680ce0039aaf3ac9279266ec521/Source/RenderPasses/SVGFPass)
at commit `eb540f6748774680ce0039aaf3ac9279266ec521`. the adapted shaders retain
NVIDIA's BSD 3-Clause notice, also shipped as
`bin/licenses/NVIDIA-Falcor-SVGF-BSD-3-Clause.txt`.
Falcor is a source reference, not an engine dependency.

1. prepare linear view depth, its pixel derivative, signed octahedral normals,
   and previous-frame correspondence from the producer guides.
2. reproject illumination and luminance moments with accepted bilinear taps,
   falling back to a 3 by 3 search. history is capped at 32 observations.
3. estimate variance for histories shorter than four observations with the
   reference 7 by 7 filter.
4. apply 5 by 5 wavelet passes with doubling spacing. protect the selected
   intermediate texture for the next frame while the other two textures rotate.
5. restore the receiving albedo and emission when the producer supplies them.

reference defaults are four iterations, feedback tap one (the second wavelet
pass), luminance sigma 10, normal power 128, radiance alpha 0.05, and moments
alpha 0.2. those six controls are exposed. there are no optional stages or
adaptive replay paths. changing settings, losing frame continuity, or changing
the sample grid or an input resource invalidates history.

## engine input contract

each radiance sample and its guides describe the same producer location.
filtering stays on the producer's native sample grid. existing lighting
upsampling reconstructs reduced AO, GI, and visibility signals. the denoiser
has no separate resolution or resampling owner.

raster guides contain device depth, world shading normal, pixel motion, and
surface derivatives. depth and normal derivatives are evaluated in the raster
shader before alpha discard, so helper lanes remain on the same primitive.
computing them from neighboring completed pixels would cross object boundaries
and weaken rejection exactly where it is needed. only the prepare pass converts
engine depth and pixel motion to the private SVGF representation.

path tracing supplies centered primary hits for SVGF, with depth, normal,
motion, diffuse albedo, and emission from that hit. its depth derivative comes
from the primary triangle plane and adjacent camera rays. its normal derivative
is conservatively zero because that producer has no raster helper lanes.
raw path tracing retains its primary jitter. screen-space GI already supplies
incident illumination; composition applies the receiving material once.

GI sources contain outgoing diffuse radiance and emission. they use current raw
sun, flashlight, and sky visibility observations, so GI does not accumulate
another filter's history as a fresh observation. the angular GI integral tests
source sidedness with its geometric normal. it does not multiply by another
source cosine after measuring coverage in solid angle.

sky visibility owns occlusion for each IBL component it affects. screen AO
applies only to the remaining IBL components. an unused AO producer and filter
are skipped, while AO settings, its diagnostic view, and requested hit distance
remain available.

## deliberate differences from Falcor

- previous depth is compared in the previous camera's coordinates. current and
  previous view depth differ when a camera translates.
- every history tap is bounds checked. history length follows the same accepted
  weighted taps as illumination and moments.
- image-edge variance taps clamp to the image, avoiding out-of-bounds reads.
- path sample alpha marks transport validity. failed attempts retain accepted
  history without counting a new observation. missing history can be filled
  from valid spatial neighbors instead of filtering a false black sample.
- albedo division and reconstruction use the same 0.001 floor. this preserves
  nonzero specular transport on zero-diffuse materials. combined diffuse and
  glossy transport remains an approximation of basic SVGF.
- engine glue converts pixel motion and odd reduced grids. scalar visibility
  stores its value and variance in RG32_FLOAT. RGB illumination and moments use
  RGBA32_FLOAT. scalar packing preserves the reference equations and precision.

matching raster sample grids share immutable prepared guides for the frame.
signals retain separate moments and illumination history. binding sets are
reused while their resources match. nonmaterial signals return the final
wavelet texture directly; only path tracing needs material reconstruction.

`uvsr_denoising_gpu_reference` checks analytic temporal moments, history reset,
previous-camera depth, rejected correspondence, moving surface boundaries,
material reconstruction, independent signals, and odd native grids. actual
engine images during movement and after stopping remain a separate acceptance
requirement. see [validation](validation.md) and the
[replacement postmortem](postmortem/denoising-replacement.md).
