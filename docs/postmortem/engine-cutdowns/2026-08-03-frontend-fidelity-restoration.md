# Front-End Fidelity Restoration

## Technical Summary

The August 3 candidate restores the useful information density and initial
presentation of the pre-cutdown interface without restoring any retired
renderer route, resource family, shader permutation, benchmark planner, or
shadow technique. The most visible result is a detailed one-effect Statistics
panel, an expanded Debug drawer, a resolution-aware Visibility Reconstruction
disclosure, and a command interface whose guidance and concise results remain
inside its single reserved row. Long results leave that row only through a
deliberately opened, bounded details view.

This is a successor to the August 2 front-end-restoration addendum, not a
revision of its artifact. The July 30 shader report and August 2 engine cleanup
retain their original dates, counts, hashes, behavior, and confidence boundaries.
This report owns only the August 3 follow-up.

The candidate is local and uncommitted on `codex/engine-core-cleanup` over base
commit `f7c0c87d8cba6880428fbc34400eb2882fb5182e`. It is technically verified but
is not Canonical verified, merged, published, or a controlled performance
result.

## Report Identity

| Field | Value |
| --- | --- |
| Completion date | 2026-08-03 |
| Scope | Front-end fidelity over the retained August 2 backend cutdown |
| Branch | `codex/engine-core-cleanup` |
| Base commit | `f7c0c87d8cba6880428fbc34400eb2882fb5182e` |
| Prior artifact | `build-frontend-restoration/bin/uvsr.exe` |
| Prior artifact SHA-256 | `58B58FBD643859D12CFB9A746BD5FA563048CB1F569B56449E31FBE6A5F82458` |
| Replacement artifact | `build-frontend-fidelity/bin/uvsr.exe` |
| Replacement artifact SHA-256 | `CF8D6519939D30CFB203862F6C3BDC411E23E9D608A484D1885539EF92B38EB6` |
| Replacement artifact size | 2,500,096 bytes |
| Replacement artifact write time | `2026-08-03T06:19:27.3007215Z` |
| Frozen build-input diff identifier | `b3ebf1493c768697f14b9bb2caccc4a2c1010058` |
| Untracked build inputs | None in the recorded build-input scope |
| Completed execution plan | `docs/exec-plans/completed/frontend-fidelity-followup.md` |
| Publication state | Local only; no commit, push, pull request, merge, or release requested |

## Revert-Sensitive Changes First

The changes below are ordered by the likelihood that a user might want to tune
or revert their presentation. None requires undoing the backend cleanup.

### Detailed Statistics Presentation

This is the largest intentional reversal of the first compact frontend pass.
The effect dropdown remains one-at-a-time so the panel does not become a wall of
data, but the selected effect now uses the labeled, striped two-column
presentation that made the pre-cutdown interface readable.

The selector again has a visible **Effect** label, its reset arrow beside that
label, a full-width value field, and default focus on the active choice. The six
general values remain on one compact dash-separated line because that earlier
condensation was explicitly accepted.

The retained views are:

| Effect | Restored Presentation | Availability Rule |
| --- | --- | --- |
| Complete Renderer | Complete Frame, Scene Setup and Clears, Geometry, Closest Surface Resolve, Direct Lighting, Screen-Space Visibility, Material Picking, Environment Background, Tone Mapping, and Output Blit | Each row requires its own completed renderer timer query |
| Ordinary renderer stage | Selected stage plus Complete Renderer Frame for context | Each row requires its own completed renderer timer query |
| Screen-Space Visibility | Complete Effect, First Trace, Reconstruction, Composition, named-stage total, unattributed timer difference, three memory groups, dispatches, read resources, and write resources | Requires an active Visibility pass and a completed effect query |
| Screen-Space Shadows | Support status, Trace, dispatches, work groups, samples, and output memory | Requires the retained shadow pass to be active, supported, and query-complete |
| Temporal Reconstructive | Complete Effect, three timing stages, four history-memory measures, effective cost, minimum history formats, history validity, accumulated frames, resets, dispatches, and history sample/access counts | Requires Temporal Reconstructive to be enabled and its submitted query to complete |
| Conservative Morphological | Complete Effect, Edge Detection, Candidate Processing, and Apply | Requires the technique to be enabled and all current queries to complete |
| Multisample | Status, requested samples, hardware-active samples, Geometry, Direct Lighting, and Closest Surface Resolve | Uses the actual raster sample count and rejects the timing rows when the format topology falls back to one sample |

