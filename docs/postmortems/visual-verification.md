# visual verification

## turn rendered behavior into queryable facts

**observed.** UVSR had many source checks, unit tests, shader compilations, integration cases, and capture cases. several failed features still passed their available automation because the tests asserted wiring, finite values, state transitions, or a few stationary frames. they did not describe the visible failure over the camera path, motion interval, or scene condition where a person noticed it.

**inferred.** tests became slow because broad suites were doing work that a smaller set of decisive oracles could have rejected earlier. when a failure remained primarily visual, agents repeatedly opened the renderer, interpreted images, changed code, and reran large gates. the output was hard to search and hard to compare across attempts.

**recommended.** every GPU test should emit a compact machine-readable record. a person can still inspect images, but an agent should be able to answer the first useful questions from text:

- which stable case failed,
- on which source, build, adapter, driver, backend, shader language, scene, camera, resolution, and seed,
- which assertion failed first,
- the expected and observed values,
- the image metric and threshold when applicable,
- the expected, actual, and difference artifact paths,
- the exact reproduction command and duration.

stable text makes failures searchable, comparable, and cheap to hand off. it also keeps an LLM from treating an impression of a screenshot as a correctness proof.

## use several kinds of oracle

no single metric establishes graphics correctness. select the smallest set that detects the known failure.

| oracle | catches | common blind spot |
| --- | --- | --- |
| exact bytes or integers | layout, copy, addressing, counters, IDs | visually close floating-point output |
| numeric ranges and invariants | finite output, conservation, bounds, ordering | spatial defects inside valid totals |
| selected pixels or crops | local regressions and known features | defects outside the selected region |
| structural buffers | depth, normals, IDs, motion, coverage, resource state | later composition and presentation |
| perceptual image score | distributed visible difference | semantic errors that remain numerically small |
| temporal trajectory | lag, shimmer, ghosting, popping, recovery | static image defects outside the path |
| performance counters and timings | cost and inactive work | correctness and image quality |
| human review | unexpected or artistic failure | repeatability and precise diagnosis |

include alpha, color space, exposure, and image encoding in the contract. a comparison between differently exposed or differently sampled images is not a valid regression result merely because a tool can compute a number.

## learn from AGFX without copying its contract

at inspected revision [`f91b108a`](https://github.com/AmelieHeinrich/agfx/tree/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7), AGFX provides a useful inspiration for concise GPU evidence. its [image comparison](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx_tests/test_compare.cpp) computes NVIDIA FLIP mean and maximum error, writes a magma error image, and fails when the mean exceeds the case threshold. its [runner](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/src/agfx/agfx_tests/main.cpp) writes JSON with case status, duration, thresholds, numeric results, and artifact paths. its [report viewer](https://github.com/AmelieHeinrich/agfx/blob/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7/tools/test_report/index.html) reads that same JSON.

this project should adopt the principle, not AGFX's test inventory or acceptance threshold. FLIP is useful because it turns an image difference into queryable values and a localized error map. it cannot prove correct geometry, resource lifetime, temporal stability, physical energy, intended artistic output, or equal performance. each project case needs a reviewed reference, a reasoned threshold, and at least one structural or semantic assertion for the failure it is meant to catch.

updating a golden is an explicit review action. a runner must never make its own output correct by silently replacing the reference.

## use a layered gate

run cheap, diagnostic checks before broad visual work.

1. validate pure contracts, layouts, arithmetic, state transitions, and failure handling.
2. compile shaders and inspect required reflection or intermediate representation.
3. execute a minimal GPU case with validation enabled and read back structural buffers.
4. compare a deterministic still image with numeric and perceptual oracles.
5. run short named motion sequences for temporal techniques.
6. run matched performance cases only after correctness holds.
7. validate the exact staged package separately from the developer build.

one layer cannot stand in for another. shader compilation does not prove coordinate spaces. a clean debug layer does not prove the intended image. a good FLIP score does not prove lifetime safety. a source check does not prove execution. a developer executable does not prove the packaged artifact.

## bound the suite and its output

**observed.** UVSR multiplied feature settings, scenes, histories, and test variants faster than it improved failure detection. broad reruns produced long waits and large logs while the decisive visual uncertainty stayed open.

**recommended.** every case needs one named risk, one deterministic fixture, and a reason to exist. avoid Cartesian expansion when a schema test can cover domains and a few rendered cases can cover behavior. record zero executed cases and unsupported hardware as non-passes.

set time budgets by layer. print a concise progress line for each case and stop at the first infrastructure failure. preserve the complete structured record, but keep terminal output focused on the first actionable mismatch. after a broad failure, rerun the exact case. rerun the full gate only after a relevant repair or a new uncertainty.

## minimum result schema

```text
schema_version, run_id, source_revision, build_identity, executable_sha256,
case_id, hypothesis, backend, adapter, driver, shader_language, compiler,
scene, camera, viewport, color_space, seed, warmup, action_sequence,
outcome, first_failure, assertions, metrics, thresholds, artifacts,
command, duration_ms, skip_reason
```

for temporal cases, add frame index, current and previous sample identity, history validity, and per-frame measurements. for performance cases, add timing method, clock and profiler state, sampling window, and complete pipeline stage totals.
