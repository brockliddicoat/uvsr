# experiment design

## start with one disprovable claim

**observed.** several UVSR experiments grew from one idea into families of settings, shader paths, histories, presets, UI controls, documentation, and tests before the central image or performance claim was established. ReSTIR path tracing, sample accumulation policies, temporal reconstruction, sky presentation, and screen-space diffuse all acquired supporting systems that made the original question harder to isolate.

**recommended.** an experiment begins with one sentence:

> under these fixed conditions, mechanism A will improve measured outcome B over baseline C by at least threshold D within budget E.

record the scene, camera, hardware, resolution, formats, shader and compiler identity, warmup, sampling window, timing method, and image oracle before implementation. define a failure or retirement threshold. if the mechanism misses it, preserve the evidence and remove the path before adding variants.

## separate experiment, product, and recovery states

an experiment should have three explicit outcomes:

- **promoted**, because matched evidence proves a useful result and the product can own its lifetime cost.
- **revised**, because a specific new hypothesis can be tested by one bounded change.
- **retired**, because the result failed, the prerequisite is missing, or the ownership cost exceeds the value.

retirement is not evidence that the idea can never work. it prevents an unproven path from becoming permanent product surface. preserve the hypothesis, exact source identity, evidence, and a narrow revival gate. do not preserve active controls, allocations, branches, or build variants merely because recovery might be useful later.

## delay the multiplication points

settings, shader permutations, runtime branches, persistent schema, report cases, documentation, and UI are multiplication points. each adds combinations and future compatibility work even when it creates no separate compiled shader.

use this order:

1. one fixed implementation and one fixture,
2. one decisive correctness oracle,
3. one matched comparison with the baseline,
4. one failure and resource-lifetime path,
5. a second fixture that tests generality,
6. only then a user-facing choice or persistent setting.

**observed.** UVSR showed that a runtime toggle can add no shader permutation while still adding ABI, resources, state transitions, UI, presets, snapshots, tests, and docs. counting compiled variants alone understated the burden.

## measure the complete cost

an optimization ledger must distinguish exact measurements, derived values, engineering estimates, algorithmic expectations, and diagnostic observations. they cannot be summed as if they were equivalent.

measure complete active paths. for screen-space visibility, include hierarchy construction, traversal, reconstruction, filtering, and composition. for path sampling, include acceleration maintenance, tracing, reuse, denoising, and history. for a feature toggle, include idle allocations and CPU or GPU work. for a shader change, include compile time, binary size, pipeline creation, and runtime behavior.

compiler output can prove generated instructions. it cannot prove physical register allocation, occupancy, cache behavior, or frame time. a stage timing can prove cost in one controlled run. it cannot prove a portable performance win.

## compare at equal conditions

use the same scene, camera, output resolution, internal resolution, formats, shaders where applicable, draw order, warmup, capture interval, and hardware state. compare equal time when techniques trade samples for reconstruction, and equal quality when they trade cost for error. report both when possible.

keep source and candidate outputs together with their machine-readable identities. if the baseline is rebuilt, reconfigured, or rerun under a different driver, it is a new baseline. visual memory and screenshots from another executable are not controls.

## build a diagnostic before a control

**observed.** UI and persisted settings often arrived before the project had a decisive way to tell whether the underlying behavior worked. this made incomplete experiments look like supported product features and expanded the test matrix.

**recommended.** expose the measurement first. a temporary command, readback, fixed case, or structured record is usually enough. after the mechanism proves useful, design the product control around a stable user need. the control should never permit a known-invalid mode.

MSAA per-sample visibility illustrates the rule. correctness should hold whenever multisampling is active. a user toggle that allows incorrect per-pixel visibility is not useful configurability. temporal AA recipe choices similarly should not remain exposed when one coherent reconstruction contract is required.

## sequence the minimum visible proof

large visual features should prove their central relation before surrounding content:

- a sky proves world, camera, horizon, and one celestial orbit before palettes, stars, drawers, and automation.
- temporal reconstruction proves sample positions, reprojection, depth rejection, and disocclusion before history recovery and sharpening.
- a flashlight target proves a continuous screen-space trajectory before response curves and aim coupling.
- denoising proves one signal and matched guides before several signals, resolutions, and adaptive policies.
- path sampling proves unbiased or deliberately biased contribution behavior before reuse strategies and user presets.

this order makes failure cheap. it also produces a small piece that can be retained when a larger idea is retired.

## keep presentation work downstream

**observed.** tonemapper drawers, LUT presentation, UI animation, and detailed frontend restoration could be internally sound while the upstream lighting or runtime contract was unstable. presentation work then multiplied acceptance effort and sometimes concealed the underlying defect.

**recommended.** stabilize the source signal and interaction contract before adding grading, animation, or polish. UI motion must not delay commands, change layout ownership, or make test timing ambiguous. a strategic sunset can be the correct result even when code is technically competent, because sequence and ownership cost are part of engineering quality.
