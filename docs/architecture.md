# architecture

this document describes the current renderer architecture. UVSR is a Windows 11, DirectX
12 renderer. ImGui and one product feature set are permanent. developer builds
add tests and diagnostics, not alternate rendering features.

Donut is an intentionally retained architecture dependency. direct ownership
work complements its framework services. the executable links `donut_app`,
`donut_engine`, and `donut_render`; their runtime calls, shaders, patches, and
nested dependencies are supported boundaries, not a detachment backlog.

## ownership map

| area | current owner | boundary |
| --- | --- | --- |
| process startup and identity | UVSR | [`engine_startup.cpp`](../src/engine_startup.cpp), [`engine_identity.h`](../src/engine_identity.h), and generated version resources own startup checks and the source, settings, configuration, and release identity. |
| window, device, and message loop | Donut with UVSR policy | [`uvsr.cpp`](../src/uvsr.cpp) uses Donut `DeviceManager` and `ApplicationBase`. GLFW remains behind `donut_app`. UVSR owns adapter policy, DirectX 12 requirements, Agility SDK checks, and failure reporting. |
| GPU abstraction | direct NVRHI | The direct `third_party/nvrhi` pin owns resource, command list, descriptor, query, and ray tracing interfaces. only its DirectX 12 backend is enabled. |
| scene, VFS, view, and draw model | Donut with UVSR lifecycle | Donut still owns the scene graph, glTF import, texture cache, VFS, camera and view types, draw strategy, and several geometry contracts. [`uvsr_scene_lifecycle.cpp`](../src/uvsr_scene_lifecycle.cpp) owns UVSR load, promotion, and retirement policy. |
| render targets and focused passes | UVSR | `renderer_*` owners implement targets, shader loading, common passes, geometry integration, readback, logging, scene work, and retirement. effects under `src/` own FXAA, sky, exposure, flashlight, lighting, and path tracing behavior. |
| shaders | UVSR direct DXC path | UVSR owns the compiler pin, dependency scan, blob format, staging, and package inventory. a small set of packaged framework shaders still comes from Donut sources. |
| UI | direct ImGui plus Donut integration | UVSR pins and patches ImGui directly. `UIRenderer` still derives from Donut's ImGui renderer and material editor integration. |
| package and launcher | UVSR | CMake owns the renderer package. the native C++ launcher verifies signed feeds and exact packages, then installs and starts `uvsr-engine.exe`. |

[`DirectDonut.cmake`](../cmake/DirectDonut.cmake) declares the retained
Donut targets without using Donut's root build. reviewed patches are staged in
the build tree. the `donut/` checkout stays pristine.

## runtime flow

`WinMain` validates startup policy, creates the Donut DirectX 12 device manager,
selects an adapter, creates the window and swapchain, registers the scene viewer
and UI render passes, and enters the message loop.

the viewer then:

1. mounts packaged media and shader roots;
2. creates the direct and retained shader, cache, descriptor, and pass
   services;
3. loads a retained scene through the background scene worker;
4. uploads bounded scene work, promotes one complete scene, and retires the old
   scene only after GPU use is safe;
5. updates camera, settings, lights, and invalidation state;
6. renders geometry and material data, ray visibility and lighting,
   retained accumulation and exposure passes, display processing, and ImGui; and
7. presents through the current Donut device manager.

the order is a resource contract. a producer must publish only complete finite
data. consumers must not read resources before their producer and required
barriers complete. resize, scene change, camera cut, material or light change,
and relevant setting changes invalidate the histories they affect. a failed
replacement must leave the previous valid resource or fail closed. it must not
publish partial state.

## lighting contracts

`WorldSpaceRepresentation` publishes one borrowed `RaySceneView` after its
TLAS and scene buffers are coherent. every ray technique borrows that view.
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

keep Donut pinned and pristine. improve UVSR-owned code at explicit boundaries
without replacing Donut framework services. delete only proven first-party
duplication or unused adapters. do not create a detachment path or add needless
coupling.

keep direct NVRHI as the current GPU boundary within the Donut integration. a
later direct D3D12 rewrite or change to Donut's role is a separate user
decision. preserve all protected features and assets while changing ownership.
