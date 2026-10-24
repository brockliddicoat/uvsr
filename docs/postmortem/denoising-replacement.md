# denoising failure and replacement

the previous repair did not meet the user's visual requirements. its passing
checks do not override that result. this postmortem concerns the dirty source
identity `c4dbcb31c2a241f5864cb08ae2f80551ef2615fb-dirty-7c0d9d6bd084`,
preserved in this task's startup snapshot. it distinguishes observed decisions
from likely causes. no single shader defect has yet been proved to explain all
the reported artifacts.

## where scope went wrong

the user explicitly said the whole system could be broken. the repair nevertheless
retained one configurable foundation for bilateral, SVGF, adaptive replay, multiple
filter resolutions, six signals, and many independently switchable stages. fixing
individual symptoms within that foundation left too many interacting assumptions
to establish a trustworthy baseline.

the acceptance decision was also too broad. the report counted 26 developer
checks, 101 integration cases, and 32 capture cases. those establish particular
execution paths and numerical properties, not satisfactory denoising. whole-image
error in a tonemapped 8-bit image can improve when a filter blurs detail. selected
settled frames and the first frame after a camera nudge cannot establish behavior
through sustained movement or the several seconds after stopping. the user's
reported oily evolution, washed colors, and poor bilateral output remained the
actual acceptance failures.

## likely implementation mistakes

- the implementation substituted a custom geometric-plane filter and relative
  depth rejection for the reference's depth-derivative weighting and reprojection
  tests. that may be defensible independently, but it was another algorithm to
  validate while the integration was already uncertain.
- surface preparation, source sampling, filter resolution, and reconstruction
  shared custom grid conventions. even a correct local filter cannot recover
  from a signal paired with another sample's geometry. the producer's actual
  sample location must define the guide, including at odd image boundaries.
- derivative guides need the same primitive's raster helper lanes. reading
  completed neighbor pixels crosses surface boundaries and weakens rejection
  at silhouettes. the replacement therefore writes those derivatives in the
  raster pass, before alpha discard.
- path tracing combined diffuse and specular transport, divided it by a custom
  material response, and used independently jittered primary hits. this is not
  automatically equivalent to Falcor's color, albedo, emission, and G-buffer
  contract. texture boundaries and glossy surfaces are especially sensitive.
- the temporal code accepted history with a relative depth threshold and normal
  dot threshold, while the reference uses derivatives and a neighborhood search.
  synthetic motion fixtures cannot establish that the engine's motion units,
  direction, jitter correction, and depth convention agree with those equations.
- the old defaults were not even an exact reference baseline. Falcor uses four
  wavelet iterations and a zero-based feedback tap of one. the old implementation
  used five iterations and a one-based feedback value of one. this changes both
  spatial smoothing and the image accumulated next frame.

these are supported mismatches and risks, not proof that each caused the user's
image. the previous report's claims about repaired defects remain historical
claims until reproduced against the replacement's independent checks.

## what changes this time

replace all old filtering, replay, history, and denoiser sampling code with one
basic SVGF pipeline. retain Raw for comparison and the existing effect selection.
remove bilateral and A-SVGF settings and implementations. no compatibility path
may execute the discarded filters. old stored settings may be discarded through
an explicit schema migration.

use [Falcor SVGFPass at the pinned revision](https://github.com/NVIDIAGameWorks/Falcor/tree/eb540f6748774680ce0039aaf3ac9279266ec521/Source/RenderPasses/SVGFPass)
as the primary implementation. retain its license and provenance. translate its
resource bindings and pass execution into UVSR, and document every material
algorithm deviation. Falcor and Donut both rendering through DirectX does not
make their G-buffers, motion vectors, or material signals interchangeable.

establish the input contract before tuning: current noisy illumination, matching
linear depth and normal, previous-frame correspondence, and explicit emission
and material reconstruction. keep each signal's history separate. use direct
resource ownership and explicit pass order. future bilateral or adaptive work
can extend those proven inputs without retaining dormant implementations now.

acceptance needs independent reference comparisons, constant and noisy surface
fixtures, slope and boundary cases, and actual engine captures at rest, during
movement, and after stopping. inspect detail, brightness, edge leakage, and
temporal trails separately from noise error. prove the exact executable used.
if visual evidence remains poor or unavailable, report that gap instead of
describing the replacement as fixed.

## recovery and measurement

the first replacement also failed the user's visual acceptance. matching the
SVGF pass equations did not establish that its inputs or final lighting were
correct. a second investigation isolated each signal and captured its linear
values before and after every filter stage. it found these upstream defects:

- spatial noise animation masked low recurrence bits and revisited a diagonal
  orbit. spatiotemporal noise repeated the same 64 observations indefinitely.
  more frames therefore did not make the old accumulated references independent.
- GI source radiance omitted emission and the angular estimator applied an
  additional source cosine. both could remove transport before filtering.
- GI consumed filtered parent lighting, then filtered that history again without
  a matching variance model. it now receives raw parent observations.
- full scene sky visibility and screen AO multiplied overlapping occlusion.
  with explicit user approval, sky visibility now owns each affected IBL
  component. the remaining AO consumers and controls are preserved.

these repairs do not justify changing exposure to conceal dark output.
compare the filtered image with a converging raw mean at the same camera and
settings. the source fixes are independently testable; visual acceptance still
requires the actual moving and stationary scene results.

performance also suffered from redundant guide preparation, per-dispatch
binding creation, feedback copies, output copies, and four-channel storage for
scalar visibility. the replacement now shares matching guides, caches binding
sets, protects feedback through texture rotation, and stores scalar values and
variance in two channels. measure timings without another renderer or image
readback competing for the GPU.

`work/svgf-replacement/baseline` contains the 755 startup files, with hashes in
`baseline-manifest.json` and an independent `original-index` copy. the active
branch is `codex/denoiser-continuation-20260905`. do not reset or stage the inherited
changes. the original task's build and running engine remain independently owned.

measure changed first-party text against this startup snapshot, including new
files and deletions. report line additions, deletions, net change, and a labeled
context estimate at checkpoints. build and visual evidence must describe the new
candidate; the original repair's evidence cannot certify it.
