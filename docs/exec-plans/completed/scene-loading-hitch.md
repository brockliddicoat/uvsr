# Scene Loading Hitch

## Goal

Keep the UVSR window responsive during initial scene loading and later scene
changes, including the largest bundled scenes, without changing rendered scene
content or camera-collision behavior.

## Root Causes

- Scene activation transformed, copied, and BVH-sorted every collision triangle
  on the render thread after asynchronous import had already completed. San
  Miguel has about 9.97 million triangles, so this also created roughly 359 MB
  (342 MiB) of collision triangles and 200 MB (191 MiB) of BVH nodes in one
  activation frame.
- Donut copied complete mesh arrays into GPU upload memory in one activation
  frame. San Miguel's imported mesh arrays total roughly 424 MB (404 MiB).
- Import used every logical processor while its coordinator busy-waited. UVSR
  also elevated the whole interactive process to high priority, so import work
  could starve both the render thread and desktop compositor.
- Texture finalization could consume at least 20 milliseconds of a loading
  frame.
- The first render synchronously generated visibility blue noise, decoded and
  resampled the HDR environment, and constructed disabled optional shadow
  pipelines. The disabled sparse-shadow pass alone created 86 compute PSOs,
  while deferred lighting and temporal AA each created another multi-PSO batch
  in one preparation call.
- Renderer activation reapplied PBR material values after Donut's final scene
  refresh, leaving all resulting material and bindless-table writes for the
  first visible frame.
- Later scene changes synchronously called `waitForIdle` before old-scene
  teardown or replacement loading could start.

## Implementation

- Collision extraction and BVH construction, visibility blue-noise generation,
  and initial HDR decoding, projection, and cube resampling now run on the scene
  worker. Their completed data is moved to the render thread as one prepared
  handoff.
- Imported immutable mesh arrays now upload in pieces of at most 8 MiB per
  loading frame. Partially written index and vertex buffers preserve a valid
  NVRHI state across command-list boundaries and resume from their exact byte
  offset on the next frame. Auxiliary buffer creation, scene-table refresh,
  activation, post-activation material-buffer refresh, render-target creation,
  and pass preparation use separate later loading phases rather than stacking
  on the final array write or first visible frame.
- Scene import reserves two logical processors for interactivity, caps itself at
  eight workers, blocks its coordinator on a condition variable, and leaves the
  ordinary renderer process at normal priority. Explicit performance captures
  can still request high priority.
- Texture finalization uses a soft 4 millisecond target checked between complete
  textures; one texture can exceed it, but queued textures no longer consume a
  deliberate 20 millisecond slice.
- Optional directional-shadow, sparse-shadow, and diagnostic CSM passes are now
  constructed only when their feature or benchmark needs them.
- Initial IBL GPU upload and prefilter work advances one face or mip per loading
  update. The shared visibility, PBR deferred-lighting, MSAA visibility-resolve,
  and temporal-AA pipelines are likewise prepared one per loading frame before
  the first scene frame.
- Scene switches arm a graphics-queue event on the following render frame, poll
  it behind the loading presentation, and defer teardown until the old frame is
  retired. A full GPU idle remains only as a correctness fallback if an event
  query cannot be allocated.
- Loading presentation calls record their maximum wall-clock frame gap, which
  is emitted with the completed loading-frame count for regression diagnosis.

## Remaining Limits

The exact visibility trace specialization still depends on the final runtime
execution plan, so its first selected PSO remains lazy. Donut's forward and
G-buffer graphics PSOs are also created on first draw. These are individual
first-use pipelines rather than the former multi-pipeline startup batches. IBL
steps and whole-texture finalization are soft work units rather than hard GPU or
CPU time limits.

## Verification

- The complete Release build succeeded.
- All 36 Release CTest cases passed, including the new scene-loading worker
  policy test and expanded renderer source contracts.
- The final exact Release executable, SHA-256
  `7473DBA81F0C0612646FD855E22F0F23040595ECED9311C147940CD3127C0486`,
  completed a labeled default-scene smoke launch, remained responsive in all
  80 quarter-second startup probes, and reached a rendered PBR Sponza frame.
- A cold Retextured San Miguel launch remained responsive in all 15 samples
  taken across 30 seconds and reached its authored camera with the full scene
  rendered.
- That same final executable accepted an in-process switch from PBR Sponza to
  Retextured San Miguel with a 253 ms click-plus-capture round trip, remained
  responsive in all 160 quarter-second process probes over the next 40 seconds,
  and reached a live rendered San Miguel frame.
- The runtime smoke exposed and drove repairs for two staged-upload defects: a
  missing cross-command-list buffer-state handoff and a guard that skipped a
  partially written index buffer merely because its destination already
  existed.
