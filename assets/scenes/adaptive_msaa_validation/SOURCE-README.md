# Adaptive MSAA Validation Scene Source

## Authorship and License

This package is original deterministic validation content authored for UVSR
in August 2026. It does not derive geometry, textures, or binary data from an
external asset. The inline PNG pixels were generated directly from the small
checker, tangent-space normal, and emissive patterns described below. UVSR
contributors are the copyright holders, and every file in this directory is
distributed under the MIT License in `LICENSE.txt`.

The glTF `asset.generator` field identifies the exact original generator as
`UVSR Adaptive MSAA Validation Scene Generator 1.0`. Its binary vertex, index,
and animation data and all three PNG images are embedded as data URIs so the
package has no untracked runtime dependencies.

## Intended Coverage

The fixed layout isolates overlapping triangles, two primitives that share one
material, two nodes that instance one mesh, a checkerboard alpha mask, a
back-facing double-sided triangle, a deliberately tiny triangle, a steep
depth-varying triangle, a low-roughness metallic panel, a tangent-space
normal-mapped panel, localized emissive texels, thin silhouette spikes, and a
moving foreground edge. Every primitive is an indexed triangle list with
positions, normals, tangents, and texture coordinates.

The `MotionTaaSweep` animation uses a standard glTF 2.0 LINEAR translation
channel over two seconds. It moves `AnimatedOccluder` from left to right and
back to provide deterministic motion-vector, TAA, and frame-to-frame error
coverage. Donut's glTF importer supports this translation channel, but the
descriptor does not select playback. Use the diagnostics-only
`debug.adaptive-msaa.validation-animation` control and set
`debug.adaptive-msaa.validation-animation-phase` to one exact normalized phase
for each matched Reference/adaptive capture. Nearby-light, moving-shadow-edge,
and screen-space visibility cases use UVSR's flashlight, directional-shadow,
and visibility controls; the scene does not invent renderer-specific light
metadata.

For motion-vector or temporal captures, keep diagnostics and camera/frame
freeze on, leave validation animation off at its settled phase-zero state, and
use the action below instead of later ordinary screenshots:

```text
/run debug.adaptive-msaa.capture-validation-frame <phase-a> <bmp-a> <phase-b> <bmp-b>
```

The atomic action resets history, saves A, and then saves B on the immediately
following renderer frame, so B consumes A's previous transform and, when TAA is
enabled, its temporal history.
It advances only the transaction-owned TAA phase, previous view, and
deterministic seed. After each pair, UVSR automatically disables validation
animation and returns to phase zero; wait for both settlement refreshes. Retain
the same frozen seed and replay the exact action for every Reference/adaptive
comparison. Any scene, implementation, sample-count, size, AA, topology, or
revision change during a pair aborts it.

`image_diff_matrix.json` is the machine-readable acceptance recipe. It fixes
the 1920-by-1080 camera, the two consecutive animation phases, all fifteen case
identities, their nonempty image regions, their named scene targets, and the
TAA, flashlight, shadow, and screen-space visibility switches that distinguish
otherwise shared geometry. Runtime receipts bind its SHA-256 to every captured
frame so relabeling a Reference image or reusing an unrelated region cannot
satisfy the image gate.

## Reproduction Settings

Launch `adaptive_msaa_validation.scene.json` and use its exact initial camera:
position `(0.0, 0.2, 8.0)`, direction `(0.0, 0.0, -1.0)`, up
`(0.0, 1.0, 0.0)`, and vertical field of view `42.0` degrees. Freeze the camera
and frame for static 2x, 4x, 8x, and 16x comparisons. Enable animation only for
the motion/TAA and frame-to-frame comparisons. Collect flashlight, directional
shadow, and screen-space visibility variants as separate matched captures.

## Inline Texture Definitions

The alpha texture is an 8-by-8 RGBA checker with two-pixel cells and alpha
values restricted to zero or 255. The 4-by-4 normal texture alternates four
normalized-looking tangent-space directions around neutral blue. The 4-by-4
emissive texture is black except for its diagonal and central two-by-two region,
which use orange-white texels. These deliberately abrupt transitions exercise
sample ownership and explicit-gradient reconstruction without relying on
photographic content.
