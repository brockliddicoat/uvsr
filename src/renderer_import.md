# scene import

the [canonical scene](renderer_scene.md) is the runtime CPU authority. parser
objects, source buffers and vendor containers belong to one import operation.
production uses the fastgltf path below. image decoding, GPU upload, application
scene descriptions and scene publication have separate owners.

the [decoded image owner](renderer_import_image.md) records codec selection,
pixel/layout ownership, checked capacities and remaining vendor allocation limits.
the [CPU loading transaction](renderer_import_load.md) connects those owners to
checked native file I/O, partial-model diagnostics, cancellation and joined worker
handoff before GPU recording.

## parser configuration

[DirectFastgltf.cmake](../cmake/DirectFastgltf.cmake) pins fastgltf 0.9.0 at
`f89e438230b6624d5e886fac0d1829b7c7299b2e` and simdjson 4.6.2 at
`61641f9a7aedc762d3b1c049a2b2a44d1c6db8a2`, with archive SHA-256, complete source
tree digests and license hashes. the supplied static simdjson target prevents
upstream package discovery or downloads of an unpinned transitive version.
the libraries are excluded from the default build until a real consumer links them.

both compile as C++17 with exceptions disabled. simdjson uses its supported
`SIMDJSON_EXCEPTIONS=0` mode. MSVC uses `_HAS_EXCEPTIONS=0` and `/EHs-c-`;
a [forced compile guard](../cmake/RequireNoCppExceptions.h) checks every vendor
translation unit. the upstream fastgltf target's `/EHsc` option is removed.
float fields, the default memory pool and ordinary vendor vectors retain their
upstream configuration. custom SmallVector, double-default fields, C++ modules,
experimental physics, upstream tests/examples and installation are disabled.

vendor STL stays private to parsing and conversion. keeping upstream
containers preserves supported parser ownership and decoding behavior; replacing
them is not a second container-library project. simdjson and explicit fastgltf
error results handle malformed data and selected allocation failures. other
vendor/standard-library allocations remain fatal on exhaustion in this mode;
fastgltf's `raise` helper aborts when exceptions are disabled. an error result
does not prove every allocation is recoverable. first-party counts, byte ranges,
workspaces, I/O and publication still require checked outcomes.

[parser tests](../tests/import_parser_tests.cpp) exercise owned data after input
and parser destruction, explicit malformed/version/required-extension errors and
reuse after rejection. parsing an extension does not implement its rendering
semantics. the importer must enable only implemented behavior and reject
unsupported required extensions explicitly.

## private document and accessor conversion

[`ImportDocument`](renderer_import.h) owns one loading operation. its public
contract contains plain metadata, indices and borrowed views; fastgltf and simdjson
types occur only in the implementation and [its private state header](import/renderer_import_private.h).
successful parsing replaces the
previous document. failure leaves it usable. move transfers the owner, and reset
is idempotent. the [production loading transaction](renderer_import_load.md)
converts and releases this document before GPU recording and visible publication.

the input view is borrowed only during `Parse`. a checked GLB envelope precedes
vendor reads. a temporary simdjson DOM validates version/extension declarations,
base64 shape, raw and decoded URI delimiters, component widths, node-object shape,
incompatible matrix/TRS declarations and dangerous bounds before fastgltf parsing. the
private [metadata preflight](renderer_import_metadata.cpp) also validates consumed
extension shapes, finite values, texture references, sampler enums, camera fields,
light references and animation targets/modes. that
DOM dies first; the second parse is justified by the pinned parser's unchecked
base64 and URI assumptions. it is loading work, with its cost included in later
import measurements. JSON-only GLB files use the JSON entry point, and unknown
trailing chunks are ignored. truncated, overflowing or misplaced known chunks
fail explicitly. the vendor JSON nesting limit is 1,024; focused fixtures accept
depth 1,000 and reject depth 1,100 without first-party recursive traversal.

