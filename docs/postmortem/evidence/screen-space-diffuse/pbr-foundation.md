# UVSR PBR Foundation

## Scope and Invariants

UVSR owns one scene-linear metallic-roughness BSDF in `src/pbr.hlsli`.
Deferred and forward shading both include that file; the fixed neutral AgX
transform, output gamut conversion, and display transfer happen afterward.

- Material roughness is perceptual roughness. Microfacet roughness is
  `alpha = max(perceptualRoughness², 0.002)`.
- Light and view directions point away from the surface.
- Visibility is always `0 = occluded`, `1 = visible`.
- Emission is additive radiance and is never multiplied by visibility.
- Opacity is carried through material evaluation and the G-buffer. Existing
  alpha testing remains active; transmission/refraction is not part of the
  base BSDF.
- Single-sided normal orientation follows raster front-face classification.
  Double-sided material normals are instead oriented into the view hemisphere
  before forward shading or G-buffer encoding. This preserves ordinary
  back-face lighting and valid negative-determinant glTF instances even when
  their declared winding differs from the shared rasterizer state.
- Ambient occlusion is not part of the BSDF or direct lighting. Authored
  material occlusion modulates global diffuse IBL, roughness-aware specular
  occlusion, and screen-space diffuse transport. When screen-space ambient
  visibility is active, it attenuates diffuse IBL and joins authored
  material occlusion as the input to specular occlusion. Screen-space GI is
  not multiplied by screen-space ambient visibility.

## CPU Material Contract

`PbrMaterialParameters` in `src/pbr_material.h` contains:

| Field | Default | Valid range/meaning |
|---|---:|---|
| `baseColor` | `(1,1,1)` | Linear dielectric diffuse color or metallic F0, clamped to `[0,1]` |
| `metalness` | `0` | `[0,1]` |
| `perceptualRoughness` | `0.5` | `[0,1]` |
| `ior` | `1.5` | `[1,3]` in the initial upload path |
| `emissive` | `(0,0,0)` | Nonnegative scene-linear radiance |
| `opacity` | `1` | `[0,1]` |
| `featureMask` | `0` | Eight reserved feature bits |

`ValidatePbrMaterialParameters` repairs non-finite values and clamps authored
values. `ApplyPbrMaterialParameters` runs after scene import and whenever UVSR
restores/normalizes loaded materials.

Donut's metallic-roughness constant layout has no IOR field, but its
`specularColor` field is unused in that workflow. UVSR uploads the IOR-derived
dielectric F0 through that existing field, avoiding another material constant
buffer or binding. Specular-gloss materials retain their original field and
are reconstructed to metallic-roughness by the shader with the default IOR
1.5 dielectric baseline.

Feature-mask bits are reserved for coat, anisotropy, translucency, refraction,
scattering, thin-film iridescence, absorption, and dispersion. Existing
subsurface and transmission metadata maps to reserved bits, but no new lobe is
evaluated yet.

## G-Buffer Layout

The original four material targets total 24 bytes per pixel. UVSR changes
their interpretation without increasing their sizes, then adds one `R8_UNORM`
attachment for authored material ambient occlusion. Total always-on PBR
G-buffer bandwidth is 25 bytes per pixel, excluding depth. Picking uses a
separate `RG16_UINT` target and runs its material-ID geometry pass only after a
pick request; it is not an every-frame MRT.

| Target | Format | Channels |
|---|---|---|
| G0 | `SRGBA8_UNORM` | Linear base color in RGB through hardware sRGB conversion; opacity in A |
| G1 | `RGBA8_UNORM` | Octahedral geometric normal in RG; IOR remapped from `[1,3]` in B; 8-bit feature mask in A |
| G2 | `RGBA16_SNORM` | Linear shading normal in RGB; perceptual roughness in A |
| G3 | `RGBA16_FLOAT` | Scene-linear emissive radiance in RGB; metalness in A |
| G4 | `R8_UNORM` | Authored material ambient occlusion for global diffuse/specular IBL and screen-space diffuse transport |
| G5 (conditional) | `RGBA16_FLOAT` | Current-to-previous pixel motion in XY; previous-minus-current device-depth delta in Z; validity in A |

