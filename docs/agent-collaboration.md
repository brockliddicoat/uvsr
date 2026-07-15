# Agent Collaboration Protocol

Use this full protocol for concurrent writers, cross-worktree integration, or a
high-risk multi-agent task. Simple read-only fan-out may use the compact
assignment and handoff rules in the root `AGENTS.md`. The purpose is to gain
parallel speed without losing correctness, provenance, or another agent's work.

The user is not expected to fill out the tables or message templates in this
document. The coordinator derives them from ordinary conversation and repository
inspection, maintains them internally, and asks only for a material product,
priority, destructive, or publication decision that cannot be inferred safely.

## Operating Model

One agent is the coordinator. The coordinator may delegate bounded work, but it
retains ownership of the task graph, assignment boundaries, shared decisions,
integration state, user communication, and the overall completion claim.

Workers own only the task and paths assigned to them. A worker does not become
an integrator because it finishes first, discovers related cleanup, or can see
another worker's changes.

Use this default team shape:

1. One coordinator/integrator.
2. Zero or more read-only explorers or reviewers.
3. At most one writer for each disjoint file/resource set.
4. One designated build/runtime operator when build trees, the GPU, or a UVSR
   window are shared resources.

Only add another agent when there is another independent, ready work item. A
free concurrency slot is not a reason to fill it.

## Decide Whether Work Is Parallelizable

Good parallel work includes:

- repository and dependency exploration;
- independent source or standards research;
- test discovery and test-gap analysis;
- reviews focused on different risks;
- log, crash, trace, or performance-data analysis;
- implementation in disjoint modules with an already agreed interface.

Keep work serial when it involves:

- two edits to the same file;
- an interface, schema, ABI, resource layout, or shader contract that is not yet
  decided;
- a producer and consumer where the consumer requires the producer's result;
- repository-wide formatting, generation, dependency changes, or migrations;
- one build directory, one executable, one GPU benchmark, or one interactive
  renderer window;
- uncertain deletion or a change whose safest design depends on investigation.

If decomposition cannot produce disjoint write sets, use one writer and parallel
read-only reviewers. Group strongly coupled files under the same owner.

## Coordinator Preflight

Before implementation, the coordinator records:

- user goal, user-stated constraints, and coordinator-derived acceptance
  criteria;
- current commit, branch/detached state, and `git status --short --branch`;
- `git worktree list --porcelain` and relevant unmerged branches;
- live remote state when the task depends on "latest," integration, publishing,
  or release status;
- applicable `AGENTS.md`/`AGENTS.override.md` files;
- active execution plans, open pull requests, visible Codex tasks, and the full
  README Coming Soon section;
- affected subsystems and shared hotspots;
- dependencies and the required integration order;
- verification commands and any required fixed visual/performance baseline.

For a complex or concurrent project, copy `docs/exec-plans/TEMPLATE.md` to a
unique file under `docs/exec-plans/active/`. Use one file per project so agents
do not contend on a single ledger. Only the project's coordinator edits it.
Follow `docs/exec-plans/README.md` for the active, completed, and abandoned
lifecycle.

The coordinator then creates an assignment table with one row per worker:

| Field | Required content |
| --- | --- |
| Task | Stable task ID and one concrete objective |
| Owner | Agent/thread identity |
| Base | Exact commit or state version |
| Read scope | Relevant paths and sources |
| Write scope | Exact files/directories/resources owned |
| No-touch scope | Peer, user, integration, and generated state |
| Dependencies | Task IDs and integrated revisions required first |
| Contract | Interfaces, signatures, formats, and invariants |
| Deliverable | Findings, patch, commit, tests, or artifact |
| Done when | Behavioral acceptance and required checks |
| Stop when | Collision, stale base, ambiguity, authority, or safety triggers |

Do not assign a dependent implementation before its contract and prerequisite
revision are stable. Do not assign the same writable path to two live workers.

## Assignment Message

This is an internal agent-to-agent message, not a user prompt form. Every worker
receives enough context to act without guessing:

```text
Task ID:
Objective:
Why it matters:
Base commit/state:
Read scope:
Write scope:
Do not touch:
Dependencies already integrated:
Interface/invariant contract:
Acceptance criteria:
Required verification:
Allowed Git/external actions:
Return format:
Stop and report if:
```

A worker acknowledges any externally consumed interface or resource contract
before implementing it. An ambiguous public contract is a blocker, not an
invitation to invent a competing design.

## Shared Checkout Rules

Subagents can share a filesystem and Git index. Disjoint file ownership does not
make simultaneous index/history operations safe.

- Only the coordinator stages, commits, switches branches, merges, rebases,
  stashes, resets, cleans, updates submodules, or runs repository-wide formatters
  and generators.
- A worker edits only assigned paths and does not stage them.
- The coordinator stages, commits, builds, or tests only after every relevant
  writer has handed off and released its paths, or after all affected writers
  acknowledge an explicit write freeze. No writer patches during that freeze.
