# testing contract

the test system must serve two audiences from the same facts. a person needs a compact visual report, while a tool or coding agent needs structured results and bounded detail queries. neither view owns separate truth.

## inspiration inventories

the inspected AGFX revision contains a broad GPU test inventory with validation, byte-golden, and image-golden cases. its runner uses FLIP for image comparison and writes JSON consumed by a browser report. this is an inspiration for explicit, queryable evidence. AGFX's case list, default threshold, and report layout do not become this project's requirements.

ShaderToHuman contains golden-image regression groups, documentation branches, and examples that can inspire shader-language and presentation cases. they are examples to evaluate, not pre-existing passes or a required compatibility suite.

every project case must begin with a named risk and project-owned expected behavior. record which inspiration motivated it when useful. do not create one-to-one source dispositions merely to imitate another repository's inventory.

## required oracles

- exact or numeric buffer and image assertions where appropriate.
- alpha checks, because RGB-only comparison can miss failures.
- focused regions or crops for local defects.
- perceptual comparison with reviewed thresholds, never as the only oracle.
- short named frame sequences for temporal behavior.
- nonzero executed-case counts and explicit skipped or unsupported results.
- backend, adapter, driver, compiler, shader language, commit, executable hash, seed, and fixture identity.

goldens are reviewed inputs. missing goldens must fail rather than being silently generated and accepted. random tests use stable recorded seeds. repeated runs distinguish deterministic failures from instability.

at inspected revision [`f91b108a`](https://github.com/AmelieHeinrich/agfx/tree/f91b108a111d2ca3ca4b6586b6cb5dd750064fd7), AGFX records FLIP mean and maximum error, a threshold, case duration, status, and artifact paths in JSON. [visual verification](postmortems/visual-verification.md) explains which parts are useful here and why FLIP alone cannot prove graphics correctness.

## report and query interface

the report should present project-owned API behavior and shader regression views. filters include suite, case, backend, shader language, feature, outcome, and run identity. failed rows lead to expected, actual, and difference images, numeric summaries, first mismatches, source locations, reproduction commands, and artifacts.

the machine-readable result format should include:

```text
schema_version, run_id, source_revision, build_identity, executable_sha256,
case_id, hypothesis, suite, backend, shader_language, adapter, driver,
compiler, scene, camera, viewport, seed, outcome, assertions, metrics,
thresholds, first_failure, artifacts, command, duration_ms, skip_reason
```

support concise summaries and bounded detail queries. stable case IDs and result paths matter more than verbose logs. an LLM may navigate results, but its visual judgment is never a correctness oracle.

## test tiers and duration

run pure contracts and compiler checks before GPU work. run structural readback before image comparison. run deterministic still cases before temporal sequences. measure performance only after correctness holds, then validate the exact package separately.

set a duration budget for each tier. after a broad failure, rerun the exact case rather than the unchanged suite. avoid Cartesian scene and setting expansion when schema tests can cover domains and named rendered cases can cover causal behavior.

## upstream compiler tests

rust-gpu changes need focused compile-pass, compile-fail, disassembly, validation, optimizer-survival, layout, and runtime consumer tests. place generic compiler regressions upstream. keep framework and NoGraphicsAPI integration tests in this project. passing isolated compiler tests does not prove the actual consumer path.

## retained scene fixtures

the Bistro Interior and San Miguel files under `assets/scenes` are protected future integration fixtures. their current presence proves only source retention. a test may claim scene support only after it records the exact scene identity, backend, adapter, shader language, load result, rendered assertion, and applicable license boundary.
