# Advanced Settings and Developer Workflows

UVSR exposes one DirectX 12 renderer. The live command catalog in
[`src/ui_settings_command_catalog.h`](../src/ui_settings_command_catalog.h) is
authoritative for exact names, domains, defaults, supported verbs, and snapshot
persistence. This guide explains the retained behavior without duplicating the
whole catalog.

## Keyboard and Interface Controls

- **Escape** or **~** opens and closes Settings.
- **/** opens the command interface. Enter applies, Tab completes, Up and Down
  recall history, and Escape cancels editing.
- **M** opens or refreshes the Material drawer for the center surface.
- **F** toggles the camera flashlight when text input is inactive.
- **Z** cycles Off, 2x, 3x, 4x, and 5x pixel inspection.
- Freelook uses **W/S**, **A/D**, and **Q/E** for forward, lateral, and vertical
  movement. Arrow keys look, **X/C** roll, and **V** restores upright roll.

Without a startup code, settings begin at canonical defaults. Reset restores the
applicable factory value. Capture copies the current frame, and Restart restarts
the engine.

## Retained Renderer Surface

**General** selects Ray Marching or Path Tracing, the graphics adapter, adaptive
sync, camera mode, and scene. Bistro Interior and San Miguel are packaged.
Runtime selects Bistro first, then the first sorted retained scene as fallback;
runtime evidence, not ordering, must determine whether either scene is more
reliable. Dynamic scene selection uses the catalog's unique filename or display
name.

**Representation** owns BVH build preference, BLAS/TLAS update policy, and the
ray-traversal gate shared by directional visibility, sky visibility, flashlight
shadows, and path tracing.

**Noise** selects spatial white, spatial blue, or spatiotemporal blue sampling
at 64, 128, 256, or 512 square resolution and controls animation. For Ray
Marching, the single **Accumulate Samples** toggle owns one cumulative
every-pixel scene-linear mean. Path Tracing always advances its own fixed
cumulative history. Its one product count is the asynchronously read center-
pixel accepted-sample value that backs the displayed mean; the per-pixel GPU
counter texture is internal history storage, not another product counter. The
Ray Marching toggle neither controls nor resets Path Tracing history. There are no
averaging, adaptive scheduling, workload, history-preset, or path-specific
accumulation selectors. History resets whenever camera, scene, resolution,
material, geometry, lighting, environment, mode, noise, or another
image-defining input changes. See [Noise Assets and Sampling](noise.md).

**Visibility** preserves the full AO/GI product surface: Low, Medium, High,
Ultra, and Custom quality; projected-angle, solid-angle, and cosine-weighted
estimators; full, half, and quarter resolution; 1–64 samples; radius, thickness,
distribution, and per-effect noise. AO retains enable, strength, hit-distance,
and 16/32-bit precision. GI retains enable, intensity, hit-distance, and
16/32-bit precision. Their combinations and quality/filter choices are
protected.

**Denoising** retains Raw and the supported spatial/NRD choices for AO, GI,
directional shadows, and sky visibility. AO supports joint bilateral, Gaussian
bilateral, and ReBLUR; GI and sky also support ReLAX; directional shadows also
support SIGMA. Radius, quality, resolution, history, disocclusion, and anti-lag
controls remain where the selected method consumes them. Missing required
hit-distance data must leave the raw signal valid.

**Aliasing** retains TAA, FXAA, sharpening, and 2x/4x/8x/16x MSAA for Ray
Marching. TAA exposes Low, Medium, High, and Ultra recipes plus the current
history, motion, rectification, blend, jitter, cost, and sharpening overrides
listed in the command catalog. MSAA must trace directional visibility for every
covered sample; it must not broadcast one pixel result across a mixed-coverage
pixel. A quality recipe is a starting vector, not separate renderer code.

**Sky** selects all six retained HDR environments, common exposure, automatic
exposure, diffuse/specular IBL, the environment background, and ray-traced sky
visibility. Sky visibility retains its diffuse/specular effects, hit distance,
1–64 samples, noise override, maximum distance, and ray bias. Every HDR source
remains byte-for-byte protected; see the
[Environment Catalog](../assets/environments/README.md).

**Lights** edits scene lights and the camera flashlight. The flashlight retains
its analytical beam, finite emitter, motion, noise, and ray-traced shadow
controls. **Directional Shadows** has only enable, maximum distance, and ray
bias; direct binary ray-query visibility is the fixed algorithm.

**Debug**, **Material**, and **Interface** retain current world/visibility/PBR
views, material editing, Amp/Ogg skins, bundled Noto Sans and ProggyClean font
roles, colors, animations, and zoom. A debug selection changes presentation,
not the estimator or history contract.

## Fixed Path Tracer

Path Tracing selects one conventional recipe: four maximum bounces, one fresh
sample per pixel per frame, uniform analytical-light selection, a fixed
diffuse/GGX proposal mixture, and Russian roulette beginning at continuation
three. These are implementation constants, not settings, commands, snapshot
fields, presets, or compatibility aliases.

The tracer reconstructs opaque and alpha-tested material data at committed DXR
hits, evaluates emission, analytical lights, environment misses, diffuse and
GGX transport, and writes one cumulative mean/count history. It does not use a
configurable solver matrix, alternate reuse histories, a separate primary
surface, a path denoiser selector, TAA, or MSAA. See
[Path Tracing Transport](path-tracing-transport.md) and
[PBR Foundation](pbr-foundation.md).

## Commands and Snapshots

Use `get`, `set`, `toggle`, `reset`, and `run`; `help` and `list` discover the
live surface. Paths and verbs are case-insensitive ASCII, while values follow
the catalog domain. Quote values containing spaces. Aliases such as `/scene`,
`/camera`, `/skin`, and `/ui` desugar to canonical paths.

The 32-character lowercase snapshot code begins with its four-digit registered
schema version. Copying it persists the complete canonical represented values in
the local versioned catalog. After the scene is ready,
`/settings.load <32-character-code>` completely preflights decoding, hash,
membership, and domains; captures all live values; runs catalog-order setters;
and reads everything back. Apply or readback failure rolls back through the
ordinary setters and reports the failure stage and rollback result. Restored
values may still conservatively invalidate or rebuild transient render history.
`--settings-snapshot <code>` uses the same path during startup and exits on
failure. Dynamic scene, adapter, selected-light, and selected-material
identifiers must already match live state; select them through their ordinary
interfaces. Their dependent properties apply through ordinary setters when
available. An unavailable saved property is a no-op only while that property is
also unavailable live. Session-only panel state and actions are excluded. Build
`uvsr_settings_snapshot_decoder` to decode a code; missing catalogs, unknown
versions, collisions, and fingerprint mismatches fail explicitly. The
allocation and integration rules are in
[Settings Snapshot Schema Versions](settings-snapshot-schema-versions.md).

## Build and Validation

Developer and CI builds use `BUILD_TESTING=ON`; production uses
`BUILD_TESTING=OFF`. Keep one external build tree per worktree:

```powershell
cmake -S . -B <external-build-root> -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build <external-build-root> --config Release --parallel
ctest --test-dir <external-build-root> -C Release --output-on-failure
```

After focused tests, run the full developer gate once, then build and smoke the
exact fresh production package. Runtime evidence must bind source identity,
configuration, settings hash/version, executable SHA-256, scene, camera,
resolution, warmup, and capture window. Exercise Bistro and San Miguel, all six
HDR environments, every noise resolution, AO/GI combinations, sky and
flashlight shadows, directional shadows at 1x/2x/4x/8x/16x, path-tracer resets,
and representative static, motion, disocclusion, resize, and lighting changes.
Compilation or a non-black frame is not visual, performance, or package proof.
