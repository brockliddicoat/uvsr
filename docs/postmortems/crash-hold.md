# CRASH-HOLD

`CRASH-HOLD` was a scoped machine-safety boundary created after three Windows bug checks occurred during repeated execution of the launcher's real system-services contract test. it was not a source freeze, a diagnosis, or a claim that the launcher caused a kernel defect. it stopped the small set of operations correlated with the crashes while allowing independent work to continue.

the response itself was sound. the test harness needs redesign before any equivalent integration test is attempted again.

## what happened

the strongest preserved correlation was an explicit run of `uvsr_launcher_tests.exe --system-services`. the last persisted command started that executable at `2026-09-10T01:50:54.6088126Z` with process ID 6456. the next Windows boot began about 15 seconds later.

three system reports shared:

- bug check `0x3B`, `SYSTEM_SERVICE_EXCEPTION`,
- exception `0xC0000005`, an access violation,
- WER bucket `AV_nt!CmpKeySecurityIncrementReferenceCount`.

the test output stopped at a case named `sequence reuse and downgrade rejection`. that name did not locate the crash. the case first created a fixture, packaged a renderer, constructed the installer, and executed installation. only then did it check sequence and downgrade behavior.

four surviving fixture journals narrowed the interruption. all four reached launcher phase `state-activated`, stopped before `shell-committed`, and contained no renderer transaction or installed renderer state. shortcuts and staging registry keys survived. this placed the interruption in launcher shell activation before renderer installation.

source review identified a candidate path. the shell layer created a staging registry key, wrote and flushed values, then called `RegRenameKey` to publish it. rollback and fixture cleanup could also delete registry state. this was a useful focus for offline analysis. it was not proof that `RegRenameKey`, the launcher, or any particular source line caused the kernel failure.

## what the evidence did not prove

the named `nt!` routine is where Windows detected the fault. it does not by itself identify the origin of corrupted state or the caller that created it. the preserved evidence did not prove:

- the exact API active at the instant of failure,
- a launcher defect,
- a Windows or registry defect,
- a driver defect,
- earlier memory corruption,
- recent JSON work,
- GPU activity,
- build parallelism,
- task context size, or
- the printed test assertion as the trigger.

three minidumps existed, but Windows denied ordinary access to them. a narrow administrator copy script was prepared and never executed. the smallest useful diagnostic step was to copy those existing dumps, preserve their hashes, and inspect their exception context, stack, triggering process, and loaded modules offline. deliberately reproducing another blue screen was not a reasonable substitute.

## what the hold prohibited

the hold covered commands that performed or could transitively reach the correlated system mutations:

- the launcher `--system-services` suite,
- installer install, repair, and uninstall paths,
- registry and shortcut mutation,
- cleanup of the interrupted fixtures,
- wrappers whose selected cases reached those operations,
- production-service validation and health checks that invoked installer state.

ordinary command labels were not trusted. a default launcher selection had been changed to pure cases, but every proposed command still required a path and resource audit before execution.

the hold also rejected deliberate crash reproduction, Driver Verifier, rebooting, driver changes, or elevation as routine implementation steps. those actions could change the machine, destroy evidence, or create another interruption before the existing dumps were understood.

## what could continue

the hold was deliberately narrower than the repository. after inspecting their command paths and resource ownership, work could continue on:

- source review and edits,
- incremental compilation,
- static checks,
- pure CPU tests,
- isolated serialization and settings tests,
- builds that did not launch the held path,
- documentation and recovery evidence.

this distinction matters. `CRASH-HOLD` did not mean “nothing can be done.” it meant “do not claim or execute the affected integration layer until it is safe.”

later Donut-removal work continued in those independent areas. every system-services, installer, runtime-health, and exact supported-launch result remained pending. a source build or pure test pass could not convert a held gate into a pass.

## why the larger goal was blocked

the larger goal required a usable, exact production package proven through its supported launcher and installer path. that acceptance chain included the same system-service behavior that could no longer be executed safely.

this created a precise evidence gap:

1. source and independent build work could advance.
2. pure tests could prove narrow contracts.
3. the supported install, repair, rollback, shortcut, registry, and launch transaction could not be rerun.
4. without that transaction, exact package and release acceptance remained incomplete.

the goal was therefore blocked at the integration and release layer, not because all engineering had stopped and not because a root cause had been found. changing tasks, branches, models, or source goals did not make that evidence gap disappear.

the preserved dirty checkout and index were also part of the incident evidence. resetting or cleaning them would have damaged the ability to connect source, interrupted fixtures, journals, and test receipts.

## what would have lifted the hold

a disciplined lift sequence would have been:

1. preserve the interrupted fixtures, journals, events, commands, executable hash, checkout identity, and index identity.
2. copy the three existing dumps without modifying the originals.
3. perform offline dump analysis and identify the exception context, stack, process, and loaded modules.
4. connect that evidence to the narrowest source, operating-system, driver, or prior-corruption hypothesis.
5. repair or isolate the concrete failure mechanism.
6. redesign the integration harness so registry, shortcut, installer, and cleanup phases have separate cases and durable before-and-after journals.
7. review a smallest-risk rerun plan with explicit authorization, monitoring, rollback, and a stop condition.
8. execute only the smallest affected path.
9. expand to the exact package and supported-launch gates only after the narrow case is stable.

if the dump evidence pointed outside the project, lifting the hold would still require a reviewed containment or environment change. uncertainty alone was not evidence that the risk had passed.

## general lesson

real operating-system integration tests are valuable because mocks cannot prove shell, registry, shortcut, installer, rollback, and process behavior. they also deserve stricter containment than pure tests.

a broad case that packages, installs, mutates shell state, asserts policy, and cleans up has poor causal resolution. when it fails catastrophically, the last printed assertion can be misleading and cleanup can destroy the best evidence. system integration should use small phase boundaries, durable journals, unique fixture namespaces, idempotent recovery, and an external supervisor that records the exact phase before each mutation.

a safety hold should name the prohibited path, the allowed independent work, the evidence needed to lift it, and the gates that remain pending. it should never become a vague claim that the project is broken or a quiet excuse to mark unavailable validation as passed.
