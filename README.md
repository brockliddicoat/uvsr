# UVSR

**Unified Visibility Stochastic Rendering Engine**

<!-- uvsr-codebase-size:start -->
UVSR is a focused DirectX 12 research renderer built on NVIDIA's pinned Donut
framework and NVRHI. It ships with five ready-to-run research scenes, a
production-focused deferred PBR path, and independently testable visibility,
anti-aliasing, shadow, and diagnostic systems.

**First-Party Lines of Code:** 88,249 non-blank source lines.

**Third-Party Lines of Code:** 387,603 non-blank source lines.

**Total Lines of Code:** 475,852 non-blank source lines.

Counts cover UVSR source, tests, tools, build scripts, retained pinned
dependency source, and final first-party dependency overrides. Documentation,
assets, licenses, binaries, and generated build output are excluded. Regenerate
with `tools/update_readme_line_counts.cmd --write`.
<!-- uvsr-codebase-size:end -->

## Renderer Highlights

- **Visibility Bitmask AO and GI:** A 32-sector mask converts finite-thickness
  screen-space samples into ambient visibility and one-bounce diffuse
  transport; newly claimed sectors prevent double-counting.
- **DXR Implementation:** Material-aware inline ray queries drive sun, sky, and
  flashlight visibility with alpha-tested cutouts.
- **Ratio Estimators:** Correlated visible and unshadowed RGB responses reduce
  current-frame ray-traced sun-shadow variance; sky visibility separately uses
  a 1-64-sample cosine-hemisphere visible-ray ratio for environment lighting.
- **Deferred PBR:** A packed G-buffer feeds material-aware lighting, SH9 diffuse
  IBL, and prefiltered GGX specular IBL; median-luminance exposure feeds AgX.
- **Composable Anti-Aliasing:** Deferred 2x-16x MSAA, TAA with Filament and
  Sobol jitter, display-linear Fast Approximate AA, and CMAA2 can be combined.
- **Noise Research Stack:** Deterministic white, blue, and 64-layer
  spatiotemporal blue-noise textures support global and per-effect sampling.
- **Physical Diagnostic Flashlight:** A dedicated scene spot light uses shared
  runtime profile data for its two-lobe beam and finite-emitter ray-traced
  shadows; photometry, color, shape, emitter size, collision-aware mount, and
  motion are tunable.
- **Shared Ray Representation:** One BLAS/TLAS system and master traversal gate
  serve every ray-query effect without erasing individual settings.
- **Explicit Lighting Gates:** Ambient Fill and contribution gates make direct
  and environment composition inspectable without hidden fallback lighting.
- **Composable Debugging:** Lighting, visibility, buffer inspection, and
  thread/wave diagnostics remain independently selectable.
- **Deterministic Verification:** Reference tests and source contracts cover
  estimators, noise, PBR, anti-aliasing, resources, and the packaged shader
  bundle.
- **Compact Runtime Surface:** The build retains 306 first-party shader
  permutations, twelve Settings drawers, and 184 commands without dormant
  experiments.
- **Five Packaged Scenes:** Sponza Decorated, Sponza Plain, Bistro Interior,
  San Miguel, and Classroom Interior ship ready-to-run; Bistro and San Miguel
  copy glTF buffer views byte-for-byte into standard external buffers below
  GitHub's per-file limit, with no runtime reconstruction.
- **Extensive Documentation:** The [engineering library](#engineering-documentation)
  records architecture, equations, validation, provenance, negative results,
  and restoration boundaries.

## Coming Soon

No renderer feature is currently announced for integration. Local experiments
and uncommitted candidates remain under evaluation and are not commitments to
ship.

## Build and Run

Requires Windows with a DirectX 12-capable GPU and current driver, CMake 3.24 or
newer, Visual Studio with a C++17-capable MSVC toolchain, and Git with submodule
support. Configure, build every Release target, run the deterministic tests,
and launch from PowerShell:

```powershell
git clone --recurse-submodules https://github.com/brockliddicoat/uvsr.git
cd uvsr
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\bin\uvsr.exe
```

The first configure may download Microsoft's Direct3D 12 Agility SDK. Optional
build variants and the complete validation workflow are documented below.

## Engineering Documentation

- [Complete Control Scheme and Developer Workflows](docs/advanced-settings.md)
  covers the full control scheme, Settings drawers, command interface, build
  variants, and validation flows.
- [PBR Foundation](docs/pbr-foundation.md) defines material inputs, G-buffer
  packing, lighting equations, IBL, contribution gates, validation, and
  extension points.
- [Screen Space Visibility](docs/screen-space-visibility.md) documents the
  shared AO/GI traversal, estimators, reconstruction, memory contracts,
  supported quality profiles, and validation boundary.
- [Ratio Estimation](docs/ratio-estimation.md) distinguishes correlated sun
  shadow estimation from the sky visible-ray ratio and documents their
  sampling, single-ray routes, ray safety, composition, and limits.
- [Noise Sampling and Asset Provenance](docs/noise.md) defines global
  inheritance, effect overrides, precomputed assets, centered sampling,
  temporal progression, and provenance.
- [Temporal Aliasing Options](docs/temporal-aa-options.md) defines temporal,
  fast approximate, morphological, and multisample composition, history
  behavior, and coordinate conventions.
- [Engine Cutdown Archive](docs/postmortem/engine-cutdowns/README.md) keeps the
  dated shader and renderer cutdown reports, their measurements, complete
  removal inventories, and restoration boundaries.
- [Visibility Estimator Validation](docs/visibility-estimator-validation.md)
  records the shared C++/HLSL measure contracts and deterministic fixtures.
- [GPU Clock Normalization](docs/performance/gpu-clock-normalization.md)
  describes an advisory same-GPU trend estimate while preserving raw clean-run
  GPU time as the official score.
- [Environment Catalog](assets/environments/README.md) lists imported HDR
  radiance sources, hashes, licenses, and calibrated default exposures.
- [Experiment Postmortems](docs/postmortem/) preserve retired work, negative
  results, reusable evidence, and explicit restart conditions.

## Licensing

- **Community Use:** UVSR's first-party code is source-available under the
  [Polyform Noncommercial License 1.0.0](LICENSE.md). Noncommercial use and
  modification are welcome; when sharing, preserve the license and Required
  Notice.
- **Commercial Use:** Commercial use or sublicensing requires a separate
  written agreement. [Contact the UVSR project](mailto:brockliddicoat@gmail.com).
- **Legal Details:** Third-party material remains under its own terms. See the
  [Legal Guide](legal/README.md) for the full scope, documentation registry,
  commercial-readiness notes, and contributor agreement.
