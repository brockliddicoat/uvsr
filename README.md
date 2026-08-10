# UVSR

**Unified Visibility Stochastic Rendering**

<!-- uvsr-codebase-size:start -->
**First-Party Lines of Code:** 88,233 non-blank source lines.

**Third-Party Lines of Code:** 387,603 non-blank source lines.

**Total Lines of Code:** 475,836 non-blank source lines.

Counts cover UVSR source, tests, tools, build scripts, retained pinned
dependency source, and final first-party dependency overrides. Documentation,
assets, licenses, binaries, and generated build output are excluded. Regenerate
with `tools/update_readme_line_counts.cmd --write`.
<!-- uvsr-codebase-size:end -->

UVSR is a focused DirectX 12 research renderer built on NVIDIA's pinned Donut
framework and NVRHI. It ships with five ready-to-run research scenes, a
production-focused deferred PBR path, and independently testable visibility,
anti-aliasing, shadow, and denoising systems.

## Renderer Highlights

- **Unified Screen-Space Visibility.** Stochastic AO and one-bounce diffuse GI
  share traversal, exact sample budgets, noise, and reconstruction; each can
  emit physical hit distance.
- **DXR 1.1 Visibility.** Inline ray queries drive sky, sun, and finite
  flashlight visibility with alpha-tested cutouts. Sky and sun also expose
  correlated ratio estimation.
- **NVIDIA NRD.** Optional ReBLUR, ReLAX, and SIGMA paths independently denoise
  AO, GI, sky visibility, sun shadows, and flashlight shadows.
- **Deferred PBR.** A packed G-buffer feeds material-aware lighting, SH9 diffuse
  IBL, and prefiltered GGX specular IBL; median-luminance exposure feeds AgX.
- **Composable Anti-Aliasing.** Deferred 2x-16x MSAA, TAA with Filament and
  Sobol jitter, display-linear Fast Approximate AA, and CMAA2 can be combined.
- **Noise Research Stack.** Deterministic white, blue, and 64-layer
  spatiotemporal blue-noise textures support global and per-effect sampling.
- **Physical Flashlight.** A two-lobe analytical emitter provides finite
  ray-traced penumbrae, animated sampling, and a collision-safe camera mount.
- **Shared Ray Representation.** One BLAS/TLAS system and master traversal gate
  serve every ray-query effect without erasing individual settings.
- **Explicit Lighting Gates.** Ambient Fill and contribution gates make direct
  and environment composition inspectable without hidden fallback lighting.
- **Composable Debugging.** Lighting, visibility, buffer inspection, and
  thread/wave diagnostics remain independently selectable.
- **Deterministic Verification.** Reference tests and source contracts cover
  estimators, noise, PBR, anti-aliasing, resources, and the packaged shader
  bundle.
- **Compact Runtime Surface.** The build retains 306 first-party shader
  permutations, twelve Settings drawers, and 184 commands without dormant
  experiments.
- **Five Packaged Scenes.** Sponza Decorated, Sponza Plain, Bistro Interior,
  San Miguel, and Classroom Interior need no separate downloads or conversion.
- **Extensive Documentation.** The [engineering library](#engineering-documentation)
  records architecture, equations, validation, provenance, negative results,
  and restoration boundaries.

## Coming Soon

This section summarizes stable work that is active but not yet merged into
`main`. Experimental entries are not promises that the work will ship.

- **Main Lighting, Denoising, and Controls — In Development**
  (`codex/main-lighting-denoising-controls`). Integrates the ray traversal
  master switch, physical flashlight shadows, hit distance producers, ratio
  choices, alpha-tested visibility, NVIDIA NRD, shared precomputed noise,
  automatic display exposure, updated lighting defaults, and the loading
  progress estimate while removing production screen space directional shadows.
- **Screen Space Visibility Shared Shader Helpers — In Review**
  (`devin/1784102514-screen-space-shared-helpers`, PR #10). Consolidates shared
  depth, pixel coordinate, and safe normal helpers without changing equations,
  bindings, UI, or scenes.
- **Visibility Degenerate Path Test Coverage — In Review**
  (`devin/1784102780-visibility-test-coverage`, PR #11). Adds reference coverage
  for degenerate clipping, radial mask edge cases, and blue noise rank fields
  without changing runtime rendering.

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

The first configure may download Microsoft's Direct3D 12 Agility SDK. The
normal build leaves optional NVIDIA NRD processing disabled; its license-gated
build flags and the complete validation workflow are documented below.

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
- [Heitz Ratio Estimator Shadows](docs/heitz-ratio-estimator-shadows.md)
  documents the single dispatch matched RGB estimator, current frame sampling,
  hard shadow path, ray origin safety, independent composition, shared
  BLAS/TLAS representation, and limits.
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

- **Community Use.** UVSR's first-party code is source-available under the
  [Polyform Noncommercial License 1.0.0](LICENSE.md). Noncommercial use and
  modification are welcome; when sharing, preserve the license and Required
  Notice.
- **Commercial Use.** Commercial use or sublicensing requires a separate
  written agreement. [Contact the UVSR project](mailto:brockliddicoat@gmail.com).
- **Legal Details.** Third-party material remains under its own terms. See the
  [Legal Guide](legal/README.md) for the full scope, documentation registry,
  commercial-readiness notes, and contributor agreement.
