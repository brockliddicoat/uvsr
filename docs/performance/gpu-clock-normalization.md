# GPU Clock Normalization

## Purpose

This reference defines how UVSR agents may retain useful performance trends
when the same physical GPU runs the same workload at different graphics clock
speeds. Clock-normalized values are advisory estimates. The raw GPU
milliseconds from a clean benchmark window remain the official measurement.

Use the companion helper:

```powershell
.\tools\normalize_gpu_clock_benchmark.ps1 -SelfTest
```

Copy
`tools/gpu-clock-normalization-profile.example.json` into an untracked
`outputs/gpu-clock-normalization/` record when establishing a real machine
profile. Do not replace the example with one machine's local identifiers.

## Non-Negotiable Scope

Clock normalization is valid only for repeated measurements on the same
physical GPU. It is not a cross-GPU comparison technique.

- Require the same machine-scoped GPU identity on the sample and its reference.
  Prefer a host identity plus a persistent PCI or Plug and Play device identity.
  A DXGI adapter LUID is useful within the interval over which it remains
  stable. The adapter marketing name is descriptive evidence, not identity.
- A normalization-tool profile keyed only by adapter name does not establish
  physical-GPU identity. On any machine without a verified local profile, treat
  normalization as unavailable even if the adapter name matches a calibrated
  model.
- Two GPUs with the same marketing name are still different physical GPUs.
- Never normalize a discrete GPU to an integrated GPU, one model to another,
  desktop silicon to laptop silicon, or one physical example of a model to
  another physical example.
- Every physical GPU selects and stores its own reference graphics clock. If a
  GPU needs separate power or thermal profiles, give each profile its own
  calibration rather than borrowing another GPU's reference.
- Values normalized to GPU A's local reference and values normalized to GPU B's
  local reference are in different reference frames. Do not compare those
  normalized values against each other. A cross-GPU report uses matched raw
  measurements and describes the hardware differences.
- Never normalize CPU setup, culling, submission, or recording time.
- Never use normalization to excuse a device error, unsafe temperature,
  visibly incorrect output, incomplete warmup, changed workload, or failed
  control bracket.

## Per-GPU Reference Profiles

Establish a profile separately for each physical GPU and power/thermal mode:

1. Select the stable UVSR control workload, normally Position 1.
2. Match the driver, HAGS state, display mode, power source, OS power mode,
   vendor performance mode, fan/thermal preset, renderer priority, scene,
   camera, resolution, graphics settings, and telemetry source.
3. Complete shader, resource, and cache warmup.
4. Capture at least 1,000 consecutive frames or 15 seconds per run, whichever
   is longer.
5. Repeat the cold, clean run three times and choose the median run by raw total
   GPU time.
6. Use the median loaded graphics clock from that selected run as the GPU's
   reference clock. Do not use an advertised boost clock or another GPU's
   result.
7. Record a validated clock range by repeating the identical workload at
   naturally occurring lower and higher clocks. Keep the range only when
   per-frame normalization makes the results agree within the chosen error
   band, normally three to five percent.
8. Record the reference memory clock or current-clock bandwidth, utilization,
   power, temperatures, thermal headroom, limiter state, telemetry cadence,
   and raw control timings.

Invalidate or version the profile when the physical GPU, VBIOS, driver,
performance profile, fan/thermal policy, memory behavior, or telemetry
interpretation changes materially.

## Establishing Physical GPU Identity

On Windows, start with the vendor-neutral Plug and Play inventory:

```powershell
Get-CimInstance Win32_VideoController |
    Select-Object Name, PNPDeviceID, DeviceID, AdapterCompatibility
```

Match that inventory to the exact adapter UVSR reports. Combine the selected
adapter's persistent Plug and Play or PCI identity with a stable local host
identity in the untracked profile. Fail closed when a hybrid or multi-GPU
machine cannot be mapped uniquely.

When NVIDIA NVML is available, its device UUID and PCI bus ID are stronger
additional evidence:

```powershell
nvidia-smi --query-gpu=uuid,name,pci.bus_id --format=csv,noheader
```

Use an equivalent stable vendor identifier when AMD, Intel, or another vendor
exposes one; the generic machine-scoped Plug and Play identity remains the
fallback. Do not commit real UUIDs, host identifiers, or local profile files
unless the user explicitly asks. Keep them under the untracked
`outputs/gpu-clock-normalization/` directory.

## Formula and Per-Sample Method

For one GPU-timed sample:

`time at reference clock = raw GPU time * observed graphics clock / reference graphics clock`

For example, a 1.600 ms pass observed at 1,800 MHz and normalized to that same
GPU's 2,200 MHz reference is:

`1.600 * 1800 / 2200 = 1.309 ms`

Also retain the clock-work index:

`clock-work index = raw GPU time * observed graphics clock`

The index is smaller-is-better and is useful for checking whether two clock
states represent approximately the same amount of graphics-clock-sensitive
work. It is not a device-independent unit.

Normalize each frame with the clock paired to that frame, then calculate the
median, percentiles, and confidence interval from the normalized frames. Do not
multiply an aggregate time by an unrelated average clock when per-frame
telemetry is available. Record telemetry generation and age so a clock sample
cannot be silently paired with the wrong frame.

On the same GPU, an unsmoothed clock-capacity value derived from graphics clock
and the fixed shader-core count produces the same ratio as MHz. UVSR does not
expose cached peak capability in Performance; collect observed per-frame
graphics-clock telemetry rather than substituting an advertised specification.

## Evidence Beyond Graphics Clock

Clock speed is only the scaling input. The following evidence determines
whether the estimate is trustworthy, directional, or invalid:

| Evidence | Why It Matters | Treatment |
| --- | --- | --- |
| Physical GPU identity | Prevents accidental cross-GPU scaling | Exact match required |
| Workload identity | Clock scaling cannot repair different coverage or work | Scene, camera, resolution, settings, API, frame cap, present mode, and comparison key must match |
| Workload counters | Missing casters, pages, taps, or dispatches can look artificially fast | Compare feature counters and stage activity before accepting equal work |
| Build identities | Establishes exactly what changed | Record both executable hashes, source identities, and the intended code delta |
| Driver, OS, and HAGS | Can change scheduling, shader compilation, and runtime behavior | Match or start a separately labeled comparison series |
| Graphics utilization | Low occupancy makes inverse-clock scaling less predictive | Grade the estimate; never insert an assumed 100 percent |
| Memory clock and bandwidth | A memory-bound pass does not scale with graphics clock alone | Require stability; downgrade above five percent drift and treat above fifteen percent as directional |
| Power state | Voltage, P-state, AC/battery state, power draw, and power caps can change more than clock | Record and compare with the GPU's own power profile |
| Fan and thermal profile | Different cooling behavior changes sustained boost and memory temperature | Keep the same profile and record fan-policy changes |
| Temperature and headroom | Heat can alter clocks, voltage, memory, and boost behavior | Keep the thermal gate and live limiter checks independent of normalization |
| Thermal limiter state | Active limiting makes a simple clock ratio incomplete | Directional at best; unsafe or errored runs are rejected |
| CPU and background load | CPU starvation can create GPU bubbles that clock scaling cannot fix | Apply the normal process and adjusted-CPU gates |
| Renderer priority and duplicate processes | Scheduling differences can dominate a small feature time | Require the established priority and exactly one intended renderer |
| Warmup and cache state | Compilation, uploads, and cold cache work are different workloads | Normalize only the matched warmed or explicitly cold state |
| Resource health | A leak, VRAM pressure, paging, or growing process state can degrade later runs | Track dedicated GPU memory, process memory, handles, and run order |
| Sample count and coverage | Sparse telemetry can bias an aggregate | Require at least 95 percent clock-paired samples for a published summary |
| Telemetry cadence and age | A stale clock may belong to another boost state | Pair by generation; downgrade beyond one cadence and treat beyond two as directional |
| Before/after controls | Detects drift, contention, leaks, and progressive heating | Keep advisory estimates only when the device stayed healthy and workload identity remained exact |
| Visual and debug validation | Less work caused by missing shadows is not an optimization | Incorrect output rejects both raw comparison and normalization |
| Stage attribution | CPU-, memory-, and mixed-bound stages scale differently | Normalize GPU stages separately and label mixed-bound totals as lower confidence |
| DVFS residual | The same-GPU linear model must be demonstrated rather than assumed | Compare normalized results across at least three natural clock bands and record the remaining error |