Base-color quantization follows ordinary sRGB8 material storage. Geometric
normals have 8 bits per octahedral component; this is sufficient for
back-side validity checks but not intended for high-frequency shading.
Shading normals and perceptual roughness use signed 16-bit normalized storage.
Emission and metalness use half floats. IOR has 256 steps over `[1,3]`, which
provides finer common-dielectric F0 precision than storing raw F0 in UNORM8.
Feature flags are exact at eight bits. Material ambient occlusion has eight
linear bits. The separate picking target retains the original 16-bit material
and instance channels, so scenes with more than 65,535 entries can alias during
picking; visibility does not consume those IDs. G5 exists while temporal
screen-space visibility needs velocity or while Deferred MSAA visibility needs
a coherent closest-sample guide set. Its XY convention is
current-to-previous pixels; Z is previous-minus-current device depth; A
distinguishes a valid zero velocity from cleared background or a previous point
behind the camera. The conditional target is not counted in the 25-byte
always-on total.

Deferred MSAA keeps G0–G5 and depth multisampled through material decode and
direct lighting. Screen-space visibility does not average these attributes.
Instead, a static 2x/4x/8x compute permutation copies every guide from the same
closest valid reverse-Z sample into a single-sample visibility G-buffer. The
final per-sample lighting resolve applies the signed visibility correction in
proportion to raster coverage, leaving uncovered sky samples unchanged.

The deferred decoder normalizes both normals and flips an invalid shading
normal back into the geometric-normal hemisphere. The BSDF rejects light or
view directions below the geometric surface. A full shading-normal energy
correction is intentionally left for a later transport-focused revision.

## Implemented Equations

Dielectric normal-incidence Fresnel:

`F0 = ((ior - 1) / (ior + 1))²`

Metallic workflow:

- `specularF0 = lerp(dielectricF0, baseColor, metalness)`
- `diffuseColor = baseColor * (1 - metalness)`

Schlick Fresnel:

`F = F0 + (1 - F0) * (1 - VoH)^5`

Lambert with Fresnel energy sharing:

`diffuse = diffuseColor * (1 - F) / PI`

GGX/Trowbridge-Reitz distribution:

`D = alpha² / (PI * (NoH² * (alpha² - 1) + 1)²)`

Height-correlated Smith-GGX visibility:

`V = 0.5 / (NoL * sqrt(NoV² * (1 - alpha²) + alpha²) + NoV * sqrt(NoL² * (1 - alpha²) + alpha²))`

Single-scattering specular:

`specular = D * V * F`

Lobe PDFs are exposed for Lambert cosine sampling and GGX NDF reflection
sampling. Sampling itself is not included in this raster foundation, so there
is no non-visible-normal sampling routine to replace; a future sampling path
must add Heitz visible-normal GGX sampling and its matching PDF together.

Direct-light evaluation keeps its terms explicit:

`result = incidentRadiance * BSDF * max(dot(Ns,L),0) * visibility / samplingPdf`

All current raster lights are deterministically enumerated, so light-selection
and directional PDFs are both one. Directional lights have constant incident
radiance. Point and spot lights use inverse-square attenuation with a minimum
distance squared of `1e-4`; their authored range, when nonzero, supplies the
only smooth range cutoff. Spot lights add their authored cone falloff.

## Global Image-Based Lighting

UVSR owns one infinite global IBL environment. It is deliberately not a
general probe-volume system: one selected radiance source, one persistent
resource set, and one receiver implementation serve forward, deferred, and
screen-space composition.

The selected imported source is decoded once in scene-linear radiance and
converted to a persistent 512-by-512 `RGBA16_FLOAT` source cube with ten mips.
Both radiance-derived lighting maps and the background come from that exact
cube or the exact source samples used to build it:

1. A CPU SH9 projection applies the normalized Lambert kernel and writes a
   16-by-16 `RGBA16_FLOAT` cube containing unit-albedo outgoing diffuse
   response `E / pi`. Receivers apply material diffuse weight and occlusion;
   they do not divide by pi again.
2. Mip zero of a 256-by-256 `RGBA16_FLOAT` specular cube retains the sharp
   source. Its remaining eight mips use Donut's 1,024-sample GGX prefilter.
   Mip `m` is generated for perceptual roughness
   `(m / (mipCount - 1))^2`, and receivers select
   `sqrt(perceptualRoughness) * (mipCount - 1)`.
