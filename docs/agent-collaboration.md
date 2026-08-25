# Agent Collaboration

Use this procedure for concurrent writers, cross-worktree integration, or a
shared build/GPU task. Root `AGENTS.md` is sufficient for ordinary single-writer
and read-only work.

## Ownership

One coordinator owns decomposition, contracts, integration order, shared state,
user communication, and the completion claim. Assign the smallest useful team.
Each writable file or resource has one owner; other agents may inspect it but
must not edit it. One designated operator owns each build tree, executable,
renderer window, and GPU measurement session.

Parallelize independent discovery, review, or disjoint implementations. Keep
schema, ABI, shader-layout, migration, shared-resource, and producer/consumer
decisions serial until their contract is stable. A free slot is not a reason to
create work.

## Preflight and Assignment

The coordinator records the exact base, relevant status, loaded instructions,
owned and excluded paths, dependencies, protected behavior, acceptance evidence,
verification, and authorized Git/external actions. Inspect live remote state only
when recency or publication matters.

Give each worker one bounded objective with its base, read/write/no-touch scope,
settled interfaces, done conditions, required checks, and concise return format.
Do not make the user choose agents, branches, worktrees, files, or commands.

Use an execution plan only when complex multiwriter dependencies cannot be held
safely in the task. One coordinator owns that file. Routine work needs none.

## Shared Work

Workers edit only assigned paths and do not stage, commit, switch, merge, stash,
clean, update submodules, run repository-wide generators, or alter peer-owned
processes unless explicitly assigned. Re-read a file immediately before
patching and treat every unexpected change as peer- or user-owned. Stop that
write scope and notify the coordinator rather than discarding or reformatting it.

Send an update when a discovery changes another slice's input, interface,
verification, order, or risk. Report exact evidence and requested action, not
status chatter. After two identical no-progress failures, preserve evidence and
replan instead of repeating the same attempt.

Use isolated worktrees and build trees for independent writers when practical.
Isolation does not make incompatible designs composable. Serialize builds that
share a tree and all renderer, GPU, profiler, and package operations. A timed-out
build may leave children alive; inspect before retrying.

## Integration and Evidence

Only the coordinator integrates shared hotspots or changes repository history.
Freeze affected writers, inspect each scoped diff, integrate in dependency order,
run focused checks after meaningful slices, then inspect the combined diff and
run the full applicable gate once. Resolve conflicts by preserving both valid
intents, never by wholesale side selection.

Visual acceptance, passing tests, compilation, launch, and performance are
different evidence. Bind claims to exact source, configuration, settings,
scene/camera, package, and artifact. Re-establish acceptance after any repair or
rebuild that changes the observed code or settings.

## Handoff

A worker returns under roughly 200 words: status, exact base/final identity,
changed paths/contracts, result, checks and outcomes, unrun checks, risks,
dependencies, next action, and explicit ownership release. Store large logs or
captures as referenced artifacts, not transcript.

The coordinator declares completion only after ownership is released, the
integrated diff is reviewed, acceptance criteria map to evidence, applicable
developer and production checks pass, and risks/external actions are reported
truthfully. Move durable decisions and recovery facts into maintained docs or
the existing postmortem archive, then delete transient plans.
