# Execution Plans

Use an execution plan only for complex multiwriter work whose dependencies,
contracts, or verification state cannot be held safely in the task. Routine,
read-only, and single-writer work needs no plan.

Copy [the template](TEMPLATE.md) into `active/` with a unique descriptive name.
One coordinator owns it. Record only current decisions, dependencies, evidence,
and blockers; do not paste chat, command logs, or machine incident narration.

At completion, move durable requirements, measurements, recovery commits, and
lessons into the maintained document or existing postmortem hierarchy, then
delete the active plan. Git history is the plan's recovery record. Do not build
completed or abandoned archives.
