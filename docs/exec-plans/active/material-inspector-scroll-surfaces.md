# Material Inspector, Scroll Stability, and Floating Surfaces

## Status

- State: active
- Coordinator: `/root`
- Project/integration branch and worktree:
  `codex/ui-skins-command-reset` in
  `C:\Users\brock\OneDrive\Documents\uvsr\work\ui-skins-command-reset`
- Base commit: `3ee4cebf6986e5145c81306d55bcadb1ca14fd07` plus the
  complete local uncommitted UI-skins, command-interface, and camera-reset
  candidate recorded in the two preceding completed plans.
- Started: 2026-07-30
- Last updated: 2026-07-31
- Planned archive:
  `docs/exec-plans/completed/material-inspector-scroll-surfaces.md`

## Goal and Done Condition

Goal:

Stabilize fast Settings scrolling, bring the Amp command surface into the same
blur-and-shadow family as the other floating panels, and turn **M** into a
screen-center material inspector whose placement, crosshair, and motion compose
cleanly with pixel zoom.

Done when:

- [ ] Rapid wheel input at the Settings scroll limit cannot produce a visible
      lurch, reversal, or one-frame overshoot.
- [ ] Amp's command interface has the same backdrop blur and analytic
      outside-only shadow treatment as the established floating panels without
      changing OG's stock/no-blur behavior.
- [ ] Plain **M**, after command/text-input capture gates, opens the material
      data for the material under the exact screen-center aim point.
- [ ] The crosshair is enabled while the material inspector is open and returns
      to its prior policy when the inspector closes.
- [ ] The material inspector has the same width as pixel zoom, sits one
      consistent margin below it when zoom is visible, and otherwise occupies
      the corresponding top position.
- [ ] Opening, closing, or changing pixel zoom smoothly shifts the material
      inspector between its resolved positions in Amp; OG remains immediate.
- [ ] Materials shares Settings' stacked blue header, neutral Amp body token,
      luminance-only blur, and floating-panel shadow. Its editable controls sit
      on the same translucent light plate as a Settings drawer, texture
      annotations exactly match the authored drawer-header blue, and Material
      Domain matches the slider width.
- [ ] The Materials title has no X button; its triangle closes the complete
      panel through Amp's zoom-out/fade or OG's immediate endpoint rather than
      entering native collapsed state.
- [ ] Materials keeps zoom's full resting width while following its animated
      lower edge and uses its own Amp zoom-and-fade open/close motion.
- [ ] Plain **/** opens and closes the command interface without inserting the
      shortcut character; Escape cancels edits without closing it.
- [ ] Focused/full tests, exact Release compilation, documentation checks, and
      exact-build visual/input smoke checks pass.
- [ ] Work remains local, uncommitted, and unpublished unless the user later
      authorizes a Git or publication action.

## Scope

In scope:

- Settings scroll-anchor and end-of-range stabilization.
- Amp command-window backdrop blur and analytic shadow registration.
- Screen-center material picking and selected-material editor state.
- Material-inspector visibility, crosshair policy, width, placement, and
  zoom-dependent motion.
- Material title/body styling and backdrop registration, annotation color, and
  domain-control width.
- Input capture ordering for **M** and **/**, focused tests, UI reference
  documentation, and source contracts.

Non-goals:

- Changing material shader behavior or adding new material properties.
- Persisting inspector state across launches.
- Changing zoom size, consistent-margin size, OG's motion-free policy, or the
  complete slash-command catalog.
- Integrating the isolated flashlight/Ambient Fill branch.
- Committing, pushing, opening a pull request, merging, or publishing.

Affected subsystems and paths:

- `src/uvsr.cpp`
- Existing or new pure UI layout/animation helpers under `src/`
- `overrides/imgui-ui.patch`
- `overrides/imgui-runtime-policy.patch`
- `overrides/donut-app.patch`
- Focused tests under `tests/`
- `docs/advanced-settings.md`
- `docs/ui-integration-agent-procedure.md`
- `README.md` only if its existing local-candidate summary needs reconciliation
- This execution plan

