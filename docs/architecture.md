# architecture

this document describes the current renderer architecture. UVSR is a Windows 11, DirectX
12 renderer. ImGui and one product feature set are permanent. developer builds
add tests and diagnostics, not alternate rendering features.

Donut removal is in progress. `donut_app` and `donut_core` retain the device and
window host. the core archive compiles only logging; inline Donut math remains
in the host's gamepad path. the Donut engine target, native comparison shaders,
engine patch stage and JsonCpp target are retired. NVRHI remains the initial graphics backend; fastgltf owns
production glTF parsing through the private import boundary.

the [glossary](glossary.md) links shared terms. current portable examples are
[rendering values](../src/renderer_contracts.md), [borrowed worklists](../src/checked_worklist.md)
and the [readback](../src/renderer_pixel_readback.md) and
[scene lifetime](../src/renderer_scene_lifetime.md) boundaries. the new
[flat scene owner](../src/renderer_scene.md) receives the complete import transaction
and owns mutable scene values. the production importer cutover is implemented;
its fixed structural and visible comparison remains open.

## ownership map

| area | current owner | boundary |
| --- | --- | --- |
| process startup and identity | UVSR | [`engine_startup.cpp`](../src/engine_startup.cpp), [`engine_identity.h`](../src/engine_identity.h), and generated version resources own startup checks and the source, settings, configuration, and release identity. [durable logging](../src/engine_diagnostic_log.md), [native SHA-256](../src/sha256.md) and [executable paths](../src/windows_executable_path.md) have checked owners. |
| window, device, and message loop | Donut with UVSR policy | [`uvsr.cpp`](../src/uvsr.cpp) uses Donut `DeviceManager`; the native scene viewer implements `IRenderPass` directly. GLFW remains behind `donut_app`. UVSR owns scene loading and retirement, adapter policy, DirectX 12 requirements, Agility SDK checks, and failure reporting. |
| frame order | UVSR | the [frame module](../src/renderer_frame.md) owns the concrete scene, UI and presentation sequence through a temporary private Donut adapter. |
| GPU abstraction | direct NVRHI | The direct `third_party/nvrhi` pin owns resource, command list, descriptor, query, and ray tracing interfaces. only its DirectX 12 backend is enabled. |
| mutable CPU scene and geometry selection | UVSR | the [flat scene owner](../src/renderer_scene.md) owns records, edits, temporal snapshots and the checked draw list used by geometry/depth passes. material/light commands have no native mirror. |
| material editing and camera collision | UVSR | [generation-checked editing and collision](../src/renderer_scene.md#editing-and-camera-collision) consume canonical records. one joined worker borrows packed CPU geometry before camera activation and visible publication. |
| files, shaders and view model | UVSR | explicit file owners, checked shader loading and renderer view values serve production. native scene, texture, view and common-pass comparisons use preserved reference data. the remaining Donut core boundary is host logging and inline gamepad math. |
| scene import | UVSR private document, description, geometry and image owners | the [import boundary](../src/renderer_import.md) owns checked parsing, iterative conversion/composition, paths and canonical records. one joined handoff moves scene, packed geometry and decoded images to [`uvsr_scene_lifecycle.cpp`](../src/uvsr_scene_lifecycle.cpp). the NVRHI resource owner uploads them before render publication. public values exclude parser and graphics types. |
| render targets and focused passes | UVSR | `renderer_*` owners implement targets, shader loading, common passes, geometry integration, readback, logging, scene work, and retirement. effects under `src/` own FXAA, sky, exposure, flashlight, lighting, and path tracing behavior. |
| shaders | UVSR direct DXC path | UVSR owns the compiler pin, dependency scan, checked loading, blob format, staging, and 27-family package inventory. captured controls replace the retired native comparison shader target. |
| UI | direct ImGui with a temporary host interface | UVSR owns context, input, styling, atlas upload and draw resources. `UIRenderer` retains only the Donut `IRenderPass` host interface. |
| package and launcher | UVSR | CMake owns the renderer package. the native C++ launcher verifies signed feeds and exact packages, then installs and starts `uvsr-engine.exe`. |

[`DirectDonut.cmake`](../cmake/DirectDonut.cmake) declares the retained
Donut targets without using Donut's root build. reviewed patches are staged in
the build tree. the `donut/` checkout stays pristine.

## runtime flow

`WinMain` validates startup policy, creates the Donut DirectX 12 device manager,
selects an adapter and creates the window and swapchain. the viewer mounts media
and shader roots, creates services and starts the scene worker. after UI
initialization, one borrowed binding enters the [frame sequence](../src/renderer_frame.md).
that page owns frame order, resource validity, completion and allocation phases.

[`uvsr_command_line.h`](../src/uvsr_command_line.h) publishes startup options only
after parsing succeeds. text borrows process-lifetime arguments; dimensions use
zero when unspecified, and adapter indices use minus one so adapter zero remains
selectable. [`settings_snapshot_code.h`](../src/settings_snapshot_code.h) exposes
the checked snapshot-code validation owned by the authoritative settings catalog.
the parser and this validation use explicit results without C++ exceptions.

the [settings snapshot owner](../src/settings_snapshot.md) keeps decoded byte
strings, canonical output and catalog payloads in checked storage. native lookup
takes an explicit location, and the controller publishes canonical text and its
fixed fingerprint together only after successful refresh. catalog persistence
uses a native byte writer that publishes only after write, flush and close succeed.

## lighting contracts

`WorldSpaceRepresentation` publishes one borrowed `RaySceneView` after its
TLAS and scene buffers are coherent. every ray technique borrows that view.
the [canonical scene owner](../src/renderer_scene.md#ray-selection) supplies
caster membership and material classification. [canonical GPU tables](../src/renderer_scene.md#gpu-tables)
provide ray geometry, shared raster/ray materials and canonical instance data.
the same affine encoder supplies raster and TLAS transforms. previous transforms
advance after successful visible submission; light-only changes do
not upload instances or refit the TLAS. private scene GPU owners supply mesh/texture
handles and descriptor slots. loading-only skin initialization retains its current shader.
`generation` invalidates bindings only when resource identity changes, while
`contentRevision` invalidates temporal or progressive history after in place
updates. techniques do not rebuild or duplicate the scene acceleration data.

`RenderTargets` publishes one single-sample raster `LightingSurfaceView`.
the deferred PBR pass consumes depth, material lobes, normals, emissive data,
and authored material AO. it writes completed scene-linear HDR directly to
`HdrColor`. material AO affects approximate environment lighting, not direct
light or emissive energy. ray traced sky visibility remains an independent
environment estimator with explicit diffuse/specular application.

there is no screen-space diffuse producer, source-radiance copy, diffuse-only
base-lighting image, private history, debug route, or dormant extension buffer.
`uvsr_lighting_history.cpp` owns environment assets and retained lighting-history
invalidation. path tracing borrows `RaySceneView` and shares material/light
semantics; it does not require a raster surface or run after the raster compositor.

future RT diffuse, RTAO, and surfel GI should borrow these scene and surface
contracts. each concrete producer owns its real outputs, history, validity, and
resource failure behavior. introduce a typed result only when its consumer is
implemented, with explicit units, covered directions, and frame validity. combine
lighting once at the frame's scene-linear composition boundary. do not build a
generic planner, duplicate G buffers or TLAS data, or allocate speculative outputs.
an AO result attenuates only its intended ambient response. indirect radiance is
added once. incomplete screen-space visibility remains unknown for RT or surfel
completion; multiplying independently completed AO images double-counts occlusion.

the [diffuse postmortem](postmortem/screen-space-diffuse.md) owns historical
methods and future acceptance criteria. its old controls are recovery evidence.

## future reconstruction

`uvsr_render_frame.cpp` owns frame composition. a future reconstruction call
belongs after completed scene-linear lighting/accumulation and before AgX,
FXAA, output transfer/dithering, and UI. it would return the image consumed by
display processing. `RenderTargets::Init` already distinguishes render size
from presentation size; no future-only buffers or framework are needed now.

`uvsr_render_setup.cpp` produces an unjittered, reverse-Z DirectX projection
with an infinite far plane. its near plane is `max(0.1, sceneDiagonal * 0.0005)`.
the planar view supplies matrices and viewport to rasterization. if a future
reconstruction needs jitter, introduce it here with an explicit previous/current
matrix contract and verify the selected SDK's conventions then.

the raster and path tracing passes produce no motion or temporal depth guides.
those resources and previous-view tracking were removed with their last consumer.

scene color is linear HDR. auto exposure supplies the multiplier consumed by
AgX; FXAA filters the resulting display-linear image. a future reconstruction
must specify its exposure normalization without changing these existing owners.
each lighting history owns its reset conditions; frame/scene lifecycle handles
resize and scene retirement. camera cuts, material/light/environment edits,
solution changes, and resource failure must invalidate the affected history.
borrowed frame inputs remain valid through their submitted GPU work, and retired
scene resources wait for completion. a future history gets its own owner and
failure/reset proof when implemented.

## state and identity

[`ui_settings_command_catalog.h`](../src/ui_settings_command_catalog.h) is the
typed authority for every represented name, domain, default, UI binding,
persistence class, and action. [`settings_snapshot_schema.h`](../src/settings_snapshot_schema.h)
owns persisted schema identity. UI rows, snapshots, diagnostics, the settings
hash, engine version, and packaged canonical settings must agree with those
sources.

settings application is transactional. parse and validate the full candidate,
apply selectors and values in their defined order, and roll back all changed
state on failure. session only values do not silently become persisted values.
old names are accepted only by explicit, bounded migrations.

## verification boundary

unit and source contract tests prove arithmetic, layouts, state transitions, and
wiring. direct DXC proves shader compilation and reflection. NVRHI validation,
the DirectX 12 debug layer, and PIX prove GPU lifetime and barrier behavior.
package validators prove staged and extracted inventories. only a provenance
bound smoke of the exact production package proves visible runtime behavior.

see [Build and Shaders](build-and-shaders.md) for build, shader, package, and
release gates. see [Performance](performance.md) for measurement evidence and
[Recovery](recovery.md) for historical source boundaries.

## change rules

keep dependency checkouts pinned and pristine. replace Donut owners with their
actual consumers, then remove unused adapters, patches and build dependencies.
preserve controls, behavior and legal material. pure engine contracts must not
expose NVRHI or native API types; keep current graphics implementation details in
named private adapters. a complete second renderer is not part of this removal.
