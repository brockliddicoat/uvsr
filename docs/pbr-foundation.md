# PBR Foundation

## Product Boundary

UVSR has one deferred PBR path for opaque and alpha-tested surfaces. Donut's
generic forward, legacy, and
deferred lighting permutations are not packaged as alternate renderer modes.
This keeps material encoding, motion vectors, visibility, MSAA, lighting, and
debug interpretation on one contract.

## Material Inputs

The retained material model consumes base color, opacity/mask state, metalness,
roughness, tangent-space normal data, emissive color, and material ambient
occlusion. Double-sided instances preserve a view-facing shading frame without
changing their authored material identity.

Authored emission is visible material radiance. It is not a recursive
screen-space diffuse source and has no visibility-owned bounce metadata.

## G Buffer

The packed G-buffer stores the values needed by deferred direct lighting,
environment lighting, material picking, motion, visibility, and debug views.
MSAA uses multisampled G-buffer attachments and preserves each sample through
material decode and lighting before resolving HDR radiance.

The normal channels have distinct meanings. The shading normal remains the
material derived smooth or normal mapped normal used by the BSDF. The geometric
normal is the view facing raster triangle plane normal derived from world space
position derivatives. That flat normal drives geometric hemisphere gates and
ray origin construction, so smooth vertex normals cannot weaken separation from
the actual BLAS triangle plane.

The ray-query representation carries only opaque and alpha-tested triangle
geometry. Its material-aware candidate helper interpolates the exact geometry
UV and applies base-color alpha plus material cutoff before accepting an
alpha-tested hit. Blended and transmissive domains are excluded from the binary
visibility hierarchy. Sun, sky, and flashlight queries share this contract.

Resource creation follows active consumers. Motion vectors exist when TAA,
MSAA closest surface resolve, or an active NRD signal needs them. Visibility
guides and source radiance exist only while AO or indirect diffuse consumes
them.

## Direct Lighting

Deferred lighting evaluates the scene's supported directional, point, and spot
lights with shared material gates. The camera flashlight is one analytical spot
light in that same submission. Its first party two lobe beam profile replaces
only that exact light's ordinary cone response; there is no duplicate hotspot
light and no private raster shadow system.

The flashlight's selectable full angular size controls one analytical spherical
emitter in both incident-energy evaluation and finite shadow rays. It defaults
to 2.86 degrees at the one-metre reference distance and spans 0 through 20
degrees without adding a second light. Positive radius uses projected solid
angle to keep the near field finite while converging to the authored luminous
intensity's inverse square result in the far field. Zero radius preserves the
exact point-light energy and hard center ray. A positive radius traces four
noise-shifted directions over the emitter's visible spherical cap and averages
their visibility. The factory beam color is pure linear white. A dedicated
emitter-aware collision sphere sweeps the authored mount through the camera
collision hierarchy and resolves stationary overlap when the radius changes.
Collision may stop or slide the emitter to keep it outside geometry, but it
does not sample receiver depth, scale the camera offset, or retarget the
authored beam. Lens sway remains a later direction-only presentation effect and
cannot feed back into collision or light position. The rejected receiver-driven
centering experiment is documented in
[Flashlight Camera Centering v1](postmortem/flashlight-camera-centering-v1.md).

Direct visibility has separate fixed slots for the exact flashlight and primary
sun pointers. The flashlight slot consumes its finite ray traced scalar
visibility. The sun slot consumes either the correlated RGB ratio or a raw
scalar visibility replicated across RGB. A mismatched or unavailable producer
falls back to neutral white. Each selected visibility is multiplied once into both
the diffuse and specular contribution of its source light.

When Shadows selects SIGMA and the required raw visibility plus physical hit
distance is present, denoised visibility replaces that light's raw slot. Sun and
flashlight use separate SIGMA signals and histories even though they share one
visible Denoising policy.

The primary sun initializes to irradiance `8` and a `0.2` degree full angular
size. Screen space directional shadows are absent from main. Their implementation
is preserved only with the CSM and SVSM experimental branch.

