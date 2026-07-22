# UVSR Agent Guide

Agent policy version: `2026-07-16.1`.

## Product and Scope

- Treat UVSR as a focused production renderer, not a general Donut sample.
- Follow near-YAGNI principles: keep only behavior the product currently needs.
  Avoid speculative features, compatibility layers, unused controls, dormant
  shaders, and abstractions with no active caller.
- The executable and engineering slug are lowercase `uvsr`; the displayed
  product name is uppercase `UVSR`.
- Donut is a pinned dependency. Do not edit files under `donut/`; implement UVSR
  behavior in first-party code or narrowly scoped build-time overrides.
- `Donut-Samples/` is local reference material, never UVSR source.
- DirectX 12 is the product backend. Keep Vulkan and DX11 disabled unless a task
  explicitly requires them.
- Do not commit generated `build/` content or `work/` artifacts.

## Work Like a Human Teammate

- `docs/casual-agent-language.md` gives human-facing examples of these defaults;
  it is a reference, not required syntax or a prompt template.
- The user may speak casually, use short follow-ups, and refer to the current
  work as "it," "this," or "that." Never require the user to fill out a task
  form, provide a commit SHA, choose a worktree or branch name, list files, name
  agents, or specify routine test commands when repository inspection and this
  guide can resolve them.
- The coordinator translates the conversation internally into the outcome,
  verified base, scope and non-goals, ownership, dependencies, acceptance
  evidence, and publication authority. Record those details in agent messages
  or an execution plan; do not make the user recite or approve the bookkeeping.
- Inspect first, state any material interpretation in one short update, and
  continue. Ask one concise question only when unresolved alternatives would
  materially change visible product behavior or defaults, quality versus
  performance, licensing or asset treatment, destruction of uncertain work,
  priority between incompatible user-accepted efforts, or the destination or
  authority for an external action.
- A casual request to implement, add, fix, remove, compare, or improve something
  authorizes the smallest complete local change plus required documentation,
  builds, tests, smoke checks, and repair of failures introduced by that change.
  It does not authorize pushing, opening a pull request, merging, releasing,
  deleting unrelated work, or rewriting published history.
- Keep casual requests scoped to their stated outcome. "Finish it," "clean it
  up," or "make it better" does not authorize unrelated roadmap work, uncertain
  deletion, UI redesign, or an unspoken rendering-quality tradeoff.
- Resolve pronouns to the object named in the current message, then the exact
  candidate most recently shown or launched for the active task, then the last
  recommendation awaiting approval, then the current task-owned diff. Associate
  a screenshot with a build only when task context, visible window identity, a
  manifest, or the user's wording supports that provenance; capture the observed
  scene/settings at acceptance time. Never bind an attachment to the last launch
  solely by recency. Ask only if multiple materially different referents remain.

## Conversational Build and Publication Language

- "Latest verified," "newest version," "newest good build," or an unqualified
  "latest" means the newest Canonical verified checkpoint on the live canonical
  target's first-parent history. Use an unfinished candidate only when the user
  or active context explicitly names that experiment. "Last working build"
  means the most recent verified checkpoint in the active task lineage.
- "Nearest verified" means the exact named checkpoint if it is verified;
  otherwise use the verified ancestor requiring the fewest ancestry steps from
  the active lineage head or declared base. If none exists, verify the intended
  lineage base. Fall back to canonical only when no lineage is implied. Never
  choose by file time, folder name, subsystem similarity, or a divergent branch.
  Record the resolved SHA and tell the user briefly; do not ask them to find it.
- For "nearest verified," a noncanonical checkpoint qualifies only when it is
  Technically verified and has Product acceptance when that task requires it.
- If no trustworthy verified manifest or checkpoint exists, say so briefly and
  establish one from the intended clean lineage base before building the
  requested feature. Use canonical only when no feature lineage is implied.
  Never invent a "verified" label to avoid that step.
- Track the active work item, exact base and candidate, last launched manifest
  and settings, automated verification, user visual acceptance, branch/PR state,
  and integration target so short follow-ups remain unambiguous.
- "Run it" or "launch it" launches the current task candidate. If none exists,
  launch the last verified build associated with the active task. Launching does
  not itself verify the build.
- "That looks good" or similar language records visual acceptance only for the
  exact build and settings most recently shown. It does not waive automated
  checks and does not authorize GitHub changes.
- "That is broken," "that is not right," or equivalent feedback rejects the
  exact result most recently shown, leaves the last verified baseline unchanged,
  and authorizes diagnosis and repair within the current scope. It does not
  authorize reverting unrelated work or publishing the candidate.