Typed effect timing structures are copied into the existing periodic Statistics
snapshot. This replaces the lossy preformatted strings without creating a new
producer, planner, readback, resource, or timer. An unavailable row displays
`--` or a direct status; it never presents a stale or fabricated `0.000 ms` as
measured work.

Among retained effects, one data-backed presentation is intentionally not
restored: the four
environment mean-radiance rows. The retained runtime no longer persists the
prepared environment's average luminance after upload. Restoring those rows
would require new persistent renderer state and getters, exceeding a
presentation-only fidelity pass. The omission is preferable to inventing a
value or reviving backend surface solely for Statistics.

If this Statistics restoration is later reduced, preserve the typed snapshot,
per-row availability gates, complete-frame context, actual multisample count,
and one-effect selector. The safe rollback boundary is presentation density,
not timing correctness.

### In-Input Command Results

The floating result window above the command row is removed. It could cover
Settings controls and changed height according to result length. The command
interface now has exactly one visual row:

- Empty guidance reads `Try help / Enter applies / Tab completes / Up/Down
  history / Slash closes`.
- The slash separators replace the denser dash separators without changing
  command parsing.
- A completed command displays `Success: ...` in green or `Error: ...` in red
  inside the empty input.
- The result disappears when the next edit begins.
- A long or multiline result adds a tiny trailing details button. The button
  opens a bounded, scrollable, selectable read-only view only when clicked.
- Up and Down history recall, Tab completion, Enter submission, Slash close,
  and Escape ownership remain unchanged.

The result uses the input hint surface rather than copying feedback into the
editable command buffer. This preserves immediate typing, parser behavior, and
history contents while satisfying the one-row visual contract.

The first line of a long `list` or help response can be clipped in the compact
input, but the deliberate details button retains access to the complete text
without automatic overlap. Reintroducing an automatically opened multiline
popup would restore the original problem.

### Expanded Debug Defaults and Neutral Labels

Debug now starts expanded, and its World, Visibility, Physically Based
Lighting, and Screen-Space Shadows groups also start expanded. ImGui's stored
disclosure state takes ownership after a user clicks a header, so collapsing a
group remains persistent while the parent drawer is reopened.

The three selectors that formerly displayed **Final Image** now display
**Default**. **Default** is more accurate than **None** because ordinary world
rendering remains active; only the diagnostic replacement is inactive. World
continues to display **Scene**, which describes its material presentation more
precisely than either neutral term.

This rename is intentionally presentation-only. The following contracts do not
change:

- `VisibilityDebugView::FinalImage` remains the normal Visibility state.
- `PbrLightingDebugView::None` remains the normal lighting-filter state.
- `ScreenSpaceShadowIsolationView::None` remains the normal shadow state.
- The stable command value `final` remains accepted for existing scripts.
- Debug choices remain composable and do not enable or disable unrelated
  physically based lighting, sky, light, shadow, Visibility, or aliasing state.

The narrow rollback boundary is the three label strings and five initial-open
flags. Runtime enums, command tokens, shader constants, resources, and
composition order must not be renamed as part of a visual-copy revert.

### Resolution-Aware Reconstruction Disclosure

Visibility Reconstruction now starts collapsed when Visibility traces at full
resolution, because the default route consumes an already full-resolution
field. It starts expanded for half- or quarter-resolution tracing, where the
reconstruction decision is immediately relevant.

The behavior uses a conditional `ImGuiTreeNodeFlags_DefaultOpen` value, and the
Reconstruction header has no every-frame `SetNextItemOpen` call. The default is
therefore only an initial suggestion; ImGui can retain the user's later header
state instead of this code forcing it on every frame.

