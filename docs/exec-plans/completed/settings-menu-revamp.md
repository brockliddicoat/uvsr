# Settings Menu Revamp

## Status

- State: complete
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/settings-menu-revamp` at `C:\Users\brock\OneDrive\Documents\uvsr\work\settings-menu-revamp`
- Base commit: `0224649055f2218dcf1dbab4af4a1ea8a6b894f9`
- Started: 2026-08-14
- Last updated: 2026-08-14
- Archive: `docs/exec-plans/completed/settings-menu-revamp.md`

## Goal and Done Condition

Goal: Simplify and clarify the settings UI, repair its persistent edge shadows,
remove obsolete diffuse reconstruction choices, normalize denoiser choices and
labels, improve gated alpha presentation, reorganize sample accumulation, and
add a versioned copyable settings snapshot hash plus a decoder script.

Done when:

- [x] Every requested label, layout, option, visibility, and drawer behavior is
  implemented from the exact GitHub base without reviving unrelated features.
- [x] A 32-character settings snapshot identifier with a four-character version
  prefix is continuously visible in the settings header, copyable, and decoded
  by a checked script into the selected menu settings.
- [x] The settings-edge shadows are a new implementation that remains visible
  whenever the settings panel is expanded.
- [x] Targeted tests, a clean build, runtime smoke checks, and visual inspection
  pass for the exact candidate executable.

## Supersession

The user rejected the retained-row collapse presentation and the fixed viewport
shadows after inspecting this exact candidate. The follow-up plan
`settings-menu-collapse-material-followup.md` supersedes the shadow behavior,
snapshot schema `0001`, and gated-alpha numeric presentation recorded here.
This plan remains the historical evidence for the candidate that was tested.

## Scope

In scope:

- Material and Interface drawer row layout and material-control clarity.
- Denoising, Diffuse, path-tracing denoiser, ray-marching denoiser, Noise, Sky,
  color-picker alpha, and accumulation UI requested by the user.
- Removal of exposed diffuse reconstruction choices and relocation of Gaussian
  Bilateral and Joint Bilateral into every effect's denoiser selection.
- Complete replacement of the two settings-panel edge shadows.
- Versioned settings snapshot encoding, copy behavior, and decoder tooling.
- Required documentation, tests, build, runtime smoke, and visual evidence.

Non-goals:

- Publishing, pushing, opening a pull request, merging, releasing, or changing
  renderer algorithms beyond removal/routing of the named UI choices.
- General settings redesign outside the named drawers and shared conventions.
- Performance benchmarking or quality claims for denoisers.

Affected subsystems and paths:

- Settings UI and renderer settings state under `src/`.
- UI-focused tests and support scripts under `tests/`, `tools/`, or `scripts/`
  as repository structure requires.
- Current product documentation and this execution plan. This base has no
  `README.md` Coming Soon section to reconcile.

Shared hotspots reserved for the coordinator:

- `README.md`, root build files, settings serialization/version contract,
  settings UI source shared across drawers, and final build/runtime resources.

## Baseline

- Canonical repository/remote: `origin/main` at
  `0224649055f2218dcf1dbab4af4a1ea8a6b894f9` from live `git ls-remote`.
- Local versus remote state: isolated feature worktree is equal to live remote;
  the original checkout is intentionally untouched and is ahead 2/behind 52
  with unrelated untracked files.
- Verified source commit/build: source base is exact `0224649`; final local
  candidate is `build/settings-menu-revamp/bin/uvsr.exe`, SHA-256
  `326FC3D691BF8225EBA7A1004344204EFD93785D3B2A88C9A1D945CB3B7B3CD4`.
- GPU, scene, camera, resolution, and settings preset when relevant: visual UI
  smoke only; use a representative bundled scene and record the exact state.
- Known pre-existing failures: none established for this isolated base yet.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| UI architecture survey | Existing drawer/widget conventions and exact owners | Complete | Coordinator implementation |
| Denoiser/settings survey | Current enums, mappings, and runtime constraints | Complete | Coordinator implementation |
| Snapshot-format survey | Deterministic compact format and test/tool integration points | Complete | Coordinator implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- Snapshot text is exactly 32 lowercase hexadecimal characters; characters
  1-4 encode the settings format version and the remaining 28 are a
  deterministic fingerprint of the complete canonical settings export.
- Copying the fingerprint records its exact canonical export in the local UVSR
  snapshot catalog. The decoder resolves the copied fingerprint from that
  catalog and reports every represented setting without lossy bit packing.
- Any incompatible settings-layout or export-contract change must increment the
  version prefix.
- Existing renderer/shader contracts remain unchanged unless investigation
  proves that removing diffuse reconstruction requires dead-code cleanup.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| UI-SURVEY | Read-only explorer | Shared isolated worktree | `0224649` | None | None | Complete |
| DENOISE-SURVEY | Read-only explorer | Shared isolated worktree | `0224649` | None | None | Complete |
| HASH-SURVEY | Read-only explorer | Shared isolated worktree | `0224649` | None | None | Complete |
| SPATIAL-DENOISER | Isolated-path writer | Shared isolated worktree | Coordinator dirty state after enum contract | `src/denoising_pass.{h,cpp}`, `src/denoising_cb.h`, `src/denoising_spatial_cs.hlsl` | DENOISE-SURVEY | Complete |
| DIFFUSE-CLEANUP | Isolated-path writer | Shared isolated worktree | Coordinator dirty state after enum contract | Screen-space visibility implementation and shaders only | DENOISE-SURVEY | Complete |
| IMPLEMENT | `/root` | `codex/settings-menu-revamp` | `0224649` | Task-owned source, tests, tooling, docs | All surveys | Complete |
| REVIEW | Independent reviewer | Shared isolated worktree | Final dirty candidate | None | IMPLEMENT | Complete |

## Assignment Contracts

### UI-Survey: Map Drawer Layout and Shadow Behavior

- Owner/thread: read-only subagent assigned by `/root`
- Branch/worktree: shared isolated worktree, no Git operations
- Base commit/state: clean `0224649`
- Read scope: settings UI source/tests and existing performance header behavior
- Write scope: none
- No-touch scope: all files, Git state, builds, renderer process
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: report existing helpers and exact code paths;
  do not propose a competing settings format
- Deliverable: file/line map, conventions, likely defects, and test seams
- Done when: every material/interface/shadow/alpha/sky request is mapped
- Required verification: source trace only
- Allowed Git and external actions: read-only local commands
- Stop and report if: source differs from `0224649` or another writer appears
- Handoff revision/artifact: complete UI layout, shadow, picker, and retained-line source map
- Handoff acknowledged by/on: `/root`, 2026-08-14

### Denoise-Survey: Map Denoiser and Accumulation Controls

- Owner/thread: read-only subagent assigned by `/root`
- Branch/worktree: shared isolated worktree, no Git operations
- Base commit/state: clean `0224649`
- Read scope: denoiser enums, drawer rendering, runtime use, tests, and docs
- Write scope: none
- No-touch scope: all files, Git state, builds, renderer process
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: distinguish UI-only removals from runtime
  algorithm deletion; flag uncertain code deletion instead of assuming it
- Deliverable: exact option/mapping inventory and safe change recommendation
- Done when: every denoising/diffuse/noise/path/ray-marching request is mapped
- Required verification: source/reference trace only
- Allowed Git and external actions: read-only local commands
- Stop and report if: deletion dependencies are uncertain or state changes
- Handoff revision/artifact: complete denoiser, Diffuse, and accumulation runtime map
- Handoff acknowledged by/on: `/root`, 2026-08-14

### Hash-Survey: Design a Versioned Snapshot Contract

- Owner/thread: read-only subagent assigned by `/root`
- Branch/worktree: shared isolated worktree, no Git operations
- Base commit/state: clean `0224649`
- Read scope: settings state structures, current serialization/hash helpers,
  clipboard/UI utilities, script conventions, and tests
- Write scope: none
- No-touch scope: all files, Git state, builds, renderer process
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: exactly 32 lowercase hex characters with a
  four-character version prefix and deterministic round-trip decoding
- Deliverable: recommended encoding, coverage boundary, integration paths, tests
- Done when: a practical reversible contract is specified with capacity risks
- Required verification: capacity calculation and source trace
- Allowed Git and external actions: read-only local commands
- Stop and report if: requested reversibility is impossible within 112 payload
  bits without a material product choice
- Handoff revision/artifact: 220-value capacity inventory and retained-line integration trace
- Handoff acknowledged by/on: `/root`, 2026-08-14; exact fingerprint/catalog contract selected

### Implement: Integrate the Requested Settings Changes

- Owner/thread: `/root`
- Branch/worktree: `codex/settings-menu-revamp`
- Base commit/state: `0224649` plus accepted survey decisions
- Read scope: repository-wide as needed
- Write scope: task-owned source, tests, decoder tooling, current documentation
  including `assets/environments/README.md`, and this execution plan
- No-touch scope: `donut/`, generated `build/`, unrelated plans, binary assets,
  and original-checkout changes
- Build directory and runtime/GPU/resource lease: coordinator-only isolated
  build directory and one task-owned UVSR process
- Dependencies already integrated: none until surveys complete
- Interface/invariant contract: preserve runtime defaults; versioned hash must
  deterministically round-trip all represented settings
- Deliverable: complete local candidate and exact executable
- Done when: requested behavior is implemented and required evidence passes
- Required verification: focused tests, full relevant suite, clean build,
  runtime/UI smoke, and screenshot inspection
- Allowed Git and external actions: local edits/builds only; no commit or publish
- Stop and report if: user-owned collision, uncertain runtime deletion, or hash
  capacity requires excluding user-visible selected settings
- Handoff revision/artifact: full task-diff audit with five repair findings and
  explicit approval of rendering, deletion, snapshot, shadow, layout, picker,
  accumulation, label, and shader contracts
- Handoff acknowledged by/on: `/root`, 2026-08-14; all findings repaired

### Spatial-Denoiser: Implement Built-in Filters for Every Signal

- Owner/thread: `/root/spatial_denoiser`
- Branch/worktree: shared isolated worktree, no Git operations
- Base commit/state: `0224649` plus coordinator changes to
  `src/denoising_settings.h`
- Read scope: denoising pass/shaders, five call sites, raw texture contracts,
  and relevant tests
- Write scope: `src/denoising_pass.h`, `src/denoising_pass.cpp`,
  `src/denoising_cb.h`, and new `src/denoising_spatial_cs.hlsl`
- No-touch scope: `src/uvsr.cpp`, `src/denoising_settings.h`, `src/shaders.cfg`,
  CMake/root files, tests, docs, overrides, plans, Git state, builds, runtime
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: coordinator enum/helper/radius contract in
  `src/denoising_settings.h`
- Interface/invariant contract: Joint Bilateral and Gaussian Bilateral execute
  before the NRD branch in no-NRD builds, handle all five signal types and
  reduced AO/GI inputs, require no hit-distance or motion resource, preserve
  raw texture format and full-resolution output, own no temporal history, and
  safely release/rebind resources across method/format/input changes
- Deliverable: source implementation only plus exact shaders.cfg/test guidance
- Done when: the first-party branch is internally complete for R8/R16/R32 and
  RGBA16/RGBA32 inputs and third-party behavior remains unchanged
- Required verification: source inspection and any compile-free checks; do not
  configure or build while the coordinator is writing
- Allowed Git and external actions: assigned file edits only; no staging,
  commit, network, build, or runtime
- Stop and report if: the stable interface is insufficient, an output encoding
  cannot be preserved, or any owned file has unexpected changes
- Handoff revision/artifact: first-party five-format spatial pass and shader implementation
- Handoff acknowledged by/on: `/root`, 2026-08-14; ownership released

### Diffuse-Cleanup: Delete Selectable Reconstruction Paths

- Owner/thread: `/root/diffuse_cleanup`
- Branch/worktree: shared isolated worktree, no Git operations
- Base commit/state: `0224649` plus coordinator changes to
  `src/denoising_settings.h`
- Read scope: Screen-space visibility implementation, shaders, call sites,
  shader registry, tests, and relevant documentation
- Write scope: `src/screen_space_visibility.h`,
  `src/screen_space_visibility.cpp`, `src/screen_space_visibility_cb.h`,
  `src/screen_space_visibility_cs.hlsl`, and
  `src/screen_space_visibility_filter_cs.hlsl`
- No-touch scope: `src/uvsr.cpp`, `src/ui_settings_command_catalog.h`,
  `src/shaders.cfg`, CMake/root files, tests, docs, overrides, plans, Git state,
  builds, and runtime
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: none
- Interface/invariant contract: remove the selectable reconstruction settings
  and packed-edge path end to end while preserving one unconditional
  guide-aware full-resolution upsample whenever Diffuse resolution is reduced;
  full-resolution Diffuse must bypass reconstruction; AO/GI output formats and
  downstream contracts must remain unchanged
- Deliverable: source deletion only plus exact `shaders.cfg`, call-site, test,
  and documentation guidance
- Done when: no reconstruction enum/filter/radius/packed-edge implementation
  remains in the assigned files and the required reduced-resolution upsample is
  the sole reconstruction path
- Required verification: source inspection and compile-free checks; do not
  configure or build while the coordinator is writing
- Allowed Git and external actions: assigned file edits only; no staging,
  commit, network, build, or runtime
- Stop and report if: packed data is still required by any non-reconstruction
  consumer, the guide-aware upsample cannot preserve existing output contracts,
  or any owned file has unexpected changes
- Handoff revision/artifact: selectable reconstruction and packed paths removed; automatic upsample retained
- Handoff acknowledged by/on: `/root`, 2026-08-14; ownership released

### Review: Independently Audit the Candidate

- Owner/thread: independent read-only subagent assigned after implementation
- Branch/worktree: shared isolated worktree, no Git operations
- Base commit/state: final task-owned dirty candidate
- Read scope: full task diff, relevant tests, and acceptance list
- Write scope: none
- No-touch scope: all files, Git state, builds, renderer process
- Build directory and runtime/GPU/resource lease: none
- Dependencies already integrated: IMPLEMENT
- Interface/invariant contract: review correctness, dead paths, deterministic
  snapshot decoding, UI consistency, and requested deletion scope
- Deliverable: prioritized findings with exact paths/lines or explicit approval
- Done when: every request is mapped to code and tests
- Required verification: diff/source review and test-result audit
- Allowed Git and external actions: read-only local commands
- Stop and report if: implementation is still changing
- Handoff revision/artifact: full dirty-candidate audit with no P0/P1 findings
  and five repaired P2/P3 findings
- Handoff acknowledged by/on: `/root`, 2026-08-14

## Integration Order

1. Complete and reconcile the three read-only surveys.
2. Implement the disjoint built-in spatial denoiser branch and Diffuse cleanup
   while the coordinator implements shared UI and snapshot work.
3. Integrate shader registration and call-site/UI/command routing after both
   rendering handoffs.
4. Apply remaining drawer-specific removals, moves, labels, and layout changes.
5. Add focused regression coverage and decoder tests.
6. Freeze writes, run independent review, repair findings, then build and inspect.
7. Reconcile documentation and archive this plan at closeout.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Requested source behavior | Focused test assertions and source trace | Repository-discovered targeted tests | Final 45-of-45 CTest matrix passed |
| Snapshot round trip | Known vectors, determinism, and decoder output | Decoder/unit tests plus exact-build copy | C++ vectors and Python tests passed; exact copied `00012112b360c8f8e4d80e750e83a414` decoded 220 settings with version `0001`, `ui.visible=on`, and `visibility.radius=3` |
| Build correctness | Successful clean candidate build | Repository build command | Full Release all-target build passed; exact executable SHA-256 `326FC3D691BF8225EBA7A1004344204EFD93785D3B2A88C9A1D945CB3B7B3CD4` |
| Runtime/UI correctness | Successful launch plus drawer screenshots | Task-owned runtime smoke | Exact rebuilt executable launched as PID 31720 at Sponza Position 1; settings code, shadows, retained collapsed row, reorganized drawers, wrapped footer, and all four built-in bilateral routes were exercised without a modal or crash |
| No stale removed UI | Repository search and independent review | `rg` plus diff review | Searches, diff hygiene, Title Case validation, and independent review passed |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-14 | Use exact live `origin/main` commit `0224649` in a new isolated branch/worktree | The original checkout is diverged and contains unrelated work | All |
| 2026-08-14 | Use one coordinator writer with parallel read-only surveys | Core drawers share UI source and settings contracts, so competing writers would create unsafe overlap | All |
| 2026-08-14 | Use a versioned fingerprint plus an exact copy-time snapshot catalog | The 216+ post-change values include arbitrary floats and dynamic selections and cannot fit reversibly in 112 bits; catalog lookup preserves the required length and exact script output without omitting settings | HASH-SURVEY, IMPLEMENT |
| 2026-08-14 | Keep the ratio/MSAA candidate isolated and serialize any later integration | Its coordinator reported a separate `0224649` worktree with overlapping `src/uvsr.cpp` and settings-catalog changes; neither task should absorb the other implicitly | IMPLEMENT, future integration |
| 2026-08-14 | Keep the ratio-task TAA edge-shake repair isolated as well | That task may touch TAA, sample-accumulation code, `src/uvsr.cpp`, focused tests, and docs. This task changes accumulation presentation only; eventual composition still needs one semantic integrator from the common `0224649` base | IMPLEMENT, future integration |
| 2026-08-14 | Resolve the snapshot catalog through Windows' writable `FOLDERID_LocalAppData` and search canonical plus packaged stores in the decoder | A packaged child cannot write outside its package-local store; the fallback preserves exact decoding for both ordinary and Codex-packaged launches | IMPLEMENT |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-14 | `/root` | Complete | Local branch/worktree candidate based on `0224649` | Live remote and local identity confirmed; no publication action taken | Ownership released |
| 2026-08-14 | UI-SURVEY | Complete | Read-only handoff | Material/Interface, shadow, alpha, Sky, and retained-header seams mapped | Ownership released |
| 2026-08-14 | DENOISE-SURVEY | Complete | Read-only handoff | Runtime routes, data gates, and safe deletion boundary mapped | Ownership released |
| 2026-08-14 | HASH-SURVEY | Complete | Capacity inventory and integration trace | Proved reversible 112-bit packing impossible; fingerprint/catalog contract selected | Ownership released |
| 2026-08-14 | SPATIAL-DENOISER | Complete | Five-format first-party compute path | Source self-check passed; coordinator packaging and call-site integration complete | Ownership released |
| 2026-08-14 | DIFFUSE-CLEANUP | Complete | Net removal of selectable reconstruction and packed paths | Source self-check passed; automatic reduced-resolution upsample retained | Ownership released |
| 2026-08-14 | `/root` implementation | Complete | UI, commands, decoder, docs, tests, packaging, and runtime repairs composed | Full Release all-target build, final 45-of-45 CTest, exact-build 220-setting copy/decode, and visual smoke passed | Local candidate ready for user review |
| 2026-08-14 | REVIEW | Complete | Independent read-only full-diff audit | No P0/P1 findings; five P2/P3 findings repaired | Ownership released and handoff acknowledged |

## Risks and Escalation Triggers

- Snapshot lookup depends on the copy-time local catalog; the decoder must fail
  clearly for an unknown hash and accept an explicit catalog path for sharing.
- The separate ratio/MSAA candidate plans a default-on Multisample Adaptive
  control and overlaps `src/uvsr.cpp`, `src/ui_settings_command_catalog.h`,
  Heitz CPU/HLSL, related tests, and documentation. This worktree must remain
  isolated; eventual composition requires one integrator and semantic conflict
  resolution from the common `0224649` base.
- That task is also repairing a violent TAA edge-shake regression and may touch
  TAA/sample-accumulation behavior, `src/uvsr.cpp`, focused tests, and docs. Do
  not import, rebase, or infer compatibility from textual conflict freedom.
- Visual UI verification needs one controlled renderer process and may expose
  pre-existing launch/environment issues unrelated to this patch.

Stop and ask the user if:

- Deleting reconstruction code would remove behavior still used outside Diffuse.
- A material layout choice remains ambiguous after matching established menus.

## Completion

- Final integrated commit: none; user did not authorize a commit
- Verification summary: full Release all-target build, final 45-of-45 CTest,
  README line-count contract, exact-build copy/decode of all 220 settings,
  runtime/UI smoke, diff hygiene, and Title Case checks passed
- Independent review: complete with no P0/P1 findings; all five P2/P3 findings
  repaired before final verification
- Coming Soon/documentation update: this base has no Coming Soon section;
  current settings, denoising, Diffuse, Noise, path-tracing, environment, and UI
  integration documentation is updated
- Pushed/PR/merged, or intentionally local: intentionally local unless the user
  later authorizes publication
- Remaining experiments or follow-ups: the independent ratio/MSAA and TAA
  edge-shake work remains isolated and needs serialized semantic composition
  before any future integration
- Active ownership released: yes
- Archived to completed/abandoned path: completed at
  `docs/exec-plans/completed/settings-menu-revamp.md`