These fields are evidence and gates. Do not invent additional correction
factors for utilization, temperature, power, memory drift, or background load.
Silently correcting several variables can make a bad run look precise while
removing a real optimization.

## Display, Presentation, and Spike Interpretation

Record the complete active display state even when the benchmark reports an
isolated GPU timestamp. Monitor refresh rate is not part of the clock
normalization formula, but it can change presentation cadence, queue depth,
CPU wakeups, compositor work, idle gaps, boost behavior, memory clocks, heat,
and the frame indices at which periodic spikes appear.

| Display Factor | Possible Effect | Required Treatment |
| --- | --- | --- |
| Primary monitor refresh rate | Changes refresh interval, present cadence, idle gaps, and periodic spike alignment | Match and record the exact active OS mode in hertz |
| Panel maximum versus active refresh | The advertised maximum may differ from the current Windows mode | Record the active mode, not the box specification |
| VRR, G-SYNC, or FreeSync | Makes instantaneous scanout cadence vary with frame delivery | Match enabled state and operating range; record whether VRR was active |
| VSync | Can cap throughput, block present, alter queue depth, and quantize frame times | Keep disabled or identically enabled; never mix states |
| Driver or application frame cap | Creates intentional idle time and can lower clocks or utilization | Disable for uncapped work or match the exact cap |
| NVIDIA Reflex, Anti-Lag, low-latency mode, or maximum frame latency | Changes CPU/GPU queue depth and present scheduling | Match every latency control |
| Windowed, borderless, or exclusive fullscreen | Selects different compositor and flip behavior | Match the exact window and presentation mode |
| Desktop Window Manager composition | Can add display-synchronous work even with application VSync disabled | Keep compositor conditions and visible windows comparable |
| Multiplane overlay state | Can change scanout and composition paths | Treat unexplained MPO changes as a comparison warning |
| Primary display resolution | Changes scanout bandwidth and may change the UVSR backbuffer | Match both desktop and application resolution |
| DPI and desktop scaling | Can change window client size, compositor scaling, and capture cost | Match scaling and verify the actual backbuffer dimensions |
| HDR, SDR, color depth, and pixel format | Changes scanout format, bandwidth, and composition | Match HDR state, bit depth, and color mode |
| Multiple active displays | Can raise idle memory clocks, power, composition work, and thermal load | Match display count, resolutions, refresh rates, and which GPU drives each output |
| Mixed-refresh displays | Can create compositor cadence interactions and persistent memory-clock changes | Record every active rate; do not reduce the record to the primary display |
| MUX, Optimus, Advanced Optimus, or hybrid-copy route | Can add an inter-GPU copy and change which adapter owns presentation | Match the output route and verify UVSR's rendering adapter |
| External monitor, dock, USB display, or DisplayLink | Can change routing, copy work, interrupts, and power behavior | Match the connection topology or start a new baseline series |
| Cable or port change | Can change link mode, color format, refresh capability, and routed GPU | Record the port and effective display mode when it changes |
| Window focus, occlusion, or minimization | Windows and drivers may throttle or stop rendering | Keep UVSR visible, focused as required, and unobscured in matched runs |
| Overlays, notifications, animated wallpaper, and desktop video | Add compositor or GPU work and can create periodic spikes | Disable task-owned overlays and record unavoidable resident activity |
| Screen capture, PresentMon, cursor overlay, or remote desktop | Adds capture, copy, encode, or composition work | Apply the exact same capture path or keep it outside the timing window |