3. **Show Environment Background** depth-tests the unfiltered source cube
   behind scene geometry. It uses the same cube orientation and radiance
   scale as lighting; it is not a separately exposed or graded sky image.

Donut's deterministic 1,024-sample split-sum integration builds one persistent
64-by-64 `RG16_FLOAT` environment BRDF LUT. The LUT is source independent; it
completes the material receiver as
`prefilteredRadiance * (F0 * A + B)`.

The shared receiver applies roughness-aware Schlick diffuse/specular energy
sharing. It also fades reflections that a perturbed shading normal sends below
the geometric horizon. Forward and deferred lighting use the same evaluator.
When screen-space visibility owns the final indirect composite, both global
IBL lobes move to that composite so occlusion is applied exactly once.

### No-Hidden-Ambient Invariant

Before IBL, the UVSR receiver always added a hidden two-color hemispherical
ambient term:
`lerp(bottom, top, normal.y * 0.5 + 0.5)`. That normal-only gradient illuminated
every surface without visibility and could not distinguish open sky from a
fully enclosed or fully shadowed region.

IBL integration removed the term rather than layering the new environment over
it. When both IBL lobes are disabled or have zero strength, the indirect
environment contribution is exactly zero; the remaining image contains
shadowed direct lighting, emission, and actual SSGI, so regions with no
contribution can reach deep black. Neither the shared direct-light BSDF nor the
fixed neutral AgX tonemapper changed as part of this removal. This is a
future-project invariant: do not restore the hemispherical term or replace it
with a constant, procedural, missing-asset, or other visibility-free ambient
fallback.

### Sources, Exposure, and Strength

| UI Source | Kind | Default Exposure |
|---|---|---:|
| **Day - Kloppenheim 03** | Imported balanced day; factory default | `-2.75 EV` |
| **Bright Overcast - Snow Field 2** | Imported low-contrast overcast | `-2.50 EV` |
| **Soft Day - Farm Field** | Imported neutral-warm day | `-3.25 EV` |
| **Night - Kloppenheim 07** | Imported dedicated cloudy night | `-5.00 EV` |
| **Starry Night - Qwantani** | Imported dedicated clear night | `-6.50 EV` |
| **Legacy - Quadrangle Cloudy** | Imported comparison source | `-3.00 EV` |

These are the complete six-source catalog; UVSR has no procedural source. The
files and source hashes are recorded in the
[environment catalog](../assets/environments/README.md). Each source retains
its authored relative radiance and is not normalized to another source.
Selecting a source restores its calibrated default exposure.
The two night selections do not rotate, recolor, or disable the separate
directional scene light.

All IBL controls are grouped in the **Sky** drawer. Exposure is one common
`2^EV` multiplier for diffuse IBL, specular IBL, and the optional background.
Changing it does not regenerate any texture. **Diffuse Strength** and
**Specular Strength** independently range from `0.00` to `2.00`, default to the
`1.00` radiometric reference, and multiply their lobes after common exposure.
They do not change the background. **Diffuse IBL** and **Specular IBL**
independently set their lobe scale to zero when disabled; a zero strength has
the same exact-zero lighting result. **Show Environment Background** is
independent from both lighting toggles. White World neutralizes the selected
radiance before all derivations and applies the same common reference scale to
every consumer.

An unchanged imported source performs no upload, convolution, prefilter, or
BRDF-LUT work after warmup. The BRDF LUT is generated once. Exposure, lobe, and
strength controls update constants only. A missing or invalid imported asset
clears the active probe and background, deactivating the environment to exact
zero. It cannot reveal a procedural fallback or retain a stale previous
source. The failed request is latched so the render loop does not retry
synchronous disk I/O or repeat the warning every frame.

### Ambient and Specular Occlusion

With screen-space visibility disabled, authored material AO linearly
attenuates diffuse IBL and supplies the roughness- and view-aware specular
occlusion input. It never attenuates direct lighting.