embedded base64 and GLB bytes belong to the retained private asset, which survives
the parser and input buffer. a checked, fixed buffer table is sized from the parsed
buffer count. `SupplyBuffer` copies exactly the declared external length into
checked storage before replacing old bytes; callers may release their input on
return. `BufferInfo.uri` borrows the document's UTF-8 URI with percent escapes
decoded once. it must not be decoded a second time. file resolution and image
decode belong to the loading transaction; deferred GPU upload uses its owned
output. no byte view escapes to a deferred consumer in this layer.

accessor metadata validates component/shape compatibility, nonzero counts, output
byte multiplication, view indices, subtraction-based ranges, offsets, strides,
matrix column alignment and sparse descriptors. sparse indices are checked for
strict increase and bounds after their buffers become resident. reads require an
exact count of live output scalars. unresolved buffers, invalid sparse data and
non-finite decoded float values leave that output unchanged. integer reads accept
only unsigned, non-normalized scalar/vector sources, avoiding float-to-integer
narrowing. material, primitive, node and animation semantics are validated by their
later converters; a parsed document alone is not a publishable scene.

the adopted helper is the callback overload of `fastgltf::iterateAccessor`, through
a private buffer adapter that is called only after validation. its loops merge
sparse values, honor stride and normalize components. a plain aggregate element
trait retains column-major matrix order and zero initialization for accessors
without base storage. the range check permits the specification's omitted final
matrix padding. neither the recursive scene helper nor the unchecked default
buffer adapter is called. callbacks only inspect or copy scalars. float-source
reads make a finite-value pass before writing, without allocating a second full
output buffer. this extra pass is part of the conversion cost.

private `std::byte`, `std::string_view`, `std::get_if` and `std::move` adapt the
vendor API without adding owning collections. placement construction starts the
asset/table object lifetimes in checked storage. the vendor asset owns its existing
flat arrays and PMR pool; destruction releases external copies and then those
arrays. a checked byte per source node retains authored transform/name/light
presence that the parser's value representation can lose, including an authored
identity transform. a checked byte per source camera distinguishes an authored
empty name from an omitted name. no recursive runtime tree is created here. document, node
metadata, fixed buffer table and external byte-copy allocations have checked
failure and retry fixtures; this does not change the vendor exhaustion boundary.

[`import_accessors_tests`](../tests/import_accessors_tests.cpp) checks exact
strided, normalized, sparse and padded matrix values; owned JSON/GLB/base64/external
data; source destruction and moves; malformed data, output preservation and
allocation recovery. its explicit asset-scan mode resolves only the fixed local
fixtures and converts every Bistro and San Miguel accessor. that mode checks
decoder coverage, not structural/rendering equivalence or general filesystem I/O.

## geometry and hierarchy conversion

[`ConvertImportScene`](renderer_import_scene.h) writes one canonical CPU scene,
one move-only geometry owner and, when referenced textures exist, one image owner.
all supplied outputs must be empty. a local candidate owns
every intermediate allocation; errors destroy it and preserve the supplied
document and outputs. after success, the document and all source inputs may die.
canonical records, names, indices, attributes, morph POSITION payloads and initial
joint matrices are copied into their named owners. CPU record publication makes
those tables readable; the loading owner still controls GPU preparation and
visibility of the complete scene.

the converter validates the entire node forest before selecting a scene. parent,
child and sibling links support iterative cycle checks and preorder traversal.
multiple parents, duplicate children/roots, cycles, missing indices, disconnected
skin joints and exhausted workspaces return explicit errors. the chosen scene is
the explicit index, otherwise the default scene, otherwise the first scene. an
asset with nodes but no selectable scene is rejected. a synthetic file root keeps
the retained name and root order.

