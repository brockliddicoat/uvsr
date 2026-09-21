# ShaderToHuman GaussianSplatting

source: Electronic Arts ShaderToHuman `d6f98b7d67da802053cd9c702082fa741dec42e7`, under the unchanged [BSD-3-Clause notice](../../legal/licenses/ShaderToHuman-BSD-3-Clause.txt). the [manifest](gaussian-sources.json) freezes all 14 Gaussian files, shared includes and declared inputs. safe [Gaussian programs](../../crates/shader-to-human/programs/gaussian/mod.rs) feed the nine-entry [RustGPU module](../../shaders/rust/shader_to_human_gaussian.rs). the [native example](../../crates/shader-to-human/examples/gaussian.rs) uses the existing AGFX owners and the [eight-sample attachment support](multisample.md).

the active compute, PLY raster and custom resolve paths execute on Windows Vulkan. numeric checks pass, while strict original-HLSL image parity remains incomplete. this checkpoint does not close T018 or T024-T029.

## source and graph mapping

| original file | Rust behavior or explicit disposition |
| --- | --- |
| SplatCommon.hlsl | math.rs, ply.rs and programs.rs. quaternion/SRT, covariance/conic, raster bounds, ray integration, procedural and PLY splats, random coverage and shared scene drawing |
| InitPlyCS.hlsl | gaussian_init_cs. bounded header scan writes the four source u32 fields |
| Debug.ply.bin | exact 51,128-byte source fixture, 200 records, 62 float fields per record. no regenerated points |
| SplatBaseCS.hlsl | gaussian_base_cs and base_image. TESTID 0/1/3 labels, source background and ramp |
| SplatCS_example.hlsl | gaussian_main_cs and compute_image. projected ellipse, analytic ray and eight-sample stochastic ray programs |
| SplatVS_example.hlsl | gaussian_splat_vs and splat_vertex. six vertices per PLY record, source conic-oriented bounds and flat interpolators |
| SplatPS_example.hlsl | gaussian_splat_fs and splat_fragment. source color, Gaussian opacity, random depth, fragment depth and sample mask |
| FullScreenQuadVS.hlsl | gaussian_clear_vs and fullscreen_vertex, six source vertices. graph capitalization differs, so the mapping uses the actual filename |
| ClearColorPS.hlsl | gaussian_clear_fs and clear_fragment. source scene, integer-pixel ray, manual sRGB encoding and alpha 0.5 |
| CustomResolveMSAA.hlsl | gaussian_resolve_cs and resolve. eight samples, active WEIGHT_EXPERIMENT=0 and denominator epsilon |
| Clear.hlsl | empty source body. retained as a no-op after source-compatible zero initialization |
| s2h_splat.gg | fixed dependencies and resources in the table below. no new render graph abstraction |
| s2h_splat.gguser | saved camera, offset, randomization, ray limits and tweak inputs |
| Splat_example.hlsl | inactive older prototype, not invoked by the graph and not translated by this checkpoint. stale relative includes and four-argument drawArrow calls disagree with the current five-argument declaration. do not invent a missing width to claim parity |

| graph nodes, all 23 accounted for | execution or resource mapping |
| --- | --- |
| SplatTestBase0, SplatTestCS0, C0 | completed base then main dispatch, TESTID 0 |
| SplatTestBase1, SplatTestCS1, C1 | completed base then main dispatch, TESTID 1 |
| SplatTestBase3, SplatTestCS3, C3 | completed base then main dispatch, TESTID 3 |
| PlyFile, InitPly, PlyHeader | frozen PLY words and one init invocation before any consuming program |
| UIState | source allocation is unused by active Gaussian programs. no invented state transitions |
| C4, ClearColor, DrawQuad, Depth | zero-initialized single-sample color, no-op Clear, 200 six-vertex instances, D32 Greater reverse-Z with zero depth clear |
| C5MSAA, DepthMSAA, ClearMSAA, DrawQuadMSAA | eight-sample color/depth, source fullscreen clear, then the same 200 instances |
| CustomResolveMSAA, C5 | explicit eight-sample fetch into a single-sample output |

the graph's unused ClearDepth shader declaration points to an absent file. it is not an executed dependency. inactive debug branches and the alternate resolve weight experiment remain inactive and are not counted as translated behavior. stale indirect-dispatch metadata is not evidence of executed indirect work. the current caller uses the declared 800x600 dimensions and direct 100x75 groups. exact Gigi graph-compiler handling of those stale links remains unverified.

## hosting, data and color contracts

