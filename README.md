# UVSR

**Unified Visibility Stochastic Rendering**

<!-- uvsr-codebase-size:start -->
**First-Party Lines of Code:** 114,859 non-blank source lines.

**Third-Party Lines of Code:** 387,622 non-blank source lines.

**Total Lines of Code:** 502,481 non-blank source lines.

Counts cover UVSR source, tests, tools, build scripts, retained pinned
dependency source, and final first-party dependency overrides. Documentation,
assets, licenses, binaries, and generated build output are excluded. Regenerate
with `tools/update_readme_line_counts.cmd --write`.
<!-- uvsr-codebase-size:end -->

UVSR is a DirectX 12 renderer built on NVIDIA's pinned Donut framework and its
NVRHI graphics abstraction layer. It ships with two ready-to-run Intel PBR
Sponza flat-roof scenes. The default **PBR Sponza Decorated** includes Intel's
separately distributed curtains and ivy; **PBR Sponza Plain** uses the same
architecture without either add-on.

## Renderer Baseline

- Deferred shading, UVSR PBR, screen-space visibility AO/GI, and the matched
  environment background start enabled. Global image-based lighting defaults
  to Poly Haven's CC0 **Day - Kloppenheim 03** source at its calibrated
  `-2.75 EV`; both diffuse and specular lobes start enabled at `1.00`
  strength.
- The normal **Aliasing** drawer contains **Enabled**, **Method**, **Quality**,
  **History Frames**, **History Strength**, **Dejitter**, and **Sharpness**.
  The **Statistics** drawer places **Run Current With Motion** directly below
  **Run Current**.
  Method independently selects **Temporal Reconstructive**, **Conservative
  Morphological**, or **Multisample Reference**. Every method exposes **Low**,
  **Medium**, **High**, and **Ultra**. Long-term temporal qualities use
  progressively stronger MiniEngine temporal bundles, and CMAA2 supplies an
  optional presentation morphology with four conservative strength levels.
  Multisample Reference displays **Low (2x)**, **Medium (4x)**, **High (8x)**,
  and **Ultra (16x)**. Temporal and Multisample presets leave CMAA2 off by
  default so each method has no hidden full-screen morphology pass. In Deferred
  mode, MSAA preserves every G-buffer sample through material decode and PBR
  lighting, then averages final RGBA16F radiance. CMAA2 can be selected after
  that resolve; Temporal Reconstructive can likewise select CMAA2 as its
  presentation morphology without resetting history.
  Screen-space visibility remains available with Deferred MSAA: it selects one
  coherent closest reverse-Z surface per pixel for visibility and
  coverage-weights only that surface's signed lighting correction back into
  the per-sample MSAA result.
  History Frames reports no history for spatial methods and exposes a 1-32
  prior-frame slider for long-term temporal methods. Its inherited values are
  3/6/9/12. History Strength ranges from 0% to 200%. Values above 100% reinforce
  only history that already passed motion, bounds, reverse-Z depth,
  disocclusion, and rectification gates, and remain capped by the selected frame
  horizon; they do not resurrect rejected history.
  Low and Medium use **1x Bilinear** reconstruction and Pair Tristimulus
  rectification. High uses **1x Bicubic**, and Ultra uses **5x Bicubic** with
  Dejitter enabled. The Reconstruction dropdown also exposes **9x Bicubic**,
  the complete nine-bilinear-tap Catmull-Rom reconstruction.
  **Aliasing Algorithm Configuration** exposes concrete Subpixel Morphology,
  Motion Source, Reconstruction, and Rectification selections in
  least-to-most-expensive order. Rectification retains Pair Tristimulus and
  Variance YCoCg. Sharpness is disabled by default for every quality. Stable
  Interior and the two per-pixel rectification modes are retired.
  MiniEngine TAA owns the temporal history, validity, reset, bounds,
  motion/jitter, reverse-Z validation, and early-rejection infrastructure.
  The motion-test button runs the exact Benchmark Position 1 warm,
  right-45-degree, hold, and return sequence used by the CLI benchmark. Both
  paths are uncapped and advance the turn by exactly `0.375` degrees per
  rendered frame. GPU speed changes elapsed wall-clock time, not the rendered
  path or the 256 measured samples. The button writes a self-validating report,
  keeps the app open, and returns the camera to Piloted.
  Effective temporal image or history-layout changes reset state exactly once;
  presentation-only CMAA2, Sharpness, and image-equivalent execution changes do
  not. Post-CMAA2 sharpening uses the resolved-RGB shader permutation rather
  than treating CMAA2's unused alpha as temporal confidence. Forward and legacy
  shading leave temporal AA unavailable because they
  do not produce the required motion contract. Visibility Temporal
  Reconstruction remains mutually exclusive until that history uses the same
  jitter convention. Unavailable AA and Statistics guidance uses the same
  muted status color, and the AA explanation stays on two lines inside
  Settings. The complete method, quality, coordinate, reset,
  performance, and benchmark contract is in
  [`docs/miniengine-taa-options.md`](docs/miniengine-taa-options.md).
- Screen-space visibility traces AO/GI at selectable full, half, or quarter
  linear resolution. Visibility-owned temporal accumulation is not exposed;
  renderer TAA owns temporal stability in the current build. The **Spatial
  Reconstruction** section exposes an explicit **Unreconstructed Full
  Resolution Input** choice, guide-aware upsampling for reduced-resolution data,
  the two legacy joint-bilateral reconstruction methods, and the retained Intel
  edge-guided methods. Full-resolution visibility can therefore composite
  unfiltered current output without a spatial dispatch or filter target.
- Screen-space visibility uses one exact sample budget shared by AO and every
  GI bounce. The factory High preset traces 20 samples through the compact
  Runtime loop. **Samples** directly exposes the complete 1-64 range; there is
  no sampling-mode dropdown or Fixed/Generic shader family. **Distribution**
  stays with Estimator, Noise Pattern, Samples, Radius, and Thickness in
  **Shared Visibility Sampling**. Adaptive sparse sampling, its feedback
  resources, and the separate later-bounce count selector remain removed.
