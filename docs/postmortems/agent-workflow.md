# agent workflow

## test competence before assigning the whole goal

**observed.** UVSR agents were often asked to finish broad goals such as replacing a framework, repairing a renderer, proving a visual feature, or producing a release. an agent could make progress on source changes while lacking the environment, context, graphics judgment, or debugging ability needed for the final acceptance step. because the goal stayed open, later turns repeated edits and checks without resolving the missing capability.

**inferred.** persistence became a loop when the system treated effort as evidence of progress. a large goal concealed several different competencies, including source archaeology, API design, shader work, GPU debugging, visual evaluation, build repair, packaging, and release control.

**recommended.** start with a competence gate. before implementation, require the owner to identify:

- the exact behavior and smallest failing case,
- the relevant source and dependency identity,
- the contract being changed and all known consumers,
- the tool, device, and access needed to observe success,
- the decisive proof and what it cannot prove,
- the first two likely failure modes,
- the point at which the work should be reduced, handed off, or stopped.

if an agent cannot state these, assign discovery rather than implementation. a bounded discovery result can still be valuable. pretending it is an implementation task creates churn.

## make goals finite

replace goals such as “finish the renderer” with a sequence of outcomes that each fit one review:

1. reproduce one failure on a pinned source and executable.
2. locate the smallest owner and consumer set.
3. write or expose an oracle that detects the failure.
4. implement one behavior with no optional variants.
5. pass the focused oracle and inspect the diff.
6. run the applicable integration layer once.
7. decide to retain, revise, or retire the experiment.

write the done condition before editing. list exclusions when a nearby goal could be mistaken for part of the task. completion should be a falsifiable state, not a percentage or an impression that many files changed.

## use a loop detector

an agent is looping when it repeats the same action without gaining evidence. examples include rebuilding an unchanged failing target, reopening the same screenshot, modifying thresholds without a model of the failure, or recreating a broad plan after each context loss.

stop after two materially identical failures with no new evidence. preserve the exact command, output, source identity, and attempted explanation. then choose one action:

- reduce to a smaller reproducer,
- inspect a lower layer,
- add the missing diagnostic,
- ask a better-qualified reviewer a bounded question,
- change the hypothesis,
- record the external blocker and stop.

changing syntax, branch names, or test order does not count as new evidence.

## keep one coordinator and explicit ownership

**observed.** overlapping agents, worktrees, build trees, renderer windows, and GPU sessions made it easy to validate one source while editing another or to overwrite useful evidence. coupled work also produced incompatible partial designs.

**recommended.** one coordinator owns the design, writable files, integration order, acceptance mapping, and final claim. delegate only independent discovery or review after interfaces are stable. every file, build tree, process, renderer window, GPU session, and external action has one owner.

an assignment should include the base revision, read scope, excluded paths, question, evidence format, and done condition. a worker returns findings and uncertainty. the coordinator checks decisive evidence and performs coupled edits and integration.

## preserve context as evidence

LLMs lose reliability when the important state exists only in conversation. at each coherent checkpoint, record:

- source branch, commit, dirty state, and relevant diff,
- dependency, toolchain, configuration, and generated-input identities,
- build directory and executable SHA-256,
- scene, camera, settings, adapter, driver, and package identity when applicable,
- commands, outcomes, first failure, and unrun checks,
- the current hypothesis, rejected explanations, and next decisive action.

commit coherent verified checkpoints while they remain easy to review and recover. do not create commits merely to increase count, and do not let a long debugging interval accumulate unrelated changes. use one purpose-named task branch and a direct pull request to `main`. merge-only, per-agent, and speculative branches make provenance harder rather than safer.

## label the strength of every claim

use distinct labels for source inspection, compilation, unit behavior, GPU execution, image agreement, temporal stability, performance, package validation, and release publication. an implementation can be complete at one layer and unproven at the next.

record observed values as observed. label estimates and inferred explanations. unsupported hardware, skipped cases, zero executed cases, stale artifacts, and a different checkout are not passes. when evidence cannot be produced, report the precise missing condition instead of expanding a weaker check into a completion claim.

## design handoffs for the next reader

a useful handoff is short enough to scan and complete enough to reproduce. it names the outcome, source and artifact identity, changed contracts, decisive evidence, remaining uncertainty, and next action. large logs and images belong in referenced artifacts. durable rules belong in maintained documentation, while temporary plans and raw measurements belong in ignored work files.

failure records should explain what was tried, why it looked reasonable, what contradicted it, and what future evidence could justify another attempt. that lets a new agent learn without inheriting the old conclusion as dogma.