Shared hotspots reserved for the coordinator:

- `src/uvsr.cpp`, UI backdrop registration, input dispatch, crosshair policy,
  documentation, build trees, renderer/GPU control, Git state, integration, and
  the completion claim.

## Baseline

- Canonical repository/remote: the task branch remains local-only on canonical
  base `3ee4cebf6986e5145c81306d55bcadb1ca14fd07`.
- Local versus remote state: the worktree contains the intentional uncommitted
  UI-skins candidate plus completed-plan records. No existing change may be
  discarded or rewritten.
- Verified source commit/build: the immediately preceding dirty candidate passed
  an exact Release build, focused 9/9 UI tests, full 32/32 tests, documentation
  checks, and exact-build Amp/OG visual smoke. This new work invalidates affected
  evidence after its first implementation edit.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU, PBR Sponza Decorated, 1920x1080 output, Amp and
  OG.
- Known pre-existing failures: none in the preceding candidate.
- Runtime lease: occupied. PID 16392 is the user-launched
  `build-materials-neutral-production` candidate and may not be touched. The
  style-stack repair is built in a distinct directory and remains unlaunched.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Settings scroll stability | Apply one direct current-frame-clamped anchor correction only when ImGui has no finite scroll target | Decided and implemented | Scroll repair |
| Material picking | Reuse the on-demand `MaterialIDs` pass and synchronous readback with typed cursor-focus and center-inspector purposes | Decided and implemented | **M** inspector |
| Floating-surface effects | Split Material title/body into slots 2/3, move command to slot 4, and reuse shared blur/shadow tokens | Decided and implemented | Amp surfaces |
| Pixel zoom geometry | Keep inspector X/width on zoom's resting rail while following the presented lower edge and vertical pre/post shift | Decided and implemented | Inspector layout |
| Input ownership | Keep **M** after slash/text-input capture; make plain **/** the command open/close toggle and retain Escape as ImGui cancel | Decided and implemented | Input dispatch |
| Flashlight task | Preserve its separate runtime/GPU lease and defer semantic integration | Coordinated | Verification |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- **M** is a plain-key toggle for the center-aimed material inspector. It does
  not fire while the command bar or another ImGui text input owns keyboard
  input.
- Opening resolves the material at the exact viewport center and selects that
  material in the existing editor model. Failure to hit an editable material
  must fail closed without opening stale material data.
- The inspector's resolved width and right edge are identical to pixel zoom's
  resting geometry throughout zoom motion. Its vertical target follows the
  presented zoom lower edge by one consistent margin, or returns to the
  corresponding top-margin anchor when zoom is not visible.
- Amp interpolates inspector placement and its own retained zoom/fade
  presentation without overshoot or overlap. OG applies every endpoint
  immediately.
- Inspector appearance participates in the existing crosshair visibility
  predicate so the crosshair fades with the closing panel; it does not
  permanently mutate an unrelated user setting.
- Materials title/body geometry, neutral full-RGBA surface, luminance-only
  blur, rounding, outline, and shadow reuse the Settings surface contract. Its
  embedded editor controls sit on the exact drawer-background plate and share
  the parent appearance transform. Texture filenames use the exact authored
  drawer-header blue RGB at full text opacity, and Material Domain uses the
  default slider column width.
- Materials passes no open pointer to ImGui, removing the native X.
  Its title triangle's deferred native collapse request is consumed in the
  click frame and routed to the same full-window close target as **M**, keeping
  the full body submitted throughout Amp's retained fade. Title double-click
  cannot enter the native collapsed state.
- Plain **/** toggles the command interface in both directions and suppresses
  its paired character event. Escape never closes the command interface.
- Amp's CLI uses the shared backdrop-blur and outside-only panel-shadow
  constants. OG retains immediate stock presentation without blur.
