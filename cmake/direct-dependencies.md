# direct dependency boundaries

the `Direct*` modules own pinned sources, target configuration and licenses.
`PatchedSources.cmake` applies local changes to copies below the build directory;
vendor sources remain pristine. `DirectDonut.cmake` only assembles temporary
consumers. its core archive compiles only logging; the device host still includes
inline Donut math for gamepad dead zones. portable scene math belongs to the
scene owner.

`DirectNVRHI.cmake` creates one common NVRHI target and one D3D12 target, including
the selected validation sources. it also configures DirectX-Headers and GUID
targets. the parent modules configure ImGui, GLFW, image codecs, JSON and import
libraries before any Donut target. `DirectDXC.cmake` owns the build-only compiler;
`DirectAgilitySDK.cmake` owns the runtime SDK. `RuntimeAssetStage.cmake` and
`RuntimeLicenseMappings.cmake` stage direct inputs without Donut locations.

## compiler and allocation boundaries

`NoCppExceptions.cmake` disables C++ exceptions and force-includes the compiler
guard for retained source-built C++ dependencies. `_HAS_EXCEPTIONS=0` propagates
to header consumers. simdjson uses `SIMDJSON_EXCEPTIONS=0`. GLFW compiles as C.
the remaining Donut logging unit uses the same exception guard. the Donut host and
older first-party targets remain separate migration work; dependency flags do
not certify their callers or opaque DXC, SDK, CRT and driver internals.

`uvsr_stb_image` owns one image reader/writer implementation for all consumers.
`uvsr_tinyexr` owns one EXR implementation. implementation-only codec dependencies
stay private to their consumers; targets that directly include their headers
declare that dependency. [image ownership](../src/renderer_import_image.md)
defines checked output, cancellation, capacity and codec allocation limits.

disabled exceptions do not provide recoverable vendor OOM. NVRHI's ordinary
allocation and STL containers, ImGui allocation and
TinyEXR internal storage retain fatal allocation assumptions. existing checked
native resource/descriptor failures remain distinct from these boundaries.
production scene import uses fastgltf/simdjson. the Donut engine comparison
target and shader stage are retired. JsonCpp's last application consumer was
the unused broad core compilation; its target, parser test, source patch and
generated header alias are now retired. [the historical record](../legal/documentation/jsoncpp.md)
preserves its identity and terms. cgltf remains in the retained-asset validator.

## iteration and vendor internals

the stb GIF patch expands dictionary prefixes into one fixed 8192-byte buffer
owned by each decode. it checks indices and cycles before emission, preserving
pixel order, interlacing, transparency and the existing supported dictionary.

the ImGui window patch uses existing parent links and a per-window child cursor
for depth-first sort/draw traversal. a serial detects repeated entry, and the
context window count bounds visits. serial wrap clears marks before reuse. this
requires exclusive ImGui context access, as the surrounding frame API already
does. it adds no traversal allocation. invalid private hierarchy terminates
before traversal can continue; this is a programming-state failure, not a
recoverable input or allocation result. root draw layers and child filtering
remain owned by ImGui. tests compare ordered draw data with the original helper,
exercise serial wrap, and reject duplicate, cyclic, oversized and wrong-parent
hierarchies.

remaining vendor recursion is disclosed separately from first-party exceptions:

| selected vendor path | reachability and limit |
| --- | --- |
| ImGui TrueType compound glyph expansion | reads the required Windows Segoe UI fonts. the reviewed Semibold and Bold files have acyclic graphs of at most three and two glyph levels. this observation is specific to those bytes; it is not a validator or bound for other font revisions. the vendor shape API also conflates empty glyphs and allocation failure. font loading/rasterization changes must revisit graph, failure and stack behavior. |
| ImGui curve subdivision and stb font tessellation | existing recursive helpers cap levels at 10 and 16 respectively. no first-party generic curve caller was found; font rasterization is active. these are vendor caps, not measured stack budgets or first-party recursion exceptions. |
| stb font edge sort and atlas growth | quicksort recurses on the smaller partition and loops on the larger. atlas retries increase clamped dimensions and stop when they cannot grow. altered counts or atlas limits require renewed review. |
| GLFW first error on a worker | persistent failure allocating that thread's first TLS error record can recurse. the initializing thread uses a static record. current GLFW callers stay on that thread; introducing worker calls requires handling this vendor boundary first. event callbacks must not reenter polling. |
| fastgltf and simdjson optional helpers | recursive scene-walk, JSON-pointer and serialization helpers are not used by the selected importer. the adopted parser/conversion paths use iterative state or explicit worklists. |

these disclosures do not approve new first-party recursive adapters. vendor
revision, adopted caller, font-byte or allocator changes invalidate the relevant
review. source-built libraries keep their supported versions and formats; no
general vendor-stack or recoverable-allocation guarantee is claimed.

EASTL is not selected. borrowed views, checked worklists and owner-specific
prepared arrays cover the new storage needs. vendor STL remains private to its
real consumers and requires a vendor change or replacement to remove completely.
