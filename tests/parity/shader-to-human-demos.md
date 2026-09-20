# ShaderToHuman documentation demos

source: Electronic Arts ShaderToHuman `d6f98b7d67da802053cd9c702082fa741dec42e7`, frozen in the existing [source manifest](shader-to-human-sources.json). the [BSD notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt) applies. all math and drawing in [demos](../../crates/shader-to-human/demos/mod.rs) is safe, allocation-free Rust shared by CPU tests and GPU entries.

the source inventory has35 explicit documentation branches. there are37 selected executions here, because3D's default checkerboard view and Intro are also observable source behavior. generated web/editor files are not additional algorithms. source language aliases do not create a second shader adapter obligation.

| source file and branches | Rust mapping | case IDs |
| --- | --- | --- |
| docs_src/Gather_docs.hlsl,0..6 | [gather](../../crates/shader-to-human/demos/gather.rs), initialization, text, integer/hex/float, box/disc and progress | s2h.docs-gather-0..6.opt0/3 |
| docs_src/Scatter_docs.hlsl,0..5 | [scatter](../../crates/shader-to-human/demos/scatter.rs), the source's gather drawing, including its normalized coordinate round trip | s2h.docs-scatter-0..5.opt0/3 |
| docs_src/2D_docs.hlsl,0..10 | [2D](../../crates/shader-to-human/demos/two_d.rs), shapes, half-spaces, AA, lines and sRGB ramp | s2h.docs-2d-0..10.opt0/3 |
| docs_src/3D_docs.hlsl,1..5 plus default0 | [3D](../../crates/shader-to-human/demos/world.rs), checkerboard, spheres, lines, arrows, bases and boxes, with source3x3 sampling and shadows | s2h.docs-3d-0..5.opt0/3 |
| docs_src/UI_docs.hlsl,0..5 | [UI](../../crates/shader-to-human/demos/ui.rs), clear button, radio, checkbox, scalar/RGB/RGBA sliders | s2h.docs-ui-0..5.opt0/3 |
| docs_src/Intro.hlsl | `intro` in the shared module, colored title and source version14 | s2h.docs-intro-0.opt0/3 |

## source behavior and state ownership

the [native example](../../crates/shader-to-human/examples/docs.rs) uses the existing AGFX buffer, texture and compute owners. [GPU entries](../../shaders/rust/shader_to_human_demos.rs) have explicit roles: demos_cs reads a complete80-byte state snapshot and writes the RGBA8 image. update_ui_cs has exactly one invocation and commits a state update after the image pass finishes. the128-byte root carries dimensions, category/branch, camera, mouse and previous mouse. all state transport uses explicit u32 words and byte packing. no Rust struct layout is used for buffer serialization.

the fixed evidence inputs are800x600, zero-initialized UI values, idle mouse for documentation images and the same [reconstructed camera](fixtures/shader-to-human/camera.txt) used by the regression investigation. these are matched reference/port inputs, not a claim to reproduce an unrecorded documentation screenshot's host settings. native resources, pipelines and state persist across frames. one coordinator owns all GPU submissions.

| inherited source behavior | disposition and evidence |
| --- | --- |
| Scatter_docs uses ContextGather throughout | preserved. the mapping names the actual drawing mode. original source and Rust images agree for all six branches |
| its crosshair branch only prints a heading | preserved. this case does not claim to demonstrate crosshair drawing. the actual library primitive retains separate coverage |
| 2D arrow and triangle branches contain only TODO comments | preserved as complete background writes. tests require constant images and equality between the two branches. required implemented arrow/triangle primitives are elsewhere |
| UI_docs adds a different pixel offset from the other documents | preserved. integer mouse coordinates select four neighboring source pixels under the source's ties-to-even test |
| source UI invocations can write the same state concurrently | host adaptation. render uses private copies of the old snapshot, then exactly one invocation commits the selected mouse pixel. CPU cases demonstrate the four overlapping selections without executing a GPU race. frame order and resulting state are deterministic, with no claim of equivalence to an undefined racing result |
| HLSL UI_docs reads capture state but only calls deinit in its GLSL branch | preserved. newly captured slider state is not saved, and a supplied capture is not cleared on release. CPU and GPU sequences cover the omission. changing it requires a separately documented interaction contract |
| the3D HLSL document spells one local as GLSL vec3 | Rust uses Vec3. the original-HLSL reference needs an explicit vec3-to-float3 alias to compile. the initial DXC failure and adaptation are preserved. no formula is rewritten |

these are observed source choices and future cleanup candidates, not measured performance costs. no rendering algorithm, tolerance or golden changes to conceal a mismatch.

## verification

[six CPU cases](../../crates/shader-to-human/tests/docs_contracts.rs) check root offsets, all state words, representative samples from every case, independent shape/background assertions, four-pixel selection, click/hold/release, radio/clear/color controls and the source capture omission. CPU source checks are separate from native evidence.

the [compile command](../../tools/theta/compile_s2h_library.py) adds `--demos`. both opt0/opt3 modules pass independent Vulkan1.3 SPIR-V validation with only Shader/VulkanMemoryModel. the native owner still requires and queries its existing Vulkan1.4 feature contract. the shader/native review is [U-019](../../UNSAFE.md#u-019-documentation-image-and-persistent-ui-passes).

the [runner](../../tools/theta/run_s2h_demos.py) verifies exact executable/source/shader identity, all74 documentation images, opaque alpha, independent shape assertions and30 UI frames. the15-frame sequence covers all three radio choices, clear, checkbox press/hold/release/repress, scalar endpoints/outside drag, RGB blue and RGBA alpha/release. all20 state words are checked against an independent explicit oracle, including untouched and reserved words. ordered completion values prove the recorded render/copy/update/copy sequence. [six evidence controls](../../tools/theta/test_s2h_demos.py) reject forged self-consistent state expectations, missing cases, stale identities, bad images, reordered inputs and false parity claims.

E-041 passes all 74 images and 30 UI frames in each Debug/Release host with zero core/synchronization diagnostics. all 104 corresponding images and 30 state readbacks match across host configurations. original image agreement stays separate. an ignored D3D source control compiles the 37 original documents with unchanged library/math, fixed matching inputs, private zero UI state and the explicit 3D type alias. all 37 two-execution controls complete with zero debug warnings/errors. it supplies an independently executed source reference, not a product backend or replacement for Vulkan evidence.

| source comparison | opt0 / opt3 exact RGBA result |
| --- | --- |
| gather0..6, scatter0..5, UI0..5, Intro | all exact |
| 2D0..7 and9..10 | all exact |
| 2D8, sRGB ramp | opt0 differs at17 pixels by one byte, opt3 exact |
| 3D0, checkerboard |3 differing pixels at both levels, maximum4 |
| 3D1, spheres |189 differing pixels at both levels, maximum40 |
| 3D2, lines |322 /351 differing pixels, maximum56 /52 |
| 3D3, arrows |809 /820 differing pixels, maximum79 /80 |
| 3D4, bases |392 /444 differing pixels, maximum65 /51 |
| 3D5, boxes |1 differing pixel at both levels, maximum1 |

61/74 exact source comparisons pass. the other13 remain failed comparisons. this extends arithmetic/capture investigation without relaxing original regression goldens or choosing a new threshold. complete documentation image parity, all seven example families, persistent original regression interactions and actual NGAPI integration remain required. translated bodies and passing execution/state checks do not close T029.
