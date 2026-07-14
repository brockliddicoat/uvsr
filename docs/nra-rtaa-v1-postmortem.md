# NRA-RTAA v1 Postmortem

NRA-RTAA v1 is a retired experiment. It was not suitable for shipping because
it was too expensive, shimmered on small details, and became visibly less stable
in the lower-cost profiles. Persistent-history resurrection was the only option
that consistently reduced the observed edge shaking and swimming. This document
preserves the useful lessons; the implementation and its debug surface have been
removed.

## Outcome

The experiment attempted native-resolution temporal anti-aliasing with analytical
surface validation, reactive classification, thin-geometry coverage tracking,
variance clipping, edge-aware spatial fallback, persistent-history resurrection,
and sharpening. Those pieces did not form a stable base resolver. They formed a
large collection of recovery systems around a reprojection and sample-acceptance
contract that had not first been proven on the renderer's hardest cases.

The failure should not be read as evidence that temporal reconstruction cannot
work in UVSR. It is evidence that the next experiment must prove a much smaller
resolver before adding exception handling.

## Development chronology

The repository history makes the sequencing problem visible:

- `fdc606d` (`feat: add native analytical reconstructive taa`) added 6,023 lines
  and introduced the complete subsystem in one change. The first version already
  contained 32 debug modes, two history filters, velocity dilation, depth/normal/
  material/object validation, explicit and automatic reactive masks, thin-feature
  classification and diffusion, variance clipping, spatial fallback, persistent
  resurrection, sharpening, three temporal presets, and 1,145 lines of reference
  tests.
- `83fe09f` (`fix: stabilize and optimize reconstructive taa`) followed roughly
  one hour later and added another 2,112 lines while deleting 599. It changed the
  resolved-color versus raw-auxiliary jitter coordinates, background handling,
  grazing-depth tolerance, automatic reactivity, thin-feature evidence, coverage
  lock lifetime, history confidence, fallback scheduling, and resurrection safety.

This order inverted the normal risk-reduction sequence. Recovery mechanisms and
configuration breadth were engineered before the minimum reprojection loop had
demonstrated stable edges under camera motion, object motion, disocclusion,
subpixel geometry, and reduced-cost operation.

## Why the image shimmered

No capture matrix isolated one single defect, so the following is a ranked causal
analysis rather than a claim that one line caused every artifact.

1. **The current sample footprint moved while history acceptance changed
   discontinuously.** Projection jitter intentionally moves subpixel coverage.
   At a thin edge, the center pixel can alternate between foreground, background,
   and a borrowed velocity source. Depth, normal, material, bounds, and coverage
   gates were multiplied into one confidence. A small phase-dependent change in
   any hard gate could switch the result between accumulated history, clipped
   history, spatial fallback, and current-only color. That is a temporal step
   function, perceived as shimmer or swimming.

2. **Resolved color and validating auxiliaries had different sample grids.** The
   implementation stored resolved color on a display grid while depth and surface
   identity came from a jittered G-buffer. The follow-up commit had to separate
   `previousPixelCenter` from `previousAuxiliaryCenter` and apply the jitter delta
   only to the latter. This is a fundamental reprojection contract, not a polish
   item. Building thin coverage, reactive logic, and resurrection before this
   contract was settled made every higher-level signal suspect.

3. **Velocity dilation solved a hole but created an ownership problem.** Borrowing
   neighboring motion can find history at silhouettes, but the borrowed motion
   does not make the neighboring surface the center pixel's surface. The follow-up
   explicitly stopped borrowed depth, normals, and IDs from contaminating center
   history. Even with that repair, a pixel changing coverage can alternate between
   center motion, borrowed motion, and camera reconstruction as jitter phases
   change.

4. **Thin-geometry heuristics classified an unstable signal and then fed it back.**
   v1 combined authored flags, depth curvature, normal/material contrast,
   neighborhood diffusion, accumulated coverage, and a lock lifetime. The repair
   commit changed the classifier substantially to avoid treating ordinary slopes
   and one-sided polygon silhouettes as thin strips. A temporally accumulated
   classifier can stabilize correct evidence, but it can also preserve a wrong or
   phase-dependent classification.

5. **Automatic reactivity initially confused jitter variation with animation.**
   The repair changed reactivity to consider only history outside the broad current
   3x3 range and required motion corroboration. Before that, stationary high-
   frequency detail could reduce its own history weight simply because the jittered
   current sample differed from reprojected history.

6. **Clipping and fallback reduced noise by erasing the very subpixel signal the
   resolver needed to integrate.** Tight current-neighborhood clipping is safe at
   disocclusions but repeatedly cuts old edge colors when geometry appears in only
   some jitter phases. Spatial fallback then blends current neighbors rather than
   increasing temporal sample support. This can make an edge softer without making
   its position stable.

7. **Lower-cost profiles removed support precisely where the base resolver was
   weakest.** Performance and Balanced forced bilinear history sampling and
   compiled out resurrection. Their reduced spatial and thin-feature work exposed
   the phase-dependent acceptance problem instead of merely lowering quality. A
   scalable algorithm should degrade gradually; profile-dependent shaking means
   the expensive tiers were compensating for a broken invariant.

## Why history resurrection appeared to help