- "Verify it" runs the task-appropriate build, tests, smoke checks, visual
  comparisons, and performance checks. When an active implementation task
  already authorized edits, repair failures introduced by that work and rerun
  the checks. A verification-only request remains read-only: diagnose and report
  failures without changing code. Report the highest verification state proven
  and anything still missing.
- "Save it" means make a focused local commit of task-owned changes after the
  required local checks. It does not push.
- "Push it," "push it to GitHub," or "put it on GitHub" means commit task-owned
  changes if needed after required local checks and push the current feature
  branch to its established remote. Update an already established pull request
  if appropriate, but do not create a new pull request, merge, release, or push
  the canonical branch directly.
- "Open a PR" means run available checks, push the current feature branch, and
  create or update its draft pull request against the established target while
  listing remaining checks. "Update GitHub" updates the current branch and its
  already established PR; if none exists, push the feature branch only unless
  context clearly requested a new PR. Neither phrase authorizes merge.
- "Merge it" counts as acceptance of the unambiguous current candidate and
  authorizes final combined verification, push/PR prerequisites, and merge of
  that exact work into the established target. It does not authorize a release,
  unrelated branches, or a direct push to the canonical branch.
- "Do it" approves the immediately preceding clearly recommended action and its
  stated scope. "Keep going" or "finish it" continues the current scope to a
  locally verified candidate; neither phrase adds publication authority.
- "Revert that" reverts the last task-owned change or checkpoint while
  preserving unrelated work. "Abandon it" preserves a recoverable checkpoint,
  releases ownership, and marks the task abandoned; it does not delete the
  branch or artifacts.
- If "ship it," "publish it," or another casual phrase still leaves push, PR,
  merge, or release destination genuinely unclear, finish local verification
  and ask one short question. Do not re-ask when the current context already
  makes the action and target unambiguous.
- These phrases compose naturally. For example, "That looks good—verify it"
  records visual acceptance and runs remaining checks; "push it once it passes"
  verifies and pushes the feature branch; "that looks good—merge it" authorizes
  the verify/push/PR/merge chain for only the current accepted work item. Visual
  acceptance belongs to the exact artifact, scene, and observed settings. Any
  artifact- or settings-changing repair invalidates it; reverify and re-establish
  required product acceptance before merge rather than silently merging an
  unseen replacement.

## Multi-Agent Coordination

- Before concurrent writing, cross-worktree integration, or another high-risk
  multi-agent task, read and follow `docs/agent-collaboration.md`. Read-only
  fan-out may use the compact assignment and handoff rules in this file. Use
  `docs/exec-plans/TEMPLATE.md` for a long-running, cross-subsystem, or
  multi-writer task.
- One parent/coordinator owns decomposition, assignments, cross-agent decisions,
  integration, final verification, shared documentation, and the completion
  claim. Workers complete only their assigned scope.
- Use the smallest useful team. Prefer parallel exploration, source research,
  test discovery, log analysis, and independent review. Keep dependent steps
  serial and prefer one implementer plus reviewers over competing implementers.
- Delegation is not isolation. Maintain one writer per file or resource at a
  time. Use a separate branch and worktree for independent write-heavy tasks;
  otherwise give workers disjoint path ownership in the shared checkout.
- Prefer one canonical UVSR clone plus linked task worktrees. Use an independent
  no-remote clone only for an explicitly quarantined experiment or when linked
  worktree isolation is insufficient. Verify branch and worktree identity with
  Git; never infer it from a folder name.
- Name new task branches `codex/<area>-<outcome>` unless the user or an existing
  repository convention requires otherwise, and use the same descriptive slug
  for its worktree and execution plan. Do not rename a published or user-owned
  branch merely to enforce this convention.
- Before spawning a writer, record its objective, base commit, allowed and
  forbidden paths, dependencies, interface contract, acceptance criteria,
  required checks, and handoff format. Do not start work against an undecided
  shared interface.
- The coordinator owns shared hotspots unless explicitly reassigned. These
  include `README.md`, `AGENTS.md`, root build files, `src/shaders.cfg`, global
  settings, generated registries, dependency metadata, binary scene/LUT assets,
  both sides of shared CPU/HLSL contracts, and any file already claimed by
  another worker.
- In a shared checkout, only the coordinator may stage, commit, switch branches,
  merge, rebase, stash, reset, clean, update submodules, or run repository-wide
  formatting/code generation. A worker in an isolated worktree may commit only
  when its assignment explicitly allows it. No agent may push without the
  user's explicit authorization.
