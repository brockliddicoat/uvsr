# GPU Clock Normalization

## Purpose

Clock normalization can compare repeated GPU timings from one physical adapter
when boost state differs slightly. It cannot make different GPUs, workloads,
drivers, power limits, thermal states, displays, or renderer outputs comparable.
Raw frame time remains the primary measurement.

## Fixed Identity

Before collecting data, record exact source and executable SHA-256, settings
hash/version, adapter identity, driver, HAGS, power profile, display route,
resolution, refresh/HDR/VRR/vsync/frame-cap state, scene, camera, renderer
settings, warmup, sample window, and telemetry source. Use Bistro or San Miguel
with a saved camera and unchanged retained assets.

Create a machine-local profile from
`tools/gpu-clock-normalization-profile.example.json`. A reference clock belongs
to one physical GPU and one validated control workload. Never copy it to another
adapter, including the same marketing model. Keep completed profiles and raw
telemetry outside Git.

## Measurement

1. Close unrelated GPU work, overlays, capture tools, and avoid battery power.
2. Run `tools/check_uvsr_thermal_state.ps1` for a stable preflight. Do not waive
   missing sensors or thermal limits without recording the reduced confidence.
3. Launch the exact `uvsr-engine.exe`, load the fixed workload, and complete the
   declared warmup.
4. Bracket the candidate with control runs. Capture complete-frame GPU time,
   feature-stage time, clocks, utilization, temperature/headroom, power,
   limiter state, and telemetry coverage.
5. Use the same run count and sample duration for every candidate. Select the
   median run by raw complete-frame GPU time unless a different rule was fixed
   before measurement.
6. Reject the sample if output differs, the device reports an error, warmup is
   incomplete, contention or a limiter is active, temperature is unsafe,
   telemetry is stale/incomplete, or bracket controls drift beyond the declared
   bound.

The normalization helper applies only this fixed-clock model:

```text
normalized_ms = raw_ms * observed_graphics_clock / reference_graphics_clock
```

Run its deterministic contract check with:

```powershell
pwsh -NoProfile -File tools/normalize_gpu_clock_benchmark.ps1 -SelfTest
```

For a real sample, pass matching `GpuIdentity`/`ReferenceGpuIdentity` and
`WorkloadIdentity`/`ReferenceWorkloadIdentity` plus the measured timing, clocks,
and available guardrail telemetry. Identity mismatch is a hard failure.

## Interpretation

Report raw and normalized complete-frame time together, with percentiles and the
feature's own cost. Normalization is an estimate, not observed performance. It
does not correct memory bandwidth, occupancy, cache, power, CPU, presentation,
or thermal behavior. A faster sub-pass is not a product speedup when total frame
time or output regresses.

Keep the full accepted and rejected sample ledger. Do not average rejected runs,
mix machine profiles, tune the reference after seeing a candidate, or use
normalization to replace a same-state rerun. Performance claims require the
exact artifact, controlled output comparison, and sufficient repeated samples
to show that the result exceeds run-to-run noise.

## Tool Boundary

`tools/get_uvsr_performance_tools.ps1` downloads pinned external measurement
tools into ignored work storage and verifies their size and SHA-256. Their
licenses remain applicable. `tools/check_uvsr_thermal_state.ps1` captures
preflight/measurement guardrails; `tools/normalize_gpu_clock_benchmark.ps1`
validates and computes the narrow normalization. None is shipped with UVSR.