Immediate history was validated against the immediately previous jitter phase. At
a small edge, that phase is often the worst possible reference: the surface may
have disappeared, changed ownership, failed a depth/normal/material gate, or been
clipped to the current neighborhood. Persistent slots retained colors from older
jitter phases. Resurrection projected the current world point into those slots,
validated them independently, and selected a candidate when immediate confidence
was low or clipping was severe.

That increased the chance of finding a phase in which the same small feature had
usable coverage. It also bypassed a single bad immediate-history decision, so the
edge stopped following every new jitter sample. In signal-processing terms,
resurrection supplied a longer and less correlated temporal support window.

This was a useful diagnostic but not a sound foundation:

- it consumed two additional 28-byte-per-pixel history slots at its largest
  configuration, taking history storage from about 56 to 112 bytes per pixel;
- it added extra color, metadata, depth, and surface reads on difficult pixels;
- it was restricted to camera-static motion because v1 stored no multi-frame
  object-motion chain;
- it could hide immediate reprojection or acceptance errors rather than correct
  them; and
- it was absent from the cheaper profiles where stability was needed most.

The observation that resurrection helped therefore points to insufficient and
phase-fragile immediate temporal support. It does not validate the rest of v1's
classification machinery.

## Performance failure

The default path used two full-resolution compute passes before tone mapping. It
allocated prepared motion/classification targets plus at least two 28-byte-per-
pixel history slots. The resolve shader used a cooperatively loaded halo tile and
could execute neighborhood statistics, structural thin classification, history
filtering, depth/normal/material validation, YCoCg clipping, automatic reactivity,
spatial fallback, resurrection, and sharpening-related outputs.

At 1920x1080, immediate history alone was documented at roughly 110.7 MiB. The
largest persistent configuration doubled that to roughly 221.5 MiB, excluding the
prepared targets and output. The old estimate of approximately 133 logical bytes
read and 36 bytes written per pixel for the default resolve was not a controlled
hardware bandwidth measurement, but it correctly identified a bandwidth-heavy
design. The algorithm also carried substantial branch and register pressure, so
the cost was not only allocation size.

The important lesson is that a temporal AA base pass should be demonstrably useful
with one compact history and a small fixed neighborhood. Extra history surfaces,
classifiers, and fallback paths must each earn their cost against measured failure
cases.

## What was over-engineered too early

The following work preceded proof of the base resolver and made failures harder to
attribute:

- 32 debug outputs and a large UI before a small capture matrix existed;
- separate temporal presets and static performance profiles before one trustworthy
  quality level existed;
- reactive-mask estimation despite no meaningful first-party explicit producer;
- a multi-signal thin-geometry classifier, diffusion, coverage feedback, and lock
  lifetime before ordinary silhouettes were stable;
- configurable Catmull-Rom reconstruction before bilinear reprojection was proven;
- spatial fallback before rejection behavior was understood;
- persistent resurrection before immediate history was trustworthy;
- sharpening before reconstruction stability was established; and
- broad analytical tests that validated formulas and ABI behavior but could not
  detect temporal image instability.

These features were individually defensible. Their simultaneous introduction was
the mistake: too many mechanisms could hide, amplify, or compensate for the same
base defect.

## Required order for a successor experiment

NRA-RTAA v2 should not be started by restoring v1. A future anti-aliasing
experiment should pass these gates in order:

1. **Fixed non-jittered baseline:** one current color, one history color, one
   motion convention, bilinear reprojection, and an obvious current/history blend.
2. **Projection contract:** automated coordinate tests plus visual camera pan,
   rotation, resize, reverse-Z, and static-scene tests. Color and every validating
   auxiliary must have explicitly documented sample grids.
3. **Minimal disocclusion:** depth-only rejection first. Add normal and identity
   gates one at a time only if a captured failure requires them.
4. **Jitter integration:** enable one low-discrepancy sequence and prove that static
   one-pixel lines, fences, foliage, specular edges, emissive edges, and sky
   silhouettes converge instead of alternating.
5. **Motion matrix:** camera-static, camera-moving, rigid-object, skinned-object,
   subpixel-motion, and newly revealed background cases at 30, 60, and 120 Hz.
6. **One reconstruction filter:** keep bilinear until the acceptance logic is
   stable. Compare a sharper filter only with controlled captures and timings.
7. **Measured additions:** clipping, reactive handling, dilation, longer history,
   or spatial fallback enter separately, each with a named artifact, before/after
   captures, GPU time, memory cost, and an easy removal path.
8. **Quality scaling:** lower-cost modes reduce sample quality without changing
   the fundamental history-validity semantics. If a mode begins to shake, it does
   not pass.

The minimum acceptance set should include temporal difference heat maps or an
equivalent objective stability metric, offline supersampled references for static
scenes, GPU timings, occupancy/register data, and a memory-traffic estimate. Unit
tests remain necessary for coordinate algebra and packing, but they are not an
image-stability result.

## Reusable conclusions

- Stabilize sample ownership and reprojection before classifying content.
- A hard confidence product is dangerous at subpixel edges; continuous confidence
  or explicitly hysteretic decisions need tests across the full jitter cycle.
- Older history helping more than immediate history is evidence to inspect the
  immediate history contract first.
- Debug breadth does not replace a small, repeatable capture matrix.
- A lower-cost profile must preserve correctness invariants, not compile out the
  only mechanism masking instability.
- Do not promote a temporal reconstruction technique without controlled visual
  and performance evidence from the target renderer.

