# start and resume

use [prompt.md](prompt.md) as the clean agent handoff. it follows the Spec Kit constitution, specification, plan, tasks, and implementation sequence. the prompt contains execution instructions, while the audit index and research retain review history. these documents do not install Spec Kit CLI commands.

## activation and bounded reading

read current AGENTS.md, the constitution, prompt, and the active work card. T001 is the initial task. compare live main with the pinned baseline, inspect relevant instruction/contract changes, then pin the chosen revision. preserve unrelated work and follow actual authority for branches, commits, and publication.

M0-M3 produced the local compiler draft in E-035. the active request continues through the complete Vulkan AGFX and ShaderToHuman ports. select T024-T029 and their source mappings first, with T018 integration and separate T023 Linux evidence. use the current task ledger and do not stop at the old compiler milestone. native Windows Vulkan and actual NGAPI on Windows are required primary evidence, with Linux portability separate.

create ignored `work/theta/STATE.md` and `NOTES.md` if absent. tasks owns completion, the card owns current position, and detailed notes own experiments. [execution](../../execution.md) preserves durable checkpoints and [lessons](../../lessons.md) preserves reusable findings. [UNSAFE.md](../../../UNSAFE.md) owns the central safety audit. do not make a competing stage ledger.

load only the selected task, relevant story/requirements/contracts, source owners and direct callers, nearest tests, and relevant notes. enumerate the complete source inventory when needed for a parity task. avoid recursively loading historical plans or every postmortem.

## current work card

```text
status: prepared | working | blocked | checkpoint
milestone, task and requirement IDs:
source baseline and dependency/tool pins:
owned paths, build directory, process/GPU session:
current hypothesis and decisive check:
changed files and preserved diff:
last observed result and evidence path:
unsafe records affected and review status:
execution entry and lesson references:
unrun checks and missing capabilities:
next action:
consecutive materially identical failures without new evidence: 0
experiments on current design decision: 0
```

before a significant experiment, record the hypothesis, exact command or command-file path, expected discriminator, and evidence destination. afterward record exit status, observed result, source/configuration identity, limits, and next action. keep facts and reproducible conclusions rather than private reasoning or a transcript of routine calls.

update the card before changing tasks or yielding. append meaningful conclusions and failed approaches to execution. promote findings useful beyond the immediate task to lessons, with their evidence classification and applicability limits. record “no new reusable lesson” when appropriate instead of inventing one.

## recovery

after **two materially identical failures without new evidence**, stop repeating that approach. checkpoint after **six experiments on one architectural decision**, even when individual experiments produced information. preserve the smallest reproducer, hypotheses tried, source/diff/artifact identity, affected requirements, and what would justify retrying.

reduce the case, inspect a lower layer, add a diagnostic, use a simpler contract-preserving route, or move to independent eligible work. if no useful authorized work remains, give the precise blocker and needed resource or decision. do not reset counters through renamed tasks, new agents, branches, or caches.

missing hardware stays missing evidence. conventional descriptors cannot become native heaps. compiler-only success cannot become runtime proof. a local draft cannot become a merged contribution. maintain the requested priority without silently waiving a required primary capability.

before restarting an interrupted command, inspect its process, exit record, and artifacts. keep one owner per build/GPU session. do not discard upstream or repository checks to hide a failure.

## checkpoint instructions

verify the selected task against its requirement and oracle. update completion only where evidence closes it. reconcile every changed unsafe site and its central record. append exact change, evidence layer, unrun checks, limitations, lessons, and next action to execution. refresh the work card and choose the next eligible task from the active port track.

at M3 deliver the generic diff, local PR text, actual Windows NGAPI reproduction, safety/ABI decisions, promised-feature evidence, and full CI status. pending remote jobs must stay visible. supporting parity and later APIs retain their own status.

## useful read-only baseline commands

```powershell
git remote get-url origin
git ls-remote origin refs/heads/main
git rev-parse HEAD
git status --short --branch
gh api repos/brockliddicoat/uvsr/commits/main --jq .sha
```

planned runner verbs become commands only after implementation. do not run guessed executables or report nonexistent checks as passed.