Do not multiply or divide a GPU pass time by monitor refresh rate. Use refresh
rate to interpret scheduling and spikes:

- Compute the nominal refresh interval as `1000 / active refresh rate Hz`.
- Report a spike by frame index and wall-clock timestamp.
- Report spike frequency both per 1,000 rendered frames and per minute. A
  frame-coupled event should remain stable per frame; a timer, compositor,
  telemetry, or operating-system event may remain stable per second.
- Keep the spike's absolute GPU milliseconds as the performance value. Its
  percentage of the refresh budget is a separate user-visible severity metric,
  useful for explaining why the same spike is less tolerable at a higher
  refresh rate; it is not normalized GPU work.
- Check whether its interval is near one or an integer number of refresh
  periods, compositor ticks, telemetry polls, benchmark-log flushes, cache
  update intervals, or thermal-monitor samples.
- Repeat with the same GPU workload at another refresh rate only as a
  diagnostic. If spike periodicity follows refresh cadence, identify a
  presentation or scheduling interaction; do not call the altered run a
  matched performance comparison.
- For a benchmark that advances motion per frame, such as 0.1 degrees each
  frame, refresh rate changes wall-clock motion speed but not the per-frame
  step. For time-based motion, a refresh-rate change also changes sampled
  positions and therefore changes workload identity.
- A pass measured by GPU timestamps can remain numerically independent of
  Present while its clocks, utilization, and surrounding queue behavior change.
  Keep the display state matched even when Present is outside the timer.

## Small System Influences

Record small influences when investigating sub-millisecond changes or rare
spikes. Most are comparison controls or downgrade reasons, not mathematical
normalization inputs.

| Influence | Possible Effect | Treatment |
| --- | --- | --- |
| Telemetry polling interval and phase | Can alias with refresh cadence or miss short boost changes | Record cadence, generation, and sample age |
| Performance drawer refresh and log flushing | Periodic formatting, file writes, or readbacks can create spikes | Match update rates and keep debug telemetry outside headline runs |
| Timer-query latency and source-frame pairing | A current clock can be paired with an older GPU time | Preserve source frame, generation, and age |
| Benchmark duration and starting phase | Short runs can overrepresent a periodic event | Use long fixed windows and report spike counts |
| Warmup duration | Shader compilation, uploads, cache filling, and residency settle at different rates | Define and verify the exact warmup state |
| Run order | Heat, caches, memory growth, and battery charge drift over time | Bracket and alternate baseline/candidate order when practical |
| OS timer resolution and scheduler quantum | Changes CPU wakeup and submission cadence | Keep the same applications and power configuration |
| Game Mode, HAGS, hardware scheduling, and fullscreen optimizations | Change scheduling and presentation behavior | Match every state and record changes |
| CPU frequency, core parking, E-core/P-core placement, and affinity | Can create GPU bubbles even in a nominally GPU-bound frame | Record CPU performance state and never GPU-normalize CPU time |
| Interrupt and DPC activity | Audio, network, USB, storage, and drivers can create isolated stalls | Use control brackets and process/system evidence |
| Audio device and sample rate | Can change periodic interrupt cadence | Keep the audio path matched for spike studies |
| Network traffic and wireless scanning | Can create CPU/DPC spikes | Record unusual activity; do not terminate user work without authorization |
| Storage, indexing, antivirus, and shader-cache writes | Can stall CPU submission or trigger background work | Warm caches, match policy, and inspect unexplained spikes |
| Shader and driver cache state | A cold or invalidated cache is a different workload | Label cold runs or warm both sides identically |
| GPU validation, DRED, debug layers, and markers | Add CPU/GPU overhead and may alter submission | Disable or match exactly |
| Process priority and focus boost | Alters CPU scheduling and frame delivery | Require the established UVSR priority and matched focus |
| Process memory, VRAM use, handles, and threads | Growth can reveal leaks, paging, or accumulated work | Record start/end values and reject unexplained degradation |
| Page-file and memory-compression activity | Can create long-tail stalls | Treat active pressure as contamination |
| AC adapter, battery state of charge, and charging behavior | Changes platform power budget and heat | Match AC/power state and record charging changes |
| BIOS, firmware, VBIOS, and vendor control profile | Changes boost, fan, and power policy | Version the GPU profile after changes |
| Ambient temperature, laptop surface, lid position, and airflow | Changes sustained clocks and thermal headroom | Record meaningful environmental changes |
| Fan hysteresis and delayed cooldown | Identical temperatures can still have different near-future cooling behavior | Use before/after controls and adequate settling time |
| Keyboard, mouse, controller, RGB, and USB polling | Can add small periodic CPU/interrupt work | Keep peripherals stable for spike attribution |
| Browser video, Electron animation, and assistant capture workers | Can use GPU, video, copy, or compositor engines | Keep task-owned capture workers outside accepted timing windows |
| Secondary GPU or media-engine activity | Shared power or memory fabrics can affect the selected GPU | Record per-adapter activity and fail closed when attribution is ambiguous |
| Integrated-GPU shared-memory traffic | CPU and GPU can contend for the same memory fabric | Record CPU load and current memory behavior; core-clock scaling is lower confidence |
| Driver P-state transition and residency | The same instantaneous clock may follow different recent idle histories | Match warmup and examine clock/power history |
| Sampling or profiling tool overhead | Monitoring can perturb a very small pass | Use identical minimal tooling and quantify it with controls |

