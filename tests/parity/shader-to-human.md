# ShaderToHuman source mapping

source: Electronic Arts ShaderToHuman version 14, revision `d6f98b7d67da802053cd9c702082fa741dec42e7`. the [manifest](shader-to-human-sources.json) freezes 76 relevant source, graph, example and golden files. the [BSD notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt) applies to the translated code, packed font and imported images. intersection formulas retain the source attribution to Inigo Quilez.

the safe, allocation-free [Rust library](../../crates/shader-to-human/src/lib.rs) translates all 81 implemented library definitions, counting source overloads. this is an implementation inventory, **not a complete parity result**. E-036 records the initial CPU and compile-only evidence. [documentation demos and persistent UI](shader-to-human-demos.md) now map all35 branches, the default3D view and Intro, with separate implementation, execution, state and image-comparison evidence. the five golden groups and seven example families retain their own T027-T029 requirements. no unexecuted group counts as a pass.

## library mapping

the `s2h_` prefix is omitted from Rust methods. the gather, scatter and 3D contexts retain separate state and source initialization. Rust names below are defined in the linked modules. stateless helpers drop the unused source context argument.

| source definitions | count | Rust disposition |
| --- | ---: | --- |
| `s2h_fontSize`, `s2h_fontLookup` | 2 | [MiniFont and Font](../../crates/shader-to-human/src/font.rs), exact 192 packed words, font size and lookup |
| gather `s2h_init`, `s2h_setCursor`, `s2h_deinit`, `s2h_setScale` | 4 | [ContextGather](../../crates/shader-to-human/src/gather.rs) `new`/`with_font`, `set_cursor`, `deinit`, `set_scale` |
| gather `s2h_printCharacter`, six `s2h_printTxt` overloads | 7 | `print_character`, const-generic `print_text`. `text!` expands literal bytes into fixed u32 character codes at compile time |
| gather `s2h_printSpace`, `s2h_printLF`, `s2h_printInt`, `s2h_printHex`, `s2h_printFloat`, `s2h_printBox`, `s2h_printDisc` | 7 | `print_space`, `print_lf`, `print_int`, `print_hex`, `print_float`, `print_box`, `print_disc` |
| `s2h_drawDisc`, `s2h_drawCircle`, `s2h_drawHalfSpace`, `s2h_drawRectangle`, `s2h_drawRectangleAA`, `s2h_drawCrosshair`, `s2h_drawLine` | 7 | gather `draw_disc`, `draw_circle`, `draw_half_space`, `draw_rectangle`, `draw_rectangle_aa`, `draw_crosshair`, `draw_line` |
| `s2h_getHalfSpacePlane`, `s2h_drawTriangle`, `s2h_drawArrow`, `s2h_drawSRGBRamp`, `s2h_coordinateSystem` | 5 | `half_space_plane`, `Triangle`, gather `draw_triangle`, `draw_arrow`, `draw_srgb_ramp`, `coordinate_system` |
| both `s2h_computeDistToBox` overloads | 2 | `distance_to_box(position, center, half_size)`, `distance_to_aabb(position, min_max)` |
| `s2h_frame`, `s2h_button`, `s2h_radioButton`, `s2h_checkBox`, `s2h_progress` | 5 | [gather widgets](../../crates/shader-to-human/src/widgets.rs) `frame`, `button`, `radio_button`, `check_box`, `progress` |
| `s2h_sliderFloat`, `s2h_sliderRGB`, `s2h_sliderRGBA` | 3 | `slider_float`, `slider_rgb`, `slider_rgba` |
| `s2h_tableInt`, `s2h_tableFloat`, `s2h_function` | 3 | `table_int`, `table_float`, `function`, with statically dispatched callbacks and `Option` for an absent row |
| `s2h_accurateLinearToSRGB`, `s2h_accurateSRGBToLinear`, `s2h_indexToColor`, `s2h_colorRampRGB` | 4 | [math](../../crates/shader-to-human/src/math.rs) `linear_to_srgb`, `srgb_to_linear`, `index_to_color`, `color_ramp_rgb` |
| 3D `s2h_init` | 1 | [Context3D](../../crates/shader-to-human/src/world.rs) `new` |
| `s2h_sphIntersect`, `s2h_boxIntersection`, `s2h_cylIntersect`, `s2h_cylNormal`, `s2h_dot2`, `s2h_coneIntersect` | 6 | `sphere_intersection`, `box_intersection`, `cylinder_intersection`, `cylinder_normal`, `Vec3::length_squared`, `cone_intersection`. box normal is returned beside distances |
| `s2h_drawAABB`, `s2h_drawLineWS`, `s2h_drawArrowWS`, `s2h_drawBasis`, `s2h_drawSphereWS`, `s2h_drawCheckerBoard`, `s2h_drawSkybox`, `sceneWithShadows` | 8 | 3D `draw_aabb`, `draw_line`, `draw_arrow`, `draw_basis`, `draw_sphere`, `draw_checker_board`, `draw_skybox`/`draw_skybox_with_font`, `scene_with_shadows` |
| scatter `s2h_init`, `s2h_setCursor`, `s2h_setScale` | 3 | [ContextScatter](../../crates/shader-to-human/src/scatter.rs) `default`/`with_font`, `set_cursor`, `set_scale` |
| scatter `s2h_printCharacter`, six `s2h_printTxt` overloads | 7 | `print_character`, const-generic `print_text` |
| scatter `s2h_drawCrosshair`, `s2h_printLF`, `s2h_printInt`, `s2h_printHex`, `s2h_printFloat`, `s2h_printBlock`, `s2h_printDisc` | 7 | `draw_crosshair`, `print_lf`, `print_int`, `print_hex`, `print_float`, `print_block`, `print_disc` |