The disclosure does not select a reconstruction method, allocate a resource,
change a profile, or invalidate rendering history. A future revert should
change only the initial disclosure flag.

## Backend Cutdown Remains Intact

No August 3 source change restores any of the following retired features:

- Visibility-owned temporal accumulation or its dormant history textures;
- sample resurrection and its production permutations;
- recursive or multiple-bounce indirect diffuse;
- a Visibility depth hierarchy;
- packed/fused ambient-occlusion-only profiles;
- reference/runtime Visibility planners or factory experiment profiles;
- Visibility benchmark runners, schemas, exports, or thermal support;
- sparse virtual shadow maps or diagnostic cascaded shadow maps;
- dead forward-rendering modes;
- high-dynamic-range Conservative Morphological permutations;
- monolithic mutually exclusive anti-aliasing selection;
- the transparent shadow edge overlay; or
- Packed Depth reconstruction.

The renderer still exposes one deferred DirectX 12 route, one current-frame
Visibility pass serving Ambient Occlusion plus one-bounce indirect diffuse, one
Screen-Space Directional Shadow technique, independently composable temporal,
morphological, and multisample aliasing techniques, and the retained debug
views.

## Source and Interface Change Map

| Path | Change | Backend Effect |
| --- | --- | --- |
| `src/uvsr.cpp` | Restored Statistics table hierarchy, retained typed timing snapshots, disclosure defaults, Debug copy, slash guidance, colored in-input results, and an explicit long-result details view | No shader, binding, resource, enum, command-domain, or render-pass change |
| `tests/ui_source_contract_tests.cpp` | Added exact labels, conditional initial disclosure, table hierarchy, availability, retired-surface, command-row, and explicit-details contracts | Test-only |
| `README.md` | Updated current-candidate summary and generated line counts | Documentation only |
| `docs/advanced-settings.md` | Documented current Statistics, Debug, Reconstruction, and command behavior | Documentation only |
| `docs/screen-space-visibility.md` | Documented conditional Reconstruction disclosure, Default label, and retained Statistics rows | Documentation only |
| `docs/pbr-foundation.md` | Updated current Debug selector copy | Documentation only |
| `docs/ui-integration-agent-procedure.md` | Advanced the canonical UI contract to `2026-08-03.1` | Documentation only |
| `docs/postmortem/engine-cutdowns/README.md` | Added this separately dated artifact boundary | Documentation only |
| `docs/postmortem/engine-cutdowns/2026-08-03-frontend-fidelity-restoration.md` | Recorded the revert-sensitive change ledger, frozen artifact identity, evidence, confidence, and remaining cutdown opportunities | Documentation only |
| `docs/exec-plans/completed/frontend-fidelity-followup.md` | Preserved scope, ownership, decisions, verification, and closeout | Documentation only |

## Count and Permutation Comparison

Documentation, assets, licenses, binaries, and generated build content are
excluded from line counts. First-party counts include source, tests, tools,
build scripts, and final first-party dependency overrides. Physical
`src/uvsr.cpp` lines include blanks and comments, so they are not directly
additive with the nonblank repository count.

| Measure | Pre-Cutdown Baseline | August 2 Restoration | August 3 Fidelity Candidate | Net From Baseline |
| --- | ---: | ---: | ---: | ---: |
| First-party nonblank source lines | 145,256 | 64,030 | 64,374 | 80,882 fewer (55.68%) |
| `src/uvsr.cpp` physical lines | 33,577 | 17,029 | 17,219 | 16,358 fewer (48.72%) |
| Third-party nonblank source lines | 388,207 | 388,207 | 388,207 | Unchanged |
| Total nonblank source lines | 533,463 | 452,237 | 452,581 | 80,882 fewer (15.16%) |
| Core first-party shader tasks | 823 production / 3,033 developer | 268 | 268 | 555 fewer than production (67.44%) |
| Screen-Space Directional Shadow tasks | Included in broader historical catalogs | 46 | 46 | Current specialist catalog unchanged |
| Integrated current shader tasks | 899 production | 390 | 390 | 509 fewer (56.62%) |
| Runtime shader package files | Historical package differs | 39 | 39 | Current exact package unchanged |
| Command catalog entries | Historical catalog differs | 122 | 122 | Current UI parity unchanged |

