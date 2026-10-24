# Flashlight Shadow Fix Report

## Summary

The flashlight already rendered, bound, and sampled a geometry shadow map. The
visible failure came from its pose: the emitter sat only 5 cm forward on the
camera's optical axis. From a pinhole camera, an on-axis point-like light casts
a receiver shadow directly behind the visible caster, so the caster hides the
shadow it produces.

The fix moves the shared flashlight emitter off axis and converges the beam
back to the optical axis at 6 m. Its default remains 4 cm forward and now uses
a 20 cm lateral mount, resolved as approximately 17.9 cm to camera right and
8.9 cm below the camera. **Camera Offset** varies that lateral displacement
from 0 to 40 cm while preserving the mount direction. This creates controllable
physical parallax without changing the shadow algorithm, light layout, resource
bindings, or number of geometry renders.

## User-Visible Symptom

With **Cast Shadows** enabled, the flashlight illuminated columns and walls but
did not appear to project a separate occluder silhouette. Switching **Cast
Shadows** off and on at the original benchmark view produced little obvious
change.

Ambient Fill and the directional sun can reduce contrast because a flashlight
shadow removes only the flashlight's direct contribution. They were not the
root cause: the same missing silhouette remained when tracing the flashlight
visibility independently.

## Reproduction

The rejected build used:

- DirectX 12 deferred PBR.
- PBR Sponza Decorated at Benchmark Position 1.
- `flashlight_1` enabled at its default 600 cd, 42-degree, 30 m settings.
- The same camera and geometry while toggling **Cast Shadows**.

The beam clearly lit a foreground column and its receiver surfaces, but the
camera-facing caster covered the position where its flashlight shadow was
projected. The earlier visual smoke checked illumination, not a receiver-only
silhouette beside an occluder, so it could not prove shadow visibility.

## Complete Pipeline Audit

The investigation traced every stage before changing code:

1. UVSR creates one sampled 2048-by-2048 planar depth texture, framebuffer, and
   biased depth pass for the flashlight.
2. The simple spill and optional realistic hotspot reference the same shadow
   map.
3. The resolved light transform is published to the scene graph before the
   shadow view is refreshed.
4. Opaque and alpha-tested scene geometry renders into the map before forward
   or deferred light preparation.
5. Donut and UVSR use `shadowCascades` for the planar-map association. The
   flashlight preserves `shadowChannel.x` for the separate screen-shadow
   visibility selector and uses only `shadowChannel.yzw` for its beam basis.
6. Deferred, deferred-MSAA, and compiled forward PBR paths bind the map, fill
   the cascade constants, comparison-sample visibility, and multiply the
   flashlight's direct-light result.

This ruled out a missing texture binding, unpopulated depth map, stale
transform, reverse-Z mismatch, comparison-sampler defect, shape-metadata
collision, and realistic-lobe resource conflict. Existing depth bias can
reduce near-contact detail, but it cannot explain a scene-wide absence.

## Root Cause

Let the eye, light, caster, and receiver lie on one view ray. When the eye and
point-like light have the same lateral position, the light ray through the
caster reaches the receiver along the same projected image position. The
caster therefore occludes both the receiver and its shadow in the final camera
image.

The former flashlight origin added only a 5 cm forward displacement:

`P_light = P_camera + 0.05 * cameraForward`

That changed shadow scale slightly but created no horizontal or vertical
parallax. PCF filtering and depth bias made the small remaining edge difference
even less apparent.

## Implemented Fix

`ResolveFlashlightMountPose(cameraLateralOffsetMeters)` now defines one tested
camera-basis pose:

- Default position: approximately `(+0.17888544 m right, -0.08944272 m up,
  +0.04 m forward)`.
- Lateral range: `0.00 m` to `0.40 m`, along the same two-to-one right/down
  direction.
- Aim target: the camera optical axis at `6.0 m`.
- Default eye-to-emitter distance: approximately `0.204 m`.
- Toe-in angle: approximately `1.92 degrees` by default and `3.84 degrees` at
  the maximum offset.

The 6 m convergence target is deliberately farther than the exact 5.7603 m
minimum needed to keep the maximum toe-in below the four-degree half-angle of
the narrowest supported beam. The default and maximum emitter positions lie
outside UVSR's 10 cm minimum camera-collision sphere. The mount has no hand,
body, or wall-collision simulation, so close geometry can intersect or occlude
the virtual emitter.

`UpdateFlashlightMotion()` composes that pose with the active camera basis.
Simple mode uses the converged direction directly. Realistic mode uses the same
direction as its initialization, correction target, and lag-clamp target before
adding bounded sway. The spill light, internal lens hotspot, packed beam basis,
and planar shadow view still consume one resolved pose.