- Stage exact paths, inspect `git diff --cached`, and confirm that every staged
  file belongs to the intended handoff before committing. Never stage a file
  while another agent may be in the middle of an edit.
- Re-read each file immediately before patching it.
- Inspect `git diff -- <owned-path>` before and after editing.
- Treat every unexpected modification as potentially user- or peer-owned.
- Never repair a collision by discarding, checking out, or reformatting the
  unfamiliar change.
- A worker releases ownership in its handoff; silence does not release it.

If an unexpected diff appears inside the worker's write set, stop writing that
scope. Report the path, expected base, observed state, and whether an atomic edit
is currently in progress. The coordinator chooses one of: transfer ownership,
serialize the changes, or move a writer to an isolated worktree. Ask the user
only when the resolution selects between materially incompatible product
outcomes or a real product-priority decision.

## Isolated Writer Rules

Prefer one branch and one worktree per independent writer. Isolation separates
files, HEAD, and the index; it does not make designs semantically compatible.

- Record the worktree path, branch or detached state, and starting commit.
- Never check out one branch in two worktrees.
- In Codex desktop, use Handoff when moving a worktree task into the local
  checkout. Where Handoff is unavailable, return an exact base/head plus a
  focused commit or patch artifact for the integrator.
- Remember that a worktree is a snapshot. Later changes in another worktree do
  not appear automatically.
- Give each writer a separate build tree and experiment label.
- Supply required ignored local files through the approved setup process or
  `.worktreeinclude`; never assume licensed assets or prior build products exist.
- A worker may make focused commits only when assigned. It may not push, merge,
  rebase, or rewrite history unless the user and coordinator explicitly approve
  the exact operation.
- Branch/tag deletion, worktree removal/move/prune, Git garbage collection,
  shared-ref mutation, and submodule add/update/deinit are repository-wide
  metadata operations. Only the repository-level coordinator/integrator may run
  them, and only when explicitly in scope.
- Do not use one permanent worktree as isolation for multiple concurrent writers.

## During Execution

Workers send a concise update when a discovery changes another task's inputs or
the integration plan. Notify the coordinator immediately for:

- required write scope outside the assignment;
- a changed API, ABI, shader binding, G-buffer layout, serialized setting, build
  contract, or asset/package format;
- an overlap with a peer or active project;
- a stale base or dependency revision;
- an unexpected diff or process;
- a destructive step, new external authority, or uncertain deletion;
- a failed prerequisite that invalidates downstream work;
- two consecutive attempts that fail for the same reason.

Status chatter is not a substitute for evidence. Prefer messages that state the
decision or blocker, affected task/path, observed evidence, and requested action.
The coordinator steers or cancels duplicated work rather than allowing multiple
agents to keep exploring the same path.

## Build, Test, and Renderer Serialization

Build trees, packaged shaders/assets, compiler processes, executables, GPU
profilers, and application windows are writable resources.

- Never run concurrent configure/build/test commands against the same build
  directory.
- A timed-out command may leave compiler children alive. Inspect process state
  before retrying; do not start a second build that will contend on object files.
- Close or restart only the executable launched for the assigned experiment.
- Do not rebuild over a running locked executable; coordinate a controlled
  restart with the runtime owner.
- Only one agent drives a UVSR window. Stop automation if the user is interacting
  with it.
- Serialize GPU benchmarks. Use the same commit baseline, hardware, scene,
  camera, resolution, settings, warmup, sample count/window, and measurement
  method.
- Record total frame time, the feature's own GPU cost, visible/culled work, and
  correctness evidence. Reject a speed claim based only on a faster sub-pass.

## Worker Handoff

Return a distilled record, not a transcript:

```text
Task ID:
Status: done | partial | blocked
Owner:
Base revision:
Final revision/commit or artifact:
Files/resources changed:
Public contracts changed:
Implementation/findings summary:
Decisions and assumptions:
Verification commands and outcomes:
Checks not run and why:
Known risks or pre-existing failures:
Dependencies affected or invalidated:
Unresolved questions:
Recommended integration/next action:
Ownership released:
```

`done` requires the assigned acceptance criteria and checks. A build or test that
was not run must be labeled unrun. Large logs, screenshots, traces, and reports
should be saved as artifacts and referenced from the handoff.

## Integration Protocol

Only the designated integrator changes the integration branch or shared
hotspots.

Across independently launched projects, acquire one repository-level integration
lease before changing the target branch or publishing. The lease can be a
configured merge queue or the existing target/PR owner; feature branches being
disjoint does not make two simultaneous integrations safe. If no owner exists,
the current canonical coordinator claims and advertises the lease and serializes
other integrations. Ask the user only when ordering would choose between
materially incompatible accepted outcomes or external authority is missing.
Frame that question in product terms; never ask the user to select an agent,
worktree, branch, file lease, or merge order.

1. Confirm every input's base, artifact/commit, scope, and handoff status.
2. Fetch the live target when remote state matters, record its exact commit, and
   reclassify each input as current, behind, ahead, or diverged.