- The **Estimator** control exposes **Uniform Projected Angle**, **Uniform Solid
  Angle**, and **Cosine-Weighted Solid Angle**. Uniform Solid Angle is the
  default. The cosine path is fully compiled and uses the complete joint-cosine
  CDF, projected slice mass, `pi` GI normalization, and no duplicate receiver-
  cosine factor.
- Renderer settings always start from factory defaults; **Reset Settings**
  restores those defaults in-session, and settings are not carried between
  launches.
- The loading screen has no progress track. Its first line uses
  `Loading scene: <name>, please wait...`, with the suffix cycling continuously
  through one, two, and three dots while scene work remains active. It still
  reports real object, import-step, decoded-texture, and GPU-ready texture
  counts, without an unreliable launch timer.
- Off, 2x, 3x, 4x, and 5x are one cycle shared by the footer **Zoom** button and Z.
  The button text stays **Zoom** in every state. The first press enables 2x;
  pressing Z at 5x disables zoom, and the next press enables 2x again. The
  centered 50-percent white crosshair is visible only while zoom is active.
- The zoom panel occupies 28 percent of the renderer width at the top-right
  Settings margin. Its height is derived from that width and the current
  renderer aspect ratio. It copies the untouched presented scene before UI
  composition, then loads integer source texels without a sampler so the pixel
  under the crosshair becomes an exact 2x2, 3x3, 4x4, or 5x5 destination group. Its
  8 px rounded silhouette is cut away from that exact image before the
  full-weight Settings-style one-pixel translucent gradient outline is layered
  on top. The panel grows smoothly from a centered 86-percent rectangle while
  it fades in, and reverses that scale and opacity motion while fading out over
  180 ms. Changes between enabled magnification levels use a separate
  symmetric 180 ms scale pulse: the outgoing exact integer factor eases to the
  86-percent midpoint, switches without blending, and the incoming exact factor
  eases back to full size. Every intermediate rectangle remains integer-sized.
  A slightly shadowed bottom-center label reports the magnified pixel area as
  **4x**, **9x**, **16x**, or **25x** and follows the same fade, animated
  bounds, and midpoint level switch as the panel.
  Once the fade-out reaches zero, disabled zoom submits no capture or composite
  GPU work. Benchmark runs suspend it immediately.
- Settings launches hidden. The first Escape press opens only General;
  Visibility, Buffers, Statistics, Aliasing, Sky, and Lights start collapsed.
  The complete panel uses the magnifier-matched 180 ms grow-and-fade from a
  centered 86-percent rectangle in both directions, including every nested
  child surface and its backdrop blur. Its analytic outside-only shadow matches
  the magnifier's 10 px softness, 0.34 opacity, and 3 px downward offset.
- The settings overlay uses the installed Codex desktop app's Windows system
  UI face: 16 px Segoe UI Semibold with a 65-percent-wider word-space advance.
  Non-Windows builds fall back to bundled Geist 1.7.2 under the SIL Open Font
  License 1.1. The neutral-black panel stays at 0.60 opacity with a 4 px
  backdrop blur and one subdued transparent graphite treatment across every
  drawer body. Drawer headers use the authored transparent ImGui blue.
  Dropdown fields, dropdown-arrow and folder-button backgrounds, and slider
  tracks all reuse the panel's tinted-black RGB at 0.72 opacity, with
  opacity-only hover and active states. The four footer action buttons reuse
  the drawer's transparent graphite surface. Dropdowns replace ImGui's sharp
  arrow with a compact Bézier-rounded triangle, and the larger drawer and tree
  disclosure triangles use the same rounded-corner construction in both
  orientations. The Settings title-bar disclosure hover uses the menu's 4 px
  frame rounding. Slider knobs use the same transparent blue appearance as the
  drawer headers. Two-state toggles animate between endpoints; their active
  track is 50-percent-opaque white and their solid compensated knob matches the
  rendered header blue. Dropdown popups roll down and roll up at full alpha by
  clipping a fixed-size, fixed-layout popup; rows never stretch or reflow, and
  input received before roll-down completes is discarded instead of replayed.
  Selections remain staged for at least 250 ms, through the originating popup's
  exact scoped roll transition, every affected layout/appearance transition,
  and one later fully composed idle frame before renderer commit. A selection that
  changes dependent rows waits for roll-up, collapses the committed structure,
  swaps to staged structure only while hidden, and expands completely before
  that commit. Controls directly owned by an enabled/disabled toggle do
  not rest in gray:
  their old layout remains for the setting-change frame, then the complete
  region collapses over 180 ms after the renderer consumes the disabled state.
  Enabling applies the setting first and reverses the same animation.
  Top-level drawer bodies animate through the same measured-height path,
  including Visibility, Buffers, and Statistics, and every sibling header is
  separated by the authored vertical item-spacing gap. The outer edges retain
  an 8 px radius.
- The renderer/GPU summary and first performance line are pinned above an
  independently scrolling settings body, so they stay attached and visible at
  every drawer position. The panel shrinks to its open drawers and only scrolls
  when its content reaches the available screen height. Its fixed 29.3-font-
  height width is independent of changing status digits and is capped only by
  the available window width. Standard sliders and dropdowns retain the same
  track width through nested animated children.
  The performance line flows naturally from the left, and the status lines use
  an explicit 2 px gap. One queued snapshot captures and applies the top,
  visibility, and temporal-AA stats together 24 times per second. The first GPU clock
  sample is displayed directly; later hardware targets remain sampled every
  500 ms. Displayed gb/s follows each raw sample directly, while tflops alone
  moves toward each new target in 0.1 increments on each 24 Hz snapshot. The
  performance line reports resolution, frustum-culled submitted triangles,
  current-clock memory bandwidth, utilization-adjusted current-clock FP32
  tflops, frame time, and fps in that order, leaving fps at the outside edge.
  Triangle counts use compact labels such as `1.2m tris`. Millisecond and
  tflops values use one decimal place.