- Scroll stabilization must respect active wheel input and ImGui's pending
  scroll target, clamp to the current maximum, and never compensate the same
  content-size change twice.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `scroll-lurch-audit` | `settings_cli_inventory` | Shared task worktree | Current dirty candidate | None | Existing scroll anchors | Complete |
| `material-inspector-audit` | `consistent_margin_layout` | Shared task worktree | Current dirty candidate | None | Existing pick and zoom paths | Complete |
| `cli-surface-audit` | `fast_experiment_workflow` | Shared task worktree | Current dirty candidate | None | Existing backdrop pipeline | Complete |
| `source-contract-tests` | `settings_cli_inventory` | Shared task worktree | Integrated source | `tests/ui_source_contract_tests.cpp` only | Frozen contracts | Complete |
| `documentation` | `fast_experiment_workflow` | Shared task worktree | Integrated source | Two UI guides only | Frozen contracts | Complete |
| `independent-review` | `consistent_margin_layout` | Shared task worktree | Integrated source | None | Integrated implementation | Complete |
| `integration` | `/root` | Shared task worktree | Explorer handoffs | Coordinator hotspots | Frozen contracts | Automated Verification Complete |

## Assignment Contracts

### Scroll Lurch Audit

- Owner/thread: `settings_cli_inventory`
- Base commit/state: current dirty candidate and this plan
- Read scope: Settings scroll preparation, anchor tracking, ImGui scroll-target
  interaction, UI motion, and focused tests.
- Write scope: none
- No-touch scope: all files, build trees, renderer processes, Git state, and
  external systems.
- Deliverable: precise root cause, minimal repair contract, and deterministic
  test cases including fast wheel input at the lower limit.
- Done when: the coordinator can distinguish layout compensation from direct
  wheel behavior and implement without hiding legitimate anchor correction.
- Stop and report if: the defect is inside pinned Donut source rather than a
  first-party hook.

### Material Inspector Audit

- Owner/thread: `consistent_margin_layout`
- Base commit/state: current dirty candidate and this plan
- Read scope: picking/readback, material-editor state, crosshair predicates,
  pixel-zoom geometry/animation, and related tests.
- Write scope: none
- No-touch scope: all files, build trees, renderer processes, Git state, and
  external systems.
- Deliverable: authoritative center-material selection path, state-transition
  contract, exact placement formulas, animation state, and test gaps.
- Done when: **M** can be implemented without guessing material identity or
  duplicating zoom geometry.
- Stop and report if: center selection would require a new GPU resource/binding
  contract.

### CLI Surface Audit

- Owner/thread: `fast_experiment_workflow`
- Base commit/state: current dirty candidate and this plan
- Read scope: command layout/presentation, backdrop rectangles, blur/shadow
  constants and compositing, Amp/OG policy, and source-contract tests.
- Write scope: none
- No-touch scope: all files, build trees, renderer processes, Git state, and
  external systems.
- Deliverable: exact registration seam and ordering for matching floating-panel
  blur/shadow without double-compositing or changing OG.
- Done when: the coordinator has one minimal implementation and test contract.
- Stop and report if: the command window cannot use the current blur pass
  without a shader/resource-layout change.

## Integration Order

1. Complete the three read-only audits and freeze the public behavior and pure
   geometry contracts.
2. Implement/test scroll stabilization and reusable inspector geometry.
3. Integrate material selection, **M** input, crosshair policy, inspector
   presentation, and CLI surface effects in `src/uvsr.cpp`.
4. Update UI documentation and source-contract coverage.
5. Freeze writes, build the exact Release candidate, and run focused/full
   automated checks.
6. After both renderer windows are released, run one exact-build visual/input
   smoke and archive this plan.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Fast-scroll stability | Deterministic lower-limit unit/source contract plus runtime stress input | Focused tests and exact-build wheel smoke | Automated contracts pass; final exact-build wheel smoke remains |