- Linked worktrees still share refs and repository metadata. Only the
  repository-level coordinator/integrator may delete branches/tags, remove or
  prune worktrees, run Git garbage collection, or add/update/deinitialize
  submodules, and only when those operations are explicitly in scope.
- In a shared checkout, stage, commit, build, or test only after the relevant
  writers hand off and release their paths, or after every affected writer
  acknowledges an explicit write freeze. Stage exact paths and review the
  staged diff; never capture a file while a peer may be patching it.
- Re-read a file immediately before patching it. If an unexpected diff, stale
  base, ownership overlap, or unrecognized process appears, stop mutation in
  that scope, preserve the state, and report the evidence to the coordinator.
  Never revert, overwrite, or tidy peer or user work.
- Every worker returns a distilled handoff: status, base/head, files and public
  contracts changed, checks and outcomes, risks or assumptions, dependency
  effects, and next action. Raw logs belong in an artifact, not the main thread.
  Ownership is released only when the coordinator acknowledges the handoff.
- A single integrator combines work in dependency order, reviews every combined
  diff, resolves conflicts semantically, and reruns relevant tests after
  composition. A clean textual merge is not proof of behavioral compatibility.
- High-risk rendering, shader, resource-lifetime, deletion, packaging, or build
  changes require an independent review before integration.
- Do not recursively fan out subagents unless the coordinator explicitly
  approves another level. Two repeated no-progress or identical-error cycles
  require a checkpoint and replan instead of blind retrying.

## Roadmap and Active-Work Visibility

- `README.md` **Coming Soon** is a user-facing roadmap and integration summary,
  not a mutex and not a live task ledger. Only the task coordinator or final
  integrator edits it; do not make every worker touch the same section.
- Small fixes, read-only investigations, and short-lived private experiments do
  not require a Coming Soon entry.
- Before complex, concurrent, shared-hotspot, integration, or publication work,
  inspect the complete Coming Soon section, `git worktree list --porcelain`,
  relevant local/unmerged branches, active execution plans, open pull requests,
  and visible Codex tasks when available. Record overlap and dependency
  decisions in the coordinator's plan before implementation.
- A complex or concurrent project uses one uniquely named file under
  `docs/exec-plans/active/` in its owning worktree. Other agents may read that
  file, but only its coordinator edits it. An unpushed branch-local file is not
  assumed visible to remote agents.
- At closeout, move the plan to `docs/exec-plans/completed/` or
  `docs/exec-plans/abandoned/`; preserve its evidence and start a new plan with
  a fresh base if archived work resumes.
- If independently started tasks overlap and have no common coordinator or
  shared visible plan, pause overlapping writes. Coordinators reconcile through
  task messaging, default integration ownership to the existing target/PR owner,
  and transfer, consolidate, or serialize work themselves. Ask the user only
  when ordering would choose between materially incompatible accepted product
  outcomes or a real product-priority decision.
- Serialize updates to the integration branch and authorized publications
  through one repository-level integrator or merge queue, even when feature
  write sets were disjoint. Before each accepted change, refresh the live target
  state, reclassify the relationship, integrate in order, and rerun combined
  checks. If independent coordinators cannot establish that lease, do not race
  merges or pushes: default to the existing target/PR owner and serialize. Ask
  the user only about incompatible product outcomes or missing external
  authority, framed in product terms—never ask them to choose an agent,
  worktree, branch, file lease, or merge order.
- Update Coming Soon once scope and branch are stable, and reconcile it once at
  integration. When work merges, remove its entry and put durable shipped
  behavior in the renderer baseline or relevant design documentation. Mark or
  remove abandoned work explicitly.
- If future work adds phase-resolved or output-resolution visibility history, a
  real jitter-aware output-space reconstruction path, or a genuine packed
  checkerboard visibility layout together with convergence evidence, remind the
  user about the retired
  [Visibility Sample Rotation v1 experiment](docs/postmortem/visibility-sample-rotation-v1.md)
  before proposing restoration. A reminder does not authorize revival.
- If the user says **“bring back the tonemapper drawer”**, **“restore the
  tonemapper drawer”**, or equivalent, read the
  [Tonemapper Drawer and LUTs v1 Strategic Sunset](docs/postmortem/tonemapper-drawer-and-luts-v1.md)
  and its restoration manifest before editing. Treat revival as one coupled
  feature: restore the archived global Tonemapper drawer/LUT implementation and
  implement the bilateral-grid local tonemapper in the same work item. Never
  restore only the old drawer as a standalone feature. The phrase authorizes
  scoped local implementation and verification under this guide; it does not
  authorize a commit, push, pull request, merge, release, or deployment.