## Environment Lighting

The global environment provides:

- Lambert-convolved SH9 diffuse irradiance;
- roughness-prefiltered GGX specular radiance; and
- a split-sum environment BRDF lookup.

The Sky drawer's Ambient Fill gate explicitly controls diffuse/specular IBL
without replacing the selected background. UVSR uses one infinite environment;
local probes, parallax correction, and probe blending are outside the product
boundary.

Ray Traced Sky Visibility has independent **Effect Diffuse** and **Effect
Specular** switches, and both default on. When the producer itself is enabled, the resolved
sky visibility therefore shapes metals and other specular environment response
as well as diffuse environment irradiance. Denoised sky visibility replaces the
raw scalar only when its explicitly selected NRD route is eligible.

## Screen Space Visibility Integration

AO modulates the appropriate ambient response. One bounce indirect diffuse
adds current frame screen space transport from the retained source radiance.
Both are optional consumers of the same visibility trace.

When configured, AO ReBLUR and GI ReBLUR or ReLAX replace the corresponding raw
trace outputs before composition. Their histories belong to the optional NRD
backend, not Screen Space Visibility. Missing hit distance or an unavailable
backend leaves the raw signal in use. PBR does not consume a depth hierarchy,
recursive bounce frontiers, or planner metadata. Renderer TAA still operates
later on the complete scene image.

## Tone Mapping and Output

A neutral AgX transform maps scene-linear HDR radiance for display. Optional
Auto Exposure builds a 256-bin GPU luminance histogram after TAA, meters the
median valid luminance, adapts toward 18% middle gray, and supplies the exposure
multiplier directly to AgX. **Maximum Brightening** and **Maximum Darkening**
independently bound the automatic target from 0 through 16 EV in each direction.
Maximum Brightening defaults to 5 EV and Maximum Darkening defaults to 2 EV.
**Exposure Compensation** adds its -18 through +8 EV
bias after that bound. **Adjustment Period** spans 0.05 through 5 seconds,
defaults to 0.20 seconds, and controls the half-life of symmetric EV-space
adaptation.

When Auto Exposure is disabled, tone mapping selects the exact established
texture-only, buffer-free AgX presentation, so the optional feature cannot
alter color. The automatic permutation changes only the scene-linear exposure
multiplier before that same AgX curve. It does not replace the established AgX
clamps or add another output transfer. This is a display transform only:
scene-linear lighting and effect histories remain unchanged. TAA, when enabled,
runs before auto exposure and tone mapping. Fast Approximate AA and CMAA2, when
enabled, run afterward in that order in the display-linear domain. Final
transfer and dithering are applied at output.

## Debug Contract

The Debug drawer separates presentation from information:

- World selects Default, White, White Detail, or White Lighting.
- Visibility selects Default, Ambient Visibility, Traced Indirect, or
  Applied Indirect.
- Physically Based Lighting selects Default, Surface Normals, Geometry
  Normals, Normal Difference, Diffuse Environment, Environment Direction,
  Reflected Environment, Reflectance Response, Specular Environment, All
  Environment Light, Specular Visibility, or Environment Level.

World appearance remains independent from information filters. A Physically
Based Lighting filter keeps Visibility executing so traced data stays valid,
while ordinary Visibility composition passes through without contaminating the
filter. An explicit Visibility view wins when both information selectors are
active. Removed screen space directional shadow debug state does not remain
available in main.

## Validation

The PBR boundary is protected by:

- CPU reference tests for material and lighting equations;
- source contract tests for shared CPU/HLSL layouts, exact sun and flashlight
  visibility matching, and the flashlight beam profile;
- shader-package tests proving the forward/legacy families are absent;
- scene and asset contracts for bundled material inputs;
- AA tests for per-sample deferred MSAA and output ordering; and
- runtime inspection through the concise Debug views.

Adding a new material or lighting feature requires a current product control or
an unavoidable renderer invariant, a shared CPU/HLSL contract, resource
lifetime evidence, and focused tests. Do not restore a parallel renderer path
only as an extension point.