`S2H_DISABLE_EMBEDDED_FONT` becomes a copyable, statically dispatched `Font`. `onGfxForAllScatter` becomes a caller-supplied pixel callback. table/function lookup hooks and the 3D `scene` hook become closures without dynamic dispatch or allocation. source ASCII aliases become literal u32 codes. `s2h_glsl.hlsl` contains language/intrinsic aliases rather than additional algorithms. the Rust equivalents of intrinsic operations used by fixtures/examples are still subject to their runtime coverage, including wave and half packing operations.

the host-only `print_ascii` convenience accepts slices. the current RustGPU backend rejects slice iteration and unsizing in this path, so shaders use fixed arrays through `print_text(&text!("hello"))`. by-value fonts avoid forming a reference to the zero-sized embedded font. these interfaces preserve source behavior without adding unsafe code or another compiler patch. the pinned scalar `glam` and `libm` versions match the shader sysroot.

## behavior retained for later review

these are inherited source choices, not measured performance problems. parity fixtures should establish the current result before any redesign.

| source behavior | current disposition | evidence needed before changing it |
| --- | --- | --- |
| float formatting truncates three fractional digits and prints the integer part's sign, so negative subunit values lose their sign | preserved in gather/scatter | source golden and explicit negative-subunit cases, then a documented formatting change |
| frame/widget under-blending uses the original alpha-weighted lerp, and the gather fixture blends that result again | preserved without a compositing redesign | exact alpha/background regressions and an explicit visual contract |
| checkbox hover is circular despite square drawing | preserved | corner/edge input cases and intentional interaction change |
| mouse-pixel selection uses HLSL `round`, whose halfway cases round to even | exact zero interval `[-0.5, 0.5]` | existing boundary cases and any revised pixel convention. [HLSL rule](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-round) |
| scatter uses fixed 8-pixel cells, a float cursor backup for integer printing, and the text color at a crosshair center | preserved | custom font, large cursor and center/arm color cases before simplification |
| sphere/line drawing preserves prior alpha, while box drawing blends alpha toward one | preserved | overlapping opaque/translucent shapes and matching depth/image oracles |
| the 3D helper uses a fixed light, shadow bias and half-grey shadow | preserved | scene-scale/depth agreement and measured needs for a configurable interface |
| the source cylinder/cone formulas subtract large nearby terms when deciding thin-shape hits | preserved, with an E-037 numerical-stability candidate | optimized/unoptimized instruction comparison, edge/coverage oracles and documented arithmetic changes before replacing the formulas. observed edge sensitivity is not a measured performance cost |

## evidence and remaining work

[source_contracts.rs](../../crates/shader-to-human/tests/source_contracts.rs) has 11 deterministic CPU cases covering packed font bits, alpha/edge behavior, formatting, scatter coordinates, color math, slider capture, table callbacks, intersections/depth and shadow state. these are focused contracts, not exhaustive numerical proofs.

the initial broad RustGPU probe compiles input-dependent gather, widgets, scatter and 3D operations to a logical Vulkan 1.3 module and passes independent SDK validation. it performs no GPU dispatch. exact commands, source/compiler/math/module hashes and rejected earlier probes are retained in ignored `work/theta/evidence/full-port`.

the five original 800x600 RGBA PNGs are frozen under [fixtures](fixtures/shader-to-human), with source paths and hashes in the manifest. candidates must not replace these goldens or choose their own tolerance. `GigiTest.py` compares complete images exactly, after two technique runs with its explicit camera and UI defaults. translating those defaults, all five fixture shaders, the documentation branches and examples is ongoing. native Windows Vulkan execution and actual NGAPI integration remain distinct required gates.

E-037 adds shared CPU/RustGPU bodies for all five complete fixture shaders under [fixtures](../../crates/shader-to-human/fixtures), and [five GPU entries](../../shaders/rust/shader_to_human_fixtures.rs). gather/table keep the source widget state and callbacks. scatter retains the source's three red-channel values in its color-content text. the 3D fixture keeps the 3x3 sampling, scene, fixed time and shadow/basis calls. all shader-side code forbids unsafe. each pixel owns private fixture state for these fixed unpressed-mouse captures. persistent interactive state remains required separately.