binding 0 is 12,880 u32 words, 51,520 bytes. words 0..3 are header_words, stride_words, format and vertex count. the next 92 words hold four explicit column-packed matrices, camera/near, dimensions/time/far, mouse, offset, ray bounds, random flags and tweak. 12,782 original PLY words follow, then two zero padding words. the padding matches the original HLSL StructuredBuffer's 16-byte stride alignment and lies outside parser bounds. the root is a 16-byte uint4. storage output is binding 1 and the multisample sampled input is binding 2, without a sampler.

the PLY header is 1,528 bytes, giving header_words=382, stride_words=62, format=0 and vertices=200. source int64 parsing narrows to u32 at the header field. scalar float properties, SH0 color, sigmoid opacity, exponential scale and real/x/y/z quaternion ordering are retained. higher SH fields and normals are skipped. the source ignores PLY offset while applying offset to procedural splats. malformed/truncated input now terminates with failure, and record reads check remaining size before multiplication. this is an explicit safety adaptation to the source's unbounded scans, not malformed-input parity.

the [three camera records](fixtures/shader-to-human/gaussian-cameras.txt) come from unchanged Gigi Camera.cpp/h at `401386cfd7c6e39e549d939e44d99bd5b49cd14d`. the saved camera retains position (-10.194503784, 6.406509876, -22.010580063) and angles (-0.364076883, 5.876432896). two declared controls use position (0,0,-10) and forward/reverse azimuth. all use source left-handed reverse-Z projection, 800x600 aspect, near 0.1, far 1000 and FOV 45. camera extraction is a CPU source control, not a capture from an unknown historical Gigi build.