- Tree-row hover states, popup selections, and keyboard selection highlights
  use the same 4 px radius as other controls. The material editor continuously
  auto-fits its selected material, including immediately after a new surface is
  picked. Every text tooltip uses one compact fixed width and height at every
  nesting depth, with wrapped copy and a consistent inner safety margin.
- Press **M** to open or close the material editor. Selecting a scene material
  does not open the editor automatically.
- The four footer actions use explicitly centered labels to compensate for the
  system font's visual baseline.
- The single **Noise Pattern** dropdown compares **Independent Hash** and the
  first-party **Toroidal Blue** rank field. Both are selected by a
  frame-coherent runtime-uniform branch in the shared Runtime shader. The
  unpacked and packed Offline assets, upload paths, bindings, and shader
  families are retired. Noise Pattern appears immediately below Estimator.
- The **Profile** dropdown directly beneath **Sampling Resolution** begins with
  exactly four product presets. **Low** uses Uniform Projected Angle, quarter
  resolution, 8 exact samples, and compact joint-bilateral upsampling;
  **Medium** uses Uniform Solid Angle, half resolution, the same 8 samples,
  and the same upsampler. Factory-default **High** uses full resolution and
  20 samples; **Ultra** uses full resolution, 48 samples, and two GI bounces.
  High and Ultra use unreconstructed full-resolution input. Every preset uses
  Toroidal Blue. Low, Medium, and High select Performance Precision buffers;
  Ultra selects Default Precision buffers. Failed
  experiments, diagnostic floors, and implementation-profile presets are not
  packaged or selectable, while the retained controls remain independently
  editable.
- While a benchmark is queued, warming up, or collecting data, an independent
  top-right overlay remains visible even when the settings UI is hidden. It
  animates from `Benchmarking.` through `Benchmarking...` and reports collected
  measured frames over the requested total, for example
  `Benchmarking... (67/420)`. Warm-up frames intentionally do not increase the
  collected count.
- Controlled Intel measurements rejected the XeGTAO profiles as a faster UVSR
  replacement, the packed-edge 4x4 paths, and the per-function math
  approximations. Their UI, benchmark entries, host paths, shaders, and
  test fixtures have been removed. The optimization ledger retains the measured
  evidence and rejection reasons.
- The PS4 4x4-by-6 scheduler, scalar and packed-gather spatial paths, prepared
  depth surface, coupled temporal pass, profiles, shader permutations, test
  fixtures, and the separate analytic-horizon attribution control have been
  removed. Their source and timing evidence remains in the ledger.
- The collapsed **Statistics** drawer begins with an effect selector for the
  **Complete Renderer**, **Geometry**, **Direct Lighting**,
  **Screen-Space Visibility**, **Anti-Aliasing**, **Material Picking**,
  **Environment Background**, **Tone Mapping**, or **Output Blit**.
  Screen-space visibility expands into its outer effect envelope, named-stage
  total, signed unattributed timer difference, depth preparation, first trace,
  one combined later-bounces row, spatial denoise,
  fused spatial denoise/upsample, required upsample, fused
  resolve/application, and composition. No stage is labeled **Other** and
  unrelated concepts are not combined. Benchmark controls and the last result
  table are in this same drawer. Two memory
  rows report exact logical **Outputs**,
  **Working**, **Mask Cache**, and **Avoided** payloads; **Shared** is explicitly
  an estimate of duplicate mask payload avoided by shared AO/GI traversal.
- Visibility controls use compact, scrollable sections modeled on the
  established AA panel: full-width dropdowns and dedicated Noise, Spatial
  Reconstruction, and Resolve areas. **Buffers** is its own sibling drawer
  directly below **Visibility**, and **Statistics** follows it. The unified
  **Profile** dropdown provides **Low**, **Medium**, **High**, and **Ultra** as
  the only presets. Only the genuinely AO-only fused final-application choices
  remain labeled **(Mutex GI)**.
  Benchmark and scene locations use folder buttons instead of displaying long
  filesystem paths in the main panel. Ordinary resolution, estimator, AO, or
  GI edits clear the quality preset and switch the selector to custom settings,
  so a preset label cannot silently survive a renderer fallback. Custom labels
  retain their originating recipe, such as **Medium (Custom)**. Buffer-format
  edits also clear the quality preset because the four recipes own
  their starting buffer formats. Every compatible custom setting remains
  active; the internal generic fallback used to compose those settings is not
  exposed as a selectable profile.
- The default deferred UVSR PBR path starts enabled. **Visibility > Enabled**
  turns visibility and PBR off or on together. The legacy Donut comparison path
  remains implemented for possible future experiments, but its separate control
  is hidden.
- **Screen-Space Shadows** starts disabled and is available only for deferred
  UVSR PBR with a primary directional light. A small UVSR adapter consumes the
  existing single-sample reverse-Z depth buffer while keeping Bend Studio's
  released CPU and GPU headers byte-for-byte unchanged, as recorded in the
  [vendored source record](third_party/bend_sss/README.md). The pass writes one
  full-resolution `R8_UNORM` visibility texture, and deferred lighting applies
  it only to the pointer-matched primary directional light's direct
  contribution.
- The shadow **Profile** menu offers **Performance**, **Balanced**, **Quality**,
  and **Custom**. Performance restores 60 samples, 4 hard samples,
  8 fade-out samples, `0.005` surface thickness, `0.02` bilinear threshold,
  contrast `4`, and every optional mode and diagnostic off without changing
  **Enabled**. Balanced applies the same reset with a 240-pixel length; Quality
  applies it with a 960-pixel length. **Length** selects compiled
  `SAMPLE_COUNT` variants of 60, 120, 240, 480, or 960 pixels. The drawer also
  exposes **Surface Thickness**, **Bilinear Threshold**, **Shadow Contrast**,
  compiled **Hard Shadow Samples** and **Fade-Out Samples**, **Ignore Edge
  Pixels**, **Precision Offset**, **Bilinear Offset Mode**, and **Early Out**.
  **Debug View** presents Bend's Edge, Thread, or Wave output directly as
  grayscale R8 visibility.
