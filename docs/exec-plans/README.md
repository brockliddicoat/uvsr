# Execution Plan Lifecycle

Use an execution plan for long-running, cross-subsystem, or concurrent writing.
Small fixes, read-only investigations, and short private experiments normally do
not need one.

Execution plans are internal coordination artifacts. The coordinator creates
and completes them from casual user requests plus repository inspection; they
are not forms the user must copy, paste, or maintain.

1. Copy `TEMPLATE.md` to `active/<unique-project-slug>.md` before assigning
   writers. The coordinator is its only editor.
2. Record the exact base, branch/worktree, task dependencies, path and resource
   ownership, interface contracts, acceptance evidence, and handoffs. Do not use
   one global active-work ledger.
3. Treat the plan as an auditable record, not a distributed lock. Agents in
   other clones cannot see an unpushed plan; state that visibility limit rather
   than assuming coordination.
4. At closeout, the integrator moves the same file to
   `completed/<unique-project-slug>.md` or
   `abandoned/<unique-project-slug>.md` in the final documentation change. Keep
   the evidence and reason; do not silently delete the record.
5. To resume archived work, create a new active plan with a fresh base and link
   the archived plan. Do not change historical completion evidence in place.

Create the `active/`, `completed/`, and `abandoned/` directories on first use.
Because each project has its own file, unrelated coordinators do not contend on
an index file.
