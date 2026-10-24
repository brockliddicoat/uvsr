# renderer scene

[`RendererScene`](renderer_scene.h) owns flat CPU scene records. its public
headers contain no parser, native graphics, vendor math, or owning container
types. the production import draft converts directly into this authoritative scene.
geometry/depth, ray selection, lighting and GPU tables read its records.
the migration belongs to the
[architecture map](../docs/architecture.md).

## ownership and publication

`Prepare` allocates one exact-capacity candidate. counts are checked against
`SIZE_MAX` and `PTRDIFF_MAX` before multiplication. every allocation uses private
`new (std::nothrow)` to establish live array objects and report failure; partial
allocation destroys the candidate. this is the owner's only retained standard
facility. there is no container growth or exception path.

`Write` and `WriteStrings` consume and copy inputs synchronously. they expose no
writable owner storage and reject writes after `Seal`. unwritten records retain
their declared defaults; defaults must pass the same validation as written
records. strings are UTF-8 byte ranges, not assumed zero-terminated buffers.

`Seal` validates references, ranges, reciprocal leaf identities, complete table
partitions, hierarchy coverage, cycles, and active numeric fields. its caller
supplies one checked workspace byte per node. failure leaves the unpublished
candidate available for correction or destruction. only successful `Publish`
with a nonzero generation exposes a read-only `View`. callers allocate a fresh
generation for each published replacement; publication is not an ID allocator.

indices are stable for one scene generation. no removals, table relocation or
hierarchy edits occur after publication. mutable commands require both a checked
generation and table index. views are synchronous, nonowning borrows. no consumer
may retain one across reset, replacement or move assignment. the lifecycle owner
must retire native consumers and stop borrows before destroying the CPU owner.

## traversal and derived data

each node stores a parent, first child and next sibling index. `Seal` produces
parent-before-child preorder plus an exclusive subtree end. it validates every
node exactly once. reverse preorder computes child-before-parent bounds and
content. propagation and destruction use loops, not recursive ownership or a
fixed-depth stack.

current TRS is authoritative. current local/world affines, node bounds,
subtree content and animation duration are derived. mesh bounds derive from
geometry unless `hasDeclaredBounds` preserves a finite imported envelope that
contains every geometry bound. the explicit flag keeps a failed `Seal` retry
from treating a previously computed bound as an input declaration. the retained
loader can supply a conservative envelope larger than the geometry union;
tightening it during conversion would change native culling semantics.
affines use double
row-vector math: `local * parent`. a node without a local transform copies its
parent's world affine directly. static quaternion components are not normalized;
this preserves imported transforms, including nonunit and zero values. animation
must normalize evaluated rotations before issuing a transform command.

buffer groups contain capacities and byte ranges, never GPU handles or copied
vertex payloads. mesh and geometry offsets count elements. geometry topology and
mesh type preserve imported representation; triangle arity and technique support
are consumer eligibility checks, not grounds to discard a whole scene. camera
validation reads only fields active for its kind and optional flags.

## transactions and temporal state

material, light and transform commands validate completely before changing live
values. transform scratch stores every proposed descendant world first, including
float-range and bounds checks. a failed command changes neither scene data nor
revisions. equal-value commands are no-ops. a full material batch validates every
index-ordered value, then commits once and refreshes derived domain content once.

`contentRevision` tracks visible authored changes. material, light and current
transform revisions identify their own changes. previous local/world affines are
independent temporal snapshots, not recomposed from current parents.
`AdvancePreviousTransforms` copies current affines only after successful visible
frame submission. it increments `previousTransformRevision` only when snapshots
change, without changing content or current-transform revisions. revision
overflow fails before mutation. loading, skipped and failed frames must not
advance snapshots.

`instanceTransformRevision` changes only when an instance world affine changes;
`previousInstanceTransformRevision` tracks its submitted snapshot separately.
light/camera-only changes leave both instance revisions unchanged. the GPU table
records their pair, so the first stopped frame uploads current/current after a
moving frame. later unchanged frames do no instance serialization or upload.
history resets compare `contentRevision` with the last successful visible
submission. loading and abandoned recordings acknowledge neither visible history
nor previous transforms.

## import and comparison controls

[`LoadImportScene`](renderer_import_load.md) produces one canonical scene, packed
geometry, decoded images and explicit runtime-light handles. the joined handoff
precedes GPU upload and derived draw/ray preparation. scene-dependent UI and visible
rendering wait for preparation submissions, collision completion and environment
readiness. queue ordering establishes readiness, not physical GPU completion.
the fixed structural/visible comparison remains required before accepting this
production cutover.