The August 3 fidelity work spends 344 first-party nonblank source lines over the
August 2 restoration candidate. That is a 0.54 percent increase relative to
64,030 lines. The added presentation is backed by data already retained by the
renderer and does not add shader tasks or runtime package files.

## Verification Evidence

| Check | August 3 Result |
| --- | --- |
| Fresh Visual Studio 2022 x64 configure | Passed in `build-frontend-fidelity` |
| Complete Release all-target build | Passed |
| Core shader compilation | Passed, 268 of 268 tasks |
| Screen-Space Directional Shadow compilation | Passed, 46 of 46 tasks |
| Integrated shader catalog | 390 tasks, including Donut's 76 retained tasks |
| Focused UI source contract | Passed |
| Full CTest | Passed, 30 of 30 tests |
| Runtime shader package | Passed the exact 39-file contract |
| Debug and Reconstruction source contracts | Passed exact labels and conditional initial-default flags; source inspection confirms no per-frame Reconstruction override |
| Statistics source contract | Passed labels, full stage rows, effect tables, query gates, actual multisample topology, and retired-surface negatives |
| Command source contract | Passed slash hint, in-input status, green/red colors, edit dismissal, no automatic floating result bar, deliberate long-result details, and Up/Down recall |
| Exact-artifact live smoke | Passed the exercised presentation checks recorded below |
| Independent review | Passed; no unresolved priority-zero through priority-two source findings, and all report-audit findings were incorporated |
| Documentation and diff hygiene | Passed the final checks recorded below |

## Final Artifact Evidence

The final build inputs are identified by the output of:

```text
git diff --binary --no-ext-diff HEAD -- CMakeLists.txt LaunchUVSR.cmd src tests tools overrides | git hash-object --stdin
```

That frozen content identifier is
`b3ebf1493c768697f14b9bb2caccc4a2c1010058`. The same scoped
`git ls-files --others --exclude-standard` query returned no untracked build
inputs. This identifier distinguishes the dirty source snapshot used for the
build; it is not a stored Git object, a commit, or a recoverable patch.

The frozen source completed a fresh Visual Studio 2022 x64 configure and a
complete Release all-target build in `build-frontend-fidelity`. Compilation
completed all 268 core shader tasks and all 46 Screen-Space Directional Shadow
tasks. The integrated catalog remained 390 tasks, the runtime package contained
exactly 39 shader binaries, and the fresh full CTest run passed 30 of 30 tests.

The exact executable is
`build-frontend-fidelity/bin/uvsr.exe`, 2,500,096 bytes, written at
`2026-08-03T06:19:27.3007215Z`, with SHA-256
`CF8D6519939D30CFB203862F6C3BDC411E23E9D608A484D1885539EF92B38EB6`.
It launched through the task launcher as UVSR Renderer D3D12 on the NVIDIA
GeForce RTX 4090 Laptop GPU at 1920 by 1080, using Sponza Decorated and
Benchmark Position 1 in the Amp interface.

The exact-artifact exercise observed Debug and all four nested effect groups
initially expanded, World displaying `Scene`, and the other three neutral
selectors displaying `Default`. Visibility Reconstruction was collapsed at
Full Resolution and expanded after selecting Half Resolution. Complete Renderer
Statistics displayed the visible `Effect` label, full-width selector, striped
table, complete-frame context, and populated renderer-stage timings. The command
row was not manipulated after the computer-control guard detected live user
input in the window; its slash guidance, green/red status, edit dismissal,
history recall, removed automatic bar, and explicit details view are covered by
the passing focused source contract and full test run instead.

