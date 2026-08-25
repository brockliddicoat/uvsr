# agent contract

policy `2026-08-25.3`. keep this file durable and short. store task state,
measurements, recipes, and detailed procedures in ignored `work/` records or
focused nearby documents.

## direction

optimize for the smallest new mechanism that unlocks the requested capability,
not the smallest diff. use it to enable the largest safe backend deletion or
rewrite. broad changes are welcome when they leave fewer owners, concepts,
dependencies, states, and context.

preserve useful user-facing control. prefer repairing or replacing faulty
implementations behind current controls. remove a control only when its visible
outcome is broken, misleading, materially redundant, or the user explicitly
approves the product cut.

future-proof through direct ownership, stable data contracts, and replaceable
slices. do not add generic frameworks, inactive paths, registries, fallbacks, or
abstraction for imagined needs.

## work

inspect status and relevant diffs; preserve unrelated work. trace current and
released consumers before deletion. remove an obsolete owner end to end,
including settings, UI, persistence, shaders, tests, build/package rules,
documentation, and legal records where applicable. do not leave dual paths.

use one writer per file and one coordinator for integration. no commit, branch
change, push, merge, release, deployment, publication, or other external action
without authority for that exact action and candidate. never edit Donut.

major work uses named targets and recoverable checkpoints. freeze metric scopes
at the start. status updates report checklist progress, additions, deletions,
net lines, and estimated context reduction. label context reduction as an
estimate; metrics never justify lost behavior, control, evidence, or clarity.

## documentation

rewrite maintained documentation in the shared style below. keep current
contracts, decisions, necessary use, recovery facts, and required legal text.
delete obsolete history, duplicate guides, stale plans, and repeated policy.
root `README.md` has a 1,200-word ceiling. use lowercase headings and preserve
exact capitalization in identifiers, product names, quoted text, and licenses.

## build and proof

use the cheapest check that can catch the changed failure. do not build or test
during planning. ordinary checks have a 10-minute ceiling; narrow or stop work
that exceeds it instead of starting repeated broad runs. build affected targets
incrementally while iterating. run one full developer gate and one fresh exact
production-package smoke only at a named checkpoint or final code handoff.
policy-only work needs content, discovery, diff, and word-count checks.

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

Donut is transitional. replace each used slice with direct first-party ownership
and delete the old path in the same change; remove the gitlink last. do not edit
Donut, add coupling, or retain it as fallback. remove Python from first-party
source, build, CI, tests, tools, launcher, documentation, and packages after
porting required behavior to C++.

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

## shared personalization

write concise, direct, lowercase prose while preserving exact technical capitalization. use plain, educated english and simple sentence structure. prefer periods, commas, and parentheses over em dashes and semicolons. avoid hyphenated word pairs.

start with useful content. omit filler, repetition, and unnecessary validation. use headings and lists only when they improve understanding. group related sentences into readable paragraphs.

prefer simple, direct solutions with few concepts and moving parts, while preserving readability and correctness. document only nonobvious constraints, invariants, and reasons. do not narrate obvious code.

apply prior context only when it clearly fits. do not restore deliberate removals unless asked. prefer the smallest implementation addition that unlocks the goal, not the smallest total change. pursue the largest justified backend cutdown while preserving useful user controls. treat control removal as a separate product decision. if a visible feature is faulty, prefer a clean reimplementation behind its controls unless the user explicitly approves removal.

maximize process simplicity and understandability, not response length. often, less is more.
