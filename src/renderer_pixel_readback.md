# integer pixel readback

[`RendererPixelReadback`](renderer_pixel_readback.h) reads one integer texel for
material/node picking. its public request is two uint32 coordinates; its result
is four uint32 words, 16 bytes at four-byte host alignment. only x/y carry picker
IDs. it exposes no vendor headers, native handles or owning STL containers. see
the [glossary](../docs/glossary.md) and [frame owner](renderer_frame.md).

## ownership and errors

the private [`NVRHI initializer`](renderer_pixel_readback_nvrhi.h) consumes handles
synchronously and retains the device, stable graphics command list, source and
compute shader. one checked `new (std::nothrow)` allocation creates its fixed
native RAII state. this private `<new>` use supplies object lifetime and cleanup,
not an owning-container framework. allocation/resource failure publishes nothing.

the [implementation](renderer_pixel_readback_nvrhi.cpp) accepts nonempty,
single-mip, single-sample RG16_UINT or RG32_UINT 2D sources. bounds and signed
shader-coordinate conversion are checked before recording. one outstanding
request is allowed. `Capture` records a one-texel compute load, a 16-byte typed
buffer write and a staging-buffer copy. `NotifySubmitted` requires Recorded state
and a nonzero token from that exact list's graphics submission. the token is a
caller assertion: this object cannot independently prove its provenance.

`ReadUInts` is legal only after submission and consumes the request. NVRHI DX12
tracks the staging buffer's actual queue/fence and blocks its map until complete;
the public token is not itself that fence. a map failure leaves output unchanged.
successful copy always unmaps. the owner must not overlap use from other threads.

`CancelRecorded` clears only a never-submitted request. it does not undo GPU work
or make a native command list reusable. NVRHI immediate lists must be submitted
before reopening; the current frame's failed immediate attempt terminates the
shell. deferred-list abandonment is tested separately. initialization and normal
capture state have distinct failure results, and the frame checks submission
before completing material selection.

the picker target is RG32_UINT, with UINT32_MAX as no-hit. this preserves every
nonnegative signed scene ID, including 65535, which the former RG16 sentinel
conflated with no-hit. widening costs four additional bytes per full-resolution
pixel. this optional target is allocated on first use and remains resident.

## replacement case

this is a source-reviewed future mapping to NoGraphicsAPI
`a2efb6c28768c5775d5b6140d5723a60a72b0936`, not an installed or executed backend.
its [public API](https://github.com/sebbbi/NoGraphicsAPI/blob/a2efb6c28768c5775d5b6140d5723a60a72b0936/include/NoGraphicsAPI/NoGraphicsAPI.hpp)
has no external device/texture import. source texture allocation and submission
must first belong to that device; swapping this file alone cannot use an NVRHI texture.

| operation | proposed private mapping |
| --- | --- |
| source | query sampled `rg32_uint` support, place the 2D texture in its texture heap, and write a sampled descriptor slot |
| parameters | backend-packed root with source slot, x/y and a GPU pointer to four uint32 result words; retain public request/result values |
| shader | Slang/SPIR-V `Texture2D<uint2>.Load`; write x/y and reserved z/w, without a sampler |
| result storage | a 16-byte addressable readback-heap range; a second typed intermediate buffer is unnecessary |
| completion | compute-write to host-read barrier, submit with an application-owned increasing TimelinePoint, then wait or poll before CPU access |

the [Vulkan contract](https://github.com/sebbbi/NoGraphicsAPI/blob/a2efb6c28768c5775d5b6140d5723a60a72b0936/docs/vulkan-support.md)
requires device-local, host-visible coherent mapped memory. retain the source,
placement, descriptor slot/heap, PSO, result heap and timeline through completion.
root bytes are copied during dispatch; referenced storage is not retained for
the application. descriptor stride comes from device capabilities. 16-bit sampled
format support is a format/usage query, not the shader 16-bit I/O capability.

commands are one-shot and must appear exactly once in the next submission.
there is no matching public abandonment operation: a future frame adapter must
resolve cancellation before concrete recording or explicitly submit/drain its
terminal partial work. do not pretend current deferred-list cancellation maps
unchanged. Vulkan 1.4, descriptor heaps, untyped shader pointers, address commands,
required shader features and suitable Slang remain explicit tool/device gates.

## checks

[`renderer_common_passes_tests.cpp`](../tests/renderer_common_passes_tests.cpp)
runs the actual production compute shader on 2x2 source images, reading one texel
per request. both integer formats and both list modes cover all coordinates,
full-width IDs, invalid bounds, overlap, submission order and immediate blocking
map. NVRHI validation and the D3D12 error queue are checked. the fixture does not
prove the production material-ID raster target's contents or full engine visuals.

the [standalone probe](../tests/portable_contracts/CMakeLists.txt) compiles the public
header without graphics dependencies and the actual private implementation with
exception-disabled NVRHI headers. only the latter target gets the NVRHI include
path. stages 7.02/7.03 establish source-built NVRHI/vendor runtime exception
closure; stage 8.07 audits this implementation's native binding allocation and
failure paths. an isolated object compile does not establish a linked runtime mode
or recoverable vendor allocation failure.