| Amp CLI effects | Shared blur/shadow constants, correct backdrop order, no OG registration | Source tests and Amp/OG capture | Source/order contracts pass; final exact-build capture remains |
| Center material selection | Hit selects the actual aimed-at material; miss cannot reuse stale selection | Focused state tests and scene smoke | Source contracts pass; preceding exact candidate opened `window_frame_01` from the center hit |
| **M** input capture | Command/text input suppresses **M**; ordinary **M** toggles inspector | Source/input tests and runtime smoke | Source contracts pass; preceding exact candidate opened from plain **M** with Settings hidden |
| Crosshair policy | Crosshair visible exactly while inspector policy requires it | State tests and capture | Source contracts and preceding exact-candidate capture pass |
| Inspector geometry | Width equals zoom; gap equals consistent margin; no overlap | Pure geometry tests and captures | Pure tests pass; preceding exact candidate matched X, width, right edge, and margin |
| Inspector motion | Amp shifts smoothly for zoom open/close/level changes; OG is immediate | Animation tests and runtime smoke | Opening and level pulse passed on preceding exact candidate; final exact-build close/OG smoke remains |
| Compilation and regression | Exact Release build and all registered tests pass | Build plus focused/full `ctest` | Passed in `build-materials-neutral-stackfix-production`, including the slider style-stack regression and full 32/32; SHA-256 `FBE602900B89A0A592F7B15C865E08CFB38823E8854415288FB5F77A8CFB0D8A` |
| Documentation and hygiene | Title Case, line counts, diff check, no stale active plan | Repository tools and status audit | Title Case and diff checks pass; line counts are current at 129,936 first-party / 387,618 third-party / 517,554 total; active plan intentionally remains until final runtime smoke |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-07-30 22:33 -05:00 | Treat the material surface as an inspector backed by the existing material editor | The user requested material data for the aimed-at material, so reusing the established property model avoids a parallel editor and preserves side effects. | Material inspector |
| 2026-07-30 22:33 -05:00 | Derive inspector geometry from pixel zoom and the consistent margin | One geometry owner prevents drift between windows and makes zoom-dependent motion deterministic. | Inspector layout |
| 2026-07-30 22:33 -05:00 | Continue source-only while both observed renderer processes are open | Neither process is safely task-owned now; closing or driving either could interfere with the user or the flashlight task. | Runtime verification |
| 2026-07-30 23:18 -05:00 | Preserve a finite ImGui scroll target and otherwise correct `Scroll.y` directly | The lurch came from queuing a second delayed target after ImGui consumed wheel input; direct same-frame correction preserves the anchor exactly once. | Scroll repair |
| 2026-07-30 23:18 -05:00 | Sequence Amp zoom and inspector motion instead of letting their rectangles cross | The inspector moves below zoom before zoom fades in, remains there through fade-out, then returns to the top anchor; OG resolves endpoints immediately. | Inspector layout |
| 2026-07-30 23:18 -05:00 | Keep center inspection distinct from cursor camera focus | A typed pick purpose reuses the authoritative readback while guaranteeing that **M** cannot move the camera or expose stale selection. | Material inspector |
| 2026-07-30 23:56 -05:00 | Translate submitted Settings content by the applied scroll delta in the same frame | Updating only `Scroll.y` still left the rendered vertices one frame behind; translating the affected root/nested draw ranges removes that last snap while keeping the fixed viewport chrome stationary. | Scroll repair |
| 2026-07-30 23:56 -05:00 | Temporarily suppress body interaction across continuation/finalization correction frames | ImGui resolves hit rectangles before the visual translation, so a non-dimming disabled scope prevents transient hit/visual disagreement without blocking wheel ownership. | Scroll repair |
| 2026-07-30 23:56 -05:00 | Follow zoom's exact presented rail, not only its resting rectangle | The inspector now shares animated X, width, bottom, margin, opening/closing scale, and level pulse; the invisible pre/post rail joins continuously at the 86-percent endpoint. | Inspector layout |
| 2026-07-31 | Keep the inspector on zoom's resting horizontal rail while following only its presented lower edge | The requested composition keeps the final zoom width and right edge stable through zoom scale and level pulses while preserving the smooth vertical relationship and consistent margin. | Inspector layout |
| 2026-07-31 | Give Material Editor its own retained Amp appearance and make **/** the two-way command toggle | Independent appearance state provides zoom-and-fade opening/closing without changing OG's immediate policy; reserving Escape for ImGui edit cancellation prevents the command surface from disappearing during correction. | Material presentation and input |
| 2026-07-31 | Use one neutral full-RGBA body token and luminance-only Amp backdrop for the three primary panels | Equal source colors were not enough while the blur retained scene chroma; the shared token and grayscale blur make Settings, Materials, and the command interface perceptually stable without removing translucency. | Amp surfaces |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-07-30 22:33 -05:00 | Coordinator preflight | Complete | This plan | Branch, worktree, status, active-plan, Coming Soon, process, and overlap audit | Run bounded read-only audits, then freeze contracts. |
| 2026-07-30 23:18 -05:00 | Three read-only audits | Complete | Frozen contracts in this plan | No writes, builds, processes, or Git state touched | Integration implemented; source contracts and review remain. |
| 2026-07-30 23:18 -05:00 | Pure helper implementation | Complete | `src/ui_animation.h`, `src/pixel_zoom.h`, focused tests | UI animation and pixel-zoom reference tests passed 2/2 | Compile full renderer after source-contract handoff. |
| 2026-07-30 23:18 -05:00 | UI documentation | Complete | UI reference `2026-07-30.8` | 101 headings, zero Title-Case violations; diff check passed | Coordinator review, then full documentation checks. |
| 2026-07-30 23:56 -05:00 | Independent implementation review | Complete | Three findings repaired, final re-review clear | SVSM suppression, animated rail, same-frame scroll draw state, and interaction barrier audited | No open source finding. |
| 2026-07-30 23:56 -05:00 | Final isolated production build | Complete | `build-material-inspector-final-production\bin\uvsr.exe` | Release build passed; 32/32 tests passed; SHA-256 recorded at handoff | Exact final runtime smoke waits for the older user-inspected PID 7660 to close. |
| 2026-07-30 23:56 -05:00 | Runtime smoke | Partial | Preceding exact candidate `main-3ee4ceb-2318` | Center **M** hit, hidden-Settings composition, zoom opening, stable geometry, and level-pulse alignment passed | Live user input appeared; automation stopped and that window was left untouched. |
| 2026-07-31 | Material surface and interaction follow-up | Superseded Candidate | UI reference `2026-07-31.1`; `build-material-surface-final-production\bin\uvsr.exe` | Clean Release build, focused 5/5, full 32/32, documentation self-test and 140-heading audit, line counts, and diff check passed; SHA-256 `B93B37970499020C09ABDF43A9788D86EE60D5AB4EC52A0F0BB651A753782730` | Superseded by the no-X and exact-blue follow-up. |
| 2026-07-31 | Final independent reviews | Complete | Integrated source and generated patched ImGui/Donut sources | Both reviews cleared the material layout, animation, input, Amp/OG, and color contracts; the one exact-count test gap was repaired and full 32/32 reran successfully | Runtime-only risk remains. |
| 2026-07-31 | Material no-X and exact-blue follow-up | Superseded Candidate | UI reference `2026-07-31.2`; `build-material-no-x-blue-production\bin\uvsr.exe` | Read-only audit confirmed the minimal `nullptr` open-pointer seam and exact `(0.26, 0.59, 0.98, 1.0)` text color; isolated Release build and full 32/32 passed; SHA-256 `AEF17DFAC5423B818C9C7ACF1C4400070FC3F5F46E556477A7BB32C78B093C3E` | Superseded because the triangle must close with retained presentation rather than collapse. |
| 2026-07-31 | Material animated-triangle close follow-up | Automated Verification Complete | UI reference `2026-07-31.3`; `build-material-no-x-blue-production\bin\uvsr.exe` | First-party handoff consumes `WantCollapseToggle` before body submission ends, the layered ImGui patch disables Material title double-click collapse, independent timing audit is clear, exact Release relink and full 32/32 pass; SHA-256 `92B9C63E5AA3050299CE9E2B616A5FB6E5BE64C44EAA09423A3AC59AD4BAD4E3` | Documentation hygiene passes; exact-build visual smoke remains blocked by untouched user-owned PID 37312. |
| 2026-07-31 | Materials neutral-surface follow-up | Automated Verification Complete | UI reference `2026-07-31.4`; `build-materials-neutral-production\bin\uvsr.exe` | Visible title is Materials; three primary Amp roots use the neutral full-RGBA body token, their blur is luminance-only, and embedded material controls use the drawer plate plus the retained child transform. Isolated Release build, focused 3/3, full 32/32, generated ImGui audit, documentation hygiene, and an independent final review pass; SHA-256 `FD9E758D2A2E36F6DCEBF47BDFCFBF33CE58E8D888BAB9A06ACB72E8D988D70A` | Exact visual smoke remains blocked by untouched user-owned preceding-candidate PID 38876. |
| 2026-07-31 | Slider style-stack repair | Automated Verification Complete | UI reference remains `2026-07-31.4`; `build-materials-neutral-stackfix-production\bin\uvsr.exe` | User accepted the preceding artifact's appearance but exposed an ImGui style-stack error. Audit found `DrawSliderFloat` popped four colors after the shared helper pushed three. The pop count is repaired, a symmetric float/integer regression contract is added, the isolated production build passes, and full 32/32 passes; SHA-256 `FBE602900B89A0A592F7B15C865E08CFB38823E8854415288FB5F77A8CFB0D8A` | Exact replacement-artifact smoke remains after untouched user-owned PID 16392 closes. |

## Risks and Escalation Triggers

- The existing material editor may assume explicit list selection rather than
  screen-center picking; stale asynchronous readback must not open the previous
  material.
- Pixel zoom is composited outside ImGui while the material inspector is an
  ImGui surface. Their geometry and animation timelines must still share one
  authoritative target.
- Scroll anchoring exists to prevent drawer-content changes from moving the
  reader's position. Disabling it globally could fix the symptom while
  reintroducing the original defect.
- Backdrop blur ordering is frame-sensitive; registering the CLI after
  compositing or masking the wrong animated bounds can blur stale pixels or
  double-render its shadow.

Stop and ask the user if:

- Center aim can hit multiple distinct editable layers and choosing one would
  materially change the meaning of the inspector.
- Correct selection requires a new persistent GPU readback or scene-resource
  contract rather than reusing the established picking path.
- The requested simultaneous zoom/inspector layout cannot fit the supported
  minimum viewport without a product-visible fallback choice.
- Work requires deleting uncertain peer/user changes, touching the other
  renderer windows, or publishing.

## Completion

- Final integrated commit: intentionally uncommitted unless later authorized
- Verification summary: the Materials neutral-surface stack-repair isolated
  Release build and full 32/32 pass. The new regression contract proves that
  both slider helpers pop the same three colors pushed by their shared style
  helper. Documentation and diff hygiene pass with current line counts; final
  exact-build visual/input smoke remains after the active older window closes
- Independent review: the new neutral RGBA, luminance blur, Materials identity,
  light child plate, style-stack, width, and retained-animation review is clear
- Coming Soon/documentation update: UI reference documentation is current;
  Coming Soon is unchanged because this remains a local candidate
- Pushed/PR/merged, or intentionally local: intentionally local
- Remaining experiments or follow-ups: exact final-build neutral-surface,
  light-plate, title, no-X/animated-triangle, and exact-blue smoke plus the
  broader Amp/OG command, rapid-scroll, zoom-close, and input-isolation smoke
- Active ownership released: source ownership is released; GPU/UI ownership
  remains with the user's open preceding-candidate PID 16392
- Archived to completed/abandoned path: pending
