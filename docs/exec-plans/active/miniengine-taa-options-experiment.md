# Anti-Aliasing UI Merge Experiment

## Objective

Maintain the merged canonical UI and newest anti-aliasing experiment while
reducing the optional execution matrix to the MiniEngine temporal path.

## Current Scope

- Keep long-term MiniEngine TAA and its existing algorithm configuration.
- Keep Intel CMAA2 as a normal method and morphology option.
- Keep spatial SMAA only as a fixed diagnostic comparison.
- Remove SMAA T2x, its histories, shader bundles, telemetry, and exclusive
  execution experiments.
- Keep only developer performance overrides that operate on MiniEngine TAA.
- Add 16x as the MSAA Ultra request and default every MSAA quality to CMAA2.
- Defer combo commits until the 120 ms fade is visibly complete.
- Draw a presentation-only centered crosshair dot.

## Validation

- [x] Build the developer and production configurations.
- [x] Run CPU reference and production shader-bundle tests.
- [x] Confirm removed T2x and SMAA performance shader names are absent from
  runtime manifests.
- [x] Launch the developer candidate.
- [ ] Exercise Temporal, CMAA2, spatial SMAA, and every MSAA quality without a
  crash.
- [x] Confirm MSAA Ultra requests 16x and falls back safely when unsupported.
- [x] Confirm all MSAA qualities resolve to CMAA2 by default.
- [x] Confirm popup selections queue and cannot commit during the opening fade.
- [x] Confirm the crosshair is centered and remains presentation-only.

## External Actions

Do not push, open a pull request, merge, or publish this experiment without a
separate user instruction.
