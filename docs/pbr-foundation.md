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
- Ambient occlusion is not part of the BSDF or direct lighting. Authored
  material occlusion modulates the approximate indirect fallback and
  screen-space diffuse transport, while screen-space ambient visibility
  modulates only the approximate sky fallback. Screen-space GI is not
  multiplied by ambient visibility.

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
| G4 | `R8_UNORM` | Authored material ambient occlusion for the approximate indirect-light fallback and screen-space diffuse transport |
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
picking; visibility does not consume those IDs. G5 exists only while adaptive
or temporal screen-space visibility needs velocity. Its XY convention is
current-to-previous pixels; Z is previous-minus-current device depth; A
distinguishes a valid zero velocity from cleared background or a previous point
behind the camera. The conditional target is not counted in the 25-byte
always-on total.

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

## Shared Contribution-Gate Contract

`src/lighting_contribution.hlsli` supplies a common early-out vocabulary to the
forward, deferred, and screen-space lighting shaders. Its source-activity mask
has independent direct, emissive, environment, indirect-diffuse, and
indirect-specular bits. Systems may add a bit to `knownInactiveSources` only
when they have proved that source class inactive for the current scope. A scope
can be skipped only when every relevant source is known inactive, so unknown
scene data remains conservatively active. The contract is intentionally open to later scene,
material, light-cluster, visibility, residency, probe, and radiance-cache data.
The shared bit definitions are compiled by C++, HLSL, and tests from
`src/lighting_contribution_shared.h`; screen-space inputs already expose a CPU
scene-activity mask, while unintegrated systems naturally leave their bits clear.

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

## Validation

PBR and visibility debug views are intentionally absent from the current build.
The Donut legacy comparison path remains implemented for possible future
experiments, but its **Enable PBR** control is not exposed in the production UI.
Forward and deferred production lighting both use the same shared BSDF.

`tests/pbr_reference_tests.cpp` validates defaults and invalid-value repair,
roughness extremes, dielectric and metallic behavior, dark/bright base colors,
IOR 1.0/1.33/1.5/2.0, directional and point lights, grazing Fresnel,
geometric-normal rejection, no-light/emission-only behavior, visibility
0/0.5/1, finite nonnegative output, independence of direct lighting from
ambient occlusion, source-mask composition, contribution-gate boundary cases,
and the four-bounce frontier recurrence.

## Current Limitations and Performance-Sensitive Areas

- GGX is single scattering. No unverified compensation term is present; the
  compensation integration point is the result of `EvaluateGGX`.
- There is no image-based specular lighting or BRDF integration LUT in UVSR's
  PBR path yet. Sky colors provide only an explicit approximate indirect
  diffuse irradiance term.
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

### Image-Based Lighting

1. Add a prefiltered scene-linear specular environment and diffuse irradiance
   representation.
2. Add the matching split-sum BRDF LUT using this core's roughness convention.
3. Evaluate diffuse IBL separately from direct light and apply ambient
   occlusion only to the explicitly missing indirect diffuse visibility.
4. Add specular-occlusion policy only after validating it against traced
   reference images.

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
