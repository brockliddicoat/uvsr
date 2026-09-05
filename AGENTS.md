# agent contract

policy `2026-09-04.3`. keep this file below 1,000 words. shorten existing rules
before adding more. keep task notes and measurements in ignored work files.

## scope

choose the simplest correct design, not merely the smallest diff. reduce duplicate
code, dependencies, and state without losing required behavior. add complexity only
when requirements or evidence justify it. do not build for imagined needs.
broad cutdown applies only when requested. otherwise stay within the task.

repair faulty behavior behind existing controls. get explicit approval before
removing a visible feature or control. do not restore deliberate removals.
smaller code or better metrics never justify lost behavior, evidence, or clarity.

## work

work in the intended checkout and read its current instructions. resolve conflicting
targets before editing. old reports are evidence, not current instructions.
inspect relevant changes and callers. preserve unrelated work and the Git index.
remove obsolete code from all its consumers without leaving duplicate paths.
keep generated files out of Git.

finish authorized work. make reasonable assumptions for routine reversible details.
ask only when uncertainty affects scope, correctness, product behavior, or authority.
advice requests do not authorize edits. commits, branch changes, pushes, merges,
releases, and publication require authority for that action.

Astra coordinates and owns design, edits, integration, and final acceptance. use
Sol medium explicitly for useful independent research or review. give workers a
focused brief and require source references and uncertainty. workers do not edit
or delegate. Astra checks decisive evidence. use commands for mechanical tasks.
one owner controls each file, build tree, renderer, and GPU session. read
docs/agent-collaboration.md only for shared resources or integration.

set checkpoints for major work. keep measurement definitions fixed. report counts
and labeled estimates at checkpoints. other updates cover findings and next steps.

## writing

update only affected documents unless broader cleanup was requested. preserve useful
recovery evidence and legal text. keep each fact in one place and README.md below
1,200 words. check changed documents only. test a checker only when it changes.

use plain, concise, lowercase prose, comments, and headings. preserve technical names,
code syntax, quotations, legal text, and existing UI labels. use connected paragraphs
and concrete facts. omit filler, invented compound labels, and repeated summaries.
comments explain reasons the code does not show. for UI work, read docs/ui.md,
then only the relevant procedure or incident.

## builds and checks

reuse a separate build tree only when its source, tools, configuration, options,
and dependencies match. build affected targets incrementally. reconfigure only when
build inputs change. clean only for proven contamination. rebuild shaders and copy
assets only when their inputs change.

run focused checks while editing. run the full required developer and exact package
checks once at a checkpoint or final code handoff. repeat only after relevant changes,
failures, or new uncertainty. investigate checks taking over ten minutes, but let
justified checks continue while progressing. planning needs no build. policy edits
need content, instruction discovery, diff, and word count checks.

research and prose answers need no executable proof. code handoffs and requested
build identification need exact source and binary records. distinguish build,
visual, runtime, and production package evidence.

[not be changed below this point]

everything below this line is immutable unless the user explicitly authorizes
that exact change.

## product contract

UVSR is DirectX 12 only. ImGui is permanent. developer and production builds
use one renderer feature set. preserve unless the user explicitly changes it:

- 2x, 4x, 8x, and correct 16x MSAA with ray-traced shadows.
- every current AO/GI option and supported combination.
- ray-traced sky visibility, the flashlight, and its ray-traced shadows.
- Bistro, San Miguel, every retained HDR and STBN/noise asset.
- the conventional path tracer, material editing, pixel zoom, timing, and
  shared rendering data required by current techniques.

absence from this list is not permission to remove another visible feature.

a deliberate sunset is part of the product baseline and stays removed unless
the user asks to restore it. a removal candidate is only a proposal and remains
until approved. preserve useful controls in retained features.

## architecture contract

Donut is retained. do not plan or perform its detachment, replace its owned
framework slices, remove its targets or gitlink, or treat direct first party
ownership as migration away from it. keep exact pins and apply patches only in
the build tree. never edit Donut or add unnecessary coupling. changing this
boundary requires new explicit user authority. remove Python from first-party source,
build, CI, tests, tools, launcher, documentation, and packages after porting
required behavior to C++.

the renderer must accommodate many lighting systems, GPU-driven rendering,
meshlets, and one coherent screen-space and world-space lighting pipeline
without parallel framework layers, inactive-technique cost, or duplicated data.
prefer explicit frame orchestration, concrete technique owners, and shared data
only for real consumers. use Wicked Engine as a pinned read-only design
reference, never as a dependency or wholesale template.

## identity and distribution

the only shipped executable names are `uvsr-launcher.exe` and
`uvsr-engine.exe`. recognize an old name only for one exact installed-state
migration. one authoritative C++ settings-schema hash owns engine identity,
Windows version fields, diagnostics, launcher data, and package metadata.

ship only launcher, engine, required runtime DLLs, compiled shaders, retained
assets, settings, notices, and licenses. public downloads contain signed
launcher or renderer artifacts, never source, interpreters, Git, CMake,
compilers, SDKs, debug layers, symbols, tests, or benchmark tools.

## publication and artifact canary

publication requires explicit authority, current remote identity, a fresh
exact-package proof, signatures or hashes, and complete legal material.

a code handoff is complete only when it links the exact `uvsr-launcher.exe` and
`uvsr-engine.exe` for the active lineage, gives each SHA-256, and states whether
each was rebuilt. if either identity is missing, stale, or from another lineage,
report the handoff blocked.