- Bend, **Sparse Virtual Shadow Maps**, and **Diagnostic Cascaded Shadow Maps**
  are independent, initially disabled directional-light visibility producers.
  Each resolves its own full-resolution linear `R8_UNORM` texture. A
  producer-neutral three-slot deferred-lighting interface multiplies complete
  factors only for their exact pointer-identical light, so any producer can be
  removed or ported alone. Unsupported, missing, invalid, dirty, over-budget,
  or out-of-range SVSM samples fall back to a valid coarser clipmap and then to
  white.
- The SVSM **Profile** menu offers **Performance**, **Balanced**, **Quality**,
  and **Custom**. Performance uses adaptive page-safe nearest-Poisson filtering
  with 8 taps, a global `+1` resolution bias, a temporary moving-light `+2`
  bias, and receiver-distance clamping. Balanced uses page-safe bilinear PCF
  with 4 taps, no global bias, a temporary moving-light `+1` bias,
  receiver-distance clamping, and adaptive filtering off. Quality uses
  page-safe bilinear PCF with 8 taps, no global or moving-light bias, no
  receiver-distance clamp, and adaptive filtering off. All three use the
  validated cache, static zero-work, packet culling, batching, sorting, and
  empty-work skips. **First Clipmap Extent** and **Maximum Light Depth** remain
  scene controls. **Dense Reference** explicitly backs all six 8192-square
  atomic-depth clipmaps and can require about 1.5 GiB, so it is intended only
  for validation.
- The normal SVSM surface keeps **Profile**, scene extent and light-depth range,
  filter kernel, tap count and adjacent **Adaptive Filtering**, global
  resolution bias, and receiver-distance clamping visible. Collapsed
  **Developer Options** contains four default-collapsed raw subgroups:
  **Resources And Cache Policy**, **Movement And Invalidation**,
  **Culling And Raster**, and **Unabstracted**. The last retains low-risk
  optimization switches that are trending toward always-on behavior while
  preserving their reference paths for validation. Collapsed **Diagnostics**
  owns benchmarks, detailed stage timing, debug views, and counters.
- Custom SVSM controls independently expose per-pixel or conservative tiled
  marking, 1/4/8/16-tap filtering, resolution policies, static reuse,
  localized invalidation, GPU-gated and batched packet submission, packet-page
  culling, guarded dirty scatter, alpha-tested specialization, and page-safe
  translation reuse. **Mode** is the single cache-policy selector,
  **Moving-Light Resolution Bias** is the single `Off`/`+1`/`+2` state, and
  enabling **Dirty Page Scatter Raster** also enables its mandatory
  amplification guard; duplicate checkboxes no longer create contradictory
  states. Fine-caster exclusion is not exposed because UVSR cannot yet prove
  every exclusion condition conservatively. The motion benchmark uses the
  total-only timing path so inner query resolves do not perturb its result, and
  it never changes or measures Bend settings.
- Diagnostic CSM is a conventional UE5-style comparison path, not UVSR's
  preferred shadow renderer. One shared persistent depth array, opaque and
  alpha-tested caster path, and full-resolution receiver serve one through four
  cascades. UE-reference profiles prefer the D3D12 R16-typeless/D16 depth path,
  nine-Gather4 manual 5-by-5 receiver, dynamic-light split exponent four,
  quadratic distance fade, inward final-cascade fade, four-texel UE phase
  snapping, normalized vertex depth/slope bias, and UE receiver bias from the
  GBuffer shading normal. Effective opaque depth-state merging, a position-only
  opaque vertex permutation, conservative projected-hull caster culling, and
  UE's radius threshold and minimum directional subject-depth span are included.
  View-dependent caster culling is automatically disabled for cached maps so
  reuse cannot omit off-camera casters. Each optional depth-path optimization
  remains an independent Custom control; D16 capability failure falls back to
  sampleable D32. The profiles provide **Single-Map Reference**, **Low-Cost CSM**,
  **UE5 CSM Reference**, **Cached Single Shadow**, **Optimized Cached Single
  Shadow**, **Optimized Cached CSM**, and **(Custom)**. Cache controls change
  only update policy: whole-map reuse, whole-cascade reuse, conservative old and
  new dirty rectangles, and exact integer-texel scrolling remain independent.
  The drawer reports paired setup, culling, clear/update, raster, and sampling
  timings plus an explicit SVSM coverage, resolution, filter, and depth match
  check. Debug views show visibility, cascade selection, and cache action.
- The normal CSM surface keeps **Enabled**, **Profile**, **Cascades**,
  **Resolution Per Cascade**, **Maximum Shadow Distance**, **Maximum Light
  Depth**, **Filter**, **Filter Taps**, and **Filter Radius** visible. Collapsed
  **Developer Options** contains four default-collapsed subgroups:
  **Projection And Bias**, **Cache Update Policy**, **Culling And Raster**, and
  the further nested **Unabstracted** group. **Unabstracted** retains reversible
  low-risk implementation paths that are trending toward always-on behavior,
  preserving their reference paths for exact validation. Collapsed
  **Diagnostics** owns detailed GPU stage timing, debug views, and live stat
  lines.
- The **General** drawer contains **Graphics Adapter**, **Camera Mode**, and
  **Camera Location** for the standardized Sponza scenes. **World Materials**
  contains the White World presentations and the **Indirect Diffuse Response**
  view. **World Scenes** labels the scene picker at the bottom.
  Named multi-model descriptors appear as one clean entry while their component
  GLBs stay available to explicit command-line loads without cluttering the
  picker.
- **White World Off** is the default. **White World On**, **White World Preserve
  Normals**, and **White World Preserve Emissives** override material color
  without modifying source assets. The last mode keeps authored emissive color
  alongside the scene's colored direct lights so authored emission remains easy
  to read.
