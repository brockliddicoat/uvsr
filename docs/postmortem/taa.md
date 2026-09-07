# TAA removal

on 2026-09-05 the user authorized removing TAA and its controls, including the
sharpening pass owned by its resolve. FXAA, exposure, display transfer, dithering,
pixel zoom, lighting denoisers, and progressive accumulation remain separate
owners. removal does not establish a quality or speed result for the successor.

## development creep and evidence

the July NRA-RTAA experiment tried to stabilize undersampled images with a native
reconstruction system. its recorded failure analysis is recoverable as
`docs/nra-rtaa-v1-postmortem.md` at
`c9d2fdca9506566d2e479757aa5791af589aeaa6`. classifiers, reactive handling,
fallback rules, resurrection,
presets, and broad debugging surfaces accumulated before the sample and
reprojection contract had been demonstrated. each mechanism added another way
to explain a bad image without isolating whether the basic history sample was
correct. this is a lesson from that rejected experiment, not proof that every
temporal method fails.

the implementation between `fdc606d7214ff281d3f829fda1592e890025c0ec`
and `83fe09fbdcf602d0293c3e4758f81a8dfb720b12` is separated by 1 hour,
54 minutes, 42 seconds using the commits' recorded timezone offsets. that span
is commit chronology, not measured development time. NRA-RTAA was retired in
`a5a75d50565ab80509b87c63cc8282084a5270e4` on July 14.

the later implementation is a different lineage. commit
`d27517538c1693b134157b93dbb612fbac493368` introduced the MiniEngine-derived
approach on July 17; `89708388c7c7af6efccd24ae1ed49131f3f45ddc` records its
subsequent verification work. the implementation removed now included history
storage and weighting policies, depth validation, motion trust, rectification,
blend domains, jitter choices, quality recipes, and sharpening. even fixed
recipes retained substantial orchestration, shader variants, histories, settings,
and diagnostic obligations. the current removal follows the user's scope
decision. old NRA images are not evidence of a defect in this later dirty build.

## removal and recovery

the twelve former `src/temporal_aa*` files totaled 5,366 lines and 192,326 bytes
at the removal baseline. these are gross owner sizes, not net savings: the old
options and tests also held surviving display behavior. FXAA options now live
in `src/fast_approximate_aa_options.h`, and retained controls are tested in
`tests/display_controls_tests.cpp`. none of those twelve old files belonged to
the frozen M.04 context union, so their deletion earns no direct M.04 reduction.
counted caller simplifications and new successor files must be measured separately.

the removed AA-owned projection jitter, history allocation, blend/resolve
shaders, presets, controls, and timing paths no longer participate in frame
execution. schema `000c` removes TAA, sharpening, and MSAA values together;
supported older snapshots validate and discard them as defined in
[settings](../settings.md). accumulation invalidation remains required by its current consumers.
the later denoising removal also retired its motion and depth guides.

the exact dirty removal baseline is HEAD
`c4dbcb31c2a241f5864cb08ae2f80551ef2615fb` plus the ignored local recovery
record `work/astra-cutdown/aa-native/baseline.json`. its archive
`work/astra-cutdown/aa-native/dirty-owners.zip` has SHA-256
`8ae4304f5a875f95717839ecc9f65c86ef6dc45c9e9e844e0bc82b5c1d14f895`.
the record supplies file hashes and staged/unstaged evidence. Git alone cannot
recover those uncommitted owners. verify the archive, then inspect selected
files; never overwrite current integration files with the whole archive.

## a future reconstruction baseline

future DLAA or bespoke TSR-like work begins from the explicit composition and
input ownership in [architecture](../architecture.md#future-reconstruction).
no SDK, reconstruction framework, placeholder control, jitter, or extra history
is retained solely for that possibility.

first prove stationary and moving reprojection, depth rejection, disocclusion,
reset, and render/output extent behavior with a small baseline. add one mechanism
only when a captured defect requires it, then compare image quality, frame time,
and memory against that baseline. passing contracts alone cannot accept an image.
the current removal still requires fresh visual, runtime, and package proof;
historical results do not satisfy [validation](../validation.md).