canonical instance indices address both the raster instance table and TLAS
instance IDs. obsolete native instance and geometry-table IDs do not constrain
conversion. hierarchy preorder remains independent of instance registration order.

material, White World, light and flashlight-pose commands directly commit canonical
values. Reset All reports
runtime-reset failure but remains best effort across independent settings.
animation metadata is retained; deliberately removed imported-animation playback
is not restored. the [resource owner](renderer_scene_resources_nvrhi.md) initializes
skinned current/previous positions once during loading, using the retained shader.
it has no visible-frame update or playback clock. retire consumers and GPU tables,
then release resources and canonical storage.

the former Donut graph adapter has retired. [captured material/light controls](../tests/renderer_scene_encoding_fixture.md)
and [GPU controls](../tests/renderer_gpu_fixture.md) retain its comparison data.
current GPU table tests construct copied canonical input and use the production
resource owner. these table and encoding tests need no mutable native mirror.

## geometry selection

[`RendererSceneDrawList`](renderer_scene_draw.h) owns checked storage for the
maximum geometry-instance count of one published scene. it allocates during
publication and releases before scene retirement. a per-view build borrows the
canonical scene synchronously, prunes preorder subtrees by content and bounds,
filters opaque/alpha-tested materials, and checks individual geometry bounds
for multi-geometry meshes without a skin prototype.

the G-buffer/depth and material-ID/depth passes share this selection. whole-mesh
chunks preserve the retained traversal boundary and sort iteratively by numeric
material, buffer and mesh IDs, node preorder, then geometry ID. sorting and view preparation
allocate no backing storage. optional rendering paths that do not request
geometry do no draw-list preparation per frame; its retained capacity is separate.

GPU resources come from the scene table owner. canonical `TexCoord0` corresponds
to the retained loader's `TexCoord1`. raster draws bind the canonical instance
buffer and use the canonical instance index as `startInstanceLocation`. material
picking carries canonical `selectionId` values.

## ray selection

[`RendererSceneRaySelection`](renderer_scene_ray.h) owns flat selected mesh,
geometry and instance records plus lookup/classification workspace. all storage
is sized from the published input and allocated at first active ray use or
selection replacement. preparation is transactional; failure retains the old
selection. reset releases it. views borrow storage until successful replacement
or reset, and contain only canonical indices.

each referenced triangle mesh is selected once. valid triangle geometry uses
canonical material domains: opaque commits directly, alpha-tested remains
nonopaque for shader candidate evaluation, and unsupported domains are omitted.
the sealed generation owns topology. only a material revision requires a
classification scan; roughness, color and other unchanged-class edits retain
the selection. transform changes retain membership and are consumed by instance
updates. inactive ray consumers do no selection scan or allocation.

`WorldSpaceRepresentation` realizes the selection with private native buffer
borrows. selected instances retain canonical registration order. the shared
affine encoder supplies both raster upload bytes and TLAS transforms; canonical
instance IDs are checked against the backend's 24-bit limit. the last built
descriptions are the derived transform snapshot. a changed current-instance
revision compares selected transforms before refitting; previous-only,
light/camera-only and unselected-instance changes need no TLAS refit. BLAS data is
static after initial skinning. native resource mismatches fail the generation.
backend description containers still require the later exception/storage cleanup;
the pure selection owner does not.

ray alpha evaluation retains explicit LOD 0, while raster uses its existing
derivative-selected sampling. the two sampling rules remain unchanged.

`ClassifyPathTracingSceneDomain` checks current canonical material values separately
from ray-caster selection. transmission, SSS and hair remain unsupported; blended
geometry is omitted. the application also checks uploaded buffers. material edits
therefore change path eligibility immediately without a native material mirror.

## GPU tables

[`RendererSceneGpuTablesNvrhi`](renderer_scene_gpu_nvrhi.h) privately owns one
scene generation's material and instance buffers, optional ray geometry buffer and upload
scratch. [`the nonallocating encoders`](renderer_scene_encoding.h) consume plain
canonical records and backend-resolved descriptor slots. they preserve material
flags, domain/opacity/cutoff behavior, texture selection and geometry byte ranges.
invalid ranges, descriptor indices and overflowing emissive products fail before
changing encoded output. assigned unavailable textures retain descriptor `-1`;
ray sampling guards that sentinel and raster retains its existing fallback.

one 112-byte `InstanceData` contains four integer fields and current/previous
48-byte affines. checked geometry prefixes follow canonical instance order;
mesh geometry ranges and curve flags retain the shader ABI. affine encoding
rejects nonfinite or out-of-float-range values before writing output. the instance
buffer and scratch each have exactly the published instance count.