When a factor is too small to prove, record it as a possible contributor rather
than inventing a correction. The purpose of the checklist is to explain
variance and design cleaner repeats, not to transform every noisy run into a
precise score.

## Quality Grades

Use a per-GPU validated range and report the reasons behind the grade:

- **A:** Same physical GPU and workload; clock inside the GPU's validated
  range; at least 95 percent utilization; memory drift at or below five
  percent; current telemetry; stable controls; no limiter or contention.
- **B:** Same identity and valid output; at least 90 percent utilization;
  moderate but understood telemetry, memory, power, or control drift.
- **C:** Same identity and valid output, but utilization or supporting evidence
  is missing or materially weaker. Retain the direction and uncertainty.
- **Directional:** The clock is outside the validated range, utilization is
  low, memory drift exceeds fifteen percent, telemetry is stale, a limiter or
  contention is active, or the controls fail or drift by more than five
  percent. A failed bracket cannot regain official-score status.
- **Rejected:** Different physical GPU, different workload, unsafe
  temperature, device error, incorrect output, or incomplete warmup.

Only raw clean-window GPU time is official. Always label normalized values
**Unofficial Same-GPU Clock Estimate** and place the raw value beside them.

## Same-GPU Example

GPU A has its own 2,200 MHz reference:

| Run | Physical GPU | Raw Time | Observed Clock | Local Reference | Normalized Time |
| --- | --- | ---: | ---: | ---: | ---: |
| Baseline | GPU A | 1.600 ms | 1,800 MHz | 2,200 MHz | 1.309 ms |
| Candidate | GPU A | 1.350 ms | 2,150 MHz | 2,200 MHz | 1.319 ms |

The raw values differ substantially, but the advisory normalized values suggest
approximately equal graphics-clock-sensitive work. Memory, utilization,
thermal, power, control, and visual evidence still determine the grade.

## Cross-GPU Example

GPU B reports 1.100 ms at 2,400 MHz. Do not normalize GPU B to GPU A's
2,200 MHz reference. Architecture, shader count, cache, memory subsystem,
driver path, power limits, and scheduling differ.