source mesh order determines the shared geometry payload, including unused meshes.
canonical meshes follow first selected instance use, followed by one skin-prototype
level. instances follow hierarchy preorder. the retained loader builds an orphaned
tree first; attachment of that completed tree, after both skin and static leaves
exist, determines its instance registration order. the earlier native canonical
bridge still handles arbitrary runtime registration order independently.

triangles, lines and line strips preserve authored uint8/16/32 indices or generated
source-local indices. component maximum values reserved for primitive restart and
indices outside the primitive vertex range are rejected. attribute count/type and
joint/weight pairing checks precede allocation or reads. decoded values use the
checked accessor path, including normalization, stride and sparse values. positions,
UVs and indices retain exact values; normal/tangent packing and generated tangent
handedness retain the native formulas. matrix nodes retain the native float-to-double
affine decomposition before canonical transform propagation.

mesh bounds retain the declared conservative envelope including the origin.
primitive bounds retain the old importer conventions. `_RADIUS` values are unioned
as scalar points; morph POSITION deltas are unioned directly. these are compatibility
limits, not general swept-curve or active-morph bounds. joint/weight selection keeps
the last corresponding source attribute set, as the retained loader does; it does
not combine eight or more influences. morph NORMAL/TANGENT channels remain ignored
and imported morph/animation playback remains absent. the flag denotes retained
metadata, not enabled animation.

mixed radius data uses a full zero-initialized array when any primitive supplies it.
the old importer clears that entire array on a missing radius and can write outside
it for later primitives. likewise, absent static joint/weight lanes now contain
zeros instead of uninitialized native vectors. morph ranges identify distinct
model-sized float4 frames, with zeros outside each mesh and for targets without
POSITION. this removes the old per-mesh offset restart and empty-frame hazards
without enabling playback.

each skin instance has a derived layout, a shared index owner and checked initial
joint matrices. its geometry view deliberately has no CPU vertex bytes. the GPU
consumer must allocate the declared layout and initialize current and previous
positions together before render publication. CPU skinning changed float rounding
in controlled nontrivial poses, so vertex deformation stays on the GPU. the import
owner validates joint indices and finite position arithmetic first. matrices and
prototype bytes borrow from `ImportGeometry` until upload has copied them; reset,
move assignment or destruction requires all upload/collision borrows to have ended.
GPU resource retirement is the backend owner's separate obligation.

all growing first-party storage is fixed during loading:

| storage | capacity and lifetime | exhaustion |
|---|---|---|
| node plans and source orders | source node count, plus one file-root order slot; includes auxiliary placement indices and three animation target stamps per node; conversion-local | checked scratch budget or allocation error |
| mesh/primitive plans | validated source counts; canonical mesh maximum is source meshes plus selected skin instances | checked count, scratch or allocation error |
| buffer plans | selected skin instances plus one shared source group | checked scratch or allocation error |
| scalar/tangent scratch | largest primitive scalar/index count, full inverse-bind accessor, or one animation sampler's times plus output scalars; tangent arrays only when generated | checked multiplication, scratch or allocation error |
| seal workspace | canonical scene's checked workspace query | explicit workspace error |
| geometry groups and palettes | exact selected group and canonical joint counts; geometry owner | checked geometry budget or allocation error |
| packed payloads | 16-byte aligned validated source attributes and model-sized morph frames; aliased indices owned once | checked byte/alignment limits, geometry budget or allocation error |

`maxScratchBytes` limits conversion-local heap workspace. `maxGeometryBytes` limits
the geometry owner, group records, palettes and owned bytes. neither includes the
parser, canonical scene, allocator overhead or stack. accounting reports requested
bytes, while the separate canonical owner reports its storage. vendor containers
remain private. `std::get_if`/`string_view` adapt the vendor representation;
type traits and placement construction check/start scalar lifetimes in fixed
storage. these uses avoid a second variant/string/lifetime implementation and add
no owning STL collection to the converter.

