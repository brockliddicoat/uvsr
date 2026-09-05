# agent contract

policy `2026-09-04.2`. keep this file below 1,000 words. replace or shorten
overlapping rules. store task state and measurements in ignored work records.

## direction

prefer the smallest new mechanism that achieves the requested capability, not
the smallest diff. broad cutdown applies when requested; otherwise preserve task
scope. reduce owners, dependencies, states, and concepts while preserving behavior.
add complexity only when required behavior or concrete evidence justifies it.
avoid speculative frameworks, inactive paths, registries, and fallbacks.

repair or replace faulty implementations behind retained controls. propose visible
feature or control removals separately and wait for explicit approval. preserve
deliberate removals. metrics never justify lost behavior, evidence, or clarity.

## work

start in the checkout being edited and read its current instructions. confirm the
target when checkout or policy identities disagree. do not import historical
instructions as current policy. inspect status and relevant diffs; preserve
unrelated work and the index. trace current and released consumers before deletion.
remove obsolete owners end to end without dual paths. keep generated files out of Git.

finish authorized implementation, including routine reversible details. preserve
prior authorization. ask only about material uncertainty in scope, correctness,
product behavior, or authority. advice requests do not authorize implementation.
no commit, branch change, push, merge, release, deployment, or publication without
authority for that action. never edit Donut.

keep Astra as coordinator with Sol medium for bounded independent investigation
or second review. workers do not edit or delegate. Astra owns design, critical
changes, integration, and acceptance, and checks decisive source evidence. use
commands for mechanical work. load docs/agent-collaboration.md only for shared
resources or integration. one owner per file, build tree, renderer, and GPU session.

use named checkpoints for major work. freeze metric scopes once. report counts and
context estimates at checkpoints, not every update. label estimates. ordinary
updates describe findings, decisions, and remaining work.

## documentation

update affected documents only; broader cleanup requires scope. preserve useful
recovery evidence and legal text. each maintained fact has one owner. keep root
README.md below 1,200 words. check changed documents only; self-test a checker only
when it changes. use lowercase authored prose, comments, and headings, preserving
technical names, identifiers, syntax, quotations, legal text, and existing UI labels.
write concrete, evidenced statements in connected paragraphs. omit filler, invented
compound labels, ceremony, and repeated summaries. explain nonobvious reasons in
comments. load docs/ui.md for UI work, then only relevant procedures or incidents.

## build and proof

reuse an isolated build tree only when source lineage, generator, toolchain,
configuration, options, and dependencies match. build affected targets incrementally.
reconfigure only for changed build inputs; clean only for proven contamination.
rebuild shaders and restage assets only when their inputs changed.

use focused checks while iterating and the full applicable developer and exact
package gate once per named checkpoint or final code handoff. repeat only after
relevant changes, failures, or new uncertainty. investigate checks exceeding ten
minutes; continue justified checks that are progressing without restarting them.
planning needs no build. policy changes need content, discovery, diff, and word
counts. research and prose answers need no executable proof. code handoffs and
requested build identification retain exact provenance. distinguish developer,
visual, runtime, and production package evidence.

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

## publication and handoff

publication requires explicit authority, current remote identity, a fresh
exact-package proof, signatures or hashes, and complete legal material.

a code handoff is complete only when it links the exact `uvsr-launcher.exe` and
`uvsr-engine.exe` for the active lineage, gives each SHA-256, and states whether
each was rebuilt. if either identity is missing, stale, or from another lineage,
report the handoff blocked.
