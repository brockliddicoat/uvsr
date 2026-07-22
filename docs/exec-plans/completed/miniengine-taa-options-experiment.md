# Anti-Aliasing UI Merge Experiment

## Objective

Maintain the merged canonical UI and newest anti-aliasing experiment while
reducing the optional execution matrix to the MiniEngine temporal path.

## Completed Scope

- Kept long-term MiniEngine TAA and its existing algorithm configuration.
- Kept CMAA2 as a normal method and morphology option with Conservative Low,
  Medium, High, and Ultra strengths.
- Removed SMAA, its pass, lookup assets, shaders, telemetry, and third-party
  source bundle.
- Retained Stable Interior directly above Sharpness, off in every preset, with
  no separate developer performance drawer.
- Added 16x as the MSAA Ultra request and defaulted every MSAA quality to CMAA2.
- Deferred combo-choice commits until the following UI frame without a close
  fade.
- Drew a presentation-only centered crosshair dot only while zoom is active.

## Validation

- [x] Rebuilt the developer shader bundle and Release executable.
- [x] Ran focused CPU reference and UI source-contract tests.
- [x] Ran the full CTest suite.
- [x] Confirmed SMAA names and blobs are absent from runtime manifests and
  staging.
- [x] Launched the developer candidate.
- [x] Exercised Temporal, CMAA2, and every MSAA quality without a crash.
- [x] Confirmed MSAA Ultra requests 16x and falls back safely when unsupported.
- [x] Confirmed all MSAA qualities resolve to CMAA2 by default.
- [x] Confirmed popup selections queue for the following rendered UI frame.
- [x] Confirmed the crosshair is centered, zoom-gated, and presentation-only.

## Product Acceptance

The merged build received direct interactive review across the animation,
dropdown, scrolling, visibility, statistics, magnification, camera, and
anti-aliasing changes. The user reported no remaining defects before requesting
canonical publication.

## Publication Verification

The clean dependency-override configuration rebuilt the developer Release
renderer and passed all 20 developer tests. The production configuration then
rebuilt 2,211 UVSR DXIL shader tasks from source, linked the Release renderer,
and passed all 21 production tests, including the packaged-shader contract.
The README line-count self-test/current-count check and the documentation
Title Case self-test/full scan also passed before commit.

## External Actions

Canonical publication to `main` was separately authorized on 2026-07-22. That
authorization did not include a force-push, pull request, or unrelated history
rewrite.