With screen-space visibility enabled, deferred lighting leaves both global
IBL lobes for the final composite. Adjusted screen-space ambient visibility
linearly attenuates diffuse IBL. Its product with authored material AO feeds
the specular-occlusion function, keeping a covered interior from reflecting
an unobstructed full sky. Screen-space GI continues to use material diffuse
throughput and authored material AO, but not screen-space ambient visibility.
Its first-bounce source radiance includes directly reflected environment
diffuse alongside shadowed direct diffuse, so sky-lit surfaces can supply the
next diffuse bounce. Authored emissive radiance remains visible in forward,
deferred, and MSAA lighting but is not classified, boosted, or transported as a
screen-space GI source. Specular IBL remains outside this diffuse transport
path. An environment-only scene therefore remains a valid GI source. Diffuse
strength scales that environment source at the same point it scales the visible
diffuse IBL, preventing SSGI from rebroadcasting a different-gain copy. This is
an occlusion heuristic rather than a bent-normal or traced glossy visibility
solution.

AO has no active lighting consumer when both environment lobes and diffuse GI
are disabled. UVSR skips the screen-space pipeline and its resources in that
state instead of dispatching a no-op composite.

## Shared Contribution-Gate Contract

`src/lighting_contribution.hlsli` supplies a common early-out vocabulary to the
forward, deferred, and screen-space lighting shaders. Its source-activity mask
has independent direct, environment, indirect-diffuse, and indirect-specular
bits. Systems may add a bit to `knownInactiveSources` only when they have proved
that source class inactive for the current scope. A scope can be skipped only
when every relevant source is known inactive, so unknown scene data remains
conservatively active. The contract is intentionally open to later scene,
material, light-cluster, visibility, residency, probe, and radiance-cache data.
The shared bit definitions are compiled by C++, HLSL, and tests from
`src/lighting_contribution_shared.h`; screen-space inputs already expose a CPU
scene-activity mask, while unintegrated systems naturally leave their bits
clear.

Hard rejection reasons are local facts rather than global availability: zero or
non-finite signal, below-threshold signal, a back-facing surface, zero
visibility, an out-of-influence light, or a material with no contributing lobe.
Any one can terminate the operation in which it was established, but cannot
silence an unrelated source class. The nonzero adjustable threshold is currently
used only for higher GI bounces; direct-light gates use an exact-zero cutoff.

Forward and deferred direct-light loops reject zero/range/cone light samples and
back-facing surface-light pairs before shadow evaluation and BSDF work. A fully
occluded shadow result exits before remaining shadow consumers. Deferred shading
also recognizes the cleared G-buffer normal as background before decoding the
other material targets. These are exact exits and do not change production
lighting.

## Camera Flashlight and Ambient Fill

`flashlight_1` is a camera-mounted, editable PBR light; its lens hotspot is an
implementation detail rather than a selectable scene light. UVSR submits the
flashlight and hotspot before ordinary scene lights. Disabling the flashlight
sets both contributions to exact zero, and its local planar shadow map runs
only while PBR, the flashlight transition, and **Cast Shadows** are active.

The Sky drawer's **Ambient Fill** gate independently controls diffuse and
specular IBL contribution while retaining the selected environment background
and per-lobe settings. This prevents a background-only sky from being mistaken
for active indirect lighting.

## Screen-Space Directional Shadows

The optional Screen-Space Directional Shadows pass consumes the single-sample
device-depth texture after the G-buffer pass and produces a full-resolution
`R8_UNORM` visibility texture. Its ray-coherent traversal shares four depth
reads per lane across the default 60-pixel trace, then evaluates the compiled
sample loop from group memory.

The CPU dispatch planner and GPU tracer are the byte-identical example released
by [Bend Studio](https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/)
under Apache-2.0, Copyright 2023 Sony Interactive Entertainment. UVSR owns the
generic projection conversion, finite-light validation, reverse-Z near/far
values, resources, shader permutations, dispatch bindings, and product surface.
The pinned `code_final_candidate.zip` archive has SHA-256
`75707A8E287D485C0F71D04FB0EDE245BB9A7E9569F1492B1C4D1F6AB943DE83`;
the complete license is retained at
`third_party/licenses/Apache-2.0.txt` and packaged with the executable.

### Settings and Shader Variants

The **Screen-Space Shadows** drawer starts disabled. Its **Profile** menu has
four entries:

| Profile | Applied settings |
|---|---|
| **Default** | 60-pixel length, 4 hard samples, 8 fade-out samples, `0.005` surface thickness, `0.02` bilinear threshold, contrast `4`, and every optional mode and debug view off |
| **Long** | Default settings with a 240-pixel length |
| **Maximum Validation** | Default settings with a 960-pixel length |
| **Custom** | Retains individually edited values |