- **Camera Mode** offers **Freelook** and **Locked**. Freelook is
  collision-enabled: mouse and arrow keys rotate the view, A/D strafe left and
  right, the wheel applies a small damped dolly, and W/S dolly at up to 16% of
  the initial framing distance per second with smooth acceleration and finite
  deceleration. Space moves upward in world space and either Shift key moves
  downward. X rolls the camera left, C rolls it right, V levels only that roll
  while preserving position and look direction, and Z is reserved for the
  pixel-zoom cycle.
  Moving inward gently
  lowers dolly sensitivity on a linear scale that bottoms out at 40% of the
  starting speed; the floor affects speed rather than position, so the eye
  remains free to continue forward without converging on a fixed pivot.
  Q/E translation stays disabled. Locked freezes the current view.
  Camera keys and mouse buttons are reconciled with physical input after UI or
  window focus transitions, preventing a consumed release event from latching
  motion. Right-click remains camera input; middle-click performs material
  picking so a right-click cannot teleport the camera to a picked surface.
- **PBR Sponza Decorated** and **PBR Sponza Plain** open in **Freelook** at
  **Benchmark Position 1**, the
  `intel-pbr-sponza-courtyard-simplified-v1` preset. The **Camera Location**
  dropdown contains that named location and an always-selectable **Piloted**
  entry,
  leaving room for more named locations later. Choosing Benchmark Position 1
  recalls the complete pose without changing Camera Mode. After translation or
  rotation moves the view away from the recalled pose, the dropdown reports
  Piloted. Choosing Piloted also detaches the location name without moving or
  reorienting the camera. The preset uses a 60-degree perspective view and a
  1920x1080 reference frame.
- The primary directional sun, `sun_1`, is selected automatically in the
  **Lights** panel. When Lights is opened, **Bend Screen-Space Shadows**,
  **Sparse Virtual Shadow Maps**, and **Diagnostic Cascaded Shadow Maps** start
  expanded with their **Enabled** toggles off. Lights itself remains closed at
  launch, preserving the first-Escape General-only view.
- Authored emissive radiance remains visible in forward, deferred, MSAA, and
  G-buffer rendering, but it is no longer classified, boosted, or transported
  as a screen-space GI source. First-bounce diffuse transport now comes only
  from shadowed direct diffuse and directly reflected diffuse environment
  lighting.
- **Indirect Diffuse Response** in **World Materials** is the sole retained
  visibility diagnostic. It displays the material-applied screen-space diffuse
  GI contribution without direct light, diffuse environment, or AO-only
  darkening. Selecting it turns White World off; selecting any White
  World presentation exits the diagnostic. The entry is available only while
  deferred PBR visibility and effective diffuse GI are active; disabling a
  prerequisite returns the dropdown to **White World Off**.
- **Limit Bounces** is on by default. While on, **Bounces** selects one through
  eight finite diffuse bounces; one keeps the original compact shader path.
  Turning the limit off enables GPU-driven contribution termination. Each later
  bounce transports only the newest light frontier, and the continuation bar
  becomes four times stricter after every bounce. A wave-coalesced GPU flag and
  indirect dispatch turn every pass after convergence into zero work without a
  CPU readback. A 16-bounce fault guard contains malformed or non-contracting
  data; it is not the normal termination condition.
- **AO Power** defaults to its identity value of 1.0. The default compositor
  is a separate shader specialization with the power operation compiled out;
  moving the slider away from 1.0 selects the powered specialization.
- **Bounce Contribution Cutoff** skips higher-bounce source shading whose
  conservative exposed upper bound is too small to matter. The default is
  `0.001`; zero keeps exact-zero exits only in explicitly limited mode. With
  **Limit Bounces** off, it becomes the nonzero starting cutoff for the
  exponentially rising continuation bar.
- Later bounces reject receivers with proven-zero diffuse throughput before
  view-position reconstruction, normal fetches, or slice setup.
- AO, GI, the GI source-radiance target, temporal history, filtered outputs,
  depth hierarchy, and extra-bounce targets exist only while their consumers
  require them. AO strength zero or GI intensity zero removes that consumer
  while the other effect can continue independently. The source-radiance target
  is also absent when direct light and diffuse IBL are both inactive. The
  default directional mask remains
  register-local and consumes zero persistent mask-cache bytes.
- Proven scene-wide source inactivity terminates the complete higher-bounce
  dispatch chain. The shared CPU/HLSL activity mask is
  extensible to future clustering, probe, cache, visibility, and residency data;
  unknown sources always remain active.
- A shared, future-extensible contribution-gate contract also gives forward and
  deferred direct lighting exact early outs for zero, out-of-influence,
  back-facing, or fully occluded lights before unnecessary shadow/BSDF work.
- Forward, deferred, and screen-space composition share one persistent global
  image-based-lighting environment. One selected scene-linear radiance field
  produces the Lambert-convolved SH9 `E / pi` diffuse cube, the
  roughness-prefiltered GGX specular cube, and, optionally, the visible
  background. A source-independent split-sum environment BRDF LUT completes
  the specular receiver. Common exposure preserves the relationship among its
  three consumers: diffuse IBL, specular IBL, and the background. Independent
  `0.00` to `2.00` diffuse and specular strengths apply after exposure without
  changing the background; diffuse strength also scales the environment
  contribution that enters SSGI. All environment controls live in the **Sky**
  drawer.
- The source menu contains exactly six imported HDR radiance sources: three
  day/overcast skies, legacy Quadrangle Cloudy, and two dedicated nights:
  **Night - Kloppenheim 07** and **Starry Night - Qwantani**. There are no
  procedural selections or lighting fallbacks. Imported sources retain their
  authored relative energy; selecting one applies its documented calibrated
  default EV. A missing or invalid asset deactivates environment lighting and
  background to zero without retaining a stale source or retrying every frame.
  See the
  [environment catalog](assets/environments/README.md) for sources, hashes,
  and default exposures.