Independent frozen-source review found no unresolved priority-zero through
priority-two issue. Independent report review recomputed every line-count delta,
percentage, and the weighted confidence result, then its build-identity,
evidence-boundary, source-map, and wording findings were incorporated. Final
validation passed the README count checker, conventional Title Case scan,
scoped local-link scan, `git diff --check`, and stale-living-document searches.

## Confidence in Remaining Features

| Remaining Area | Weight | Confidence | Evidence |
| --- | ---: | ---: | --- |
| Core build, package, and launch | 20% | 98% | Fresh complete Release build, exact shader/package contracts, exact-artifact launch |
| Visibility and reconstruction | 20% | 96% | Reference/source tests, all current shaders, resolution-aware disclosure contract, representative runtime exercise |
| Physically based lighting and scenes | 20% | 95% | Full tests, deferred composition contracts, bundled-scene runtime exercise |
| Aliasing | 15% | 95% | Temporal, morphological, multisample, and independent-composition contracts |
| Shadows and Debug | 15% | 96% | All 46 shadow tasks, focused tests, expanded composable Debug exercise |
| Interface and command catalog | 10% | 98% | Detailed Statistics, 122-entry parity, focused UI contracts, and exact-artifact presentation exercise |

The evidence-weighted average confidence in the remaining features is 96.25
percent, reported as **96.3 percent**. This is confidence in retained feature
correctness for the local candidate, not a probability of performance
improvement and not Canonical verification.

Residual uncertainty is concentrated in exhaustive visual quality across every
scene/camera/setting combination, long-duration resource behavior, adapters
other than the exercised DirectX 12 device, and controlled complete-frame
performance comparisons.

## Remaining Cutdown Opportunities

No item below was implemented in this follow-up. Each requires a fresh dirty-
state ownership and consumer audit before deletion.

| Rank | Candidate | Confidence | Why It May Be Redundant | Guardrail Before Removal |
| ---: | --- | ---: | --- | --- |
| 1 | Replace command-catalog exemption arrays with direct assertions | High | Navigation and telemetry exemption arrays appear to serve structural tests rather than runtime behavior | Prove every entry has no runtime consumer, retain explicit action exceptions, rerun 122-entry parity and completion ordering |
| 2 | Remove the secondary command-binding mirror | High | Completion candidates are rebuilt through a second constexpr surface over the authoritative catalog | Iterate the catalog directly while preserving action filtering, supported verbs, order, and completion tests |
| 3 | Reconcile experiment-label launcher plumbing | Medium | The launcher still requires an experiment value after renderer title labeling was retired | Decide whether process/window identity remains required, then update launcher, guide, wrappers, and smoke contracts together |
| 4 | Consolidate repeated Statistics row calls into typed descriptors | Medium | The restored table is intentionally explicit but some row boilerplate can be data-driven | Preserve row order, per-row availability, text-versus-number formatting, and source readability; reject abstractions that hide provenance |
| 5 | Audit remaining UI-only compatibility helpers | Medium | Some animation/layout helpers may have only one current caller after the drawer cutdown | Verify both Amp and OG behavior, popup lifetimes, focus, scrolling, and source-contract ownership before merging helpers |
| 6 | Audit retained Donut shader tasks against UVSR runtime loading | Low to Medium | The integrated Donut bundle still contains generic passes outside UVSR's first-party manifest | Trace runtime shader lookups and dependency expectations first; do not edit pinned Donut source or remove tasks solely from filename inference |

## Restoration and Rollback Boundaries

To reduce Statistics again, edit only its table composition and current UI
contract. Do not remove timer availability, typed snapshots, or effect timing
fields unless the renderer producer is independently retired end to end.

To change the multiline command-result surface, keep the one-row command
layout, history buffer, parser, and result state intact. Preserve explicit user
invocation and a bounded view rather than reviving the automatic floating
window.

To change Debug initial openness or neutral copy, edit only the user-facing
flags and strings. Preserve runtime enum values and the command value `final`.