3. Inspect each diff for ownership violations, unrelated edits, generated files,
   debug code, and undocumented contract changes.
4. Integrate one change at a time in dependency order.
5. Resolve conflicts by understanding and preserving both valid intents. Never
   take `ours` or `theirs` wholesale merely to make Git clean.
6. Run targeted checks after each meaningful integration so a failure is
   attributable to a small set of changes.
7. Reinspect the composed diff and run the full task-relevant suite on the final
   state.
8. Use an independent reviewer for high-risk rendering, shader, lifetime,
   concurrency, deletion, packaging, or architectural work.
9. Map each acceptance criterion to evidence, including visual and performance
   evidence where claimed.
10. Reconcile README Coming Soon and durable design/user documentation once.
11. After each accepted change, refresh the target identity and rerun the
    combined checks required by the new composition before accepting the next
    publication.
12. Report branch, commits, verification, artifacts, remaining experiments, and
    exactly what stayed local or was published.

Visual/product acceptance is tied to the exact artifact and observed settings.
If integration or a verification repair changes either, invalidate that
acceptance and re-establish it when required before merge. Never substitute a
repaired-but-unseen candidate under an earlier approval.

A clean merge, a passing test suite, and a plausible screenshot are different
signals. None alone proves semantic correctness. Check the process and the
claimed behavior, especially when tests could pass without exercising the new
path.

## Roadmap and Execution-Plan Lifecycle

README Coming Soon is a user-facing roadmap, not an ownership registry. Only a
coordinator or integrator updates it, after reconciling the named branch,
worktree, execution plan, pull request, and remote state. Preserve other
verified entries and do not infer that an unlisted private experiment is idle.

At closeout, move the coordinator's plan from `docs/exec-plans/active/` to
`docs/exec-plans/completed/` or `docs/exec-plans/abandoned/` and retain its
evidence and reason. Do not maintain a central plan index that every project
must edit. An archived plan is historical; resumed work starts a new active
plan with a fresh base and a link to the prior record.

## Stale-State and Retry Protocol

Before starting, resuming, or integrating, verify the assigned base, ownership,
dependency status, and unexpected repository changes.

If state is stale:

1. Stop edits that depend on the stale assumption.
2. Identify the exact intervening diff and affected contracts.
3. Mark only the invalidated task nodes and their dependents for rework.
4. Preserve completed independent results.
5. Rebase, merge, or reassign only through the coordinator. Ask the user only
   for a material product/priority decision or missing external authority.

After two consecutive no-progress cycles or identical failures, checkpoint the
state and replan. Do not rerun an entire multi-agent workflow because one node
failed when the unaffected artifacts remain valid.

If a worker disappears or cannot provide a handoff, the coordinator inventories
its last known branch/worktree, base and tip, dirty diff, artifacts, and owned
processes; preserves that state; and marks the assignment orphaned. Reassign
ownership only after confirming the prior worker is inactive and giving the new
owner an isolated or explicitly reconciled base. A timestamp expiring by itself
is not permission to overwrite work.

## Stopping and Escalation

Stop the affected writes and escalate to the coordinator when:

- ownership or task ordering cannot be determined;
- a required public interface has competing valid designs;
- progress needs a user preference, broader scope, external publication, or
  destructive/irreversible action;
- a peer or user is actively changing the same resource;
- a verification failure shows the underlying plan or premise is wrong;
- a safety, licensing, security, or data-handling constraint is unclear.

The coordinator resolves mechanical ownership, agent, branch, worktree, file
lease, and merge-order questions. Involve the user only when the unresolved
choice is a material product/priority decision, destructive authority, or an
external action/destination that conversation does not already authorize.

Time or token pressure is not success. Mark work `partial`, persist its artifacts,
release resources safely, and explain the next step. Use hard interruption only
for destructive behavior, safety, or runaway resource use; otherwise prefer a
graceful handoff.

## Completion Checklist

The coordinator may declare completion only when:

- all required workers returned `done`, or partial/blocked work is explicitly
  excluded with user-visible impact;
- dependencies were integrated in the approved order;
- the combined diff and repository status were reviewed;
- required targeted, integration, rendering, and performance checks passed;
- every acceptance criterion has evidence;
- open risks, unrun checks, and pre-existing failures are stated;
- build trees, application windows, branches, worktrees, and artifacts are in a
  known state;
- active ownership is released and duplicate agents are stopped;
- Coming Soon and durable documentation are reconciled;
- commits, merges, pushes, PRs, and local-only work are reported exactly.

## Improve the Protocol from Evidence

When a task suffers duplicated work, ownership collision, stale-state rework,
build contention, semantic merge failure, repeated retry, or a misleading pass,
record the failure pattern and promote the smallest useful prevention into one
of:

- a clearer assignment or handoff field;
- a focused repository document;
- a nested subsystem `AGENTS.md`;
- a script, linter, hook, test, schema, or CI rule;
- a reusable Codex skill or narrow custom agent.

Prefer mechanical enforcement for mechanical rules. Keep root guidance focused
on judgment, authority, and navigation.