[`import_geometry_tests`](../tests/import_geometry_tests.cpp) checks malformed
inputs, exact-capacity limits, allocation failures with retry, parser destruction,
96 captured matrix controls and complete 100,000-node deep/wide graphs on a measured
65,536-byte stack. [the captured loader reference](../tests/import_geometry_fixture.md)
compares names, hierarchy, registration order, meshes, bounds, indices and authored
attributes. [the GPU fixture](../tests/import_skin_gpu_tests.cpp) compares six
initial palettes and 36 consumed output ranges using independently uploaded converted
data and the retained shader. its real queue completion precedes resource release.
production upload/skin dispatch remains an explicit integration consumer below.

## material and texture conversion

materials and their original values live in the canonical scene. the converter
processes all source materials in authored order, including unused ones, because
their texture requests can determine the color space of later shared images.
selected mesh order then establishes the canonical first-use material table.
`materialIndexInModel`, `modelFileName` and the authored name retain source identity;
`selectionId` is an import-local first-use index. the retained loader assigns its
numeric picking IDs through a pointer-keyed unordered map. those numbers are not
stable identifiers across separately allocated imports. comparisons match source
materials first and translate only that picking ID before comparing constants.

metallic-roughness, specular-glossiness, six alpha/transmission domains, emissive
normalization/strength, normal and occlusion strength, two-sided state and NV
subsurface/hair values feed the existing raster/ray material encoder. authored
presence matters: a missing metallic-roughness object retains the old zero defaults;
an empty object uses glTF defaults. absent normal/occlusion texture views have
strength zero in authored materials, while the separate empty fallback material
keeps its constructor value one. optional texture-coordinate/sampler metadata keeps
the renderer's existing UV0 and sampler policy. only normal-map scale transforms
affect material values.

fastgltf loses some presence information and ignores errors returned by its material
extension parser. checked preflight metadata retains those distinctions, authored
image names/MIME strings and the three NV extensions it cannot parse. unsupported
required names fail before the vendor call. after validating every required name,
the importer hides only the root `extensionsRequired` key in its owned parse copy
when NV requirements are present. the validated requirement mask remains private
metadata; all other keys, chunk lengths and caller bytes stay intact. escaped keys
and JSON/GLB inputs are checked. required unsupported texture transforms and NV
subsurface transmission-color textures fail explicitly. required `NV_texture_swizzle`
needs a primary image because the retained contract permits regular-image fallback,
and the renderer does not implement multichannel recombination. optional swizzle-only
records retain their empty texture plus source/channel metadata.

[`ImportTextures`](renderer_import_scene.h) owns fixed image views, source requests,
the canonical-to-request map, contiguous swizzles, strings and copied encoded embedded bytes. it has no
parser, decoder or graphics types. texture indices match the canonical scene;
image/swizzle indices belong to this owner. views borrow until reset, move assignment
or destruction. a loading operation may release the document immediately after
conversion, but decoding/upload must finish every borrow before releasing this
owner. the scene keeps its own UI path/MIME strings; the temporary image owner dies
after its consumers finish.

base/diffuse, specular-glossiness and emissive request sRGB. metallic-roughness,
normal, occlusion and transmission request linear. the first request wins for a
shared source image or external path. separate embedded images with equal names
remain distinct. glTF texture objects share their selected image and merge swizzle
sources; duplicates from earlier texture objects are discarded, while duplicates
within one authored option batch remain. swizzle source identity excludes channel
values, matching the retained merge.

an explicit DDS image is selected before the ordinary image. otherwise, the
read-only synchronous `fileExists` callback selects an existing same-stem DDS.
the retained deferred cache returns an object even when decoding fails, so an
explicit missing DDS does not fall back to the ordinary image. path composition
keeps repeated separators and dot segments in cache keys. percent escapes are
decoded once by fastgltf. unlike the retained loader, the new owner omits stale bytes
after a decoded filename's C terminator. a real `NativeFileSystem` fixture verifies
the filename consumed by Windows; the native pixel comparison uses that I/O boundary.
different valid percent spellings of one filename consequently share one candidate
image. malformed retained cache keys could split them, including separate linear
and sRGB requests. the explicit mixed-spelling fixture records that compatibility
correction. its rendering impact remains an integrated-equivalence check before
the authoritative loader switches.

