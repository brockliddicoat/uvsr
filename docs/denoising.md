# denoising

`DenoisingPass` owns the shared filter pipelines and per-signal histories for AO,
GI, sun shadows, flashlight shadows, sky visibility, and path tracing. it prepares
depth, normal, and reprojection guides once per resolution per frame. path tracing
uses its own primary-hit guides. histories never mix signals.

the global Denoising drawer selects Raw, Bilateral, or SVGF. effects inherit it
unless **Specify Noise and Denoising** is enabled. AO and GI share a noise gate
but have separate private filters. sun shadows expose **Specify Denoising**.
the setting names and ranges have one definition in `denoising_fields.def`.

## filter controls

Raw returns the producer output without filtering. path tracing retains its
cumulative mean in Raw. Bilateral filters that cumulative mean; SVGF consumes
the current sample and owns its temporal filtering.
Ray Marching sample accumulation owns raw lighting history and disables the
denoising controls while active.

Bilateral has two guidance choices. Signal compares the noisy values themselves.
Depth and Normal uses separate geometry, covering the terms joint and cross
bilateral. Gaussian Weights applies spatial falloff to either. Radius controls
the footprint; Signal Sigma controls signal similarity in Signal mode. its
distance is the root mean square RGB difference, or the scalar difference for
visibility. small signal sigma preserves noise along with signal edges.
sky and shadow visibility always use Depth and Normal: their ray outcomes are
noise observations, not edges in the underlying lighting. their private Signal
controls are gated. global Signal guidance still applies to AO, GI, and path color.

SVGF follows the three stages in the
[requested walkthrough](https://www.mysimulator.uk/content/articles/realtime-denoising.html):

- **Temporal Accumulation** reprojects radiance and luminance moments. History
  Length caps effective history. Radiance Alpha and Moments Alpha set minimum
  current-frame weights. Depth Rejection and Normal Rejection validate surfaces.
- **Variance Estimation** computes variance from moments. Prefilter Short History,
  Minimum History, and Prefilter Radius control the spatial estimate before
  enough history exists. disabling variance uses a fixed variance of one.
- **A-Trous Filter** uses a 5 by 5 kernel with doubling spacing. Iterations,
  History Feedback, Blur Variance, Luminance Sigma, Depth Sigma, and Normal Power
  control filtering. feedback zero retains temporal output; larger values retain
  the selected spatial iteration, capped at the last available iteration.

Full, Half, and Quarter select a maximum filtering resolution, limited by the
producer's sample grid. a quarter-resolution producer therefore does not create
sixteen apparent observations by copying each sample to full resolution.
reduced output resolves to full size using geometry guides. odd dimensions use
a clamped integer sampling grid. changing filter settings or losing frame
continuity invalidates history. missing required inputs returns the raw signal.

## surfaces and materials

spatial depth weights compare a neighbor with the center's geometric plane in
view space. Depth Sigma sets tolerance in projected pixel footprints. shading
normal similarity supplies the separate Normal Power term. coplanar samples
remain connected regardless of slope or camera direction. path tracing stores
the actual primary-ray jitter with its geometric normal.

path tracing subtracts primary emission and divides by a nonzero material
response before filtering. the resolve restores both at the destination pixel,
preserving texture and emissive detail. bilateral material and emission guides
average the same accepted samples as its radiance input, so jittered material
boundaries are not divided by a different sample's material. the response combines diffuse color and
specular F0, so metals retain color. this single combined signal remains an
approximation for glossy transport. screen-space GI already supplies incident
lighting; its consumer applies the receiving material once.

reduced filtering averages the source samples on the receiving surface. when
the coarse grid has no compatible surface at a full-resolution pixel, resolve
keeps its current raw observation instead of borrowing unrelated filtered history.

temporal reprojection accepts only bilinear taps with matching depth and normal.
rejected path samples do not become black observations. primary depth, motion,
and normal are published together even when the radiance sample is rejected.

## adaptive sampling

**A-SVGF** enables correlated temporal gradients. the design follows the
[original adaptive temporal filtering paper](https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/adaptive_temporal_filtering.pdf)
and its [supplementary implementation](https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/a_svgf.zip).
independent noisy frame differences do not provide this estimate.

for visibility signals, a forward projection chooses at most one old surface
sample per current 3 by 3 tile, separately for each trace resolution. replay
uses that exact old receiver and random phase with current scene and lighting.
path tracing retains the previous sample seed and primary position, retraces
the old camera ray, and rejects a changed primary intersection. replay happens
before old raw values are overwritten, or uses a separate previous sample image.

Gradient Iterations reconstructs difference, normalization, and confidence with
an edge-aware filter. division occurs after reconstruction. Gradient Response
scales the relative difference that raises both temporal alpha values. invalid
replay falls back to ordinary SVGF accumulation.

this is an adaptation with additional sparse trace work, up to one replay per
nine full-resolution pixels. the original technique replaces selected current
samples. this implementation refreshes replay seeds after one frame. screen-space
AO/GI only replays with an unchanged camera matrix because its estimator depends
on the view. it uses ordinary SVGF during camera movement. these differences
matter when comparing cost and responsiveness with published results.

## references and evidence

the implementation was cross-checked against the
[original SVGF paper](https://cg.ivd.kit.edu/publications/2017/svgf/svgf_preprint.pdf),
[Falcor 5.2 SVGF](https://github.com/NVIDIAGameWorks/Falcor/tree/900a50ba88206691dac52c6d512e2560f731ddbf/Source/RenderPasses/SVGFPass),
[Foray A-SVGF](https://github.com/Vulkemp/foray-denoiser-asvgf/tree/f9c9732928a3613b5990dab1600c79138c1873a7),
the original adaptive implementation, and
[Q2RTX's A-SVGF notes](https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/asvgf.glsl).
Foray alone was insufficient to establish the producer replay contract. Q2RTX
was used as a design cross-check; its code is not included.

`uvsr_denoising_gpu_reference` checks analytic bilateral and temporal results,
surface rejection, sparse forward selection, all output formats, plane direction,
material reconstruction, noise reduction, moving depth edges, invalid samples,
and reduced producer grids with D3D12 GPU validation. shader reflection checks
the shared layouts. settings tests cover bounded migration from the removed NRD
schema and discard the retired performance settings from schema `000d`.

the retained runtime diagnostic covers engine integration. its separate
`UVSR_DENOISING_QUALITY=1` capture suite uses both retained scenes, both lighting
systems, raw and accumulated references, both bilateral guidance modes, and
four SVGF configurations. filtered captures follow 96 ready frames; references
follow 1,024. filtered cases also capture the first frame after a camera nudge;
references settle again at the moved position. the capture viewport is 1280 by 720.
passing numerical checks does not substitute for visual and timing review.
