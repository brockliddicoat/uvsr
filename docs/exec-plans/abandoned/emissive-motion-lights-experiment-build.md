# Emissive, Motion, Lights, and Experiment Build Cleanup

## Status

- State: superseded after partial integration
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/prune-shader-paths` in
  `C:\Users\brock\OneDrive\Documents\uvsr`
- Base commit: `b63cda9639dedf820f1251aa390b162befa22dd7`
- Started: 2026-07-30
- Last updated: 2026-07-30
- Planned archive:
  `docs/exec-plans/completed/emissive-motion-lights-experiment-build.md`

## Goal and Done Condition

Goal:

Remove the unused Include Emissive Sources product setting and every shader,
resource, preset, UI, test, and documentation path that only it permits; restore
the intended uncapped motion benchmark; make the Lights drawer present the
requested initial sun and shadow-section state; establish the historical
Visibility Distribution default; and add an opt-in experiment build that
compiles only the shader topology needed by factory startup settings.

Done when:

- [x] Include Emissive Sources and its exclusive implementation surface are
      removed end to end, with exact shader-count savings recorded.
- [x] Visibility Distribution history establishes whether 2 or 3 was the
      prior default, and the retained value is documented rather than guessed.
- [x] The motion benchmark advances at the renderer's available frame rate with
      no intentional 40-fps pacing while preserving its per-rendered-frame
      motion contract.
- [x] Opening Lights selects `sun_1`, presents Bend, Sparse Virtual Shadow Maps,
      and Diagnostic Cascaded Shadow Maps expanded, and leaves all three
      disabled.
- [x] A default-settings-only experiment shader build is opt-in, fast, explicit
      about unsupported live setting changes, and leaves full production and
      developer shader catalogs unchanged.
- [ ] Required Release builds, shader packaging, focused contracts, full CTest,
      labeled runtime checks, documentation checks, and independent high-risk
      review pass. Automated and review evidence is complete; visual/runtime
      acceptance remains pending because user-owned PID 8584 owns the renderer.

## Scope

In scope:

- Emissive-source settings, UI, renderer ownership, shader bindings and macros,
  resource lifetime, presets, benchmark identity, tests, and documentation.
- Visibility Distribution default/history evidence.
- Motion benchmark scheduling and frame-pacing code.
- Lights drawer initial selection and nested-section presentation defaults.
- A first-party opt-in default-settings experiment shader configuration and
  launch/build workflow.
- Shader-retirement postmortem and current product/build documentation.

Non-goals:

- Changing emitted-material appearance except by removing the user-selectable
  emissive-source contribution requested here.
- Changing Bend, SVSM, or diagnostic CSM algorithms, defaults, resources, or
  shaders beyond initial enablement/presentation state.
- Changing the first-Escape General-only Settings contract.
- Editing Donut, changing DirectX 12 ownership, publishing, merging, or
  rewriting history.
- Claiming a performance improvement without a matched benchmark.

Affected subsystems and paths:

- `src/uvsr.cpp`
- `src/screen_space_visibility*`
- `src/pbr_deferred_lighting*`
- `src/shaders*.cfg`
- `CMakeLists.txt`
- `tools/`
- focused renderer, UI, visibility, shader-bundle, and benchmark tests
- `README.md`
- `docs/screen-space-visibility.md`
- `docs/ui-integration-agent-procedure.md`
- `docs/postmortem/shader-path-retirements.md`

Shared hotspots reserved for the coordinator:

- `README.md`, `CMakeLists.txt`, `src/uvsr.cpp`, shader configurations, shared
  CPU/HLSL contracts, global settings/defaults, tests, documentation, build
  trees, and renderer/GPU control.

## Baseline

- Canonical repository/remote:
  `https://github.com/brockliddicoat/uvsr.git`
- Local versus remote state: `codex/prune-shader-paths` equals
  `origin/codex/prune-shader-paths` at `b63cda9`; clean at task start.
- Canonical target: `origin/main` at `a4f9bf0`; this feature branch is one
  committed change ahead and intentionally remains the active lineage.
- Verified source commit/build: `b63cda9` has the preceding shader-retirement
  verification record; the currently running user-launched executable is from
  `build-shader-prune` and identifies embedded source `a4f9bf0`, so it is not
  evidence for this new candidate.
- GPU, scene, camera, resolution, and settings preset when relevant:
  default PBR Sponza startup settings for correctness; the motion benchmark
  retains its existing exact scene/camera/settings contract.
- Known pre-existing failures: the reported near-40-fps motion run was traced
  to the AA benchmark's original 25 ms sleep. It was not a renderer performance
  regression or a committed fix lost to a merge.

## Dependencies and Interfaces