For an ideal caster at 2 m and receiver at 3 m, the default mount creates about
10.20 cm of receiver-plane displacement after accounting for the retained 4 cm
forward offset. At a 1920-by-1080 camera with a 60-degree vertical field of
view, that is roughly 32 pixels of resultant separation. The same arrangement
reaches about 20.41 cm, or 64 pixels, at the 40 cm slider maximum; zero
deliberately returns to the coaxial case.

## Why a Spotlight Remains Correct

The flashlight should remain a spotlight, not a point light. A point light
would emit in every direction, illuminate behind the user, and require a cube
shadow map. The current finite cone models a lens or reflector and needs only
one planar shadow render. The defect was the spotlight's camera-coincident
origin, not its light type.

## Regression Protection

Focused unit coverage verifies:

- Exact default right/down components and the retained two-to-one mount
  direction.
- Finite zero-offset and maximum-offset endpoints.
- A normalized, forward-facing converged direction.
- Exact intersection with the optical-axis target.
- A 20 cm default and 40 cm maximum lateral offset.
- Toe-in below the narrowest beam's four-degree half-angle at the maximum.
- Monotonically increasing ideal 2 m-to-3 m receiver parallax.
- Finite fallback and range clamping for the public offset setting.

Renderer source contracts verify that both simple and realistic modes use the
tested mount, both flashlight lobes receive the shared resolved pose, the map
is rendered once before lighting, and public/private light naming remains
unchanged.

## Verification

Completed verification:

- Focused flashlight, UI, and renderer source-contract tests passed.
- Fresh isolated production and factory Release builds passed after the Camera
  Offset, Shadows-drawer, and flashlight-control-order extensions.
- Both complete CTest suites passed 29 of 29 tests.
- The production build compiled the complete 516-task first-party shader
  topology; the factory build compiled its 51-task topology.
- Independent final review found no P0-P2 implementation defect in the 20 cm
  default, 40 cm maximum, 6 m convergence, shared flashlight pose, or sibling
  Shadows drawer. Its two P2 documentation findings were corrected.
- A separate control-lifetime audit found no P0-P2 issue in the final
  **Enabled (F)**, **Cast Shadows**, **Realistic Flashlight** order or the
  attached realistic-only animated region.
- The production candidate executable has SHA-256
  `4A96BB5EFC73AEC4CFA586228C2F345B328EACB0ECFA77748D1E4D4BEE2EDB89`.

Forward PBR remains covered by source contracts and compiled-shader packaging;
this snapshot does not expose a runtime forward/deferred selector.

The preceding Camera Offset candidate passed an exact-build DirectX 12 delta
smoke at 1920 by 1080 on Benchmark Position 1:

- First Escape opened General while Lights and the new Shadows drawer remained
  collapsed.
- Shadows opened with Bend, Sparse Virtual Shadow Maps, and Diagnostic Cascaded
  Shadow Maps expanded and disabled.
- Selecting `flashlight_1` showed the 20.0 cm Camera Offset default. The slider
  reached 40.0 cm, and its row reset returned to 20.0 cm.
- One complete F on/off cycle showed the expected animated illumination
  endpoints.
- With the flashlight on at the 20 cm default, disabling Cast Shadows removed
  the visible receiver shadow beside the foreground occluder; restoring Cast
  Shadows restored it.

Live user input appeared during final cleanup, so automation stopped and left
the exact candidate open for inspection with the flashlight on, Cast Shadows
on, and Camera Offset restored to 20 cm. The broader realistic,
rounded-square, narrow-beam, deferred-MSAA, recreate, resize, and motion matrix
remains future validation rather than evidence for this focused delta.

## Compatibility and Performance

The repair and slider do not add a light, texture, descriptor, shader
permutation, shadow lookup, or geometry pass. The slider changes only existing
per-frame pose arithmetic. Simple mode still uses one local light. Realistic
mode still uses two concentric lobes sharing one geometry render. Ordinary
lights and the three producer-neutral directional-visibility slots are
unchanged.

The stable public name remains `flashlight_1`; the internal hotspot remains
excluded from editable-light discovery. **Cast Shadows**, exact-off, PBR-off,
and controlled-experiment gates retain their existing behavior.

## Remaining Constraints

- A flashlight shadow attenuates only flashlight direct lighting. Sun, IBL,
  emissive lighting, and diffuse GI may still illuminate the same surface.
- The current point-like shadow model does not simulate a finite lens aperture,
  contact hardening, translucent transmission, or volumetric beam occlusion.
- PCF and raster depth bias can soften or separate very small contact details.
- The adjustable mount has no hand, body, or wall-collision simulation. Large
  offsets can place the virtual emitter outside the camera collision sphere
  near close geometry.