- Publishing a roadmap or execution-plan update can improve cross-task
  visibility, but this guide never grants permission to push or open a pull
  request.

## Repository Freshness When It Matters

- When a task asks for the newest build, remote comparison, branch integration,
  publication, or release work, query the live remote rather than trusting
  local tracking refs or file modification times.
- Remote freshness and build verification are separate. A newer live `main`
  commit is not "latest verified" until it reaches Canonical verified state;
  select from recorded verification evidence, not from recency alone.
- Record exact local and remote commits and classify the relationship as equal,
  behind, ahead, or diverged. Inspect uncommitted source and build state before
  deciding whether an update is safe.
- Fast-forward only when the remote is clearly newer and no local work can be
  lost. If local commits, source, or build state is ahead or diverged, do not
  pull, reset, checkout, rebase, or overwrite it; preserve and report it.
- If no upstream or matching remote branch exists, classify the work as
  local-only. Do not substitute an unrelated branch.

## Change Discipline

- Inspect `git status` and the relevant diff before editing. Preserve unrelated
  user and agent changes and never discard them to simplify a task.
- Before broad deletion, pruning, or a risky refactor, preserve only the task's
  scoped starting state. If commits are authorized and the scoped state is
  cleanly separable, use a named checkpoint commit; otherwise use an isolated
  worktree or scoped patch and ask before proceeding. Never checkpoint unrelated
  user or peer changes.
- Format every commit subject in lowercase as `<type>: <description>`, where
  `<type>` is a one-word summary such as `fix`, `feat`, `docs`, or `checkpoint`.
- Enforce the subject format before creating a commit and review it before
  handoff. Do not rewrite a commit merely for cosmetics after its SHA is cited
  by a handoff, build manifest, benchmark, or test result. If an unpublished
  rewrite is genuinely required, the coordinator must update all provenance and
  rerun affected verification. Never rewrite published or merged history
  without the user's explicit authorization.
- When the user explicitly authorizes a GitHub update, use a lowercase
  `<type>: <description>` pull-request title. If creating a merge commit, set
  its subject to `merge: pull request #<number> <description>` in lowercase
  instead of accepting GitHub's generated subject.
- For cleanup work, trace references before deleting. Remove an obsolete feature
  end to end: source and declarations, CPU/UI state, GPU resources, shader entry
  points and files, `shaders.cfg`, CMake/runtime packaging, assets, tests, and
  stale documentation.
- Search again after removal so dead includes, constants, comments, build rules,
  and fallback paths do not remain.
- Ask before deleting any code, shader, or asset whose runtime, rendering, UI,
  or packaging dependency is uncertain.
- Do not perform an external action unless authorized under the conversational
  publication rules above. Ordinary language is sufficient only when the action
  and destination are unambiguous. Force-updating or rewriting published
  history always requires separate explicit authorization.

## Build and Runtime Resource Ownership

- Verification belongs to an exact source snapshot, build directory, artifact,
  scene/settings, and check record. Any relevant edit, rebase, merge, conflict
  resolution, or integrated peer handoff makes affected evidence stale.
- Use four distinct states: **Candidate** is a built artifact; **Technically
  verified** passed required automated and runtime evidence; **Product accepted**
  ties required visual/product review to the exact artifact and observed
  settings; **Canonical verified** is a clean committed state integrated into
  the canonical target and reverified after integration. Only Canonical verified
  checkpoints may be called "latest verified."
- A dirty build may be technically verified for task-local comparison when its
  complete diff identity and artifact are recorded, but it is never Canonical
  verified or latest verified. Any artifact- or settings-changing repair
  invalidates prior product acceptance and requires the new result to be shown
  and accepted when that review is part of the task.
- Concurrent writers use separate worktrees and build directories. Never run
  two builds against the same build tree; serialize configure, build, shader
  packaging, and test operations when isolation is unavailable.
- Only one designated agent controls a UVSR window or GPU benchmark at a time.
  Do not fight user input. Close or restart only the process that belongs to the
  current experiment, and account for Windows executable/object-file locks.
- Performance comparisons must use a fixed commit baseline, GPU, scene, camera,
  resolution, settings, warmup, and sample window. Report total frame time plus
  the feature's own GPU cost; a local pass becoming faster is not sufficient if
  the full frame regresses.
