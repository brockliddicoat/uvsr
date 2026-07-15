# Talk to UVSR Agents Naturally

You do not need prompt templates. Describe the result as if you were texting a
capable teammate. The coordinator should discover the repository, exact build,
branch, worktree, files, tests, and agent assignments.

The phrases below document defaults, not required commands. Equivalent ordinary
wording—such as “the good GI build,” “the version you just showed me,” or “the
newest known-good main build”—must resolve the same way. Agents should not
correct your wording or ask you to restate it using this guide.

## Starting Work

These are complete requests:

- “Implement spherical harmonics in the latest verified UVSR build.”
- “Add spherical harmonics to the verified build closest to our current GI
  work. Keep it local and run it when it passes.”
- “Try this on the active visibility experiment, but don’t change AgX or the UI.”
- “That triangle fan is broken in the build you just launched. Fix it and show
  me again.”
- “Make this faster without changing the image. Use the default Intel Sponza scene.”

In these examples, **latest verified** means the newest Canonical verified
checkpoint on live canonical `main` history. **Nearest verified** means the
verified ancestor requiring the fewest ancestry steps from the active lineage
head or named base. A named result must itself be verified; otherwise the agent
walks to its nearest verified ancestor. It must not choose by file time, folder
name, vague subsystem similarity, or a divergent branch. “In,” “on,” and “into”
a build should all be understood naturally. A noncanonical nearest checkpoint
must have its required technical evidence and product acceptance recorded.

If no trustworthy verified build exists yet, the agent should tell you briefly,
verify the intended clean lineage base first, and then continue. It falls back
to canonical only when your request implies no feature lineage. It should not
ask you to find the build or pretend an untested checkout is verified.

If you omit publication language, the work stays local.

## Short Follow-Ups

| What you say | What the agent should do |
| --- | --- |
| “Run it.” | Launch the current candidate; if none exists, use the active task’s last verified build. |
| “That looks good.” | Record visual acceptance for exactly what was just shown; do not publish or skip tests. |
| “That’s broken.” / “That’s not right.” | Reject the last result, keep the verified baseline unchanged, and diagnose/fix the current scope. |
| “Verify it.” | Run the required checks. Fix task-introduced failures only in an already authorized implementation task; a verification-only request remains read-only. |
| “Save it.” | Make a focused local commit; do not push. |
| “Push it.” / “Put it on GitHub.” | Run required local checks, commit the current task safely, and push its feature branch; do not create a PR or merge. |
| “Update GitHub.” | Update the current branch and its existing PR; if there is no PR, push the branch only. |
| “Open a PR.” | Run available checks, push, and create or update a draft PR while listing remaining checks; do not merge. |
| “Merge it.” | Treat the unambiguous current candidate as approved, final-check it, and merge that exact work through the normal PR path. |
| “Keep going.” | Finish the current local scope; do not infer publication authority. |
| “Revert that.” | Revert only the last task-owned change, preserving other work. |
| “Abandon it.” | Preserve a recoverable checkpoint and stop; do not delete the branch automatically. |

The phrases compose:

- “That looks good. Verify it.”
- “That looks good. Push it once it passes.”
- “Put the version I just approved on GitHub as a draft PR.”
- “Merge it into main once the final checks pass.”
- “Merge the HZB fix, but leave the bilateral experiment alone.”

“Ship it” and “publish it” can mean different things. If the preceding
conversation does not already make push, PR, merge, or release clear, the agent
should ask one short question.

Visual acceptance belongs to the exact artifact and settings you saw. If final
checks repair or rebuild it with changed code/settings, the agent must show and
re-establish required acceptance before merging; “merge it” never means silently
merge an unseen replacement.

## What You Should Not Have to Provide

The agent should not ask you to choose or paste:

- a commit SHA or build path it can discover;
- a worktree, branch name, build directory, or CMake command;
- a list of files or subagent assignments;
- standard UVSR tests, documentation updates, or integration bookkeeping;
- a task-plan or handoff template.

It should ask only when a missing answer would materially change product
behavior, rendering quality versus performance, licensed assets, destruction of
uncertain work, priority between incompatible accepted efforts, or an external
destination/authority.
