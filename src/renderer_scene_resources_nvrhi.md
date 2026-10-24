# scene resource uploads

`RendererSceneResourcesNvrhi` is the private NVRHI owner of a candidate scene's
geometry and textures. it consumes the [plain import](renderer_import.md) and
[decoded image](renderer_import_image.md) owners. it does not own descriptors,
the CPU scene, collision data or per-frame animation. the production draft feeds
this owner after the import worker joins; the visible equivalence gate remains open.

preparation reads a sealed scene view synchronously. one group array and one
texture array are sized from its validated counts. they cannot grow. shared index
groups reference one GPU allocation, even when that owner occurs later in the
table. each static group owns one vertex buffer. a derived skin group owns its
output and a loading-only palette, and copies the small shader constants and
prototype index. morph payloads have no current GPU consumer and allocate no GPU
storage here. `maxStorageBytes` bounds this owner's state and arrays;
`maxBufferBytes` counts unique buffer descriptions, including temporary palettes;
`maxTextureBytes` counts format footprints across physical dimensions and mips.
driver alignment, NVRHI command storage, descriptors and vendor allocations are
separate from these counts.

preparation publishes only a complete resource table into an empty owner. failure
releases the candidate and leaves the owner empty. geometry bytes, initial joint
matrices and image byte/layout views remain borrowed during loading. the caller
keeps their owners alive until `cpuBorrows` is false or calls `Cancel`. source
scene records can be destroyed immediately after preparation. no parser or
initializer-list view enters deferred work.

each `Step` opens and submits its own command list on the graphics queue. the
application calls it before opening the frame command list, preserving NVRHI's
single recording-state authority. buffer writes split at the remaining
positive byte budget. a whole texture subresource, mip draw or skin dispatch can
exceed the budget when it is the first operation, ensuring forward progress.
generated mips retain the native short-side logarithmic count. the common blit
binds only the preceding source mip; its original three-argument entry retains
the previous all-subresource binding. the supplied initial-skin shader consumes
fifteen uint lanes and 256 threads per group. dispatches split at the remaining
byte budget or backend limit while advancing every active attribute offset. a
single vertex can exceed a smaller positive budget. per-frame skinning
can still write the derived UAV later.

NVRHI owns transitions through `keepInitialState`. buffers return to their vertex
or index, shader-resource and optional acceleration-structure input states.
textures return to shader-resource state. the owner does not add a barrier map.
DDS block-compressed base extents round up to four as in the retained loader.
every authored subresource must cover the physical GPU copy footprint; a layout
that would make the retained path read beyond its bytes fails before recording.
format support and resource creation are checked separately from CPU decoding.

NVRHI's pinned D3D12 writes copy the CPU bytes while recording. a nonzero queue
submission commits the copied byte count, including when a subsequent health
check fails. a failed write/close/submit/query latches a terminal failure, stops
further work and clears remaining borrows. a mandatory
synchronous health callback covers NVRHI's void operations. cancellation likewise
ends CPU borrowing and discards future work; it does not undo submitted commands
or replace an existing failed, completed or canceled result.
callbacks and common-pass pointers are never retained.

`Submitted` establishes queue ordering for later graphics consumers, not physical
completion. copied texture progress counts only fully submitted textures,
including their generated mips. an event query covers the latest successful submission.
`PollCompletion` changes `Submitted` to `Complete` only when that query resolves.
loading-only handles then release. reset, cancellation and destruction may also
release local GPU handles immediately: the pinned NVRHI queue lifetime tracker
retains command-instance resources, binding sets and upload storage until its
fence completes. externally owned mutable descriptor slots and published scene
replacement still require their existing outer retirement gate.

all first-party upload and destruction loops are iterative. checked `malloc`
plus placement construction gives recoverable state/array allocation and correct
lifetimes for NVRHI reference-counted handles. `<new>` is used for that object
lifetime requirement; this owner has no owning standard-library collection.
NVRHI description fields and binding containers remain private vendor types.
their internal allocation failure can still be fatal with exceptions disabled.
creation/submission failure injection verifies first-party propagation; it does
not establish recoverability of arbitrary driver or vendor out-of-memory paths.
