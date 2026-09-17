# scene loading transaction

[`LoadImportScene`](renderer_import_load.h) builds one complete CPU candidate.
it owns file bytes, temporary model slots and paths while the
[parser and converter](renderer_import.md) construct canonical records, packed
geometry and encoded images. composition consumes the temporary model owners.
decoding then creates a fixed, texture-indexed array of
[owned decoded images](renderer_import_image.md). parser documents, source files,
the application description and encoded images die before the result is ready.
failure or cancellation preserves the previous output, including its views.
replacement requires the caller to have ended all existing output borrows.

`Ready` means CPU-ready. a worker caller must join before reading or moving its
candidate. cancellation can arrive after CPU commit and before the worker returns;
the worker's canceled join then rejects that candidate. the active scene is a
separate owner until the render owner's publication transaction succeeds.
production uses this handoff in `UvsrSceneViewer`; GPU upload and visible
publication follow on the render thread.

the [GPU resource owner](renderer_scene_resources_nvrhi.md) borrows decoded
images and packed geometry through upload recording. its `cpuBorrows` flag, or
explicit cancellation, ends those borrows. geometry may remain for collision.
NVRHI retains submitted resources through actual GPU completion; CPU readiness,
worker completion and queue submission are separate boundaries.

## partial descriptions and diagnostics

direct lowercase `.gltf` and `.glb` paths require the whole model to succeed.
other extensions use the retained application-description route. descriptions
retain one slot per model in source order. recoverable model-file, parser or
required-buffer failures make that exact slot unavailable. a reference skips its
whole descriptor subtree; an unreferenced unavailable slot adds no graph nodes.
the composer requires explicit Available or empty Unavailable slots. Pending,
invalid availability and an unavailable slot with payload are errors.

allocation, capacity, workspace, overflow and cancellation failures always abort
the transaction. conversion, referenced-image and composition failures also
abort. this allow-list preserves the retained loader's partial file/parse/buffer
behavior without silently skipping models for new conversion validation.

progress reports copied counters and an explicit operation/result. file bytes
count completed reads. work counters count attempted import operations and their
completion, including failed operations. object completion counts attempted model
slots; `objectsUnavailable` distinguishes successful partial descriptions.
`PrepareImages` covers decoded-array planning, with no individual texture index.

ordinary diagnostic paths are at most 511 bytes and mark truncation. I/O uses the
full path. each unavailable description model emits one separate synchronous
`modelUnavailable` event with its source ordinal, full model path, failing
operation and `ImportResult`. the production callback logs it during that call.
paths longer than 3000 bytes use consecutive, labeled byte ranges so the logger's
fixed buffer cannot truncate the diagnostic. the output
retains the count, not a second diagnostic history. terminal failure retains its
copied progress snapshot. parser codes and Win32 system codes use separate fields.

callbacks, contexts and cancellation tokens are synchronous borrows. no callback
may reenter the load or retain a working view. cancellation is checked around
parser/converter/codec calls and between native file chunks; those synchronous
calls are not interrupted internally. there is no recursive retry.

## storage and platform boundaries

the loader uses checked fixed storage, sized from validated counts. `maxModels`
bounds description slots. `maxLoadingBytes` bounds those slots and availability
entries plus the current resolved buffer path; `peakLoadingBytes` measures only
that owner. parser, description, file and codec storage are separate costs.

`maxFileBytes` limits each file and encoded image. the reader checks before file
allocation. `maxPathBytes` bounds full I/O and image-probe paths. description
storage/scratch and conversion scratch have separate limits. geometry and encoded
image limits apply to each model and the final composed owner, rather than their
simultaneous sum. `maxDecodedBytes` includes the final image-owner array and all
decoded allocations. temporary EXR pixel planes have their own limit. vendor
allocation limits remain those of the parser and codec owners; first-party
failure injection does not prove arbitrary vendor recovery.

the Win32 file adapter validates UTF-8 and embedded NUL, owns one temporary wide
path, checks file size, reads in at most 1 MiB chunks and closes the handle before
returning owned bytes. empty files are valid I/O and invalid glTF content. checked
image existence distinguishes missing files from allocation/I/O failure, so a
failure cannot silently choose a different DDS/PNG source. Windows filesystem
and path limits still apply and return explicit errors; the input budget does
not promise a platform path capability.

[`ImportLoadStatus`](renderer_import_load_status.h) allocates one fixed block
before worker start. private Win32 shared/exclusive locking copies coherent
progress and path snapshots without later allocation. it has no parser, scene or
GPU access. reset and destruction require all readers/writers to have joined.
portable loading headers expose no Windows, parser or graphics types.

decoded texture metadata updates only alpha/source bit depth before visible
publication. it preserves path/MIME and resource indices, increments content
revision only on change and rejects stale generations. primary-image decoding
retains the old fallback for optional swizzle metadata; the retained renderer
does not implement pixel swizzling. a referenced swizzle-only request has no
uploadable primary image and fails explicitly. future swizzle support would need
an expanded loading/output contract.

runtime storage uses checked allocation and iterative destruction. `<new>` is
needed for object lifetimes in that storage; no owning standard-library
collection is used in the loader or native adapters. test-only filesystem,
string and vector owners construct independent fixtures and preserve snapshots.

[`ImportRuntimeLightOptions`](renderer_import_runtime_lights.h) optionally adds
application lights exactly once to the final direct model or composed scene.
description children convert with the option disabled. the first live directional
keeps its authored fields except the requested irradiance and angular size; an
absent sun uses the full fallback spec. the flashlight always appends, including
when an authored light has the same name. application code owns those defaults.
spec names are borrowed through the synchronous call and copied before return.
only the two existing imported sun/HDRI aliases normalize; other names keep their
authored bytes. direct conversion appends after existing graph and animation IDs,
without a second composition, resource copy or extra previous-transform pass. changed
name lengths can change later string offsets. the final aggregate owns explicit
generation-qualified sun/flashlight handles. failed conversion, image decoding or
cancellation preserves the prior aggregate and its handles.