Applying a profile preserves **Enabled**, so restoring Default does not
disable an active comparison. Editing any other control selects Custom.
**Length** maps to compiled `SAMPLE_COUNT` values of 60, 120, 240, 480, or 960
pixels. **Hard Shadow Samples** maps to compiled counts of 0, 4, or 8, and
**Fade-Out Samples** maps to compiled counts of 0, 8, or 16. Those three axes
form 45 registered compute-shader permutations; adding another trace length is
isolated to the adapter's variant table and shader registration.

The remaining continuous controls are **Surface Thickness**, **Bilinear
Threshold**, and **Shadow Contrast**. Optional algorithm modes are **Ignore Edge
Pixels**, **Precision Offset**, **Bilinear Offset Mode**, and **Early Out**.
Early Out skips receivers at the configured depth bounds, normally sky, when a
complete projected wavefront can exit together; it is disabled while a debug
view is active. **Debug View** selects Off, Edge, Thread, or Wave. The three
native diagnostics present depth discontinuities, lane indices, or projected
wavefront layout through the R8 result instead of compositing it into scene
lighting.

### Directional-Light Composite

Deferred lighting owns a producer-neutral fixed three-slot directional
visibility interface. Every slot carries only a full-resolution linear
`R8_UNORM` texture and a non-owning exact light pointer. Incomplete, stale-sized,
wrong-format, unsupported, or unmatched slots bind white and retain light index
`-1`. The shader multiplies all factors whose pointer maps to the light currently
being evaluated. Indirect diffuse, emissive radiance, environment lighting, and
unrelated lights are unchanged.

Screen-Space Directional Shadows, SVSM, and diagnostic CSM render independently
and know nothing about one another's types, settings, resources, UI, caches, or
benchmarks. `uvsr.cpp` adapts their frame-local results into the neutral
interface immediately before deferred lighting. Every complete factor that
targets the same exact light is multiplied there and nowhere else.

### Canonical Integration Boundary

This experimental branch validates its single-sample PBR deferred receiver and
the existing forward and screen-space indirect lighting paths. It intentionally
does not copy canonical `main`'s MSAA or fused-visibility shaders. Integration
must preserve those canonical paths while extending the three exact-light slots
and no-hidden-ambient invariant to every production lighting permutation. Every
imported lighting shader must also be added to the source-contract test rather
than weakening that contract.

### Future Hierarchical Boundary

This near tracer is intentionally a standalone producer. A future Hi-Z or
hierarchical far tracer can replace one neutral producer slot or deliberately
extend the renderer boundary without modifying the validated directional-shadow
interface.

### Performance Follow-Ups

The measured historical foundation is the restored ray-coherent near tracer:
on the earlier RTX 4090 Laptop session at 1902 x 1069, its white clear plus
dispatches measured 0.098–0.106 ms at 60 pixels, 0.183–0.203 ms at 120, and
0.347–0.362 ms at 240. Those are historical unmatched observations, not current
candidate measurements.

Exact follow-ups should be isolated and measured in this order: specialize the
default optional-off reverse-Z mode; reduce per-dispatch constant-buffer work
with a small wave-offset push constant; test explicit integer depth loads or a
correctly mapped gather; prove complete dispatch coverage before removing the
white clear; and investigate chunked LDS evaluation for 240–960-pixel variants.

Long-ray research remains a separate far producer. The prior proposal builds a
conservative front-minimum/back-maximum hierarchy with thickness, keeps roughly
64–96 pixels on the exact near tracer, marches coarse mips to screen exit, and
descends only where depth intervals overlap. Tile occupancy with indirect
dispatch, a guide-aware half-resolution far field, stochastic light samples
with validated temporal bitmasks, multi-bit blocker coverage, and asynchronous
compute were also discussed but never implemented or measured. They are
quality-, memory-, or scheduling-changing experiments rather than restoration
optimizations.
The current experiment does not allocate temporal history, stochastic inputs,
a thickness texture, a depth hierarchy, or a far-trace resource.

## Validation