| storage | capacity source and lifetime | exhaustion |
|---|---|---|
| material plans and canonical order | source material count plus one possible empty fallback; conversion only | checked scratch budget/allocation |
| image plans and lookup | at most two path variants per source image; conversion only | checked count, scratch budget/allocation |
| texture plans and source cache | source images plus texture objects, including swizzle-only placeholders; conversion only | checked count, scratch budget/allocation |
| lookup hashes and swizzle links | power-of-two tables at no more than half load; validated image/option counts | checked growth bound and scratch budget/allocation |
| path/MIME workspace | validated model, URI, image-name and MIME lengths; conversion only | checked byte sums and scratch budget/allocation |
| image owner | exact planned images, selected textures/swizzles, strings and embedded byte totals | `maxImageBytes` or checked allocation error |

`maxScratchBytes` includes these conversion plans. `maxImageBytes` includes the image
owner and every requested backing byte, but excludes parser storage, the canonical
scene, allocator overhead and decoded/GPU data. all owner construction is
transactional, including a late canonical allocation failure. there is no per-frame
import hash table. decoder-derived alpha mode and bit depth remain unknown in the
CPU texture record until decoding supplies those facts before render publication.

[`import_material_tests`](../tests/import_material_tests.cpp) compares actual
retained loader values, material constants, texture sharing, color-space requests,
DDS choice, decoded layouts/pixels and swizzle sources. it covers data URIs, external
images, GLB/buffer views, parser/input destruction, moves, required-extension
rejection, metadata beyond old fixed cutoffs, exact capacities and first-party
allocation failures with retry. these are semantic and ownership checks. integrated
sampling, alpha behavior and matched visual scenes remain required before switching.

## lights, cameras and animation

these records, their names and keyframes belong directly to the canonical scene.
the parser and conversion plans can die after success. light/camera records represent
placements, so two nodes referencing one definition retain independent transforms
and reciprocal node/leaf links. unselected definitions do not publish. a mesh,
including a skinned instance, occupies its authored node; additional camera and light
leaves use identity children in that order before authored children. this fixes two
retained importer defects: one shared leaf had only its last node as owner, and late
skin attachment could replace an earlier camera/light leaf.

directional glTF intensity becomes irradiance. point/spot values retain intensity,
linear color and optional range; absent range stays zero for unlimited reach. radius
and angular size keep renderer defaults. spot cones convert from radians to degrees
using the retained float operation. invalid projection/cone domains, negative light
values and non-finite consumed values fail explicitly. perspective cameras retain
optional far/aspect presence and radian vertical FOV; orthographic cameras retain
near/far and extents. camera names rename the placement node as the old leaf setter
did. missing names use the existing node name, or generated `CameraN` names when
empty. an authored empty name stays empty. light definition names remain ignored.

glTF animations retain source order, names and authored channel order. more than one
animation adds the retained `Animations` container. supported channels reference a
selected node and a validated source sampler. canonical samplers follow first channel
use; compatible translation/scaling channels share one sampler, while incompatible
vec3/vec4 targets fail. unused samplers have checked accessor references but do not
read data or allocate keys. the retained loader incorrectly paired sampler and
channel array ordinals to choose interpolation, including an out-of-bounds read when
samplers outnumbered channels; conversion uses the actual references.

