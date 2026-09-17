# scene lifetime

this module separates CPU preparation, scene publication and GPU retirement.
[frame order](renderer_frame.md) owns execution; the [glossary](../docs/glossary.md)
defines shared terms. production owns one canonical scene.

## owners and publication

[`RendererSceneState`](uvsr_renderer_scene_nvrhi.h) owns the current scene slot,
canonical CPU scene, derived tables/draws, collision world, scene catalog and
descriptor manager. the [canonical scene](renderer_scene.md) owns mutable values
after the [import transaction](renderer_import_load.md). packed geometry and
decoded images remain separate loading owners. visible materials, light values
and poses read canonical records, with no mutable native mirror.
`WorldSpaceRepresentation` owns the shared
ray representation. techniques own private histories and bindings; `RendererFrameState`
owns shared targets. no technique owns an independent imported scene.

[`LoadSceneCandidate`](uvsr_scene_lifecycle.cpp) reads frozen preparation inputs
and fills one worker-private `PreparedSceneCandidate`. generation and runtime light
specs are frozen before starting the worker; failed generations are never reused.
scene counts must fit their actual index types before publication. collision extraction checks ranges and
capacity before reserving; its [worklist](checked_worklist.md) has a derived bound.
the worker does not publish a scene or read mutable UI settings.

after `Join`, the render thread moves the aggregate into its preparation slot.
`GetSceneView` remains unavailable while mesh uploads, activation, initial skinning,
GPU tables, collision, ray representation, targets and passes are prepared.
packed geometry survives upload for a second phase on the same worker.
that phase borrows the canonical scene and packed index/position bytes while camera/light
mutation and visible rendering are suspended. after `Join`, the render owner
releases geometry, moves the collision result and activates the camera. decoded
images release once the upload owner's CPU borrows end. NVRHI owns staging
lifetime through submitted commands. readiness requires the final nonzero graphics submission
and completed environment preparation, not GPU idleness: subsequent work is ordered
on the same queue.

the [upload owner](renderer_scene_resources_nvrhi.md) submits before the frame command
list opens and reports its explicit phase. [checked byte ranges](renderer_upload_layout.h), null-resource checks and
request-scoped NVRHI error counts reject failed recording before publication. failed
candidates are discarded, never resumed in place. the retained replacement behavior
retires the old scene before importing another; it does not keep two full scenes for
rollback. resize is different: a target candidate must succeed before replacing valid
targets or clearing dependent binding caches.

## completion and deferred reuse

[`RendererSceneRetirement`](renderer_scene_retirement.h) is a portable gate with
three synchronous borrowed callbacks. its context must remain at the same address
until the gate dies. the [NVRHI owner](renderer_scene_retirement_nvrhi.cpp) owns one
event query on the graphics queue. `Begin` stops scene submission; the next `Poll`
arms the query after preceding work. completion allows the scene owner to clear caches,
release descriptors and consume the gate. query failure or a five-second timeout uses
checked queue idleness. failure of that fallback never authorizes reuse.

the [native example](../tests/renderer_scene_retirement_tests.cpp) deliberately blocks
the real graphics queue before an upload and copy. a second descriptor cannot take the
in-flight slot, and `Consume` fails while pending. after native completion, the copy is
checked and that exact slot becomes reusable. the example also exhausts the declared
table capacity and checks unchanged failure plus released-slot reuse. it proves queue
completion and descriptor ownership, not dynamic bindless shader contents.

ordinary NVRHI resources retain their submitted references through its existing
command lifetime tracking. mutable descriptor slots need the explicit scene gate.
there is no additional barrier tracker or general frame scheduler. the
[target fixture](../tests/renderer_targets_tests.cpp) checks partial creation failures
and repeated submitted replacements, then releases probe references in framebuffer,
texture and heap order to prove no other NVRHI owner remains after completion.

## jobs and teardown

the [worker contract](renderer_scene_load_worker.h) borrows a named callback context
until `Join`. its [Win32 implementation](renderer_scene_load_worker_win32.cpp) uses one
thread handle and an interlocked state. cancellation is cooperative between import,
graph, collision and environment phases. it does not interrupt vendor calls or add a
cancel UI. normal completion loses to an already requested cancellation; an exception
remains failure. diagnostics are copied into bounded storage before completion.

the worker is the viewer's last value member, so constructor unwind drains it before
destroying borrowed scene, frame and lighting owners. ordinary destruction also drains
explicitly. shutdown then arms and waits for scene retirement even when no replacement
was already pending. descriptor-owning tables reset before their manager. if waiting
or closing the thread handle fails, teardown stops fatally rather
than free a possibly live callback context. [worker tests](../tests/renderer_scene_load_worker_tests.cpp)
exercise publication, cancellation, reuse, exception precedence and destructor ordering.

canonical scene, import and resource owners drain flat arrays iteratively. the
[owner fixture](../tests/renderer_scene_tests.cpp) checks complete 100,000-node
deep/wide hierarchy order, two edits, independent previous snapshots and repeated
reset on a measured 64 KiB thread stack. copied inputs and generation-scoped
borrows replace native node/leaf aliases. the old graph's registry callbacks,
postorder leaf destruction and adapter lookup maps have no current owner.
GPU handle and descriptor lifetime remain covered by the blocked-queue and
last-reference fixtures above.

## storage and remaining boundaries

target placement uses two fixed entries, derived from HDR and presentation outputs;
framebuffer attachments use five. [placement checks](renderer_target_layout.h) reject
invalid alignment and overflow without changing outputs. scene-dependent capacities
come from imported counts, not small hard-coded scene limits. descriptor capacity is
the selected binding layout's declared limit. the
[descriptor owner](renderer_scene_descriptors_nvrhi.h) checks growth and exhaustion
and returns an explicit error. duplicate views borrow one slot with one owning
release. a failed native clear retains its resource until successful release or
manager destruction; scene teardown cannot turn that failure into early reuse.

the retained diagnostic samples collision/table capacities, source-array release,
descriptor live/peak counts, prepared texture counts, PBR binding peak and target heap bytes.
`textureQueuePeak` retains its field name but reports the fixed prepared texture total.
these are named-owner measurements, not allocator calls or resident VRAM. initial
import, activation, first use, resize and reload remain separate growth phases.

canonical scene records and collision no longer own STL containers or vendor math.
collision's retained `std::nth_element` has documented fixed-toolset ordering
evidence. collision indices and positions are borrowed bytes and use `memcpy`
scalar loads, including unaligned input. the worker's temporary throwing bridge
and remaining shell/core types disappear in later stages. descriptor and
[PBR binding](pbr_binding_sets_nvrhi.h) owners use checked allocation and iterative
cleanup; diagnostic serialization, remaining pass storage and tests belong to
stage 9. supported vendor modes and opaque native libraries
remain separate audit scopes. no whole-engine allocation-free or exception-free claim
follows from the portable worker probe. fatal allocation boundaries are not recoverable
capacity results.
