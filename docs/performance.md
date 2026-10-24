# performance evidence

performance work begins with a decision, not a preferred result. state the
question, the metric that would answer it, the quality and correctness guards,
and the acceptance or retirement threshold before changing code.

## identity

bind every result to:

- full source commit and clean or dirty state;
- exact executable and package SHA-256;
- build configuration, settings hash, and engine identity;
- scene, camera, resolution, window mode, and complete relevant
  settings;
- adapter, driver, operating system, display mode, power state, and diagnostic
  layer state; and
- capture tool and version.

keep raw captures and logs outside Git unless they are small, durable project
evidence. record their path and hash. a timing copied without this identity is
an observation, not a comparable benchmark.

## comparison

compare one named baseline with one named candidate. use the same package path,
scene state, camera, settings, adapter, display, warmup, run length, and capture
method. change one hypothesis at a time. rebuild or restart when the changed
contract requires it, and record every deviation.

image correctness comes first. reject nonfinite output, missing work, stale
history, invalid resource state, or a visible quality regression before using a
faster time. compare protected features and difficult transitions, not only a
static easy frame.

use enough repeated samples to report a distribution. preserve warmup policy,
sample count, median, relevant percentiles, spread, and outlier treatment. do
not present a best run as the result. separate CPU frame time, GPU frame time,
individual GPU events, memory, shader build time, package size, and startup
time. these metrics answer different questions and are not interchangeable.

## clock and tool state

record observed GPU clock, utilization, temperature, power, and throttling state
when the platform exposes them. normalize for clock only when the same tool
captures a trustworthy clock for every compared sample and the relationship is
appropriate for the measured workload. keep raw and normalized values together.
never infer a clock, normalize a single run, or use normalization to excuse
thermal or power state drift.

profilers, PIX captures, vendor counters, and debug layers can change timing.
use them to explain work and lifetime, then confirm the decision with the least
intrusive repeatable measurement that still preserves identity.

## evidence classes

- unit, source, and reflection tests prove contracts, not speed or image quality;
- shader event timings explain a pass, not total product cost;
- hosted CI proves repeatable build and package contracts, not target GPU
  performance;
- a developer executable does not prove the production package; and
- an exact package launch does not prove a comparison without bound settings,
  camera, captures, and repeated timings.

state estimates and inferences as such. when evidence conflicts, keep the raw
results, identify the uncontrolled variable, and rerun the smallest comparison
that can resolve it. do not broaden the experiment while its base comparison is
uncertain.

## decision record

a useful result records the baseline and candidate identities, raw metric scope,
quality findings, uncertainty, decision, and rollback identity. a rejected
experiment should leave a concise source recovery commit and the reason it lost,
not a tuning recipe. a winning change must still pass the full affected build,
package, runtime, and protected feature gate.
