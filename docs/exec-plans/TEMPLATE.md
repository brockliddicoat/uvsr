# {Task ID}: {Descriptive Title}

The coordinator fills this plan from ordinary user conversation and repository
inspection. Never ask the user to complete this template or provide mechanical
Git, file-ownership, agent-assignment, or test-command details that agents can
discover themselves.

## Status

- State: planned | active | blocked | integration | complete | abandoned
- Coordinator:
- Project/integration branch and worktree:
- Base commit:
- Started:
- Last updated:
- Planned archive: `docs/exec-plans/completed/<same-name>.md` or
  `docs/exec-plans/abandoned/<same-name>.md`

## Goal and Done Condition

Goal:

Done when:

- [ ]
- [ ]

## Scope

In scope:

-

Non-goals:

-

Affected subsystems and paths:

-

Shared hotspots reserved for the coordinator:

-

## Baseline

- Canonical repository/remote:
- Local versus remote state:
- Verified source commit/build:
- GPU, scene, camera, resolution, and settings preset when relevant:
- Known pre-existing failures:

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| | | | |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

-

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | |

## Assignment Contracts

Repeat this block for each writer. Read-only explorers may omit write/build
fields, but still need a bounded objective, deliverable, and stop condition.

### {Task ID}: {Objective}

- Owner/thread:
- Branch/worktree:
- Base commit/state:
- Read scope:
- Write scope:
- No-touch scope:
- Build directory and runtime/GPU/resource lease:
- Dependencies already integrated:
- Interface/invariant contract:
- Deliverable:
- Done when:
- Required verification:
- Allowed Git and external actions:
- Stop and report if:
- Handoff revision/artifact:
- Handoff acknowledged by/on:

## Integration Order

1.
2.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| | | | |

For performance work, record:

- baseline and candidate commits;
- GPU, scene, fixed camera, resolution, and settings preset;
- warmup and sample window/count;
- total frame time plus relevant pass costs;
- correctness/image-quality guardrails;
- before/after captures and raw measurement artifact.

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| | | | |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| | | | | | |

## Risks and Escalation Triggers

-

Stop and ask the user if:

-

## Completion

- Final integrated commit:
- Verification summary:
- Independent review:
- Coming Soon/documentation update:
- Pushed/PR/merged, or intentionally local:
- Remaining experiments or follow-ups:
- Active ownership released:
- Archived to completed/abandoned path:
