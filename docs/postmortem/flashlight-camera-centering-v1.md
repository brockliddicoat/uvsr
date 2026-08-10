# Flashlight Camera Centering v1

Status: Rejected and removed on 2026-08-09.

## Decision

UVSR no longer moves the flashlight toward the camera in response to scene
depth, a selected receiver, projected beam alignment, or proximity probes. The
experiment repeatedly produced visible lurching and sticking when the camera
panned across rows of columns, pillars, door frames, and other rapid depth
discontinuities. The final observed failure looked like the beam almost
teleported from one nearer face to the next instead of transitioning slowly.

The retained behavior is deliberately narrower:

- the flashlight keeps its authored forward, horizontal, and vertical camera
  mount;
- an emitter-aware collision sphere repairs overlap and continuously sweeps the
  mounted light so it cannot pass through static collision geometry;
- collision may physically stop or slide the emitter, but it does not scale the
  authored mount or choose an aim receiver;
- lens sway changes direction only and does not affect collision or position;
- the factory beam color remains pure linear white; and
- finite-emitter lighting, four-ray soft shadows, alpha-tested ray traversal,
  and the flashlight's independent denoising path remain intact.

This rollback is local to the active lighting, denoising, and controls
candidate. It was developed from published feature-branch commit
`f892c17e33c007db69ca10f055bd7e59301b37d0`; that commit already contained an
earlier proximity-retraction version, so returning only to that commit would
not have removed the rejected behavior.

## User-Observed Failure

The progression of product feedback was consistent even as the implementation
changed:

1. Contact-only correction centered too late and appeared to snap at a wall.
2. A longer predictive range still lurched from roughly one metre away.
3. Receiver-driven centering reacted too late or too weakly on a flat wall.
4. Slowing the response reduced single-frame motion but made the beam feel
   attached to nearby pillars.
5. Adding a delay and target-specific evidence reduced isolated pulses, but
   rows of columns still caused harsh face-to-face transitions and sticking.

The final rejection was therefore not a parameter-tuning failure. The visible
artifact survived multiple response curves, delays, cache repairs, and aim
coupling strategies.

## Experiment Timeline

### Contact-Limited Mount Retraction

The first version derived a direct sphere-travel fraction from a camera-safe
anchor to the authored flashlight mount. It uniformly scaled the complete mount
offset before the final collision sweep. This kept the emitter outside the wall
but moved most of the mount over a very short camera distance.

### Predictive Proximity Envelope

The next version used mount-direction and forward sphere probes with a cubic
near-to-far envelope. The default range began near `0.75 m`, scaled for the
collision radius and mount length, and retained an immediate physical hard
limit. It started earlier than contact-only correction but still produced a
large apparent transition near the start of the envelope.

### Camera-Center Receiver Feedback

A two-sided CPU point ray from the camera selected the nearest surface along
the view direction. For a receiver at distance `d` and the authored convergence
distance `K = 6 m`, the desired mount extension was `s = clamp(d / K, 0, 1)`.
With an undisplaced camera anchor this gives:

```text
P = C + sD
Q = C + sKF
Q - P = s(KF - D)
```

The identity preserves the authored beam direction while the mount and aim
point scale together. It is geometrically correct for one stable receiver, but
the nearest surface selected by the center ray is not stable at depth edges.

### Connected Receiver Visibility

An emitter-to-receiver ray tested whether the offset emitter could see the
camera-selected surface. Outward restoration walked a connected visible prefix
in receiver-tolerance-sized steps with a bounded query budget. Additional
states represented settled, blocked, budget-exhausted, and occluded-retraction
conditions. A two-anchor recovery path attempted to handle a blocked chord
between individually safe camera mounts.

This fixed several correctness bugs, including later visible islands being
accepted across an occluder, stale blocked state after recovery, and obsolete
recovery waypoints after collision-radius growth. It did not make receiver
selection visually stable.

### Temporal Response and Aim Coupling

The controller used a `200 ms` inward half-life and an `80 ms` outward
half-life. Aim was derived from the final filtered extension rather than the raw
receiver depth. Optical misses were changed from immediate zero-extension
commands into temporally damped retraction requests. Sway remained downstream
and direction-only.

These changes bounded scalar extension movement and removed several angular
snaps. They could not prevent repeated depth-edge samples from continually
reversing or retargeting the controller.

### Target-Specific Time to Action

The final version added:

- a `100 ms` default confirmation interval, configurable from `0` through
  `500 ms`;
- a `0.25x` through `4.00x` response-speed multiplier, defaulting to `1.00x`;
- separate accepted and pending target extensions;
- a `0.005` extension deadband;
- a `0.05` extension retarget threshold, equivalent to `30 cm` of receiver
  depth at the six-metre convergence distance;
- receiver prevalidation so optical state could not leak across faces;
- explicit receiver-release aim reset; and
- a cached Camera Movement Diagnostics disclosure.

The first qualifying sample deliberately contributed no retrospective dwell
time, and materially different nearer targets had to collect fresh evidence.
This rejected short isolated pulses in deterministic tests. Persistent or
repeatedly alternating column faces still produced visible attachment and
face-to-face lurching.

## Defects Found During Development

The experiment exposed and repaired real implementation defects before its
product premise was rejected:

- flashlight Angular Size originally shortened one center ray instead of
  sampling a finite emitter, so positive size still produced hard shadows;
- the flashlight originally had no collision volume;
- camera preset and focus teleports could sweep stale flashlight state across
  the scene;
- initialization could bypass temporal response;
- an optical miss could force immediate full retraction;
- nonzero optical misses could stall indefinitely at the receiver target;
- cached occlusion state could transfer from one receiver to another;
- receiver release could retain stale exact aim;
- later visible islands could be selected across an earlier obstruction;
- blocked state could survive collision recovery and prevent restoration; and
- radius changes during recovery could keep obsolete waypoints alive.

The finite-emitter shadow fix, teleport-safe collision reset, collision sphere,
and pure-white default are useful independently and remain. The receiver,
retraction, temporal, recovery, command, UI, and diagnostic machinery does not.

## Why the Approach Failed

The leading explanation is a discontinuous measurement feeding a continuous
scalar controller:

1. A single camera-center ray returns the nearest triangle along one infinitesimal
   line. Small camera rotations across a silhouette can switch its hit from a
   far background to a near pillar in one frame.
2. The chosen receiver point and object identity can change discontinuously even
   when camera motion is smooth. A low-pass filter can slow the resulting mount
   displacement, but it cannot make the underlying target spatially coherent.
3. Delaying a new target suppresses short hits but creates hysteresis. Across a
   row of columns, repeated valid near hits become persistent evidence, while
   the release interval is repeatedly interrupted. The result feels sticky.
4. Driving both physical emitter position and aim from that one depth amplifies
   the selected surface change through parallax. Coupling them prevents one
   class of direction snap but still makes the beam appear attached to whichever
   face currently owns the measurement.
5. Emitter-to-receiver validation answers reachability, not perceptual beam
   centering. It has no knowledge of the projected beam footprint, receiver
   coverage, screen-space centroid error, or whether the selected face is a
   stable target for the viewer.
6. The CPU collision representation is static triangle geometry. It has no
   persistent object or surface-cluster identity and treats alpha-card geometry
   as solid, matching camera collision rather than rendered coverage.

This is an engineering diagnosis, not a proven single root cause. The missing
evidence is a frame-by-frame trace that correlates selected geometry identity,
screen-space beam centroid, projected receiver coverage, and camera angular
velocity in the exact column scene where the artifact is visible.

## Why Automated Tests Passed

The focused tests were effective at finding numerical and state-machine bugs,
but their acceptance model was incomplete. They covered planar walls, discrete
target sequences, fixed half-life goldens, frame subdivision, query budgets,
visibility islands, blocked idle state, hard safety, recovery phases, and source
ordering. They did not render a beam while continuously rotating across complex
multi-depth geometry.

In particular, monotone scalar extension, bounded per-frame movement, and an
exact aim identity for one receiver do not imply perceptually smooth projected
motion when receiver identity changes. Future work must add a screen-space
trajectory metric and a scene that reproduces the rejected rows-of-columns
case before treating unit-test success as product evidence.

## Removed Runtime and UI Surface

The rollback removes the following first-party concepts:

- `CameraCollisionWorld::GetRayTravelFraction`;
- `CameraCollisionWorld::GetSphereTravelFraction` as a mount-target query;
- receiver point, hit, release, and visibility caches;
- mount extension, hard target, responsive target, accepted target, pending
  target, pending time, and recovery waypoints;
- receiver visibility and mount adjustment state machines;
- proximity, `d / 6`, connected-prefix, and temporal mount helpers;
- **Adjustment Speed** and **Time to Action**;
- `light.selected.flashlight.adjustment-speed` and
  `light.selected.flashlight.time-to-action`; and
- **Camera Movement Diagnostics**.

No compatibility aliases or dormant settings remain.

## Retained Collision Contract

The collision-only controller has no scene-depth feedback. On initialization it
places the authored emitter sphere outside geometry. When the mount or camera
moves, it continuously sweeps from the last safe emitter position to the new
authored position. When Angular Size changes the collision radius, it repairs
the old position for the new radius before sweeping. Static frames reuse the
cached position and issue no collision traversal.

The collision radius is the maximum of the camera collision radius, the
analytical emitter radius, and the flashlight minimum radius. Teleport paths
reset the cache so a new camera preset does not sweep the old emitter across the
entire scene. The authored direction is computed from the camera mount and is
not changed by collision or receiver depth.

## Requirements for a Future Attempt

A future implementation should not restore this experiment wholesale. It must
first demonstrate a stable measurement and a perceptual acceptance metric.
Promising directions include:

- sample a robust distribution of depths across the actual projected beam
  footprint instead of one center ray;
- cluster samples by persistent object, instance, primitive, or surface region
  and track confidence before changing the chosen receiver;
- measure projected beam centroid error against a stable screen-space target
  rather than deriving physical mount extension directly from depth;
- separate cosmetic beam steering from the physical collision-safe emitter and
  compare steering-only, mount-only, and combined variants;
- bound velocity, acceleration, and jerk in screen space, not only exponential
  error in a scalar mount extension;
- use explicit acquire, retain, and release confidence that is robust to
  silhouette crossings without making the beam cling to departed geometry; and
- consider whether near-camera beam alignment is better handled by optics,
  projection, or a near-field presentation rule instead of moving the physical
  light.

Required instrumentation should record, per frame:

- camera position, direction, angular velocity, and frame time;
- every depth sample and its instance, geometry, primitive, barycentrics, and
  surface normal where available;
- the selected receiver and confidence;
- authored and presented emitter poses;
- hard collision result;
- projected beam centroid and radius on the visible receiver; and
- controller target, velocity, acceleration, and state transitions.

Required product scenes include rows of columns, alternating near and far
pillars, door frames, corners, thin railings, foliage cards, a flat close wall,
and open-space transitions. Test slow and fast pans at 30, 60, 120, and 144 Hz.
No future design should merge based only on scalar unit tests; it needs a
recorded continuous trajectory and direct visual acceptance in these scenes.

## Rollback Verification

The rollback is complete only when all of the following are true:

- searches find no live receiver, proximity, mount-extension, temporal-action,
  or movement-diagnostic implementation;
- only `ResolveSphere` and `MoveSphere` remain in the flashlight collision path;
- focused flashlight, camera collision, command, UI, and renderer source tests
  pass;
- Standard and NRD Release builds pass the complete test suite and shader
  packaging checks;
- the rebuilt NRD candidate is launched for product review; and
- runtime review confirms wall penetration remains prevented and the factory
  flashlight is pure white.

Automated results and the exact candidate executable are recorded in the active
execution plan for the broader lighting, denoising, and controls work.