the [camera values](fixtures/shader-to-human/camera.txt) are origin/depth followed by 16 column values for the Rust matrix. they were extracted using unchanged Gigi Camera.cpp/h at `401386cfd7c6e39e549d939e44d99bd5b49cd14d` and Windows SDK DirectXMath, with source camera position(-2.9,9.459,-21.1), altitude/azimuth(-0.463,6.0), 800x600 projection A, left-handed perspective, reverseZ, near0.1/far1000 and FOV45. source depthNearPlane remains0. the upstream golden's exact Gigi revision is not recorded, so this is a traced reconstruction, not proof of its original host identity. Gigi's non-imported UI allocation path does not apply the blue structure-field default. the fixture starts with zero state, matching the source image. extraction inputs, code, source pins and hashes remain in ignored full-port/gigi-reference.

E-037's ten opt0/opt3 float-buffer cases produce complete finite 7680000-byte readbacks on Windows Vulkan with zero core/synchronization diagnostics. this intermediate path deliberately reports `golden_agreement: not checked`. its exploratory software quantization is superseded for original-image comparison by E-039's native RGBA8 path below. the original float diagnostic remains available for arithmetic investigation.

an ignored diagnostic compiles the unchanged HLSL library with fixed source inputs and the same linear-buffer output. gather agrees with Rust within5.97e-8 absolute error and 2D within about0.00015. HLSL 3D and opt0 Rust agree after software float32 nearest conversion, but both differ from the original PNG at337 pixels by more than one byte. opt3 Rust adds77 edge pixels above one byte relative to that control. the HLSL table control also changes400 function-plot pixels that the Rust output matches in the golden. the HLSL scatter control timed out at90seconds, with its execution phase unknown, and was not repeated. these results isolate host/compiler sensitivity without establishing a new golden or a full HLSL reference pass.

E-039 adds [five native image entries](../../shaders/rust/shader_to_human_images.rs), using the same safe fixture bodies and the original RGBA8_UNORM storage-image output. [U-018](../../UNSAFE.md#u-018-shadertohuman-rgba8-image-entries-and-two-executions) registers their bounded writes and fixed host. the host clears once and reuses the image for two completed executions, capturing each. all ten cases complete in both Debug/Release with matching first/second idle captures, identical corresponding host hashes and zero core/synchronization diagnostics. both final SPIR-V variants retain only Shader/VulkanMemoryModel and the existing feature contract.

| source group | exact original RGBA result at opt0 / opt3, both hosts |
| --- | --- |
| GatherTest | pass, all 1920000 bytes match |
| ScatterTest | pass, every byte including untouched pixels and alpha matches |
| TableTest | pass, every byte matches |
| 2DTest | fail, five pixels each differ in one channel by one byte, identical at both levels |
| 3DTest | fail, 517 /530 pixels differ, 337 /355 exceed one byte, maximum channel error 52 |

the image runner reports required=10, executed=10, execution_passed=10 and passed=6, with an overall failed parity status and nonzero exit. it preserves the expected PNGs, captures, per-channel first mismatch and exact comparison metrics. alpha is included. no tolerance, rebaseline or algorithm adjustment turns the remaining differences into passes. the larger opt0 3D differences occupy x229..377/y210..299 in the first recorded comparison, consistent with the previously identified thin-shape sensitivity, but this is localization rather than a proven root cause. original capture-environment identity and arithmetic remain open, as do persistent interactions, all 35 documentation branches, seven example families, Intro and actual NGAPI integration. T027/T029 are not closed by six passing cases.

E-040 checks the original HLSL with a small ignored D3D12 reference on the same NVIDIA adapter. all six runs complete with zero debug warnings/errors. 2D matches the original PNG exactly for cs_6_0/O3, cs_6_1/O3 and cs_6_1 with optimization disabled. 3D differs at387/387/471 pixels respectively, with257/257/299 pixels above one byte and maximum52. these controls preserve the source library, image format, two executions and reconstructed camera. they do not replace Vulkan evidence or identify the original capture environment. the exact 2D result supports a backend/arithmetic investigation of its five Rust differences. the 3D result also requires recovering the original host/compiler/device identity before attributing every mismatch to the translation.

historical Gigi source at `3f67206bdc7af94b1c5ca14465c6c174bf5c2adf`, the last commit before the golden's GitHub import, confirms the same finite perspective, left-handed/reverseZ defaults and inverse(view * projection) construction. its preview compute path defaults to DXC cs_6_1 with optimization enabled. the import at `bd7ce13c6b46a4bde8e1e1a60f927ec2e1e5df9d` does not record the actual capture date, device or compiler. current and historical camera-source inspection found no supported reason to change the frozen camera values. further numerical experiments need a new discriminator. required documentation/example implementation continues while these failed image cases stay open.
