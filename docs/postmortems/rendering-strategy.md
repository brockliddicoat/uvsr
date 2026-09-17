# rendering strategy

## choose with current hardware evidence

**observed.** UVSR invested heavily in screen-space ambient occlusion, diffuse transport, hierarchical depth traversal, filtering, temporal history, adaptive scheduling, reduced-resolution execution, and reconstruction. the work produced several locally correct optimizations. it could not recover blockers or radiance outside the visible depth buffer, and the full path also paid for depth hierarchy construction, divergent traversal, filtering, history, and composition.

**inferred.** the project kept optimizing a technique category after hardware ray tracing had become fast enough to change the tradeoff. an isolated trace-cost reduction did not establish that the complete screen-space pipeline delivered better image quality per millisecond than a simpler ray-traced path. architectural familiarity and sunk cost delayed that comparison.

**recommended.** compare complete candidate pipelines on the actual target hardware before choosing the long-term technique. fix the scene, camera, resolution, formats, shader inputs, draw order, warmup, timing window, and image-quality target. count setup, acceleration data, hierarchy construction, tracing, denoising, reconstruction, history, and composition. retain a screen-space path only when a named target, fallback requirement, or equal-time result justifies its lifetime cost.

screen-space misses are **unknown**. they are not evidence of visibility. a hybrid can use a screen-space hit as evidence when its confidence contract is satisfied, then send unknown regions to world-space completion. it should not treat missing data as unoccluded, multiply two independently complete occlusion estimates, or add two complete indirect-light estimates.

## define estimator ownership before combining techniques

**observed.** UVSR accumulated overlapping AO, diffuse GI, environment visibility, selective shadows, path tracing, and experimental reuse paths. several could affect the same energy or visibility without one explicit composition contract.

**recommended.** every estimator must name:

- the physical quantity it estimates and its units,
- the directions, distances, lobes, and geometry it covers,
- whether a miss means visible, unavailable, or unknown,
- its producer, consumers, frame validity, and reset conditions,
- how it combines with every other estimator.

ambient occlusion attenuates only its intended ambient response. indirect radiance is added once. selective shadow visibility does not silently become global illumination. one scene-linear composition boundary should own the final combination.

## treat temporal identity as data

**observed.** rotating low-resolution visibility samples was deterministic and resource-neutral, but a single history value could not preserve several receiver identities. native-resolution TAA mixed sample grids and added history resurrection before the basic reprojection contract was proven. the flashlight controller smoothed a target selected by a discontinuous center ray, so filtering delayed the jump without making the target continuous.

**recommended.** a temporal algorithm must define the identity of the value it carries. record the current and previous sample location, coordinate space, persistent surface or object identity when required, motion source, exposure domain, validity, and rejection rule. test stationary reprojection, known motion, disocclusion, resize, camera cut, scene edit, and resource failure before adding recovery heuristics.

when the input itself changes discontinuously, a low-pass filter is not a continuity model. choose a stable geometric target, projected centroid, tracked feature, or persistent identifier first. then measure screen-space position, velocity, acceleration, and visible error across a trajectory.

## make denoising prove signal preservation

**observed.** UVSR combined bilateral filtering, SVGF-style history, adaptive replay, several resolutions, and multiple signals into one configurable foundation. broad developer, integration, and capture suites passed while the visible result remained unsatisfactory. whole-image error could improve through blur, and short stationary captures did not expose sustained motion failures. guide buffers were sometimes sampled on a different grid from the noisy signal.

**recommended.** evaluate one linear signal at a time. compare its filtered result with a converging raw mean, not only with one noisy frame or a display-mapped image. require guide and signal sample locations to agree, including odd extents and boundaries. include stationary, constant-velocity, acceleration, camera rotation, disocclusion, thin geometry, specular response, and post-motion settling sequences. report bias, detail loss, lag, ghosting, variance, and cost separately.

## keep image interpretation subordinate to contracts

**observed.** the sky experiment implemented palettes, stars, controls, and multiple orbit models before proving the camera, world, horizon, and celestial relationship in a real scene. algebraically valid motion still looked wrong. MSAA and visibility experiments also showed that a darker or less noisy image is not automatically a more correct image.

**recommended.** reduce a visual idea to its smallest spatial contract and render that contract in the real coordinate system first. for a sky, prove horizon placement and one celestial orbit. for antialiasing, prove sample positions and coverage. for visibility, prove known blockers and misses. only then add artistic controls, temporal behavior, or presentation polish.