- UVSR uses one fixed neutral AgX display transform to convert scene-linear HDR
  radiance for display. The optional Tonemapper drawer, grading presets, LUT
  loader, and bundled film looks are strategically sunset while scene lighting
  is still developing. This is a sequencing decision, not a failed feature.
  The exact implementation and its paired revival contract with bilateral-grid
  local tone mapping are preserved in the
  [postmortem](docs/postmortem/tonemapper-drawer-and-luts-v1.md).

## No-Hidden-Ambient Invariant

Before IBL, UVSR always added the hidden two-color hemispherical term
`lerp(bottom, top, normal.y * 0.5 + 0.5)`. It illuminated every surface without
visibility, so fully shadowed regions looked filled even when the renderer had
computed no indirect light.

IBL integration removed that term. With both IBL lobes disabled or at `0.00`
strength, UVSR now shows shadowed direct lighting plus actual SSGI, and regions
with neither can reach deep black. This cleanup did not alter the direct BSDF
or the fixed neutral AgX tonemapper. This is a future-project invariant for
UVSR and renderers derived from it: do not restore the hemispherical term, a
procedural substitute, or any missing-asset ambient fallback.

## Coming Soon

Coming Soon is UVSR's user-facing roadmap and integration summary for stable,
active work that has not merged into `main`. It is not a mutex or a live task
ledger. An entry is not shipped on `main`, and experimental entries are not
promises that the work will merge.

- **Shader Path Retirement — Local Follow-on Candidate**
  (`codex/prune-shader-paths`). Consolidate visibility on the Runtime sampler,
  remove both Offline-noise deliveries and low-value TAA policies, simplify the
  affected Settings controls, and record a reusable shader-bloat decision
  framework. The first retirement batch is committed on the branch. The local
  follow-on removes emissive GI-source state, uncaps the AA motion benchmark,
  establishes the requested Lights defaults, and adds an opt-in
  factory-settings experiment catalog. It still depends on semantic
  reconciliation with the two visibility pull requests below.

