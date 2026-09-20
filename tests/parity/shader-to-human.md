# ShaderToHuman source mapping

source: Electronic Arts ShaderToHuman version 14, revision `d6f98b7d67da802053cd9c702082fa741dec42e7`. the [manifest](shader-to-human-sources.json) freezes 76 relevant source, graph, example and golden files. the [BSD notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt) applies to the translated code, packed font and imported images. intersection formulas retain the source attribution to Inigo Quilez.

the safe, allocation-free [Rust library](../../crates/shader-to-human/src/lib.rs) translates all 81 implemented library definitions, counting source overloads. this is an implementation inventory, **not a complete parity result**. E-036 records the initial CPU and compile-only evidence. the five golden groups, 35 documentation branches and seven example families plus Intro remain required by T027-T029. no unexecuted group counts as a pass.

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

all ten opt0/opt3 Rust fixture cases now produce complete finite 7680000-byte float readbacks on Windows Vulkan with zero core/synchronization diagnostics. this intermediate buffer path deliberately reports `golden_agreement: not checked`. exploratory quantization shows gather, scatter, table and 2D differ from the source images by at most one byte per channel, while 3D still has larger differences. CPU-only gather/table mismatches above one byte disappear on this GPU. no conversion rule or tolerance has been selected to turn these differences into a pass. original RGBA8 image writes, two-execution capture, all 35 documentation branches, seven example families and Intro remain open, as does actual NGAPI integration.

an ignored diagnostic compiles the unchanged HLSL library with fixed source inputs and the same linear-buffer output. gather agrees with Rust within5.97e-8 absolute error and 2D within about0.00015. HLSL 3D and opt0 Rust agree after software float32 nearest conversion, but both differ from the original PNG at337 pixels by more than one byte. opt3 Rust adds77 edge pixels above one byte relative to that control. the HLSL table control also changes400 function-plot pixels that the Rust output matches in the golden. the HLSL scatter control timed out at90seconds, with its execution phase unknown, and was not repeated. these results isolate host/compiler sensitivity without establishing a new golden or a full HLSL reference pass.