one 256-byte `RendererMaterialTableEntry` contains the unchanged 208-byte
`MaterialConstants` layout. ray shaders read structured entries, and raster binds
256-byte constant-buffer ranges from the same allocation. canonical material
indices select both paths. the embedded picking ID comes from immutable canonical
`selectionId`, preserving existing numeric snapshot identity independently of table order.
a material revision refreshes encoded values and raster texture
bindings; unchanged frames do no material serialization or upload.

all material/instance/texture arrays are sized from the validated scene at preparation.
private `new (std::nothrow)` supplies checked live objects; failed replacement
preserves the prior owner. geometry scratch and its buffer are prepared only for
an active ray consumer. geometry scratch is released after successful submission,
because command recording has copied its bytes into backend upload storage.

`BeginRecording` discards uncommitted revision markers. recorded data is ready for
later consumers in that command list. `CommitRecording` advances durable upload
revisions only after a nonzero queue submission; an abandoned list must record
again. allocation alone is not readiness. the application treats recording and
submission errors as terminal, then waits before teardown. adding recoverable
render retries also requires transactional loading-stage and acceleration-build
progress, which currently advance during recording.

native texture and buffer objects retain descriptor handles until GPU retirement.
the descriptor manager outlives deferred ray preparation. reset these tables after
completion and before releasing the bridge/native world. NVRHI remains the single
resource-state and command-liveness authority. Donut's material/geometry CPU tables,
GPU tables and per-material constant-buffer producers are removed from the active
build overlay. native instance CPU/GPU tables and upload producers are also
removed. loading-only skinning and mesh/texture/descriptor creation remain until
the importer/resource migration.

## editing and camera collision

material selection and focus use generation-checked scene handles. numeric
selection IDs, unique names and the no-selection sentinel retain their existing
command meanings. material panels read canonical values, names and texture paths;
their edits commit through the same typed transaction used by snapshots. a stale
pick readback cannot select a material or node in a replacement scene. the second
pick lane indexes the canonical instance and its node directly.

[`RendererSceneLightRange`](renderer_scene_light.h) borrows one scene for a
frame or UI operation. ordinals put the explicit flashlight handle first, then
preserve canonical order. inactive submission excludes the flashlight before the
deferred light cap. stale handles reject instead of referring to a replacement.
pose queries use canonical world affines. pose commands preserve the native
parent-space decomposition and commit one checked transform transaction. the
light encoder initializes every ABI field, including unused shadow sentinels;
native shadow-map/channel state without a canonical consumer rejects conversion.
`RaySceneView` carries both the scene generation and acceleration generation.
light/history validity uses the former; acceleration bindings use both.

[`CameraCollisionWorld`](camera_collision.h) owns plain triangles and BVH nodes.
its public inputs expose no native math, graphics or owning container types.
`BuildFromScene` borrows canonical geometry, instance and world-transform records,
plus packed CPU position/index spans. default canonical instance order preserves
imported order; an optional fixture permutation is checked completely. it uses
prototype geometry for skinned instances. GPU alignment may pad the position
span; each geometry's vertex count bounds local indices and proves the required
bytes exist. padding and adjacent primitives cannot supply extra vertices. broken
source ranges reject the candidate; malformed individual index triples and
nonfinite/degenerate triangles are omitted in stable order. allocation failure
preserves the previous world. `Clear` retains capacity; replacement and destruction
release it. queries allocate nothing.

collision preparation follows the one canonical conversion on the existing
worker. the loader retains its packed geometry owner, including all uploaded
attribute/morph bytes and initial joint matrices, through collision preparation. camera
animation and visible rendering stay suspended while the worker borrows them.
the render owner joins, releases that geometry owner, moves the completed collision world
and frames the camera from canonical bounds. failure or cancellation retires any
submitted GPU resources before discarding the candidate; retry starts a new import.
scene replacement transfers the old BVH to the next importer worker for destruction.

collision uses plain points and private float bounds. component division and
second-argument min/max ties preserve contact and partition behavior. conversion
checks finite values and float range before narrowing canonical double affines.
its retained `std::nth_element` operates on raw pointers with a nonthrowing
comparator. the pinned MSVC 14.44.35207 partition/insertion-sort path is iterative,
in-place and allocation-free; re-audit that implementation when changing toolsets.
this is implementation evidence, not a stronger standard-library guarantee.

collision is a prepared geometry snapshot. current application transform commands
serve light poses; mesh-transform editing and imported playback are absent.
a consumer adding moving mesh geometry must prepare replacement collision data
before querying it. scene loading owns the current collision replacement and
source-array lifetime; no collision query borrows the mutable scene.

## boundary and invalidation

