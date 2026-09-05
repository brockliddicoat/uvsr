# agent collaboration

use this procedure for concurrent writers, integration across worktrees, or a
shared build, renderer, package, or GPU task. `AGENTS.md` is sufficient for one
writer and read only work.

## ownership

one coordinator owns decomposition, interfaces, integration order, shared
state, user communication, and the completion claim. assign the smallest useful
team. each writable file, resource, build tree, executable, renderer window,
and GPU session has one owner.

use workers for independent discovery or second review. workers do not edit or delegate; the coordinator owns implementation and integration. keep
schema, ABI, shader layout, migration, shared resource, and producer or consumer
decisions serial until their contract is stable. more available workers do not
make coupled work independent.

## assignment

before work starts, record the exact base, relevant status, instructions, owned
and excluded paths, dependencies, protected behavior, acceptance evidence,
verification, and authorized Git or external actions. inspect a live remote
only when recency or publication matters.

give each worker one bounded objective with settled interfaces and read
scope, no touch paths, done conditions, required checks, and return format. the
coordinator chooses the work split. use an execution plan only when a complex
dependency order cannot be held safely in the task. one coordinator owns it.

## shared work

workers return evidence without edits. any separately authorized writer edits only assigned paths. they do not stage, commit, switch, merge,
stash, clean, update submodules, run repository wide generators, or alter a peer
process unless assigned that exact action. recheck a file immediately before
editing. treat unexpected movement as peer or user work, stop that write scope,
and notify the coordinator. never discard or reformat it.

send evidence when a discovery changes another slice's interface, input,
verification, order, or risk. state the path, fact, consumer, and requested
action. after two identical failures with no new evidence, preserve the result
and replan.

prefer isolated worktrees and build trees for independent writers. isolation
does not make incompatible designs composable. serialize work that shares a
build tree, renderer, GPU, profiler, package stage, or external account. a timed
out process may leave children alive, so inspect ownership before retrying.

## integration and proof

only the coordinator integrates shared hotspots or changes history. freeze the
affected writers, inspect every scoped diff, integrate in dependency order, run
focused checks after meaningful slices, then inspect the combined diff. run the
full applicable developer and exact package gates once at the checkpoint. the
applicable validation contract owns evidence requirements.

resolve conflicts by preserving compatible intent, not by choosing one side
wholesale. reestablish acceptance after any repair or rebuild that changes the
observed source, binary, package, or settings.

visual acceptance, compilation, semantic tests, fault injection, runtime
output, performance, and package validation are different evidence. bind every
claim to exact source, configuration, settings identity, executable SHA-256,
scene, camera, and package when applicable.

## handoff

a worker returns concise status, base and final identity, changed paths and
contracts, checks and outcomes, unrun checks, risks, dependencies, next action,
and explicit ownership release. store large logs and captures as referenced
artifacts.

the coordinator claims completion only after ownership is released, the
integrated diff is reviewed, acceptance criteria map to evidence, required
gates pass, and risks and external actions are reported accurately. move durable
facts into their canonical maintained document and delete transient plans.
