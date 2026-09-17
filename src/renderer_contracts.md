# rendering values

this module owns portable CPU/shader values, not resources or a second graphics
interface. start at the [glossary](../docs/glossary.md) and [frame owner](renderer_frame.md).

## camera and layout

[`renderer_gpu_scalar.h`](renderer_gpu_scalar.h) defines explicit scalar widths
and plain vector/matrix storage. matrices have no implicit conversion to Donut or
native descriptors. a 3x4 instance matrix multiplies a local column vector on its
left; a row-major 4x4 view matrix multiplies a world row vector on its right.
these conventions are intentionally different, not interchangeable layouts.

[`RendererViewConstants`](renderer_view_contract.h) owns current view/clip matrices,
viewport and pixel jitter. positive view Z is forward. current perspective uses
infinite reverse-Z, with hardware depth `near / viewZ`; zero depth is the cleared
background, not a finite surface. top-left pixel coordinates map to clip Y with
a sign flip. `NoOffset` matrices exclude jitter. current consumers set no jitter,
but the view owner preserves nonzero offsets without changing the convention.
orthographic and other projections reconstruct through `matClipToView` or
`matClipToWorld`, as in [GPU reconstruction](renderer_gpu_helpers.hlsli), not the
perspective-only scalar formula.

[`renderer_view.cpp`](renderer_view.cpp) constructs one plain cached view value.
invalid matrices or viewports leave the previous value unchanged. native
viewport/scissor conversion stays in [`renderer_view_nvrhi.h`](renderer_view_nvrhi.h).
[view tests](../tests/renderer_view_tests.cpp) compare constants and culling planes
with the retained native oracle, then check invalid inputs, reverse-Z,
reconstruction, viewport, jitter, mirrored and orthographic cases. native view
ownership and the old cross-library layout shortcut are no longer required.

[`renderer_gpu_contract.h`](renderer_gpu_contract.h) remains the shared GPU
layout owner. explicit C++ offsets/sizes and [compiled DXIL checks](../tests/shader_reflection.cmake)
agree with actual shader use. host alignment is not a constant-buffer binding
alignment; the backend owns binding alignment and resource addresses. persistent
scene identity must not serialize host pointers, descriptors or GPU addresses.
canonical [scene encoders](renderer_scene_encoding.h) produce material, geometry,
instance and light data; the private backend resolves descriptors and resources.

## materials and surfaces

[`renderer_material_contract.h`](renderer_material_contract.h) defines material
domains and raster classes. [canonical draw selection](renderer_scene_draw.cpp)
selects the retained opaque and alpha-tested domains; [render setup](uvsr_render_setup.cpp)
reads their canonical values and resource bindings. this adds no transparency support.

[`pbr_material.h`](pbr_material.h) normalizes CPU import values using plain fields.
`PbrImportedMaterialValues` is not the shader `PbrMaterialParameters` layout. its
old CPU feature mask had no consumer and conflicted with shader bit meanings;
feature packing stays with the real shader/material producer.
[material modes](renderer_scene_material_mode.cpp) normalize canonical values
before committing their complete batch.

[`pbr_surface_light_contract.h`](pbr_surface_light_contract.h) owns executable
normal orientation rules. geometric and shading normals are world-space; shading
stays in the geometric hemisphere. [`pbr_gbuffer.hlsli`](pbr_gbuffer.hlsli) owns
channel encoding. roughness is perceptual `[0,1]`, squared once for BSDF alpha in
[`pbr.hlsli`](pbr.hlsli). [PBR tests](../tests/pbr_reference_tests.cpp) cover finite
normalization, material validation and independent known answers.
valid material/node IDs are nonnegative signed 32-bit values; [picker transport](renderer_pixel_readback.md)
uses uint32 with UINT32_MAX as no-hit.

## temporal and color meaning

there is no current motion texture, object/camera reprojection or previous-view
resource. unused first-party previous-position lanes and the unused motion helper
were removed; no visible temporal feature was removed. the canonical scene owns
current and previous transforms. [submission](uvsr_render_frame.cpp) advances the
previous snapshot only after a successful complete scene frame.

the [frame module](renderer_frame.md) owns attempt versus submission versus
presentation order. technique-local epochs, validity and schedule tokens remain
distinct. [`lighting_accumulation_contract.h`](lighting_accumulation_contract.h)
and [`path_tracing_accumulation_contract.h`](path_tracing_accumulation_contract.h)
define accepted finite samples, saturation and repair. an attempt index does not
prove valid previous history or GPU completion.

lighting and accumulated means are scene-linear, without pre-exposure. display
exposure is applied later by AgX; it does not invalidate lighting history. AgX
produces display-linear color. [output transfer](display_output_ps.hlsl) applies
sRGB encoding and encoded-space dither, then compensates for the SRGB target's
hardware encoding. [frame resource routing](renderer_frame.md#data-and-completion)
defines the actual lifetimes. no unused color tag or opaque texture wrapper is added.

## checks and dependency boundary

these headers have no Donut, NVRHI, Windows or graphics SDK dependency. the
[standalone C++17 probe](../tests/portable_contracts/CMakeLists.txt) compiles each
header separately with exceptions disabled, including existing CPU shader helpers.
those existing public CPU shader-helper headers still expose standard-library
arithmetic facilities; their later owner migration must remove or justify them. this is not an
exception-free whole-engine claim. [pixel readback](renderer_pixel_readback.md)
is the first compiled concrete backend boundary example.

## temporary adapters

these are ordered migration obligations, not permanent public dependencies.

| adapter | current consumers | removal |
| --- | --- | --- |
| CPU shader-helper standard arithmetic | the PBR and ray contract headers included by CPU reference tests | stage 8.03 audits layouts, then stage 9.03/9.06 removes or justifies each retained math/test facility |

camera input may remain in Donut until stage 10.04; it does not require retaining
Donut's rendering-view representation. the legacy view oracle remains test-only
until the scene/core test migration.