public scene, draw/ray selection, encoding and collision headers expose plain
records and synchronous borrows. `RendererSceneDonut` and
`RendererSceneGpuTablesNvrhi` are private adapters; their native types must not
enter those headers or importer output. shader ABI packing remains separate from
canonical CPU storage. backend descriptor slots are resolved at encoding time.

| change | canonical revision | derived work |
| --- | --- | --- |
| material values | material and content | re-encode material data and refresh raster texture bindings. ray membership changes only when classification changes. |
| light values | light and content | encode canonical light values and reset affected lighting history. instance tables and ray membership remain valid. |
| current transform | transform and content, plus instance revision when an instance world changes | recompute subtree bounds. active draws rebuild from the current view; instance upload and selected TLAS transforms use the narrow instance revision. collision replacement is explicit for a consumer adding mesh motion. |
| submitted previous snapshot | previous-transform revision, plus previous-instance revision when its world snapshot changes | upload settled previous instance data on the next recorded frame. current scene content, ray membership and TLAS transforms are unchanged. |
| scene replacement | fresh nonzero generation | retire old GPU consumers and end borrows, then replace every scene-derived owner. stale commands and selections reject the old generation. |

recorded GPU readiness applies only to that command list until submission commits
its revisions. CPU publication, allocated buffers and recorded acceleration work
do not establish completed GPU lifetime. the lifecycle owner controls the loading
handoff and retirement; NVRHI controls command/resource lifetime.

## checks

the [portable consumer](../tests/renderer_scene_portable_tests.cpp) links only the
scene library and collision implementation. it prepares a canonical scene,
derives raster/ray selections, encodes shader records, sweeps a camera sphere and
checks material/transform/replacement invalidation without graphics or parser
headers and libraries. its compiler include output records the actual header
closure. this proves the CPU scene boundary; implementing another graphics backend
still requires its own resource, shader-binding and execution proof.

the concrete library and its tests compile without C++ exceptions. the
[owner tests](../tests/renderer_scene_tests.cpp) exercise copied preparation,
publication, invalid input, all allocation-failure positions and atomic edits.
100,000-node deep and wide hierarchies check every node's order, current affine,
independent previous snapshots, two edits before commit and cleanup on a measured
64 KiB thread stack. expected translations follow a closed-form sum of exact
binary fractions. [captured math controls](../tests/renderer_scene_math_fixture.md)
retain 96 independently evaluated transformed hierarchies, bounds and affine
comparisons without a Donut include or link dependency.

[geometry import controls](../tests/import_geometry_fixture.md) retain shared
meshes, buffer ranges and conservative bounds. [scene import controls](../tests/import_scene_fixture.md)
retain hierarchy, camera, light, animation and keyframe metadata, copied ownership,
malformed input and allocation retries. the [encoding controls](../tests/renderer_scene_encoding_fixture.md)
compare every material domain and texture-enable mask, including SSS/hair variants,
and exact light frames and poses. [material-mode tests](../tests/renderer_scene_material_mode_tests.cpp)
retain authored values and restore exact GPU bytes after mode changes.
focused CPU checks do not establish visual equivalence; the
[validation contract](../docs/validation.md) owns runtime cases and evidence.

[draw fixtures](../tests/renderer_scene_draw_tests.cpp) check culling, material
filtering, numeric order, whole-mesh chunks, generation and allocation failure
against fixed frustum controls. [ray-selection fixtures](../tests/renderer_scene_ray_tests.cpp)
cover shared meshes, supported/omitted domains, primitive counts, classification
edits, unchanged roughness/transform selection, generations, malformed ranges,
allocation failures and 100,000 instances.

[GPU table fixtures](../tests/renderer_scene_gpu_tests.cpp) read actual material,
geometry and instance buffers. they check different input/canonical orders,
immutable selection IDs, shared CBV/SRV ranges, six allocation failures,
abandoned/submitted recording and moving/stopped instance bytes. a candidate has
its own descriptor manager because a slot has one releasing owner. an occupied
table rejects preparation; failed candidates leave the original table usable.
shader reflection separately verifies the 256-byte material entry and field offsets.

[skin fixtures](../tests/import_skin_gpu_tests.cpp) compare initial current/previous
positions and all other retained output attributes with captured GPU bytes.
joint edits and repeated completed upload steps leave that static output intact.
a test-only ray shader derives rays from uploaded instance data and checks the
production TLAS's hit IDs, distance, motion and material identity. light-only and
previous-only edits do not refit the TLAS. the probe shader stays outside the
runtime inventory. short-lived fixture command lists wait for GPU completion
before releasing acceleration scratch; production retains its frame list.
