# testing contract

the test system must serve two audiences from the same facts: a person scanning the AGFX-style report and a tool or coding agent querying structured results. neither view owns a separate truth.

## source inventories

the research snapshot found 401 AGFX registrations across 149 distinct names in 109 C++ test files. these include repeated C, C++, and Ez API variants and are not 401 independent behaviors. the built reference runner must enumerate the authoritative baseline before compatibility mappings are frozen.

ShaderToHuman contains five golden-image regression groups, 35 documentation branches, and seven example families plus Intro. documentation and examples are candidates that need assertions, not pre-existing test passes. every source item needs a recorded implemented, equivalent, deferred, or unsupported disposition.

## required oracles

- exact or numeric buffer and image assertions where appropriate;
- alpha checks, because RGB-only comparison can miss failures;
- focused region or crop assertions for local defects;
- perceptual comparison with recorded thresholds, never as the only oracle;
- nonzero executed-case counts and explicit skipped/unsupported results;
- backend, adapter, driver, compiler, shader language, commit, seed, and fixture identity.

goldens are reviewed inputs. missing goldens must fail rather than being silently generated and accepted. random tests use stable recorded seeds. repeated runs distinguish deterministic failures from instability.

## report and query interface

the visual report keeps the AGFX appearance and provides separate **AGFX** and **ShaderToHuman** tabs. filters include suite, case, backend, shader language, feature, outcome, and run identity. failed rows lead to expected/actual/diff images, numeric summaries, first mismatches, source locations, commands, and artifacts.

the machine-readable result format should include:

```text
schema, run_id, source_revision, case_id, suite, backend, shader_language,
adapter, driver, compiler, seed, outcome, assertions, first_mismatch,
artifacts, command, duration, skip_reason
```

support concise summaries and bounded detail queries. stable case IDs and result paths matter more than verbose logs. an LLM may help navigate results, but its visual judgment is never a correctness oracle.

## upstream compiler tests

rust-gpu changes need focused compile-pass, compile-fail, disassembly, validation, optimizer-survival, layout, and runtime consumer tests. place generic compiler regressions upstream. keep AGFX and NoGraphicsAPI host integration tests in this project. passing isolated compiler tests does not prove the actual consumer path.

## retained scene fixtures

the Bistro Interior and San Miguel files under `assets/scenes` are protected future integration fixtures. their current presence proves only source retention. a test may claim scene support only after it records the exact scene identity, backend, adapter, shader language, load result, rendered assertion, and applicable license boundary.
