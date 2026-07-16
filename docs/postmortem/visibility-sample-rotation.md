# Visibility Sample Rotation Postmortem

Visibility sample rotation is a retired local experiment. It is not suitable
for integration because the user's direct comparison found no visible
improvement and found the rotation-enabled result worse and noisier, even after
temporal reconstruction was allowed to gather. A possible convergence advantage
was considered but not observed or measured.

The implementation did establish useful coordinate, history, and resource
contracts. Those technical results are preserved here without presenting the
experiment as successful product work or a feature waiting for promotion.

## Outcome

The experiment rotated the full-resolution G-buffer receiver used by each
half-resolution visibility block through all four pixels of its output-space 2x2
footprint. It kept the visibility sample count, camera projection, G-buffer,
dispatch dimensions, resource bindings, and history allocation unchanged.

Automated checks and runtime transition tests showed that the implementation was
internally consistent. The four phases were deterministic, reconstruction stayed
registered at the locked pose, unsupported layouts used the legacy receiver, and
history resets did not expose stale-state corruption.

That technical result did not produce a useful image. In the user's evaluation,
sample rotation showed no visual improvement over the fixed receiver and added
visible noise. Letting the existing temporal reconstruction gather did not
recover the expected benefit. The user considered that the rotating result might
eventually be closer to convergence, but neither the observation nor the saved
evidence established that claim.

The product verdict is therefore negative. Do not promote the current
implementation, describe it as a quality improvement, or cite convergence as a
demonstrated benefit.

## Experiment Identity

- Canonical verified base:
  `a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`.
- Local branch: `codex/sample-rotation-experiment`.
- Implementation state: source remains uncommitted, unpushed, and never part of
  Canonical UVSR.
- Documentation state: a later user request authorized a documentation-only
  feature-branch commit and push; no pull request or merge was authorized.
- Design and technical evidence:
  [`../visibility-sample-rotation.md`](../visibility-sample-rotation.md).
- Completed execution record:
  [`../exec-plans/completed/sample-rotation-experiment.md`](../exec-plans/completed/sample-rotation-experiment.md).

At the time of this postmortem, the rejected source remains only in the task's
local working tree. Publishing these records does not publish the implementation
or create an immutable source checkpoint.

## Original Hypothesis

Half visibility resolution stores one AO/GI value for each output-space 2x2
block. The legacy mapping always traces from the same pixel inside that block.
The experiment hypothesized that visiting all four receivers over four rendered
visibility frames would reduce fixed-receiver bias and let temporal history
integrate complementary visibility observations without increasing per-frame
work.

The hypothesis assumed that the four receivers were interchangeable samples of
one block signal and that one low-resolution history value could accumulate them
usefully. The visual result did not validate that assumption.

## What the Experiment Changed

- Half visibility resolution with even output dimensions used the deterministic
  phase order top-left, bottom-right, top-right, and bottom-left.
- Primary tracing, temporal guides, filter guides, and full-resolution
  reconstruction shared one CPU-resolved receiver and registration offset.
- A rendered-sample clock advanced independently of the existing 32- and
  64-frame stochastic ray schedulers.
- Full, Quarter, and odd-sized outputs fell back to the exact legacy receiver.
- Three unused constant-buffer padding words carried the receiver and centered
  offsets; no resource, pass, dispatch, binding, or shader permutation was
  added.
- Debug builds exposed fixed-phase inspection and explicit history reset;
  Release builds did not contain those controls.

This was receiver dithering for one visibility block estimate. It was not
scene-level jitter, output-space superresolution, or four independent stored
samples.

## What Was Proven

- UVSR's UI Half setting is one stored sample per output-space 2x2 block, not a
  two-sample checkerboard layout.
- A receiver phase is an end-to-end coordinate contract. Receiver selection,
  temporal and adaptive guides, and reconstruction registration must agree on
  the same pixel, units, and sign.
- Complete even 2x2 domains can share one global centered offset. A partial
  block at an odd edge cannot, so exact legacy fallback is the conservative
  no-resource solution.
- The rotation clock can remain independent of visibility ray scheduling, and
  scheduler dimension 7 can remain reserved for the cosine estimator.
- ABI and resource compatibility can be preserved, although the shared shader
  path still gained a small amount of arithmetic and constant loading.
- Frozen-phase stills and transition tests are useful correctness probes.

None of those findings proves a visual-quality or convergence benefit.

## Product Evaluation

The decisive evidence is the user's direct qualitative comparison of the local
candidate:

- no visible improvement with sample rotation enabled;
- visibly worse and noisier output;
- no recovery of the expected benefit after temporal history gathered; and
- only an uncertain possibility, not an observation, that the result might be
  closer to convergence.

The retained screenshots establish phase selection, state, and registration.
They are not synchronized temporal sequences and cannot overturn the product
evaluation. The available telemetry also lacks a controlled rotation-enabled
row, so it cannot supply a compensating performance result.

## Why the Result Likely Regressed

No synchronized diagnostic capture isolated a single cause. The following are
architecture-grounded hypotheses, not proven root causes.

1. **One History Value Mixed Four Receiver Identities.** AO, GI, depth, and
   normal history remained one value per low-resolution block. A new phase did
   not retrieve history belonging to the same output pixel; it blended with or
   rejected a block estimate produced from a different receiver.
2. **Different Receivers Can Represent Different Surfaces.** At silhouettes,
   foliage, curtains, depth steps, and normal discontinuities, the four pixels
   in one 2x2 block are not interchangeable samples of one signal. Alternating
   between them can create phase noise or trigger depth and normal rejection.
3. **The Temporal Filter Was Not Phase-Aware.** Existing visibility history was
   designed to stabilize one logical block sample over time. It carried no
   phase identity, per-phase age, per-receiver validity, or four-way confidence
   needed for deterministic spatial multiplexing.
4. **Correct Registration Could Not Restore Lost Identity.** Subtracting the
   centered output offset made the current phase spatially consistent, but the
   single stored history value still could not preserve four independent
   output-pixel details.
5. **The Experiment Changed Only Visibility Receivers.** The scene and G-buffer
   were already rendered at output resolution without projection jitter.
   Rotation could not improve geometry coverage, alpha-test coverage, material
   texture derivatives, transparency, or texture LOD; it selected among already
   shaded G-buffer pixels.
6. **The Phase Order Included Large Signal Jumps.** The first step crossed the
   full 2x2 diagonal. A zero-mean sequence is mathematically tidy but does not
   guarantee low temporal variation in the sampled visibility signal.

## Takeaways

- Inspect the physical storage layout before translating names such as Half or
  quarter rate into a sampling scheme.
- Prove that history can preserve sample identity before adding a rotating or
  jittered sample sequence.
- A sequence that covers every location is not temporal superresolution when
  all locations collapse into one low-resolution history value.
- Resource neutrality and technical correctness establish compatibility, not
  usefulness. A default-off path can still add shader work and visual noise.
- Static fixed-phase captures validate coordinates; quality acceptance requires
  synchronized accumulated and moving sequences.
- A qualitative user comparison can reject product value even when it does not
  isolate a root cause. Unmeasured convergence must remain a hypothesis.
- Define the success metric and minimum rejected scene before building debug
  breadth or production-facing controls.
- For temporal experiments, test static convergence, slow and fast motion,
  disocclusion, silhouettes, alpha-tested detail, and history-reset behavior as
  one acceptance matrix.

## Revival Triggers

Do not revive this experiment merely because another four-phase sequence becomes
available. Reconsider it only when at least one sample-identity architecture
trigger and the required evidence trigger below both exist.

### Architecture Triggers

- UVSR gains output-resolution or four-phase visibility history with per-sample
  depth, normal, validity or confidence, phase identity, and history age. This is
  the most direct fix for the lost receiver identity.
- UVSR begins rendering scene inputs below output resolution behind a real
  output-space temporal upscaler with explicit current and previous projection
  jitter plus a documented de-jittered motion convention. Rotation could then
  improve source coverage instead of only choosing a visibility receiver.
- A backend-independent packed checkerboard layout can shade and store two
  diagonal samples per 2x2 at genuine half cost, with matching guide, motion,
  history, and reconstruction contracts. Full-resolution work followed by
  discard does not qualify.
- An existing phase-aware sparse reconstruction system makes sample identity,
  disocclusion, confidence, and lock lifetime first-class without adding a
  bespoke sample-rotation history stack.

### Supporting Conditions

- Internal padding, cropping, or edge-specific registration removes the
  odd-extent partial-block mismatch without putting an edge branch in the hot
  shader path.
- Pipeline specialization can select the exact legacy shader when rotation is
  disabled without creating an unmanageable permutation surface.

These conditions can make a successor more complete or cheaper, but neither one
addresses lost receiver identity and neither is sufficient to trigger revival.

### Required Evidence Trigger

Before revival, fixed-phase crops must first show complementary visibility
detail worth integrating. Synchronized static, four-frame, slow-motion,
fast-motion, and disocclusion sequences must then show convergence rather than
phase variation. Compare against a full-resolution reference and use temporal
difference images or another objective stability measure. Only after that image
gate passes should an exclusive-GPU, interleaved baseline/off/on timing run and
external profiling evaluate cost.

### Reminder Contract

If future UVSR work introduces one of the architecture triggers together with a
credible way to run the evidence gate, remind the user that it may be time to
revisit this retired experiment and link this postmortem. The reminder does not
authorize restoring the implementation; ask before reviving it.

## Required Restart Sequence

1. Start from the newest Canonical verified checkpoint available at that future
   time. Do not restore the entire rejected working-tree patch.
2. Reproduce the current negative case with the legacy receiver and a minimal
   rotating prototype before adding controls or compatibility paths.
3. Prove with fixed-phase references that the receivers contain complementary
   information that the target history representation can retain.
4. Implement the smallest phase-resolved history and reconstruction loop.
   Retain the existing minimal guide-aware upsample and define a current-frame
   fill for invalid history; add no production UI, elaborate new fallback stack,
   or extra classifier.
5. Run synchronized static, motion, disocclusion, and convergence captures.
6. Obtain a new direct user comparison of the exact candidate and settings.
7. Measure full-frame and visibility GPU time, memory traffic, register pressure,
   occupancy, and residency after the quality gate passes.
8. Add a default-off product control only if the candidate demonstrates a
   repeatable benefit and receives product acceptance.

## Reusable Pieces

The useful parts are the architecture findings, exact phase resolver, frozen-
phase test concept, receiver/guide/filter registration contract, conservative
fallback rules, and separation from the stochastic scheduler. Treat them as
individually reviewable references. Do not treat the complete implementation or
its current history model as a successor foundation.

## Final Decision

Retire the experiment. Keep it out of the production roadmap and do not promote
the current control. Preserve the postmortem and design evidence so a future
phase-aware reconstruction change can trigger a deliberate reminder and a new,
smaller experiment rather than an automatic restoration.