- Each experimental build must identify its source commit and experiment. Keep
  unverified experiments isolated from the verified build and label their
  integration status explicitly.

## Performance Benchmark Hygiene

- Treat benchmark camera position 1 as the control measurement for every local
  performance session. Match the exact executable, source state, scene, camera,
  resolution, display mode, graphics settings, power source, and laptop thermal
  preset. Record the executable SHA-256; Git HEAD plus an identity for the dirty
  diff and untracked source files; adapter name, LUID or device ID, and driver;
  OS build and HAGS state; display and power configuration; and scene/settings
  identity. Invalidate the baseline when any identity changes, and never compare
  unlike runs.
- Split isolation checks into a preflight phase and a measurement phase. Before
  launching, require zero `uvsr.exe` processes and zero capture sessions. During
  measurement, require exactly the expected renderer PID and normalized
  executable path plus either zero captures or the one expected PresentMon PID
  and path. The user decides whether and when to close nonessential applications
  before the run; an agent may stop only stale processes belonging to the
  current experiment. Resample this identity and the absence of known builds,
  shader compiles, captures, overlays, and stress tests throughout every quiet
  or measurement window; a clean final snapshot cannot redeem contamination
  earlier in the window. Generic Windows counters cannot always attribute work
  to the selected adapter on hybrid systems, so do not claim they prove the
  absence of every renamed or unknown GPU workload. Use the bracket controls to
  reject otherwise hidden contention.
- For the final build before a benchmark window, disable MSBuild worker-node
  reuse when the build command supports it, or verify that every compiler and
  build worker has exited before preflight. Idle worker nodes from the completed
  task remain process-hygiene blockers; close only workers whose command line
  and parentage prove they belong to that completed task.
- UVSR requests Windows High process priority at startup. Record the renderer's
  live priority class with every baseline and candidate, require `High` for an
  accepted run, and invalidate comparisons against older runs made at another
  priority. Never escalate UVSR to Realtime and never lower or otherwise change
  unrelated process priorities to improve a result.
- Treat unexplained CPU or GPU activity as evidence that the user may be using
  the machine, not as permission to compete for the remaining capacity. Never
  stop, suspend, reprioritize, or reconfigure an unrelated process to make room
  for a benchmark. After an occupied or thermally failed period, do source-only
  work; do not opportunistically launch a build or runtime test when utilization
  briefly dips. Do not repeatedly poll temperatures, processes, or utilization
  while waiting for availability. Resume monitoring or intensive work only in a
  testing window the user has made available and after the complete quiet
  preflight remains clean for its full duration.
- Establish the machine's cold position-1 baseline before accepting feature
  measurements. After shader and resource warmup has settled, collect at least
  1,000 consecutive frames or 15 seconds, whichever is longer. Repeat this
  three times when creating or replacing a baseline, run a fresh clean preflight
  immediately before each repeat, retain all three raw runs, and select the
  median by position-1 total GPU frame time as the primary run.
  Record UVSR's stat-line TFLOPS, total frame/GPU time, feature time, GPU clock,
  CPU and GPU temperature, live throttle reasons, and process memory. TFLOPS is
  a current-clock-and-utilization indicator, so compare it only between
  identical position-1 control workloads, never between different SVSM options.
- Keep renderer debug views, GPU captures, validation layers, and diagnostic
  overlays disabled during accepted performance windows unless the identical
  control explicitly includes them. SVSM debug views add statistics dispatches
  and readbacks, so use them in separate diagnostic runs and return the view to
  Off before collecting a position-1 baseline or candidate measurement. The
  ordinary stat line may remain visible when it is held identical across every
  compared run.
- On Brock's current laptop, use CPU at or below 75 C and GPU at or below 55 C
  for at least 30 quiet seconds as the provisional preflight gate until a clean
  cold baseline establishes tighter machine-specific limits. Use the hottest
  live temperature sensor on the selected GPU, require selected-GPU utilization
  at or below 5 percent, and require no live thermal limiter. For normalized
  external CPU utilization, subtract the expected renderer and the complete
  live ChatGPT/Codex adjacent-task process trees from total CPU before applying
  a five-sample rolling average at or below 20 percent; allow isolated adjusted
  samples up to 50 percent. Always record total CPU, renderer CPU, excluded
  assistant CPU, the adjusted value used by the gate, and a ranked
  name/PID/percent breakdown of the remaining processes. This exclusion never
  exempts a detected build, shader compiler, capture, overlay, stress tool,
  duplicate renderer, or other process-hygiene blocker merely because Codex
  launched it. Treat 10 to 20
  percent adjusted background CPU as a recorded warning rather than an
  automatic contaminant when it is stable and represents the machine's normal
  resident workload. On another laptop, first record its cold-idle temperatures
  and official thermal limits, then require comparable cold-start temperature,
  comfortable thermal headroom, and stable clocks instead of copying these
  absolute numbers blindly.
