# PBR Foundation

## Product Boundary

UVSR has one PBR material contract and two lighting solutions for opaque and
alpha-tested surfaces. **Ray Marching** uses the established deferred pipeline.
**Path Tracing** reconstructs the same supported materials at committed DXR
triangle hits and follows complete light paths without a raster G-buffer.
Donut's generic forward, legacy, and deferred lighting permutations are not
packaged as alternate renderer modes. This keeps material encoding, visibility,
lighting, and debug interpretation on one contract even though the two
solutions have different pass topology.

## Material Inputs

The retained material model consumes base color, opacity/mask state, metalness
or reconstructed specular-gloss parameters, roughness, tangent-space normal
data, emissive color, and material ambient occlusion. Double-sided instances
preserve a view-facing shading frame without changing their authored material
identity. Material ambient occlusion remains a Ray Marching ambient-lighting
input; Path Tracing does not multiply that baked approximation into complete
transport.

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

The shared ray-query representation carries only opaque and alpha-tested triangle
geometry. Its material-aware candidate helper interpolates the exact geometry
UV and applies base-color alpha plus material cutoff before accepting an
alpha-tested hit. Blended and transmissive domains are excluded from the binary
visibility hierarchy. Sun, sky, flashlight, and complete path queries share
this contract.

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

## Path Tracing Integration

Path Tracing bypasses the G-buffer, deferred lighting, screen-space visibility,
selective ray-traced visibility, MSAA, and TAA. It reconstructs positions, UVs,
geometric normals, shading frames, and material parameters directly from the
committed triangle and the same bindless scene resources used by Ray Marching.
The shared integrator evaluates and samples Lambert diffuse plus GGX
metallic-roughness reflection, analytic-light next-event estimation, emissive
hits, environment misses, geometric-hemisphere validity, and Russian roulette.
Camera radiance transport does not apply an adjoint importance-mode
shading-normal factor. Analytic lights and BSDF-reached emissive/environment
transport are disjoint proposal sets in the current shader, so it does not
claim an active multiple-importance-sampling combination between overlapping
techniques.

Zero-radius point and spot lights and zero-angular-size directional lights keep
their exact delta visibility paths. Positive-radius point and spot lights sample
the visible sphere with matching solid-angle density, while finite directional
lights sample their angular disk. Visibility ends at the sampled emitter point,
and reusable direct-light reservoirs persist the full sample seed so every
donor is re-evaluated at the receiving surface rather than at the light center.

RTX PT is the reference Monte Carlo solver. Uniform, Power, and UVSR's
current-vertex adaptive NEE are active. The optional first-party RTXDI-like
direct reservoir replaces primary-hit NEE and can reuse a compatible
previous-frame same-pixel candidate plus one compatible previous-frame
neighbor.

ReSTIR PT is an executable seed-replay subset. It re-integrates deterministic
prior local seeds at the receiving pixel and resamples their indirect suffixes
with the current one; it has no stored reconnection vertex, hybrid shift, or
geometric reconnection. ReSTIR GI is an executable temporal-checkpoint subset.
It resamples the current and previous same-pixel local indirect suffixes without
a cross-pixel secondary-surface transform. Both persist only the current local
record, add the current primary base exactly once, and remain explicitly
separate from the optional direct reservoir. They are clean-room UVSR
implementations, not claims of NVIDIA namesake parity.

The reference running mean uses finite successful contributions, including
black and misses. **Firefly Clamp (Biased)** deliberately limits a successful
contribution before it enters that mean, so an image accumulated with the
filter enabled is not the unclamped reference estimator.

The supported transport boundary intentionally matches the world
representation: opaque and alpha-tested triangles only. Blended, transmissive,
subsurface, hair, curve, and participating-media transport remain unsupported
rather than being silently treated as opaque. The complete architecture,
solver policies, history rules, and extension boundary are documented in
[Path Tracing Transport](path-tracing-transport.md).

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
scene-linear lighting and effect histories remain unchanged. In Ray Marching,
TAA runs before auto exposure and tone mapping when enabled. Path Tracing uses
its own scene-linear progressive history and never routes through TAA. Fast
Approximate AA and CMAA2, when enabled, run afterward in that order in the
display-linear domain. Final transfer and dithering are applied at output.

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

Path Tracing replaces the screen-space Visibility and deferred PBR debug bodies
with transport-owned views of the current first-hit albedo, geometric and
shading normals, successful-sample count, retry probability, transient
stable-plane classification, and current direct-reservoir state. RTX PT may
also persist its path-layer and first-hit guide set for UVSR's spatial-only
Stable Plane Resolve; raw output remains independent. There is no
Primary-Surface Replacement stage. Indirect Reservoir displays the resampled
indirect suffix while an
effective ReSTIR PT seed-replay or ReSTIR GI checkpoint policy is active and is
disabled otherwise. Debug selection preserves estimator settings and the
history epoch; changing the view forces one all-pixel attempt so transient data
is coherent, and successful attempts enter the running mean normally.

## Validation

The PBR boundary is protected by:

- CPU reference tests for material and lighting equations;
- source contract tests for shared CPU/HLSL layouts, exact sun and flashlight
  visibility matching, and the flashlight beam profile;
- shader-package tests proving the forward/legacy families are absent;
- path-transport contracts for hit reconstruction, shared BSDF evaluation and
  sampling, finite accumulation, solver policy, and history invalidation;
- scene and asset contracts for bundled material inputs;
- AA tests for per-sample deferred MSAA and output ordering; and
- runtime inspection through the concise Debug views.

Adding a new material or lighting feature requires a current product control or
an unavoidable renderer invariant, a shared CPU/HLSL contract, resource
lifetime evidence, and focused tests. Do not restore a parallel renderer path
only as an extension point.