To change Reconstruction initial openness, retain ImGui's stored-state ownership.
Do not use an every-frame `SetNextItemOpen` call that overwrites user choice.

For any backend feature retired on August 2, follow the complete dependency and
restoration ledger in
[Engine Core Cutdown and Restoration Report](2026-08-02-engine-core-cleanup.md).
For the earlier shader-only phase, follow
[Shader Permutation Cutdown](2026-07-30-shader-permutation-cutdown.md). A
frontend rollback does not authorize restoration of either retired backend.

## Recommended Next Steps

1. Obtain product acceptance on the exact August 3 executable and observed
   Settings state before considering the frontend fidelity settled.
2. Keep the candidate local until any desired visual adjustments are made;
   every artifact-changing edit invalidates the current live acceptance.
3. If further cutdown work resumes, start with the command catalog mirrors,
   because they offer a plausible line reduction without renderer-quality risk.
4. Treat Donut shader reduction as a separate dependency-packaging project with
   runtime lookup evidence, not as a continuation of this UI pass.

## Further Questions

- Should Visibility resource rows remain inline for immediate inspection, or
  return under a default-closed **Resource Footprint** subgroup?
- After product acceptance, should the Statistics table descriptor opportunity
  be explored, or is the explicit row code easier for humans and agents to
  audit?

## Final Interface Refinement and Publication Addendum — August 3, 2026

This addendum preserves the final interface changes made after the `CF8D...`
artifact above. It does not rewrite that earlier artifact's evidence. The items
are ordered by revert sensitivity: preset identity and layout first, neutral
copy and disclosure next, then command and maintenance plumbing.

### Acceptance and Artifact Boundary

| Field | Final Accepted Candidate |
| --- | --- |
| Exact executable | `build-custom-default-copy/bin/uvsr.exe` |
| SHA-256 | `194D1637CC271330D3CC3C800F723639D7A17ABEBB97DE93DE018C79752D11EF` |
| Size | 2,516,480 bytes |
| UTC write time | `2026-08-03T08:08:44.9587581Z` |
| Artifact-producing build-input diff | `8a2a64d412d19828567b159dca64363f7dfa843e` |
| Untracked build inputs | None |
| Product acceptance | The user stated that this exact most recent build was verified and requested it be published to `main` as the new Canonical build |

The acceptance is scoped to the requested cleanup and interface behavior. The
artifact's scene, camera, resolution/window state, interface skin, and complete
settings were not recorded, so this report attaches no broader exact-scene
visual-quality or performance claim. The later clean integrated smoke records a
separate known runtime state; it does not silently replace this accepted
artifact.

### Revert-Sensitive Final Refinements

| Priority | Surface | Final Behavior | Safe Revert Boundary |
| ---: | --- | --- | --- |
| 1 | Quality identity | Changing any Algorithm value appends `(Custom)` to the selected Quality preset; selecting or resetting a Quality preset clears those overrides; preset-equivalent values reattach automatically | Keep the underlying quality preset and per-setting override ownership together |
| 2 | Temporal Cost identity | Changing any Cost value appends `(Custom)` to the selected Temporal Cost preset with the same select, reset, and reattachment rules | Do not merge Quality and Temporal Cost override state |
| 3 | Visibility identity | A setting change preserves the nearest profile and appends `(Custom)`; the adjacent reset arrow restores that profile | Preserve the profile baseline and reset placement even if layout changes |
| 4 | Dropdown copy | Provenance suffixes and duplicate inherited options are removed; only `(Automatic)` and `(Custom)` remain | Reintroduce an option only when it selects distinct behavior |
| 5 | Temporal layout | Stationary Bypass is first under Algorithm; Cost replaces Behavior; the Stationary Bypass selector retains both prior choices | Preserve the collapsible technique and Advanced animations |
| 6 | World and Debug defaults | Debug starts expanded; World displays `Default`; neutral information selectors display `Default` | Keep internal enum and command values stable when changing user-facing copy |
| 7 | Reconstruction disclosure | Full Resolution starts collapsed; reduced-resolution reconstruction starts expanded | Let ImGui retain the user's later disclosure choice |
| 8 | Command row | Slash-separated guidance, colored in-input success/error, no automatic history/result bar, and explicit long-result details preserve the one-row height | Keep parsing, completion, recall, and the full result buffer independent from presentation |