- Use temperature and thermal headroom as hard thermal evidence, and require a
  live limiter state whenever the adapter exposes one. A cumulative driver
  throttle counter may be used as a hard gate only after a quiet calibration
  proves it remains stable while its limiter is inactive. Otherwise record it
  as diagnostic evidence only. This avoids false failures from drivers that
  increment unrelated counters while idle.
- Keep cold-idle and loaded gates distinct. The 55 C temperature and 20 C
  headroom values are preflight requirements, not loaded measurement limits.
  During a measurement, require at least 5 C of live selected-GPU thermal
  headroom and an inactive live limiter, then use the Position-1 clock, TFLOPS,
  and frame-time bracket as the decisive throttling check. A hot memory junction
  below its reported limit is recorded but is not by itself proof of throttling.
- If the host does not expose a live CPU temperature to the non-elevated,
  vendor-neutral monitor, `-AllowMissingCpuTemperature` may be used for a
  reduced-evidence preflight. It still requires unconstrained CPU performance,
  the practical background-load gate, a cool selected GPU with no live thermal
  limiter, AC power, and position-1 before/after controls within the established
  clock, TFLOPS, and frame-time bands. Record CPU temperature as unknown; never
  reinterpret it as cool or use this exception to accept a failed control.
- Match telemetry to the exact adapter name shown by UVSR. On a hybrid or
  multi-GPU system, an unqualified vendor query is invalid evidence because it
  can certify a cool idle adapter while UVSR uses another one. Fail closed when
  the DXGI adapter cannot be mapped uniquely or when its temperature or required
  thermal-limit/headroom reading is unavailable. A manually transcribed
  temperature is useful for one diagnostic snapshot but cannot certify a
  30-second quiet window because it is not resampled.
- The thermal script must fail closed by default when live GPU limiter telemetry
  is unavailable. On an adapter that genuinely does not expose it, agents may
  use `-AllowMissingGpuThermalLimiter` only after supplying the adapter's
  official limit with `-GpuThermalLimitC`, establishing a cold position-1 GPU
  clock and TFLOPS band, and retaining the before-and-after control checks. The
  reduced-evidence gate still requires a live temperature, sufficient headroom,
  and a live clock; document that the limiter state was unavailable. Because
  core, hotspot, and memory limits may differ, the supplied value must be the
  lowest official limit applicable to every live GPU sensor used by the gate.
- On Brock's current laptop, treat a Position-1 stat-line result below 30 TFLOPS
  as the practical throttling floor. At or above 30 TFLOPS is generally
  thermally usable unless a live limiter, inadequate headroom, or a badly
  regressed control says otherwise. Still record a candidate whose TFLOPS,
  steady GPU clock, or control frame time differs by more than 5 percent from
  the cold baseline as a comparison-quality warning; do not label that variance
  thermal throttling while the machine-specific floor and limiter checks pass.
  Establish an equivalent floor empirically before using this rule on another
  host.
- Bracket a long experiment with the same position-1 control. If the post-run
  control fails the baseline band, discard the measurements between the two
  controls, stop runtime work, and let the machine cool before repeating. Do not
  report thermally suspect results as measurements.
- Record the renderer's PID, path, start time, private committed memory, total
  and private working set, handle and thread counts, and dedicated GPU memory as
  a time series before and after repeated identical control cycles. Also record
  shared GPU memory, system commit, available RAM, and adapter budget/usage when
  the platform exposes them. An
  unexplained monotonic increase or a control that degrades only inside one
  long-lived UVSR process is a leak candidate: restart that exact process and
  reproduce before blaming thermals. Degradation that persists across clean
  process launches and unrelated programs is stronger evidence of machine-wide
  thermal or system contention.
- Prefer one low-rate, read-only sensor logger and UVSR's internal GPU timers.
  External frame capture must be applied identically to baseline and candidate
  runs because even a monitoring tool can perturb a sub-millisecond result.
  Vendor dashboards may be opened read-only before and after a run when no
  scriptable sensor is available; do not change fan, power, TCC, overclock, or
  thermal settings without explicit user authorization.