The General drawer exposes scene-wide PBR lighting diagnostics for **Shading
Normal**, **Geometric Normal**, **Normal Difference**, **Diffuse
Environment**, **Cardinal Environment Test**, **Prefiltered Specular**,
**Environment BRDF**, **Final Specular IBL**, **Combined IBL**, **Specular
Occlusion**, and **Environment Mip**. These views isolate normal encoding,
source orientation, convolution, roughness selection, split-sum response, and
occlusion without changing the production path. Screen-Space Directional
Shadows separately exposes Edge, Thread, and Wave diagnostics as direct
grayscale views of the R8 output. The Donut legacy comparison path remains
implemented for possible future experiments, but its **Enable PBR** control is
not exposed in the production UI. Forward and deferred production lighting both
use the same shared BSDF and IBL evaluator.

`tests/pbr_reference_tests.cpp` validates defaults and invalid-value repair,
roughness extremes, dielectric and metallic behavior, dark/bright base colors,
IOR 1.0/1.33/1.5/2.0, directional and point lights, grazing Fresnel,
geometric-normal rejection, double-sided view-hemisphere orientation for
mirrored and ordinary surfaces, no-light/emission-only behavior, visibility
0/0.5/1, finite nonnegative output, independence of direct lighting from
ambient occlusion, source-mask composition, contribution-gate boundary cases,
the four-bounce frontier recurrence, neutral unmatched directional visibility,
clamping, exact multiplicative composition, common-exposure and independent
lobe-strength scaling, zero-strength inactivity, the squared specular prefilter
schedule, matching receiver mip selection, and specular-occlusion limits and
monotonicity.

`tests/diffuse_environment_asset_tests.cpp` validates the authoritative
six-entry imported source catalog, its unique names and paths, two night
classifications, calibrated default exposures, and all six checked-in HDR
files. It decodes every source, projects it through the production
lat-long-to-SH9 implementation, and verifies dimensions, finite positive
energy, cardinal directional contrast, source-derived orientation goldens, and
exposure-scaled luminance. Asset staging is dependency-driven, so changing or
restoring an HDR source reruns staging even when the renderer executable does
not otherwise need relinking.

`tests/screen_space_directional_shadows_tests.cpp` validates Default reset
values, Enabled independence, the Long and Maximum Validation lengths, and
every registered sample-count combination. It also exercises the released CPU
dispatch planner across light and viewport cases, enforces the generic adapter
and shader-wrapper contracts, and checks the pinned source sizes, notices,
license, and package registration. Runtime image quality, artifact, reliability,
and performance conclusions are recorded separately only after the corresponding
renderer evaluation.

## Current Limitations and Performance-Sensitive Areas

- GGX is single scattering. No unverified compensation term is present; the
  compensation integration point is the result of `EvaluateGGX`.
- IBL is one infinite global environment. UVSR does not capture the scene into
  probes, blend local probes, correct parallax, stream probe data, or represent
  indoor/outdoor transitions automatically.
- Imported environments have a fixed authored orientation. There is no
  user-facing rotation control or automatic alignment between an HDR sky and
  the separate directional light. Dedicated night selections likewise do not
  change that light.
- Initial HDR decode, SH projection, and source-cube resampling run on the scene
  worker. The initial GPU path advances through BRDF generation, one radiance
  face upload, one radiance mip, or one GGX specular mip per loading update;
  partially prepared maps remain hidden from scene rendering. A later
  interactive source change uses the same exact preparation path synchronously
  because environment streaming is not yet a general asynchronous pipeline.
  Unchanged warm frames remain zero-work.
- The 16-by-16 diffuse cube is an SH9 Lambert response, so it intentionally
  cannot retain sharp high-frequency lighting. The 256-by-256, nine-mip
  specular cube and 64-by-64 BRDF LUT are fixed-quality resources rather than
  scalable quality tiers.
- Specular occlusion is a bounded material/screen-visibility heuristic. UVSR
  has no bent-normal diffuse lookup, glossy visibility trace, or local
  reflection occluder representation.
- The implemented ambient visibility estimate is screen-space and scalar; it
  is not yet source-side directional sky visibility. The staged future path is
  documented under **Future Sky Visibility Recommendations** in
  [the screen-space visibility design](screen-space-visibility.md).
- Automated tests cover source/catalog invariants and shared roughness and
  occlusion math, but they do not yet read back the generated GPU cubemaps or
  BRDF LUT against an independent integrator. Runtime debug views remain the
  integration check for those generated textures.