float32 scalar times must be finite, nonnegative and strictly increasing. float32
vec3 translation/scaling and vec4 rotation outputs must have matching counts, or
three outputs per time for cubic interpolation. step, linear, rotation slerp and
cubic Hermite modes retain their values and tangents; vec3 fourth lanes stay zero.
duplicate node/attribute channels fail. duration is the canonical maximum used
sampler endpoint. weights, absent targets and targets outside the selected scene
remain ignored; their named empty animation leaves remain metadata. imported
animation and morph playback stays deliberately absent. required unsupported
extensions, including material animation pointers, fail during preflight.

animation plans use the source animation count. sampler plans and the first-use
order array use the checked sum of source sampler counts. these plans and the
shared float scratch consume `maxScratchBytes` and live only through conversion.
canonical node capacity is the file root plus selected nodes, occupied auxiliary
placements, animation leaves and the optional container. channel/key/name capacities
are checked sums of the retained data. the canonical owner allocates those exact
tables with checked failure; all outputs remain unchanged until complete conversion,
sealing and skin preparation succeed. no new per-frame import storage survives.

[`import_scene_tests`](../tests/import_scene_tests.cpp) compares actual retained
light/camera fields, node names/transforms, animation references and keyframe bytes.
separate defect fixtures prove the old shared-owner, skin-leaf and sampler-order
failures and check the corrected records. JSON and GLB inputs, ignored/unused tracks,
parser/caller destruction, moves, malformed inputs, exact capacity and allocation
failure with retry exercise ownership. integrated camera/light rendering and
publication remain later acceptance work.

## application composition and paths

[`ImportSceneDescription`](renderer_import_description.h) owns resolved model paths
and flat graph, leaf, channel, target and keyframe tables. the temporary simdjson DOM
dies before `Parse` returns. a successful parse replaces the previous description;
malformed, unsupported, over-budget or allocation-failed input preserves it. model
paths resolve against the description file, while external buffers/images resolve
against their model file. direct glTF/GLB inputs use `ConvertImportScene` without an
application root. application descriptions add the retained `SceneRoot`.

[`ResolveImportPath`](renderer_import_path.h) reproduces the retained Windows
drive, root-relative, UNC and device-prefix join rules using plain byte views. it
normalizes separators to `/` and preserves repeated separators and dot segments.
the caller supplies output capacity including its trailing NUL. inputs cannot
overlap output; invalid ranges, embedded NUL, overflow or insufficient space
preserve every output byte and the reported length. this operation neither allocates
nor accesses files. descriptor paths are literal; parser-provided URI paths are
already decoded and must not be decoded again.

[`ComposeImportScene`](renderer_import_composition.h) borrows one `ImportModel`
tuple per descriptor model in source order. each tuple owns a completed converted
scene, packed geometry and encoded image data. these are temporary loading inputs,
not additional runtime scene authorities. the description and model owners must
remain immutable through the synchronous call. the three output owners must be
empty and generation must be nonzero. every failure preserves inputs, outputs and
optional statistics. success consumes all model tuples after every fallible step,
including unused models, and publishes one complete canonical scene.

the first reference places a model template. later references clone that template
as it then exists, including its first placement's TRS, typed leaf and subsequently
attached children. only authored TRS components overwrite inherited values; the
description name always replaces the template name, including an empty name.
children are handled before the parent's typed leaf replacement, matching retained
registration order. canonical node identity and hierarchy preorder remain separate.
static meshes and materials share within one source model; skin placements own
distinct derived meshes, joints and final-pose palettes. internal joints and glTF
TRS animation targets remap into each clone; external targets retain their identity.