- Record resident vendor dashboards and sensor pollers such as MSI Afterburner,
  HWiNFO, HWMonitor, GPU-Z, and laptop-control services, but do not reject a run
  solely because they are present. Hold them identical across the baseline,
  candidate, and bracket controls. Active capture, injected overlays, GPU
  debuggers, and stress tools such as RenderDoc, PIX capture, Nsight capture,
  RTSS, OCCT, and AIDA64 are always contaminants for accepted performance
  measurements. Use them only in separately labeled diagnostic runs. The user may
  close unrelated applications through their normal UI; an agent must not close
  them. Never terminate an OEM thermal or fan-control service blindly. The
  thermal preflight reports both blocking and informational process names and
  IDs so the user can distinguish them.
- Use portable monitoring binaries only from their official release source,
  pin the version and SHA-256 digest, verify signatures when provided, and keep
  downloaded tools under ignored `work/` storage. For Windows/DX12 hosts,
  use Windows performance counters plus LibreHardwareMonitor as the first
  non-elevated cross-vendor sensor attempt, and use PresentMon as the
  vendor-neutral frame-pacing cross-check. Missing temperature sensors remain
  unknown: do not install PawnIO or another optional driver merely to turn an
  unknown reading into a pass. UVSR's own GPU timestamps remain authoritative
  for sub-millisecond pass costs.
- HWiNFO Portable may be used as an optional cross-vendor Windows corroboration
  source only when its current license permits the host's use and the user has
  separately authorized running it. Even portable HWiNFO temporarily loads a
  kernel driver and may store preferences; downloading it is not permission to
  execute it, elevate it, enable automatic logging, or retain its driver. Keep
  it outside the default bootstrap and never redistribute it with UVSR.
- Run `tools/get_uvsr_performance_tools.ps1` to fetch the pinned
  LibreHardwareMonitor and PresentMon builds into ignored `work/` storage and
  verify their release digests, every extracted file, and available signatures.
  This bootstrap targets UVSR's current Windows x64/DX12 host; the benchmark
  policy is vendor-neutral, but these particular binaries are not OS- or
  architecture-neutral.
  Once the user has explicitly made a testing window available, run
  `tools/check_uvsr_thermal_state.ps1 -Phase Preflight -ExpectedGpuName "<UVSR
  Renderer name>"` before launching. Do not invoke it repeatedly to search for
  a quiet dip outside that window. Its `thermalGateReady` result proves only the
  resampled sensor, power, and process gate for the selected phase; it does not
  prove the position-1 baseline, adapter workload, matching power configuration,
  or post-run control. It neither reads nor enforces the stat-line TFLOPS. On
  Brock's current laptop, the benchmark procedure must separately reject a
  Position-1 control below 30 TFLOPS. Run `-Phase Measurement` continuously for
  every baseline, candidate, and bracket control with
  `-MeasurementDurationSeconds` covering the exact benchmark window and the
  exact renderer PID and path; add the exact PresentMon PID and path only when
  it is actually capturing. For programmatic coordination, pass a unique
  `-MeasurementReadyPath` and begin the benchmark only after that file reports
  `state=ready` with the exact run identity, monitor PID, renderer PID and path,
  and Unix start/deadline fields. The script rejects a pre-existing marker so
  stale readiness cannot certify a new run. Acceptance additionally requires
  the same marker to finish as `state=complete`, repeat the exact identity and
  timing fields, and contain an end time covering the benchmark. A terminal
  `state=contaminated`, missing terminal state, identity mismatch, early end, or
  timeout invalidates the evidence. `-TimeoutSeconds` limits how long the script
  waits for the qualifying window to start; once it starts, the script allows
  the full requested stable or measurement duration. Any failed sample
  invalidates the entire measurement. `-DiagnosticSnapshot` takes one
  diagnostic sample but can never set `thermalGateReady`. Keep the default
  unique timestamped summary and CSV paths for every preflight and run; never
  point separate raw repeats at the same explicit output path. After a separately
  authorized manual download from HWiNFO's official site, the
  `-VerifyOptionalHWiNFO` switch verifies and extracts the pinned archive but
  never executes it. On a future host that one of these tools does not support,
  substitute an equivalent read-only, vendor-neutral OS-native source, pin and
  verify it the same way, and report any missing evidence explicitly rather
  than weakening the gate.
- On Brock's current laptop, where the non-elevated monitor does not expose CPU
  temperature, add `-AllowMissingCpuTemperature` to the preflight and every
  measurement command. This invokes the documented reduced-evidence path. Do
  not add it on another host until its CPU-temperature limitation is confirmed.