- Point/spot radii and directional angular size are not integrated as area
  lights by the initial core.
- IOR currently defaults to 1.5 at glTF import because UVSR has not yet added
  `KHR_materials_ior` import plumbing.
- Specular-gloss materials are reconstructed as an architecture-preserving
  compatibility path; metallic-roughness is the native representation.
- Opacity supports the existing alpha-test path. Full blending, absorption,
  transmission, and refraction are outside this task.
- The fifth `R8_UNORM` render target costs one additional byte of G-buffer
  write/read bandwidth per pixel. It prevents authored ambient occlusion from
  being conflated with direct shadow visibility or discarded.
- Screen-Space Directional Shadows currently trace only the primary directional light
  against visible device depth. Occluders outside the screen, hidden behind the
  first depth layer, or beyond the selected compiled sample count are absent.
  The full-resolution R8 visibility target adds one logical byte per pixel while
  the feature is enabled.
- Deferred direct lighting loops over at most Donut's existing 16 lights per
  pixel. No clustered/tiled list was added; exact contribution gates avoid
  shadow and BSDF work for ineligible lights but do not replace enumeration.
- Surface normals, view direction, diffuse color, specular F0, and roughness
  alpha are prepared once per pixel and reused by all eligible lights. Per-light
  direction/half-vector normalization and correlated Smith-GGX square roots
  remain the main arithmetic hot spots after the exact gates.
- Deferred lighting compiles a source-radiance-UAV specialization only when
  screen-space GI consumes a potentially active source. Its UVSR PBR shader
  does not enumerate or bind Donut probe inputs it does not evaluate.
  Background pixels exit on the cleared normal sentinel before the remaining
  G-buffer reads.
- Visibility temporal rejection delays history-normal and neighborhood reads
  until cheaper motion, bounds, and depth tests pass.

## Exact Extension Steps

### Multi-Scattering GGX Compensation

1. Generate and validate a directional-albedo integration LUT for the exact
   GGX/Fresnel convention used here.
2. Bind it once for deferred, forward, and future path/radiance-cache passes.
3. Add a compensation result beside `EvaluateGGX`, not inside light sampling.
4. Re-run furnace tests across roughness, F0, view angle, and metalness.

### Importance Sampling

1. Add cosine-hemisphere diffuse sampling with `PdfLambert`.
2. Add Heitz visible-normal GGX sampling and replace `PdfGGX` with the matching
   VNDF PDF in the same change.
3. Add lobe-selection probabilities based on metallic/specular energy.
4. Validate evaluation/PDF agreement and white-furnace energy before sharing
   the sampler with path tracing or radiance-cache updates.

### glTF Material Import

1. Preserve the current metallic-roughness factors and textures.
2. Import `KHR_materials_ior` into UVSR's CPU material sidecar.
3. Import emissive strength and keep opacity/alpha mode distinct from future
   transmission.
4. Upload validated IOR-derived F0 through the existing metallic-workflow
   field, then add round-trip asset tests.

### Coat

1. Define coat weight, coat roughness, and coat IOR in the CPU material.
2. Add compact storage only after measuring available material/G-buffer space.
3. Evaluate coat above the base layer with Fresnel attenuation of lower lobes.
4. Add the matching coat sampler/PDF before enabling it in path tracing.

### Anisotropy

1. Add tangent orientation and anisotropic roughness parameters.
2. Extend the shared surface interaction with a validated tangent frame.
3. Replace isotropic GGX D/V with an optional statically specialized
   anisotropic implementation.
4. Add matching VNDF sampling and tangent-rotation tests.

### Refraction

1. Add transmission weight and thin/thick-surface classification separately
   from opacity.
2. Add Snell refraction and total-internal-reflection handling using material
   IOR.
3. Define visibility/ray-query ownership outside the BSDF.
4. Add absorption only for paths with a defined interior distance.

### Spectral Fresnel

1. Replace scalar dielectric IOR with a wavelength-dependent representation or
   a compact dispersion model.
2. Add exact dielectric and conductor Fresnel behind the existing Fresnel
   function boundary.
3. Retain tristimulus Schlick as the real-time fallback.
4. Validate spectral-to-display integration before enabling dispersion or
   thin-film interference.