GPU B must establish its own reference, for example 2,550 MHz, and may use that
reference only to compare GPU B against itself. GPU A's and GPU B's normalized
values remain non-comparable. To compare the GPUs, use matched raw times and
report each device's conditions.

## Agent Workflow

1. Read this file and the repository's Performance Benchmark Hygiene rules.
2. Identify the exact physical GPU and load its own local profile.
3. Verify that sample and reference GPU identities are byte-for-byte equal.
4. Verify the invariant workload comparison key. Record baseline and candidate
   build hashes separately.
5. Run the normal preflight, warmup, measurement, and before/after controls.
6. Pair every raw GPU-time sample with unsmoothed graphics-clock telemetry.
7. Normalize each eligible frame with the local reference clock.
8. Aggregate raw and normalized values separately.
9. Grade the estimate using every supporting field above.
10. Report raw time first, normalized time second, reference clock, observed
    clock distribution, coverage, grade, and all warnings.
11. Reject any attempt to pass a different GPU identity to the helper.

## Tool Usage

Self-test:

```powershell
.\tools\normalize_gpu_clock_benchmark.ps1 -SelfTest
```

One same-GPU sample:

```powershell
.\tools\normalize_gpu_clock_benchmark.ps1 `
    -RawGpuMilliseconds 1.600 `
    -ObservedGraphicsClockMHz 1800 `
    -ReferenceGraphicsClockMHz 2200 `
    -GpuIdentity 'host-a|pci-device-a' `
    -ReferenceGpuIdentity 'host-a|pci-device-a' `
    -WorkloadIdentity 'sponza-pos1|1920x1080|csm-reference|d16|5x5' `
    -ReferenceWorkloadIdentity 'sponza-pos1|1920x1080|csm-reference|d16|5x5' `
    -GpuUtilizationPercent 97 `
    -ObservedMemoryClockMHz 9000 `
    -ReferenceMemoryClockMHz 9000 `
    -TelemetryAgeMilliseconds 100 `
    -TelemetryPollIntervalMilliseconds 500 `
    -SampleCoveragePercent 100 `
    -BeforeAfterControlDriftPercent 1 `
    -MinimumValidatedGraphicsClockMHz 1700 `
    -MaximumValidatedGraphicsClockMHz 2300 `
    -GpuPowerWatts 100 `
    -ReferenceGpuPowerWatts 100
```

The helper throws before calculation when GPU or workload identities differ.
Hard-invalid evidence returns no normalized value. It never converts CPU time
and never claims that a normalized result is official. When current-clock
bandwidth is more reliable than a memory-clock field, supply
`-ObservedMemoryBandwidthGBps` and `-ReferenceMemoryBandwidthGBps` together
instead of the two memory-clock parameters.

## Reporting Template

Report these fields together:

- Raw GPU time and sample distribution.
- Unofficial same-GPU clock estimate and distribution.
- Physical GPU identity and descriptive adapter name.
- GPU-local reference graphics clock and validated range.
- Observed graphics-clock distribution and clock-work index.
- Workload comparison key plus baseline and candidate build identities.
- Utilization, memory clock/bandwidth, power, temperature, headroom, and limiter
  state.
- Telemetry cadence, age, paired-sample coverage, warmup, and cache state.
- Before/after control drift, process-load status, and renderer priority.
- Visual/debug validation result.
- Quality grade, every downgrade reason, and a statement that cross-GPU
  normalized comparison is forbidden.

## Limitations

Inverse graphics-clock scaling is an engineering approximation. It works best
for a stable, graphics-compute-bound pass over a modest, empirically validated
clock range. It is weaker for memory-bound, latency-bound, occupancy-limited,
CPU-fed, asynchronous, mixed-engine, or rapidly changing workloads. It cannot
model voltage, boost-table, cache, memory, scheduling, or driver changes from
clock alone.

When in doubt, preserve the raw run, mark the normalized value directional or
unavailable, and collect a clean same-GPU control later.