graph `parent` paths resolve against already attached nodes, independently of file
paths. `/` or `\` selects the root. relative paths without a context and unresolved
custom parents skip the whole lexical subtree. repeated separators, literal `.`
child names, `..` parents, trailing empty-name children and first matching duplicate
siblings retain native lookup behavior. traversal above the root fails explicitly.
placing a template under its own existing descendant fails before cloning; the old
live traversal could expand without bound. no recursive ownership chain or traversal
is created.

application animation is resolved after graph/resource registration. `target` takes
precedence over `targets`; missing targets are counted and skipped. each authored
channel owns one sampler shared by all resolved targets. material targets always
use the leaf-property attribute, even when the property text is `translation`.
duplicate material names choose the first canonical material and increment an
ambiguity count; the old pointer-ordered registry offered no stable tie rule.
an animation with no resolved channels is omitted. glTF model inputs accept only
their supported TRS channels; application material/leaf properties belong to the
description. metadata preservation does not restore animation playback.

all model texture requests are processed before selecting graph resources. unused
materials and models can therefore still fix a shared image's first color space
and contribute swizzles. external images merge by exact resolved path, while embedded
images keep their source-model identity. canonical texture indices select only used
requests. the output image owner retains all requests and their owned data until
decode/upload finishes. static packed payloads transfer without copying; derived
skin groups own newly prepared palettes and retain no CPU vertex payload. input
payload ownership changes only at the final non-failing commit.

| loading storage | capacity and lifetime | exhaustion |
|---|---|---|
| description tables/strings | exact validated counts, owned until replacement/reset/destruction | checked sums, `maxStorageBytes`, allocation result |
| description traversal | flat node-count worklist during parse | `maxScratchBytes`; parser nesting limit is checked separately |
| model/resource maps | summed source nodes, meshes, groups, materials, images, requests and samplers; composition only | checked narrowing, byte sums and scratch budget |
| draft graph, leaves and clone work | input-seeded flat arrays; explicit growth as placements expand | checked growth and `maxScratchBytes`, including simultaneous old/new capacity |
| final canonical tables | exact selected graph/resource/key/string counts | transactional canonical allocation and sealing |
| output geometry/images | transferred source payloads plus new group/palette and image/request/map/string/encoded-byte storage | `maxGeometryBytes` and `maxImageBytes`, including every retained backing byte |

the scratch statistic reports peak requested first-party composition backing bytes,
including growth overlap. it excludes input owners, final scene/image/geometry
storage, allocator overhead, stack and vendor parser memory. geometry budgets charge
transferred payloads even though no copy is made. `new` and `type_traits` establish
typed lifetimes and constrain the flat array elements; private `string_view` adapts
simdjson text without ownership. no owning STL container or per-frame import cache
is added to these first-party owners. vendor allocation exhaustion retains the
separate fatal boundary described above.

the [CPU composition checks](../tests/import_composition_tests.cpp) cover atomic
failures, capacity, ownership and 100,000-node deep/wide clones on a measured 64 KiB
stack. [path](../tests/import_path_tests.cpp) and
[description](../tests/import_description_tests.cpp) fixtures compare retained
operations and fields. the [composition reference](../tests/import_composition_fixture.md)
preserves actual retained `Scene` records and shared identities, final-pose GPU
palette bytes and physical glTF/GLB/compositions from development/package layouts.
combined skin/image allocation faults retry against those captured controls.
the same CPU target runs both suites without a Donut or graphics dependency.
omitted default scenes
and malformed percent-decoding cache keys have separately observed retained defects;
their corrected outcomes are not claimed as byte-identical old behavior. these
fixtures complement the integrated rendering and upload checks below; they do not
prove exact package execution.

## retained support and migration checks

the frozen reference is the [Donut loader](../donut/src/engine/GltfImporter.cpp)
with the compiled overlay archived by the [composition reference](../tests/import_composition_fixture.md).
its engine target and patch stage are retired. this table records the retained
behavior and replacement checks. production uses the boundary below.

| input or behavior | retained interpretation and required check |
|---|---|
| files and composition | glTF/GLB, external buffers, embedded images, base64 data and percent-decoded scene-relative paths. preserve application scene descriptions, repeated model references and overrides separately from glTF. |
| texture choice | prefer explicit `MSFT_texture_dds`, then ordinary image with a same-stem DDS preference. preserve deferred/async decoding and owned embedded bytes. |
| texture identity and color | base/diffuse, specular-glossiness and emissive loads request sRGB; metallic-roughness, normals, occlusion and transmission request linear. existing image/texture caches key by source identity, so mixed-role aliases need an explicit equivalence fixture. |
| UVs and samplers | current geometry takes `TEXCOORD_0`. material sampling is a renderer policy, not glTF sampler conversion. preserve existing behavior; do not imply that parsed wrap/filter metadata changes rendering. |
| texture transforms | normal-map scale is implemented. other transforms warn and are ignored by the retained loader. optional unsupported metadata must not silently gain different semantics; unsupported required behavior must fail clearly. |
| materials | metallic-roughness and specular-glossiness, six alpha/transmission domains, opacity/cutoff, two-sided flag, normal/occlusion strength and authored values must retain exact raster/ray interpretation. |
| emissive | explicit strength stays separate from color. without the extension, normalize color by its maximum component and retain that component as intensity; black uses intensity one. |
| custom extensions | retain `NV_materials_subsurface`, `NV_materials_hair` and `NV_texture_swizzle`, including flags, scalar/vector values and source/channel metadata. the old 1,024-byte material-extension cutoff is not a new supported-size limit. |
| geometry | indexed or generated-index triangles, lines and line strips. preserve range/mesh/instance order, bounds, attribute packing, curve radius and tangent generation. unsupported primitives need an explicit outcome. |
| accessor conversion | the old direct geometry iterator lacks sparse decoding and assumes several float attribute formats. use audited fastgltf accessor utilities for normalization, sparse and strided input, with checked component/type/range compatibility before reading. |
| transforms and instances | preserve root/sibling order, shared meshes and independently placed instances. matrix input currently becomes TRS using the retained affine decomposition; native float inputs then enter double scene transforms. validate graph references and cycles before constructing any owning tree. |
| skin and morph data | preserve prototype/instance relationships, joint indices and weights, inverse binds, morph position ranges and initial current/previous skinned positions. imported animation/morph playback remains deliberately absent. |
| lights and cameras | directional irradiance, point/spot intensity/color/range, cone radians-to-degrees conversion, perspective optional far/aspect fields and orthographic extents. preserve extra leaf-node placement and generated camera names. |
| animation records | retain samplers, interpolation, keys and node/material target identity. translation/rotation/scale metadata remains; no playback control is restored. validate shared samplers and channel indices independently. |
| progress and failure | retain real parse/read/conversion work units and the existing worker cancellation checkpoints. malformed, missing or canceled candidates never become visible. joined worker borrows and GPU retirement retain their current boundaries. |

the shipped Bistro and San Miguel inputs cover large static scenes but contain
no skins or animations. San Miguel declares optional `KHR_materials_specular` and
`KHR_materials_ior`; the retained loader does not map those values into its material
conversion. dedicated fixtures must cover behavior absent from those two scenes.

## production integration

[`LoadImportScene`](renderer_import_load.md) produces the scene, packed geometry,
decoded images and runtime light identifiers on the joined worker. the
[NVRHI upload owner](renderer_scene_resources_nvrhi.md) prepares resources before
visible publication; [scene lifetime](renderer_scene_lifetime.md) owns cancellation,
collision borrowing and retirement. geometry without an authored material gets
the retained empty material. the retained runtime diagnostic compares fixed
Bistro/San Miguel alpha, shadow, camera and history behavior through this path.
dedicated structural and GPU fixtures cover lights, cameras, initial skins and
other supported data absent from those two scenes. load-phase time and memory
measurements remain separate from visual equivalence and package acceptance.

use iterative first-party node traversal with checked capacity and visited-state
validation. do not use fastgltf's recursive `iterateSceneNodes` helper. decoder
utilities have a separate call-path audit; no convenience traversal owns the
runtime graph. release parser storage once copied records and owned image/buffer
bytes exist, before deferred consumers can outlive it.