pinned [Gigi texture hosting](https://github.com/electronicarts/gigi/blob/401386cfd7c6e39e549d939e44d99bd5b49cd14d/GigiViewerDX12/Interpreter/RenderGraphNode_Resource_Texture.cpp) applies sampleCount only to multisample dimensions. C5 is therefore single-sample despite its stale field value of four. its initialization and the [transient default](https://github.com/electronicarts/gigi/blob/401386cfd7c6e39e549d939e44d99bd5b49cd14d/Schemas/RenderGraphNodes.h) establish C4's zero contents each frame.

source targets have RGBA8_SRGB base format, UNORM storage views and SRGB sampled/attachment views. compute writes retain their explicit source encoding. raster output receives attachment encoding. custom resolve retains the additional manual decode after sampled sRGB conversion, alpha-independent weighting, denominator 8.0001, final encoding and opaque alpha. these unusual conversions are preserved, not silently corrected. clear alpha 0.5 can legally quantize to either 127 or 128 under [Vulkan float-to-UNORM conversion](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html). alpha is an ordinary normalized channel in [sRGB formats](https://docs.vulkan.org/spec/latest/chapters/formats.html).

## verification and open failures

the [eight fixed inputs](fixtures/shader-to-human/gaussian-inputs.json) cover saved state, frozen frame 17, randomized frames 1/2, near/reverse cameras, procedural offset and clipped ray bounds. each runs all five outputs at opt0/opt3. Debug and Release each pass 80 numeric cases with zero Khronos core/synchronization diagnostics. all 208 corresponding output, base, depth, per-sample and input/post-buffer readbacks match between hosts. both modules independently validate with Shader, Int64, SignedZeroInfNanPreserve and VulkanMemoryModel. five entries require float32 preservation. support is queried before pipeline creation.

eleven CPU contracts cover the full source PLY, malformed bounds, field offsets, quaternion/matrices, conics, ray integration, coverage, ABI, rotated ellipse bounds, resolve and inherited degenerate corners. ten Python controls exercise missing/reordered cases, artifact identity, mutated fields/padding, finite samples/depth, all eight resolve samples and the two legal clear-alpha codes. the native oracle checks exact input preservation, initialization, ordered completion, coverage/depth agreement, camera/random/offset response and independent double-precision resolve at 64 sample-grid pixels. its one-code resolve conversion bound is separate from the strict source-image gate.

the original-HLSL control executes the same resource/step sequence with DXC Gis/O3 and explicit Gigi binding/matrix substitutions. its 13 entries specialize source TESTID values and add the same diagnostic sample probe. original library bytes and PLY stay unchanged. unused debug UV is omitted, including its inactive branch reference. the control passes 40 numeric cases with zero diagnostics. this is controlled Vulkan execution of original shader formulas, not original Gigi/DirectX backend execution.

strict comparison finds **14 of 80 final images exact per host**. all 14 are C4, with seven of eight inputs matching at both optimization levels. **66 images fail exact comparison**, 20 by at most one encoded channel value and 46 with larger differences. near-camera C4 still differs at 226 alpha pixels at opt3. all source-control input/post buffers match exactly. these failures remain required work and no threshold was relaxed.

an initial conic translation halved the cross coefficient twice. Raster.original_conic returns (a,b,c), but the corner helper accepts (a,2b,c) and halves its middle value. an independent rotated-ellipse quadratic assertion caught the mismatch. the correction increased exact images from 2 to 14 per host. inherited corner normalization is also degenerate for equal axes and for a<c,b=0. a CPU contract preserves that observed source failure. its possible role in remaining near-camera differences is an inference, not an isolated diagnosis.

the first numeric run incorrectly required clear alpha 128/255. its unchanged captures were rechecked after restricting acceptance to the two legal neighboring codes. the strict HLSL comparison remains exact. earlier compiler failures exposed unsupported pointer aggregate copies, unsizing and checked-arithmetic lowering. keeping Reader scalar and passing the word callback by reference gave an equivalent bounded safe implementation without compiler patches or an unsafe workaround. failed attempts and earlier captures remain preserved.

[U-027](../../UNSAFE.md#u-027-gaussiansplatting-source-programs) records three shader image-write blocks and seven native caller blocks, complete source/interface review, device requirements and lifetime bounds. review records precede dispatch. this is coordinator self-review and bounded execution evidence, not an independent soundness audit. the only shared native-owner change is querying, enabling and exposing supported shaderInt64 through the existing Device owner.

## reproduction

use the pinned source/backend arguments from the [Hello recipe](shader-to-human-hello.md#reproduction-and-remaining-scope), selecting `compile_s2h_library.py --gaussian`. build example gaussian in Debug and Release. source/host/module review must succeed before GPU execution. the final local modules and reviews are under ignored work/theta/evidence/full-port/gaussian-conic-compile and gaussian-conic-review-{debug,release}.json.

```powershell
python tools/theta/run_s2h_gaussian.py --executable work/theta/build/agfx-host/debug/examples/gaussian.exe --sdk work/theta/tools/vulkan-sdk-1.4.357.0 --shader-dir work/theta/evidence/full-port/gaussian-conic-compile --output-dir work/theta/evidence/full-port/gaussian-conic-debug --review work/theta/evidence/full-port/gaussian-conic-review-debug.json
python work/theta/compare_gaussian.py conic
```

the first command runs the numeric gate. the second local analysis script records all strict source differences in gaussian-conic-comparison.json. its process completion is not a parity pass. the strict control lives under gaussian-reference-strict, with its pre-gpu.json, original-source compiler recipe, commands and captures. reviews, local gates, regression and preservation records use gaussian-* names. do not overwrite old captures when testing a changed input or shader. the [resume guide](../../docs/theta-resume.md) identifies the stopping point and remaining work.

## inherited behavior for later review

all entries refer to the frozen source files above. these are observed behavior and change candidates, with no measured optimization claim.

| source behavior | current disposition and evidence needed before changing |
| --- | --- |
| unbounded PLY scans and assumed float properties | changed to bounded failure and checked record addressing. a generalized parser needs declared supported formats, truncation/overflow controls and equivalent valid-file values |
| only SH0, skipped normals/higher SH, ignored PLY offset | retained. richer shading or coordinate behavior needs explicit scene semantics and source comparisons before becoming a new renderer feature |
| covariance uses projection Y for both axes, determinant opacity correction | retained with numeric contracts. different projection or antialiasing formulas need analytic/rotated cases and image/coverage comparison |
| degenerate conic eigenvector normalization | retained and demonstrated on CPU. a stable basis is a future behavior correction requiring axis-aligned/isotropic CPU and GPU oracles |
| inverse SRT precedes ray antialiasing scale enlargement | retained, including opacity adjustment. reordering needs analytic ray-integral and clipping evidence |
| fixed eight-bit coverage, seven samples at full alpha, 20 shuffle steps | retained even for the one-sample target. alternative stochastic masks need coverage-distribution, temporal, depth and image evidence before cost claims |
| vertex path computes visibility but does not cull from it | retained. early culling needs near/behind-camera and degenerate-splat equivalence, then measured invocation/frame cost |
| repeated manual sRGB conversions, unused alpha weights and resolve epsilon | active formulas retained. simplify only with explicit intermediate format/value and alpha oracles, retaining the source comparison |
| repeated per-pixel scene/ray/text work | retained for close parity. any reuse or caching needs equivalent images, input response and measured end-to-end cost |