| Dependency or Task | Required Revision or Decision | Status | Consumer |
| --- | --- | --- | --- |
| UI reference | `2026-07-29.4` at intake; `2026-07-30.1` after normative update | Read completely, updated, and rechecked | Lights and emissive UI |
| Agent policy | `2026-07-22.1` | Read and recorded | All work |
| Screenshot reference | User-provided Lights state | Inspected | Lights presentation |
| PR #10 | Shared Visibility shader helpers | External and serialized | Later integration |
| PR #11 | Visibility degenerate-path tests | External and serialized | Later integration |
| Active shadow plans | Bend, SVSM, and diagnostic CSM own algorithm work in other lineages | Inspected; no implementation changes allowed here | Lights presentation only |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Removing emissive sources must compact every affected CPU/HLSL binding and
  shader key consistently; no stale white/black fallback resource remains
  unless another active consumer requires it.
- Distribution remains one runtime-uniform Visibility value and must not create
  a new permutation.
- Motion benchmark angular movement stays defined per rendered frame; no
  sleep, present cap, or fixed wall-clock throttle may reduce renderer cadence.
- Lights presentation changes only initial UI selection/disclosure and the
  three shadow enable states.
- The experiment shader profile is a separate opt-in build contract. Normal
  production and developer manifests remain complete and authoritative.

## UI Change Intake

- UI reference version: `2026-07-29.4` at intake and `2026-07-30.1` after the
  normative change; both were read and the final version was rechecked.
- Agent policy version: `2026-07-22.1`.
- Owning drawer: Lights.
- Removed control: Include Emissive Sources and any body it exclusively owns.
- Changed initial presentation: `sun_1` selected; Bend, Sparse Virtual Shadow
  Maps, and Diagnostic Cascaded Shadow Maps nested sections expanded; their
  Enabled toggles remain off.
- Control classification: removal of an immediate/toggle-owned control plus
  initial disclosure state; no new renderer-topology selector or deferred
  dropdown is introduced.
- Defaults and reset: scene light selection resolves to `sun_1`; all three
  shadow systems retain disabled factory defaults and their existing reset
  ownership.
- Consumers: light selection, scene-load reconciliation, shadow settings,
  nested animation state, scrolling, renderer pass creation, Statistics,
  benchmark setup, shader keys, tests, and documentation.
- Animation and scroll owner: existing Lights drawer and three
  `BeginAnimatedTreeNode` lifetimes inside `##SettingsBody`.
- Renderer cost: disclosure-state changes are presentation-only; shadow
  systems remain disabled. Emissive removal saves zero static permutations but
  removes runtime shader/API/constant-buffer work. The opt-in experiment build
  compiles 51 first-party tasks instead of the complete production build's 516.
- Presentation purity: scene-load/default initialization owns state mutation;
  UI composition remains read-only.

## Assignment Summary

| Task ID | Owner | Branch/Worktree | Base | Write Scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| EMISSIVE-HISTORY | Read-only explorer | Shared checkout | `b63cda9` | None | Plan | Complete |
| MOTION-HISTORY | Read-only explorer | Shared checkout | `b63cda9` | None | Plan | Complete |
| EXPERIMENT-DESIGN | Read-only explorer | Shared checkout | `b63cda9` | None | Plan | Complete |
| IMPLEMENT | `/root` | Current branch | `b63cda9` | All scoped paths | Explorer handoffs | Complete |
| REVIEW | Independent reviewer | Final dirty candidate | Final snapshot | None | Verification | Complete |

## Integration Order

1. Establish history and complete consumer inventories.
2. Freeze the retained Distribution, motion, Lights, and experiment-build
   contracts.
3. Remove emissive support and compact shared CPU/HLSL/resource contracts.
4. Repair motion pacing and Lights initial presentation.
5. Add the opt-in default-settings experiment shader workflow.
6. Update contracts, product documentation, UI reference, and postmortem.
7. Build, test, runtime-check, independently review, repair, and archive.

## Verification Plan

| Acceptance Criterion | Evidence Required | Command or Experiment | Result or Artifact |
| --- | --- | --- | --- |
| Emissive removal complete | Whole-tree consumer and shader-count audit | `rg`, manifest expansion, resource review | Zero static permutations; PBR extension 48 to 32 bytes; visibility post-view constants 160 to 144 bytes; retired strings absent outside absence contracts |
| Distribution history correct | Exact Git introduction/default history | `git log -S`, `git show`, blame | Default 1.0 through `f98573e`; changed to 2.0 by `a5a75d5`; no product default 3.0 |
| Motion benchmark uncapped | Source/history proof plus live frame-rate observation | focused tests and labeled motion run | Sleep, 40 Hz target, and pacing state removed; exact 0.375 degrees per rendered frame and 256-sample schema retained; live run deferred while user PID 8584 owns the renderer |
| Lights state exact | Source contracts and live screenshot comparison | UI tests and labeled launch | Source/UI contracts pass; live comparison deferred while user PID 8584 owns the renderer |
| Experiment build minimal and safe | Exact manifest, missing-option behavior, startup smoke | opt-in configure/build/launch | 51 first-party tasks, 37 blobs, exact bundle contract pass, incompatible AA CLI exits 1; interactive startup deferred while user PID 8584 owns the renderer |
| Full build unaffected | Production and developer shader catalogs compile | normal Release builds | Production 311 + Bend 46 + SVSM 105 + CSM 54 compiled; isolated developer core compiled all 2,809 tasks |
| No broader regression | Focused contracts and full suite | CTest | 28/28 production and 28/28 experiment Release tests pass |
| Documentation valid | Title Case, line counts, links, diff | repository tools | 914 headings pass; README counts updated to 114,859 first-party and 502,481 total; final `git diff --check` passes |
| Independent review | Shader/UI/resource/build review | read-only handoff | Pass with no P0/P1 findings; four runtime/product-acceptance checks remain |

