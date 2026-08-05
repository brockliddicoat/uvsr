# PBR Foundation

## Product Boundary

UVSR has one opaque deferred PBR path. Donut's generic forward, legacy, and
deferred-lighting permutations are not packaged as alternate renderer modes.
This keeps material encoding, motion vectors, visibility, MSAA, lighting, and
debug interpretation on one contract.

## Material Inputs

The retained material model consumes base color, opacity/mask state, metalness,
roughness, tangent-space normal data, emissive color, and material ambient
occlusion. Double-sided instances preserve a view-facing shading frame without
changing their authored material identity.

Authored emission is visible material radiance. It is not a recursive
screen-space diffuse source and has no visibility-owned bounce metadata.

## G-Buffer

The packed G-buffer stores the values needed by deferred direct lighting,
environment lighting, material picking, motion, visibility, and debug views.
MSAA uses multisampled G-buffer attachments and preserves each sample through
material decode and lighting before resolving HDR radiance.

The normal channels have distinct meanings. The shading normal remains the
material-derived smooth or normal-mapped normal used by the BSDF. The geometric
normal is the view-facing raster triangle-plane normal derived from world-space
position derivatives. That flat normal drives geometric hemisphere gates and
ray-origin construction, so smooth vertex normals cannot weaken separation from
the actual BLAS triangle plane.

Resource creation follows active consumers. Motion vectors exist when TAA or a
retained MSAA visibility resolve needs them. Visibility guides and source
radiance exist only while AO, indirect diffuse, or screen-space directional
shadows consume them.

## Direct Lighting

Deferred lighting evaluates the scene's supported directional, point, spot, and
flashlight contributions with shared material gates. The primary directional
light has fixed, named Screen-Space and Ratio-Estimator visibility slots. Each
slot validates its exact source light independently and falls back to neutral
white. When both are valid, deferred PBR takes their componentwise minimum so
the strongest occlusion survives without multiplying overlapping estimates.

The old SVSM/CSM taxonomy and generic composite slots remain removed. Toggling
either retained directional-shadow producer does not toggle PBR or light
submission.

## Environment Lighting

The global environment provides:

- Lambert-convolved SH9 diffuse irradiance;
- roughness-prefiltered GGX specular radiance; and
- a split-sum environment BRDF lookup.

The Sky drawer's Ambient Fill gate explicitly controls diffuse/specular IBL
without replacing the selected background. UVSR uses one infinite environment;
local probes, parallax correction, and probe blending are outside the product
boundary.

## Screen-Space Visibility Integration

AO modulates the appropriate ambient response. One-bounce indirect diffuse
adds current-frame screen-space transport from the retained source radiance.
Both are optional consumers of the same visibility trace.

PBR does not consume visibility temporal history, depth hierarchy output,
recursive-bounce frontiers, or planner/profile metadata. Renderer TAA operates
later on the complete scene image.

## Tone Mapping and Output

A neutral AgX transform maps scene-linear HDR radiance for display. TAA, when
enabled, runs before tone mapping. Fast Approximate AA and CMAA2, when enabled,
run afterward in that order in the display-linear domain. Final transfer and
dithering are applied at output.

## Debug Contract

The Debug drawer separates presentation from information:

- World selects Default, White, White Detail, or White Lighting.
- Visibility selects Default, Ambient Visibility, Traced Indirect, or
  Applied Indirect.
- Physically Based Lighting selects Default, Surface Normals, Geometry
  Normals, Normal Difference, Diffuse Environment, Environment Direction,
  Reflected Environment, Reflectance Response, Specular Environment, All
  Environment Light, Specular Visibility, or Environment Level.
- Screen-Space Shadows selects Default, Thread Lanes, or Wave Groups.

World appearance remains independent from information filters. A Physically
Based Lighting filter keeps Visibility executing so traced data stays valid,
while ordinary Visibility composition passes through without contaminating the
filter. An explicit Visibility view wins when both information selectors are
active. Shadow isolation is a deliberate full-image diagnostic; the former
transparent Edge Overlay was removed.

## Validation

The PBR boundary is protected by:

- CPU reference tests for material and lighting equations;
- source-contract tests for shared CPU/HLSL layouts and two-slot directional
  visibility composition;
- shader-package tests proving the forward/legacy families are absent;
- scene and asset contracts for bundled material inputs;
- AA tests for per-sample deferred MSAA and output ordering; and
- runtime inspection through the concise Debug views.

Adding a new material or lighting feature requires a current product control or
an unavoidable renderer invariant, a shared CPU/HLSL contract, resource-
lifetime evidence, and focused tests. Do not restore a parallel renderer path
only as an extension point.