## UI and Rendering Safeguards

- Treat the existing UI layout, wording, order, widths, defaults, and alignment
  as user-designed product behavior. Change them only when the task explicitly
  calls for a UI change.
- Every new or changed UVSR-owned interactive control must have a concise hover
  tooltip that describes its effect.
- Cleanup and infrastructure work must not silently change authored colors, AgX
  output, screen-space indirect lighting, scene lighting, or other visible
  rendering behavior.
- Keep the forward and deferred PBR paths on the shared UVSR material/lighting
  contract. The PBR comparison implementation is the documented exception to
  the dormant-feature rule: it is retained as a regression/experiment oracle,
  but its production UI control is hidden. If restored, the toggle must retain
  the same camera, scene, tonemapper, sky, and lights.
- Preserve the active display pipeline order: scene-linear HDR radiance, fixed
  neutral AgX inset transform, logarithmic encoding, default contrast tone
  scale, output gamut conversion, display transfer, then dithering. User-facing
  exposure, grading, LUTs, and local tone mapping remain absent until the paired
  revival contract above is explicitly invoked.

## Documentation

- Keep the root file named `README.md`.
- Every document an agent creates or edits must use conventional English Title
  Case for every visible heading. This includes Markdown headings and any
  standalone paragraph or list lead-in whose heading text is formatted entirely
  in bold, with an optional trailing colon.
- Apply this requirement retroactively to all agent-authored or
  repository-maintained documents in scope. Audit existing documents and correct
  every nonconforming heading, including headings in otherwise untouched or
  historical documents; title-only remediation is expressly authorized.
- Inline bold emphasis and table cells are not headings. Preserve the
  established casing of product names, acronyms, code identifiers, file paths,
  commands, literal UI text, and quoted source text; do not change ordinary
  prose.
- Run `tools/check_document_title_case.cmd` on Windows, or
  `python3 tools/check_document_title_case.py` on other hosts, after creating or
  editing documentation and before committing it. Audit the entire tracked
  Markdown set plus nonignored new Markdown files, not only the files changed by
  the current task, and do not claim completion while the checker reports a
  violation.
- Update `README.md` whenever a change affects user-visible behavior, defaults,
  controls, required assets, build/test/run steps, or intentional omissions.
  Remove stale claims instead of preserving historical behavior.
- Update `docs/pbr-foundation.md` when the material contract, G-buffer packing,
  BSDF equations, debug views, limitations, or extension path changes.
- Prefer extensive, high-signal code comments that preserve implementation
  reasoning: design intent, invariants, assumptions, tradeoffs, edge cases,
  failure modes, and non-obvious cross-component interactions. Optimize comments
  for human debuggability and future LLM inference, while avoiding comments that
  merely restate straightforward syntax.

## Verification

- Build the first-party renderer with
  `cmake --build build --config Release --target uvsr`.
- For PBR or rendering changes, also build `uvsr_pbr_tests` and run
  `ctest --test-dir build -C Release --output-on-failure`.
- Launch through `tools/launch_uvsr.ps1 -Experiment "<lowercaseword>"` and
  smoke-test after runtime, rendering, shader, scene-loading, or UI changes.
  The one-word experiment/title token must match `\A[a-z]+\z`: use one or more
  lowercase ASCII letters and nothing else. Never use uppercase letters,
  digits, spaces, hyphens, underscores, or punctuation. Valid examples are
  `main` and `localtone`; invalid examples include `LocalTone`, `localtone2`,
  and `local-tone`.
  Never launch the bare executable during agent work: every renderer taskbar
  title must identify the experiment with that lowercase ASCII letters-only
  token; the renderer appends its source commit and local `HHmm` launch time.
  Exercise the relevant forward/deferred and screen-space AO/GI combinations
  when they could be affected.
- An experiment is not verified merely because it builds, passes tests, or
  briefly looks correct. Validate the task's behavioral and performance claims
  against the recorded baseline and list any checks not run.
- Documentation-only changes require link and diff validation, not a renderer
  rebuild.
- For documentation changes, run the checker command above with `--self-test`,
  then run it again without arguments.
- Run `git diff --check` before committing.
- Before declaring the overall task complete, the coordinator maps every
  acceptance criterion to evidence, confirms all workers are done or stopped,
  reviews final repository status and untracked artifacts, and states exactly
  what was committed, merged, pushed, launched, or intentionally left local.
