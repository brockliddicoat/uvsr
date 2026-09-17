# frame

the frame owner orders work; scene and technique owners retain their data and
algorithms. [architecture](../docs/architecture.md) is the module index.

## entry and dependencies

[`RunDonutApplicationFrame`](renderer_frame_donut.cpp) is the current outer
sequence. [`DonutApplicationFrameBinding`](renderer_frame_donut.h) borrows the
scene, UI and host only while the message loop runs. it installs one plain
callback and clears it before those owners are released. it is noncopyable and
allocates no storage. registration in Donut now routes input, resize, DPI and
unfocused-policy queries, not an extensible render schedule.

this is a private, temporary Donut/NVRHI adapter, not a portable public API. its
single friend function accesses the existing shell without a second window or
device owner. remove that bridge with the Donut application replacement. its
retained `std::this_thread::sleep_for` and `std::chrono::milliseconds` preserve the
old end-of-attempt yield until that replacement; they are not allocation helpers.

## sequence

the outer function shows the actual calls and branches. input polling and
window-size processing remain in the host message loop before entry.

1. check shell failure, reset disposition, sample elapsed time, process joysticks.
2. for a visible render attempt, deliver DPI changes, animate camera/flashlight,
   then animate UI. animation precedes the backbuffer fence wait. a visible idle
   unfocused attempt animates only opted-in owners; a minimized attempt neither.
3. wait for the backbuffer, then call scene Render. its
   [lifecycle](uvsr_scene_lifecycle.cpp) handles retirement, import handoff and
   bounded upload before entering [`RenderScene`](uvsr_render_frame.cpp).
4. the scene sequence prepares targets/view, updates scene buffers and world
   representation, prepares lighting inputs and history/sample scheduling,
   then selects path transport or geometry, ray visibility and deferred lighting.
   material picking, environment background, accumulation, exposure, tone mapping,
   optional FXAA and output transfer follow. submit the scene command list before
   completion/readback work.
5. check the result before [UI](uvsr_ui_renderer.cpp). UI edits normally affect
   the next scene frame. UI captures/composites pixel zoom and draws ImGui into
   its own framebuffer view of the same backbuffer.
6. check the result again, run the pacing callback, then native Present. on
   success run the post-present callback. collect retired backend allocations
   and advance elapsed-time bookkeeping and the attempt index.

Pending skips UI and presentation but still advances the attempt index. Failed
returns before end-of-attempt bookkeeping. missing path prerequisites deliberately
submit black with a presentable result so the user can restore settings.
[`renderer_shell_failure_policy.h`](renderer_shell_failure_policy.h) owns these
result meanings. no nested callback scheduling is added.

## data and completion

| data | owner, producer and consumer | validity |
| --- | --- | --- |
| camera/view | scene camera, then [`SetupView`](uvsr_render_setup.cpp); geometry, rays and display passes | current render attempt, unjittered reverse-Z; no previous-view or motion-guide resource exists |
| scene transforms | canonical commands, buffer upload, then world representation and lighting | current changes retain the previous snapshot until successful visible submission |
| raster surface | [`RenderTargets`](renderer_targets_nvrhi.h), geometry; visibility and deferred PBR | current attempt after geometry, until target replacement; absent in path transport |
| scene-linear color | PBR writes HdrColor, or path transport writes its result; background/accumulation, exposure and AgX consume it | a complete selected transport result only |
| accumulation | [`LightingAccumulationPass`](lighting_accumulation_pass_nvrhi.h), prepare token and resolve | its owner validates extent/epoch/token; failed or abandoned work invalidates history |
| display color | AgX writes LdrColor, optional FXAA, output transfer, UI, Present | current backbuffer; UI receives the no-depth view independently of scene |

[`uvsr_lighting_history.cpp`](uvsr_lighting_history.cpp) owns view/domain
invalidation. camera, content, material/light/environment, transport and resource
changes reset affected history. do not refresh scene transforms in Animate: an
animate-only attempt must not consume changes needed by render/history work.

`FrameExecution` borrows only for one scene attempt. recording a resolve commits
CPU history indices, not GPU completion. an abandoned recording invalidates that
history through the existing epoch. open and closed-but-unsubmitted failures clear
recorded readback state, pending GPU table uploads and the recording's timer slot.
the timer index advances only after a nonzero submission. failure remains terminal;
the closed NVRHI list retains its resources until teardown and is not reopened.
submitted work keeps its ownership even when a later notification or GPU error
reports failure. submission retains referenced backend resources; scene
retirement waits through [`RendererSceneRetirement`](renderer_scene_retirement.h).
readback has separate completion. screenshots taken by scene completion exclude UI.

pass preparation distinguishes pending work from terminal failure at both the
loading and synchronous callers. failure leaves the current preparation stage in
place, and checked replacement construction preserves the preceding owner.
selected FXAA, geometry and AgX are required at their consuming stages. inactive
capability checks retain their existing gates. outer allocation checks do not
make constructor-internal NVRHI storage allocation recoverable.

scene upload advances its public phase only after command submission and required
preparation checks succeed. an unsubmitted failure clears pending table revisions;
it cannot publish scene readiness or start the next CPU worker.

## allocation phases

the current scopes below are names and attribution boundaries, not measured
allocation counts. no zero-allocation claim exists yet.

| phase | scope and exclusions |
| --- | --- |
| `frame-update` | scene Animate and camera/flashlight updates; UI Animate is separate |
| `frame-prepare` | targets, view, scene refresh/upload, world/environment preparation |
| `frame-record` | sample scheduling, selected transport, picking, resolve and display output |
| `frame-complete` | scene submission bookkeeping and requested readback/capture |
| `ui` | UI Animate/Render, fonts, pixel zoom and controls |
| `present` | backbuffer wait, pacing, native Present and backend garbage collection |

attribute startup, import, scene activation, first use, resize, shader reload,
scene mutation/refit and diagnostic capture separately from warmed fixed-state
frames. `FrameExecution` is stack scratch and borrows an ordered canonical light
range. `PathTracingPass` owns its light upload scratch with the same checked
capacity as its GPU light buffer, grown only at first use or a larger scene.
private `new (std::nothrow)` establishes array object lifetime and reports failure;
failure preserves the preceding buffer and scratch. destruction releases the array.
encoding and submission reuse that storage. temporary PBR binding descriptions
still grow, and backend caches do not remove those CPU container operations.
scene-sized traversal/refit scratch must use scene-derived capacity.

disabled ray producers check selection before construction; inactive world
representation returns before traversal. disabled accumulation and auto exposure
still retain preparation-time resources. stable IBL preparation returns without
new preprocessing. these are distinct from warmed-frame dispatch or heap growth;
later removal must preserve enable/disable behavior.

the warmed measurement scope requires fixed scene, camera, resolution and settings,
no pending preparation, pick, capture, reload or content change, and 120 stable
submitted frames. measure the next 600 submitted frames in three runs, separating
raster optional-off, active ray and path configurations. count aborted/pending
attempts separately. report allocation/free calls, bytes and high-water use by
phase and owner. C++ allocation hooks alone cannot prove total CPU/GPU residency.

## checks and examples

[`RenderScene`](uvsr_render_frame.cpp) is the current ordering example;
[`FrameExecution`](uvsr_render_frame.cpp) demonstrates borrowed lifetime and
abandoned-recording cleanup. [`PrepareAttempts` and `Resolve`](lighting_accumulation_pass_nvrhi.cpp)
are the current temporal transaction example. use the
[required developer and runtime checks](../docs/build-and-shaders.md), including
controlled captures, and prove a private frame edit does not compile the importer.