## Decisions

| Date | Decision | Reasoning and Rejected Alternatives | Tasks Affected |
| --- | --- | --- | --- |
| 2026-07-30 | Continue the current shader-retirement lineage | The requested cleanup directly extends the committed shader-retirement branch and its postmortem; starting from main would discard that accepted architecture | All |
| 2026-07-30 | Preserve first-Escape General-only behavior | The screenshot specifies the Lights drawer's contents and inner defaults, not automatic launch-time opening of Lights | Lights |
| 2026-07-30 | Keep the full catalogs as authoritative builds | A fast experiment profile must be explicit and opt-in so a local shortcut cannot silently become a production package | Experiment build |
| 2026-07-30 | Retain Distribution 2.0 | History proves 2.0 was an intentional progressive-strata change and no product revision defaulted to 3.0; Distribution remains runtime uniform | Visibility |
| 2026-07-30 | Remove emissive transport rather than hide its UI | The requested feature had zero permutation axes but still owned shader ABI, constants, metadata, masks, presets, history, UI, and tests; visible authored emission remains independent | Emissive |
| 2026-07-30 | Remove AA wall-clock pacing | The 25 ms sleep was the complete 40-fps ceiling; fixed per-rendered-frame motion retains the exact temporal sample path without throttling the renderer | Motion |
| 2026-07-30 | Lock the factory experiment profile fail closed | A short manifest without per-frame topology normalization, pass-construction guards, disabled settings, and CLI rejection could request missing binaries | Experiment build |
| 2026-07-30 | Keep work local | No commit, push, pull request, merge, or release was requested | All |

## Progress and Handoffs

| Date | Task and Owner | Status | Revision or Artifact | Checks | Risks or Next Action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 | Read-only explorers | Complete | Three distilled handoffs | Distribution/emissive consumers, AA pacing history, Lights and exact 51-task/37-blob experiment design audited | Incorporated by coordinator |
| 2026-07-30 | `/root` | Complete | Dirty local candidate and three isolated Release build trees | Production and experiment builds linked; both 28/28 CTest suites pass; all 2,809 developer-core tasks compile; CLI fail-closed check exits 1; docs, counts, and final static audit pass | Runtime product acceptance remains |
| 2026-07-30 | Independent reviewer | Complete | Read-only final diff review | No P0/P1 findings; emissive retention, ABI assertions, all deferred call sites, motion evidence, Lights defaults, and experiment fail-closed paths inspected | Runtime product acceptance remains |

## Risks and Escalation Triggers

- Emissive support may share a source-radiance buffer with retained indirect
  lighting or debug behavior; uncertain shared resources are not deleted.
- A minimal startup manifest can compile successfully yet fail when UI,
  benchmark, resize, or renderer topology selects an absent blob.
- The reported 40-fps benchmark may be a deliberate fixed-step or thermal
  safeguard rather than a merge regression; history must distinguish pacing
  from workload cost.
- Existing user interaction owns PID 8584; no agent will stop, replace, or
  automate that renderer.

Stop and ask the user if:

- Removing emissive contribution would require choosing between materially
  different remaining lighting defaults.
- A safe experiment build requires disabling user-facing choices in a way that
  materially changes the requested experiment workflow.
- Runtime verification remains blocked because the user-owned renderer stays
  active.

## Completion

- Final integrated commit: intentionally uncommitted unless separately
  authorized
- Verification summary: technically verified local candidate; normal and
  experiment Release builds pass 28/28 tests, developer core compiles 2,809
  tasks, exact bundles pass, and static audits are clean
- Independent review: pass with no blocking findings
- Coming Soon/documentation update: complete locally
- Pushed/PR/merged, or intentionally local: local
- Remaining experiments or follow-ups: launch the candidate after PID 8584 is
  closed; compare Lights with the supplied screenshot, inspect an emissive-only
  scene, run the uncapped motion benchmark, and smoke the experiment profile
- Active ownership released: yes; superseded ownership closed on 2026-08-03
- Archived to completed/abandoned path: `docs/exec-plans/abandoned/emissive-motion-lights-experiment-build.md`


## Archival Resolution

Superseded on 2026-08-03 after the retained emissive cleanup and default
history landed through commits `519306c` and `b83e7d3`. This engine cleanup
retires the factory experiment profile, antialiasing motion benchmark, and
SVSM/CSM presentation contract whose runtime checks remained unfinished.

The full historical evidence is preserved above. This archive does not claim
completion of any unchecked runtime, visual, performance, thermal, parity, or
product-acceptance criterion.