- **Screen-Space Visibility Shared Shader Helpers — In Review**
  (`devin/1784102514-screen-space-shared-helpers`, PR #10). Consolidate shared
  depth, pixel-coordinate, and safe-normal helpers used by the visibility
  sampling, depth-hierarchy, temporal, and filter shaders. This is a mechanical
  extraction with no equations, bindings, UI, or scene changes.

- **Visibility Degenerate-Path Test Coverage — In Review**
  (`devin/1784102780-visibility-test-coverage`, PR #11). Add reference coverage
  for degenerate visibility clipping, radial-mask edge cases, and blue-noise
  rank-field paths. This owns only visibility test sources and has no runtime
  rendering, UI, or asset overlap.

### Roadmap Ownership

The task coordinator or final integrator owns this section:

1. Small fixes, read-only investigations, and short-lived private experiments
   do not require an entry.
2. Before complex, concurrent, shared-hotspot, integration, or publication work,
   the coordinator reviews this entire section together with relevant pull
   requests, branches, worktrees, and active execution plans. The coordinator
   records overlap and dependency decisions in the task plan; individual
   workers do not each edit this README.
3. Add or update an entry once its scope and branch are stable. Include status,
   branch when one exists, intended scope, affected subsystems, and integration
   dependencies. A private experiment is listed only when it becomes stable
   roadmap information.
4. Reconcile the entry again during integration. Publishing a roadmap update
   still requires explicit authorization and never grants permission to push,
   open a pull request, or merge implementation work.
5. When work merges, remove its entry and move durable user-facing behavior into
   the renderer baseline or relevant design documentation. Mark or remove
   abandoned work explicitly rather than leaving a stale promise.

## Build and Run

Requirements:

- Windows with a DirectX 12-capable GPU and driver
- CMake 3.24 or newer
- A C++17-capable Visual Studio toolchain
- Git submodules initialized

At startup, UVSR selects the D3D12-capable adapter with the most dedicated
video memory. The **Graphics Adapter** selector lists every compatible GPU and
restarts the renderer immediately on the selected adapter.

Intel PBR Sponza is completely included in the repository and staged by CMake;
there is no separate model download, conversion, or scene setup step. The
default **PBR Sponza Decorated** scene composes the flat-roof architecture,
curtains, and roof-trimmed ivy. **PBR Sponza Plain** loads only the same two
architecture components. Every component remains below GitHub's 100 MB
per-file limit. Attribution and the exact runtime edits are recorded in
[`assets/scenes/intel_sponza/README.md`](assets/scenes/intel_sponza/README.md).

Configure and build a Release executable from PowerShell:

```powershell
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --config Release --target uvsr
.\tools\launch_uvsr.ps1 -Experiment naming
```

For repeated code experiments that use only the shader topology selected by a
fresh launch, configure a separate opt-in build:

```powershell
cmake -S . -B build-experiment `
  -DUVSR_DEFAULT_SETTINGS_EXPERIMENT_SHADERS=ON
cmake --build build-experiment --config Release --target uvsr
.\tools\launch_uvsr.ps1 -Experiment naming `
  -BuildDirectory build-experiment
```

This profile compiles 51 UVSR permutations and stages 37 runtime shader blobs,
versus 516 first-party compile tasks and 76 blobs in the complete production
build. It locks renderer-topology drawers to factory settings and omits Bend,
SVSM, diagnostic CSM, CMAA2, non-default visibility, and developer shader
families. A fresh build tree still compiles Donut's pinned 76-task framework
catalog once. Use this profile to shorten repeated UVSR edits, not for release,
full-settings, diagnostic, or benchmark-matrix verification.

The launcher requires one lowercase ASCII word matching `\A[a-z]+\z`; uppercase
letters, digits, spaces, hyphens, underscores, and punctuation are rejected:

```powershell
.\tools\launch_uvsr.ps1 -Experiment naming
```

For a repeatable Sponza benchmark launch, add `--benchmark-camera`:

```powershell
.\tools\launch_uvsr.ps1 -Experiment benchmark --benchmark-camera
```

This flag selects and locks **Benchmark Position 1**
(`intel-pbr-sponza-courtyard-simplified-v1`), enforces a non-resizable
1920x1080 backbuffer, blocks fullscreen transitions, and selects **Locked** so
input cannot move or reframe the benchmark view. The disabled **Camera
Location** dropdown remains on Benchmark Position 1 throughout the benchmark
launch. The benchmark pose is shared by **PBR Sponza Decorated** and **PBR
Sponza Plain**; benchmark records identify it by that preset ID while the
separate `scene` field identifies which scene was measured.

### Anti-Aliasing Benchmark Overrides

Production builds accept `--aa-enabled`, `--aa-method`, `--aa-quality`
(`--aa-preset` alias), `--aa-sharpness`, `--aa-rectification`
(`preset`, `pair-rgb`, or `variance-ycocg`), and
`--aa-benchmark-output`. Experimental execution-path
overrides require a developer build:

The same motion path is available interactively from **Statistics > Run Current
With Motion** on either standardized PBR Sponza scene. It uses the current AA
settings, writes `aa-motion-test-latest.json` beside the executable, and returns
Camera Location to **Piloted** when it finishes.

```powershell
cmake -S . -B build-aa-dev -DUVSR_AA_DEVELOPER_OVERRIDES=ON
cmake --build build-aa-dev --config Release --target uvsr
```

A production build rejects `--aa-execution`, `--aa-kernel`, `--aa-lds`,
`--aa-reuse`, `--aa-early`, `--aa-fusion`, and `--aa-cache`
instead of accepting an override whose static PSO is absent.

### Visibility Benchmark Workflow

Run one visibility profile without UI and close after its in-memory timing
summary completes with:

```powershell
.\tools\launch_uvsr.ps1 benchmark --benchmark-camera `
  --visibility-profile runtime-ao-8t --visibility-benchmark `
  --benchmark-warmup 120 --benchmark-frames 240 --benchmark-auto-close
```

Profile matching ignores punctuation and case, so either the displayed
one-click name or a hyphenated form is accepted. `--benchmark-warmup` accepts
0 through 100000 frames and `--benchmark-frames` accepts 1 through 100000.
Add `--visibility-contribution-terminated-bounces` to a GI-capable profile to
turn **Limit Bounces** off before an automated run. This deliberately clears
the one-click verification label because the effective 16-entry,
GPU-terminated workload is no longer that preset's declared bounce contract.
Unknown or unavailable profiles and invalid frame counts report to standard
error and return a nonzero process exit code; they do not open modal dialogs.
The **Statistics** drawer provides **Run Current**, **Run Current With Motion**,
and **Cancel**. Run Current measures the effective visibility configuration
being rendered, even when it no longer matches the selected preset label, and
keeps its latest stage medians and p95 values in memory for the Statistics
drawer. Run Current With Motion exercises the current AA configuration through
the controlled 45-degree path without wall-clock pacing. Its turn advances
`0.375` degrees per rendered frame, so faster rendering finishes sooner without
changing the camera samples. Both actions lock Benchmark Position 1 as required;
the visibility run resizes to 1920x1080, waits for the matching rendered
workload, and restores the previous interactive window size afterward.
The former comparison, test-matrix runners, result export, benchmark-folder UI,
`--benchmark-output`, and `--benchmark-sequence` have been removed. A live
`Benchmarking... (completed/total)` overlay continues animating while Settings
is hidden.
Readiness is based on the workload and permutation reported by the renderer,
not on a possibly stale preset label. A run can remain unavailable only while
the current settings have not reached the GPU, while no AO/GI effect is active,
outside deferred rendering, during another run, or outside PBR Sponza Decorated
and PBR Sponza Plain. The Sponza restriction remains because those are the only
scenes with the standardized camera used for comparable results.

### Advisory GPU Clock Normalization

Agents and developers may retain an unofficial clock-normalized trend when the
same physical GPU runs an identical workload at different graphics clocks.
Read `docs/performance/gpu-clock-normalization.md` and use
`tools/normalize_gpu_clock_benchmark.ps1`. Every physical GPU selects its own
measured reference clock; normalized values must never be compared between
different GPUs, including separate GPUs with the same model name. Raw clean-run
GPU time remains the official score. Utilization, memory behavior, power,
temperature/headroom, limiter state, telemetry age, background load, warmup,
sample coverage, and before/after controls determine the estimate's quality
rather than becoming hidden correction factors. The guide also records small
display and scheduling influences such as monitor refresh rate, VRR, VSync,
frame caps, compositor state, secondary displays, presentation route, capture
tools, and periodic spike alignment.

After building, Windows users can also double-click `LaunchUVSR.cmd`. It
delegates to the same required experiment launcher with a fixed main-build
label; optional renderer arguments can be appended from a terminal.
`tools/launch_uvsr.ps1` also accepts `-BuildDirectory <path>` when an isolated
production or experiment build needs to be launched without replacing the
main build.

Windowed launches request a 1920x1080 client area, keep both dimensions
divisible by eight, and center the DWM-visible frame in the selected monitor's
usable work area. The visible gap above the title bar therefore matches the gap
between the window and the taskbar whenever the work area can contain the
requested frame. UVSR requests Windows High process priority at startup;
controlled performance captures can explicitly request Normal priority for
matched comparisons.

The title reports the active graphics API followed by the description, the
seven-character source commit embedded at build time, and the local launch time
in 24-hour `HHmm` form. Each field is separated by a dash, for example
`UVSR Renderer D3D12 (naming-b216081-2117)`. CMake watches the worktree's Git
HEAD and branch ref so the embedded commit refreshes on the next build after a
commit or checkout. Direct and IDE-driven launches can instead supply the one-
word description through `--experiment naming` or the `UVSR_EXPERIMENT`
environment variable; omitted descriptions default to `main`.

The first configure may download Microsoft's Direct3D 12 Agility SDK if it is
not already cached.

### Component-Only Builds

Bend, SVSM, and diagnostic CSM are separate static-library targets. The full
UVSR application integrates all three, while a component-only configuration can
omit the other producers and their reference tests entirely. Each component
target publishes the include dependencies needed by its public header and
compiles only its own shader catalog.

Build only SVSM and its deterministic reference test:

```powershell
cmake -S . -B build-svsm -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS=OFF -DUVSR_BUILD_DIAGNOSTIC_CASCADED_SHADOW_MAPS=OFF
cmake --build build-svsm --config Release --target uvsr_sparse_virtual_shadow_maps uvsr_sparse_virtual_shadow_map_tests
```

Build only Bend screen-space shadows and its deterministic reference test:

```powershell
cmake -S . -B build-bend -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS=OFF -DUVSR_BUILD_DIAGNOSTIC_CASCADED_SHADOW_MAPS=OFF
cmake --build build-bend --config Release --target uvsr_bend_screen_space_shadows uvsr_bend_screen_space_shadow_tests
```

Build only diagnostic CSM and its deterministic reference test:

```powershell
cmake -S . -B build-csm -DUVSR_BUILD_APPLICATION=OFF -DUVSR_BUILD_BEND_SCREEN_SPACE_SHADOWS=OFF -DUVSR_BUILD_SPARSE_VIRTUAL_SHADOW_MAPS=OFF
cmake --build build-csm --config Release --target uvsr_diagnostic_cascaded_shadow_maps uvsr_diagnostic_cascaded_shadow_map_tests
```

Build every registered Release target before running the full suite. This
includes the scene, camera, PBR, AA/UI, screen-space visibility, Bend,
diagnostic CSM, SVSM, environment, command-line, benchmark, and runtime shader
bundle contracts:

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Documentation and Conventions

The [UVSR UI design and integration reference](docs/ui-integration-agent-procedure.md)
is the single source for visible hierarchy, Title Case, copy and value
formatting, visual tokens, spacing, reset behavior, animations, scrolling,
renderer-facing dropdown transactions, implementation procedure, and required
verification. Its version line is authoritative: an implementer records that
exact version before editing and in the handoff so a stale reference can be
identified when a UI element does not match the current contract.

The [PBR foundation](docs/pbr-foundation.md) documents the material contract,
G-buffer packing, equations, validation, limitations, and extension points.

The [screen-space visibility design](docs/screen-space-visibility.md) documents
the shared 32-sector AO/GI traversal, resources, coordinate/radiance contracts,
controls, limitations, and the upgrade path to persistent unified visibility.

The [AO optimization ledger](docs/ao-optimization-ledger.md) inventories every
supplied, Activision, XeGTAO, and further-research candidate; records its
classification, evidence, quality boundary, zero-cost-off disposition, and
measurement method; and ranks all implemented runtime families with explicitly
non-additive engineering forecasts. Its
[Remaining Feature Scorecard](docs/ao-optimization-ledger.md#remaining-feature-scorecard)
provides four 0-100 rankings for universal performance, situational
performance, UI nonredundancy, and their unweighted average. XeGTAO is retained
there as rejected
historical evidence, pinned to Intel
commit `a5b1686c7ea37788eeb3576b5be47f7c03db532c`; published Intel timings are
reported only as upstream provenance and never as UVSR measurements or promises.

The [visibility DXIL evidence](docs/visibility-dxil-evidence.md) provides a
reproducible historical static generated-shader comparison for the core
Reference, candidate, diagnostic, reconstruction, and fusion permutations.
Diagnostic entries describe the investigation and are no longer packaged. It does not
substitute static IR counts for target-GPU timings or physical Intel register,
spill, SIMD-width, and occupancy data.

The continuous [shader path retirement postmortem](docs/postmortem/shader-path-retirements.md)
records each removed shader family, the multiplication mechanism that caused
its bloat, the evidence and restoration boundary, and the ranked candidates for
the next retirement batch.

The [visibility estimator validation](docs/visibility-estimator-validation.md)
records the shared C++/HLSL measure contracts, deterministic reference fixtures,
and the boundary between automated evidence and required runtime evaluation.

The [retired Visibility Sample Rotation v1 notes](docs/visibility-sample-rotation-v1.md)
define the supported layout, exact four-phase sequence, history convention,
resource contract, and technical evidence. Its
[postmortem](docs/postmortem/visibility-sample-rotation-v1.md) records the negative
visual result, lessons, and explicit triggers for any future reconsideration.

The [experiment postmortem archive](docs/postmortem/) preserves retired work,
supporting evidence, and restart guidance, including the
[native-resolution analytical/reconstructive temporal anti-aliasing v1 postmortem](docs/postmortem/native-resolution-analytical-reconstructive-temporal-anti-aliasing-v1.md).

All Markdown headings, standalone bold headings, and initial bold list-item
headings use conventional English Title Case. Run
`tools/check_document_title_case.cmd` to validate the entire tracked
documentation set plus nonignored new Markdown files; the same check runs for
documentation changes on GitHub.

UVSR runs uncapped with a single planar view. UVSR-owned interactive controls
provide short, plain-English hover tooltips; new controls should follow the same
convention.
The bottom action row exposes equally sized **Reset**, **Screenshot**, **Zoom**,
and **Restart** buttons.

## Intentional Omissions

The current baseline intentionally omits:

- DirectX 11 and Vulkan backends
- VSync, stereo, and bloom controls
- Imported scene cameras and translucent rendering
- Animation playback, ambient-intensity scaling, and material-event
  instrumentation
- A preferred production shadow-map renderer; the conventional CSM path is a
  disabled diagnostic for SVSM comparisons
- Local scene-probe capture, parallax-corrected probe volumes, and probe
  blending; the implemented IBL path is one infinite global environment

## Repository Naming

Use the lowercase engineering slug `uvsr` for repository URLs, terminal
commands, package names, and folder paths:

```text
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
```

The displayed project name remains **UVSR**.