These refinements also retain the restored Buffers drawer, complete one-effect
Statistics selector and breakdown, labels beside every dropdown, hover
tooltips, animated nested groups, compact dash-separated summary line, and the
one-word **Aliasing** drawer title.

### Post-Acceptance Non-Binary Cleanup

The renderer no longer reads `UVSR_EXPERIMENT`, but the launcher still required
and exported that dead value after the accepted executable was built. The final
source removes the unused experiment argument from `LaunchUVSR.cmd` and
`tools/launch_uvsr.ps1`, disables positional binding so renderer arguments
cannot be consumed as a build path, and keeps exact `-BuildDirectory` launch
ownership. Policy now verifies the commit embedded in the retained
`UVSR Renderer D3D12 (<commit>)` title.

This cleanup changes launcher, policy, and restoration guidance only. It does
not change renderer source or the accepted executable's bytes. Its provisional
post-cleanup build-input diff identifier is
`d0b7279b3242493ebbdb1b4fbaa1e7f72ae4b77e`; the committed tree and clean
canonical executable own the final source evidence.

### Final Source and Evidence Map

| Path | Final Responsibility |
| --- | --- |
| `src/uvsr.cpp` | Preset Custom/reset ownership, final dropdown copy and layout, Debug/World defaults, conditional Reconstruction disclosure, detailed Statistics, and compact command presentation |
| `src/screen_space_directional_shadows_cb.h` | Removes the retired transparent edge-overlay constant-buffer contract |
| `src/screen_space_directional_shadows_cs.hlsl` | Removes the retired transparent edge-overlay shader output path |
| `tests/ui_source_contract_tests.cpp` | Locks labels, duplicate-option absence, Custom propagation, layout, disclosure, Statistics, command, and overlay retirement contracts |
| `LaunchUVSR.cmd` and `tools/launch_uvsr.ps1` | Launch the exact isolated build without the dead experiment-label interface |
| `docs/visibility-sample-rotation-v1.md` | Keeps retired experiment restoration guidance compatible with the current launcher and single-bounce product |
| `docs/exec-plans/abandoned/` | Preserves full Bend, Emissive, SVSM, and diagnostic CSM evidence without keeping stale plans active |
| `docs/exec-plans/completed/ao-performance-optimization.md` | Preserves completed historical evidence without restoring packed or ambient-occlusion-only runtime routes |

### Final Count Snapshot Before Canonical Build

| Measure | Pre-Cutdown Baseline | Final Accepted Source | Reduction |
| --- | ---: | ---: | ---: |
| First-party nonblank source lines | 145,256 | 64,611 | 80,645 fewer (55.52%) |
| `src/uvsr.cpp` physical lines | 33,577 | 17,306 | 16,271 fewer (48.46%) |
| Third-party nonblank source lines | 388,207 | 388,207 | Unchanged |
| Total nonblank source lines | 533,463 | 452,818 | 80,645 fewer (15.12%) |
| Core first-party shader tasks | 823 production / 3,033 developer | 268 | 555 fewer than production (67.44%) |
| Integrated shader tasks | 899 production | 390 | 509 fewer (56.62%) |
| Runtime shader package files | Historical package differs | 39 | Exact current package |
| Command catalog entries | Earlier integrated catalog: 245 | 122 | 123 fewer (50.20%) |

The accepted candidate passed all 30 registered tests, compiled 268 core plus
46 Screen-Space Directional Shadow shader tasks and Donut's 76 tasks, and
staged the exact 39-file runtime shader package. The evidence-weighted
confidence in remaining features remains **96.3 percent**. Canonical status is
reserved for the clean committed rebuild, independent deletion/package review,
runtime smoke, live remote confirmation, and completed verification record.
