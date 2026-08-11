# Interface Accents, Panel Curves, and UI Motion Follow-Up

## Status

- State: complete as an integration input
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/ui-material-hardware-skin` in
  `work/ui-material-hardware-skin`
- Base commit: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Input candidate: the complete local diff recorded by
  `docs/exec-plans/completed/ui-detached-performance-neo-depth.md`, executable
  SHA-256
  `7641889AA179BE1BC38B627ADC511AD1CA35B0A126ACFFEEC690EA615CDEF554`
- Visual feedback:
  `C:/Users/brock/AppData/Local/Temp/codex-clipboard-2dc9bd80-da90-476b-a092-7c7380985131.png`,
  SHA-256
  `63C5733D8D4AFFBF69338A95BC750708700507B7DD2768A42594707D67F2924D`
- Started: 2026-08-10
- Last updated: 2026-08-11
- Latest visual feedback:
  `C:/Users/brock/AppData/Local/Temp/codex-clipboard-cde315eb-38e7-4973-869a-dce28c5a6c90.png`,
  SHA-256
  `31AC1CDFD01FF3E229E3A9B06ABB24A41E608FA5DB78B822872B44A17CC9C8D8`
- Planned archive:
  `docs/exec-plans/completed/ui-interface-accent-curves.md`

## Goal and Done Condition

Goal: refine the exact rejected local candidate so Performance and Settings use
the ordinary top-level drawer spacing, the complete panel stack has symmetric
outer margins and no blur outside real surfaces, General is always the first
Settings drawer, and the final Interface drawer provides alpha-aware primary,
semantic, font, and background palette controls whose picker never covers menu
content beyond the scrollbar strip.

Done when:

- [x] Performance and Settings retain fully rounded edges and use exactly the
      same vertical gap as adjacent top-level drawers.
- [x] The panel stack has equal left, right, top, and bottom outer margins.
- [x] Backdrop blur and shadow are absent from every outer/inter-panel region
      where no menu surface exists.
- [x] Performance, Settings, and drawer opening/closing retain complete authored
      motion in Amp; Ogg remains immediate.
- [x] Scrollbar and carved depth highlights read from the lower edge, Amp's
      faint endpoint is slightly stronger, and ultra-bright custom Amp accents
      use a visible darkening outline.
- [x] Bright headers visibly but subtly transmit the real scene in a live capture.
- [x] Settings retains one ordinary title-to-General inset matching the side and
      bottom body margins, owned by the fixed root so its clipped inner shadow
      persists, and its scrolling child and scrollbar begin at General.
- [x] Amp uses one shared corner radius for root bodies, backdrops, drawer
      bodies, titles, controls, and the Settings scrollbar.
- [x] All visible menu margins use one scaled 1x/2x/4x spacing system, with a
      real 2x Settings-title inset and compact 1x footer-button gaps.
- [x] General is the first drawer and Interface is the final drawer, after
      Material, with no peer drawer ever preceding General.
- [x] Interface begins with a Disable Animations toggle, then
      owns skin plus Primary, Secondary, and Tertiary alpha-aware color controls
      with reset behavior and no parenthetical color names.
- [x] Primary Accent owns headers, panel titles, footer buttons, selection, and
      raised slider knobs; Advanced Accents exposes one Font Color and one
      Primary Background Color for the menu body, resting controls, and tracks.
- [x] Negative and positive accents dynamically drive CLI error/success,
      toggle off/on, and Material positive accents.
- [x] The color-picker popup opens immediately outside menu content, may overlap
      only the vertical scrollbar, and clamps without falling back over controls.
- [x] Closed authored combo paint is vertically compact without changing its hit
      rectangle, and one rounded popup highlight glides between uniform rows
      while Ogg preserves stock popup rendering.
- [x] Amp and Ogg are the only selectable skins; the rejected Neo and Noir
      experiments have no live enum, selector, or command value.
- [x] Performance omits the redundant View label and its selector spans the
      complete content width; its summary line remains visible when collapsed.
- [x] The command interface enters in width and height around a fixed bottom-center
      pivot.
- [x] Exact popup transition state gates the unchanged deferred selection settle
      and full-idle-frame barrier.
- [x] Debug-view selections, including the White world modes, use that deferred
      barrier and never mutate renderer state directly from the stock popup.
- [x] Gated sliders gray out, animate to their effective value instead of
      freezing, retain visible raised depth on Primary Accent knobs, and ease
      grayscale over 280 milliseconds. A fixed internal value lane opens exact
      numeric input without increasing slider width.
- [x] Focused and full verification, documentation checks, independent review,
      and exact-candidate runtime evidence pass for this replacement.
- [x] The Settings margin forms one fixed inset silhouette whose four inner
      fillets match the existing outer radius, and its top shadow remains over
      General while content scrolls beneath it.
- [x] Authored sliders render as two adjoining rounded bubbles without changing
      total width; the exact-input bubble uses the authored toggle width.
- [x] Authored combo options roll down when opened and roll up after selection,
      while renderer mutations remain gated behind roll completion, the 250 ms
      settle, and a fully presented idle frame.
- [x] Settings and Performance use a filled opaque inset frame whose outer and
      inner rounded silhouettes mask every corner wedge above panel content.
- [x] The Performance inset frame and outline connect its summary,
      selector, and table lines without covering interactive content.
- [x] Authored slider value bubbles are exactly twice the toggle width while
      preserving total slider width and direct exact-value input.
- [x] Slider and color numeric fields omit component/unit letters and use one
      deterministic four-digit display policy that excludes sign and decimal
      punctuation from the digit count.
- [x] Neo and Noir are removed as visible skin options, and Amp restores its
      historical all-blue secondary and tertiary semantic accents accurately.
- [x] The Settings scrollbar sits one pixel inside and flush with the shared
      inset outline instead of leaving the stock three-pixel channel.
- [x] General is rebuilt as an ordinary drawer helper with no root chrome,
      clipping, cursor-position, or extra child-window behavior.
- [x] Settings frame, shadow, and outlines render on the topmost visible child
      so General and every other expanded drawer remain beneath them.
- [x] The authored Settings and Performance stack is 20 percent narrower without
      clipping intentional labels, and the 12-pixel scrollbar channel preserves
      the common radius with a 10-pixel visible grab.
- [x] The collapsed Performance summary has balanced vertical padding and its
      retained body alone fades toward opaque through collapse.
- [x] Interface color pickers share stack zoom and opacity, stay above the
      Settings bottom edge, close on actual Settings scrolling, and render their
      cursor above all picker content.
- [x] Only the Interface color picker uses an opaque-strengthened Primary
      Background surface; generic popup colors remain unchanged.
- [x] Repeated outward wheel input at either Settings endpoint cannot accumulate
      a second scroll-anchor correction or lurch beyond the bound.
- [x] The Settings-to-command-interface gap uses the same Tight spacing as the
      Performance-to-Settings gap while preserving Section outer margins.
- [x] The fully visible command interface uses the same opaque surface as the
      collapsed Performance body while retaining its own entry and exit fade.
- [x] The scoped Amp Interface picker uses a hue wheel plus independently
      interactive rounded hue and transparency bars with rounded markers.
- [x] The Performance selector uses the ordinary long-control inset and width;
      the unfinished Hardware view and its now-orphaned backend are absent.
- [x] Footer Reset and `/reset all` restore renderer, Adaptive Sync, Interface,
      and default collapsed Complete Renderer state without changing camera,
      scene, adapter, or shell navigation.

## Scope

In scope:

- Performance/Settings stack seam and collapse animation.
- Drawer and Settings authored motion state.
- Raised, carved, scrollbar, and ultra-bright outline rendering.
- Ultra-bright custom Amp translucency.
- Interface drawer organization and live accent customization.
- Alpha-aware advanced font/background palette roles and color-picker geometry.
- Symmetric stack margins and exact backdrop silhouettes.
- One scaled 1x/2x/4x margin system across the managed menu surfaces.
- Debug-view deferred selection and slider gated/value-label presentation.
- Compact authored popup option geometry plus a narrowly scoped authored roll
  lifecycle.
- One switch that disables every authored interface animation.
- Amp/Ogg skin tokens, command/catalog exposure, tests, and current documentation.

Non-goals:

- Changing the closed combo box appearance or interaction contract.
- Changing popup option values or applying renderer mutations during popup
  motion; authored roll state remains presentation-only until the existing
  deferred transaction commits.
- Renderer algorithms, shaders, assets, publication, commit, push, merge, or
  release.

Affected subsystems and paths:

- `src/uvsr.cpp`, `src/ui_skin.h`, `src/ui_command_layout.h`, and command catalog.
- ImGui override patches and focused UI tests.
- README and current UI documentation.

Shared hotspots reserved for the coordinator:

- Production sources, override patches, CMake, README, durable documentation,
  this plan, the build tree, and the UVSR process.

## Baseline

- Canonical repository/remote: implementation remains based on requested GitHub
  main commit `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`.
- Local versus remote state: local feature work is dirty, uncommitted, and
  intentionally unpublished.
- Verified source commit/build: the input dirty candidate passed a full Release
  build and 41 CTest tests before this follow-up.
- GPU, scene, camera, resolution, and settings preset when relevant: NVIDIA
  GeForce RTX 4090 Laptop GPU, Sponza Decorated, Position 1, 1920x1080 window,
  Amp screenshot supplied by the user.
- Known pre-existing failures: none in the input candidate's automated suite;
  visual behavior in this plan is rejected by the user.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| Exact input candidate | Completed detached-Performance follow-up and executable hash above | Ready | All tasks |
| Popup roll boundary | Preserve closed `BeginRoundedCombo` presentation; apply the new roll only after the authored UI and slider layers | Settled | Coordinator implementation |
| Bright-surface depth role | Ultra-bright custom Amp accents use darkening depth instead of dark-surface highlight polarity | Settled | Coordinator implementation |
| Popup placement boundary | Scoped ColorEdit4-only override anchors outer left at Settings `InnerRect.Max.x`, then constrains to the viewport | Settled | Coordinator implementation |
| Advanced palette roles | Amp RGBA palette plus shared semantic RGBA endpoints; legacy RGB commands retained | Settled | Coordinator implementation |
| Root backdrop boundary | Four independent title/body composites with no root shadow halo | Settled | Coordinator implementation |

Public interface, ABI, shader binding, resource layout, serialized setting, or
asset/package contracts:

- `amp` and `ogg` are the advertised skin command values; `og` remains an
  accepted alias for Ogg. White/Neo/Noir have no live value or compatibility
  slot because they were uncommitted experiments in this lineage.
- The closed combo frame, label, arrow, reset ownership, and deferred selection
  path remain unchanged.
- The existing 80-byte backdrop constant buffer now consumes one prior padding
  word as a per-corner mask. Resource bindings and shader packaging are unchanged.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Motion and stack audit | `/root/ui_architecture_audit` | Shared worktree | Dirty input candidate | None | Exact candidate | Complete |
| Bright-depth and Noir audit | `/root/hardware_color_research` | Shared worktree | Dirty input candidate | None | Exact candidate | Complete |
| Popup reset and test audit | `/root/ui_test_audit` | Shared worktree | Dirty input candidate | Focused tests | Exact candidate | Complete |
| Panel spacing/backdrop audit and layout helper | `/root/ui_architecture_audit` | Shared worktree | Dirty second candidate | Layout helper and tests | Latest rejection | Complete |
| Advanced palette and override implementation | `/root/hardware_color_research` | Shared worktree | Dirty second candidate | ImGui and Donut overrides | Latest rejection | Complete |
| Color-popup audit and focused tests | `/root/ui_test_audit` | Shared worktree | Dirty second candidate | Focused tests | Latest rejection | Complete |
| Integration and verification | `/root` | Shared worktree | Dirty input candidate | Coordinator paths | All audits | Complete; awaiting visual review |
| Spacing and slider integration review | `/root/ui_architecture_audit` | Shared worktree | Dirty fifth candidate | None | Latest rejection | Complete |
| Slider animation math review | `/root/hardware_color_research` | Shared worktree | Dirty fifth candidate | None | Latest rejection | Complete |
| Spacing, Debug, and slider tests | `/root/ui_test_audit` | Shared worktree | Dirty fifth candidate | Focused tests only | Settled production symbols | Complete |

## Integration Order

1. Complete the three read-only audits and settle popup, motion, and token
   contracts.
2. Implement production changes serially in coordinator-owned hotspots.
3. Update focused tests and current documentation after production symbols and
   deletion boundaries stabilize.
4. Configure, build, run focused and complete tests, obtain independent review,
   and inspect the exact runtime candidate.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Standard panel gap and symmetric margins | Layout/source contracts plus live capture | Focused layout/render tests and runtime | Passed in focused suite and exact runtime capture |
| Exact blur silhouettes | Backdrop rectangle/corner tests and live capture | Shader/source tests and runtime | Passed in source/render tests and exact runtime capture |
| Complete motion | Endpoint and intermediate-frame assertions in authored skins | UI animation and ImGui fixture tests | Passed in focused and full suites |
| Correct depth on dark and bright surfaces | Polarity and premultiplied-strength assertions | Headless ImGui render tests | Passed; automatic dark bright-surface ramp and AA coverage pinned |
| Closed combo unchanged, custom popup removed | Before/after closed-frame contract and source deletion search | Source/render tests | Passed; stale terms remain only as negative source assertions |
| Advanced Interface palette | Exact defaults, alpha, routing, reset, command, and order assertions | Skin/source/command tests | Passed; focused UI matrix 9/9 |
| Color picker placement | Headless popup geometry plus live narrow/edge capture | ImGui fixture and runtime | Passed, including insufficient-lane fail-closed regression |
| No regressions | Full Release build and CTest suite | CMake/MSBuild/CTest | Passed; Release build and 41/41 CTest |
| Exact artifact | Hash, exact-path launch, responsive window, clean close | Runtime exercise | Replacement SHA-256 `CF64512095E199A697836746A647F1007B23FB12713633342B77902D0CA855BA` launched as PID `11736`, responsive at High priority; visual review pending |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-10 | Continue from the exact user-rejected dirty candidate. | The screenshot and requested refinements refer to that artifact; restarting from base would discard accepted prior work. | All |
| 2026-08-10 | Interpret popup reset as removing UVSR-authored opened-popup behavior while retaining stock ImGui functionality. | This creates a clean customization baseline without turning every selector into a nonfunctional control; the closed control must remain identical. | Popup reset |
| 2026-08-10 | Superseded: treat Noir as an authored-motion skin whose header palette is derived from the existing footer action-button tokens. | Later feedback removed both Neo and Noir completely and restored Amp's historical blue roles. | Noir |
| 2026-08-10 | Move Interface after Material and restore General as the invariant first drawer. | The latest user feedback explicitly forbids any peer drawer above General and supersedes the prior Material-last ordering. | Drawer order |
| 2026-08-10 | Use the ordinary top-level drawer gap between Performance and Settings. | The zero-gap tangent treatment was explicitly rejected; the requested reference is Representation-to-Noise spacing. | Stack layout/backdrops |
| 2026-08-10 | Composite Performance title/body and Settings title/body as independent rounded surfaces and remove their exterior shadow. | A union backdrop necessarily blurred the inter-panel gap, rounded-corner pockets, and outer margins. | Stack backdrops |
| 2026-08-10 | Model authored skins with per-skin RGBA primary/font/background roles and shared RGBA Secondary/Tertiary semantic roles. | Alpha must persist, skin edits must survive switching, and one requested role must drive every named consumer. Legacy three-value commands remain compatible views. | Interface palette |
| 2026-08-10 | Constrain only ColorEdit4 picker popups from a scoped Settings content-right anchor. | Public caller positioning is cleared by ImGui's nested popup path; the narrow override leaves stock combo popups and closed controls untouched. | Color picker |
| 2026-08-10 | Restore the originating-combo lifecycle as a stock-popup dismissal gate, without restoring the deleted popup renderer. | The proven 250 ms and full-idle-frame barrier remained intact; observing the native popup closed on a later frame restores its missing prerequisite and keeps clipped or hidden owners from stranding work. | Dropdown safety |
| 2026-08-10 | Superseded: use authored `FrameRounding` for both detached root bodies while retaining Ogg's stock `WindowRounding`. | The later screenshot proved the 4-pixel body arcs still looked square beside the 8-pixel scrollbar, so the Window Rounding decision below replaces this one. | Panel geometry |
| 2026-08-10 | Present the command interface with the existing uniform appearance transform around a bottom-center pivot. | The layout lane remains stable, while both width and height now enter together instead of stretching only upward. | Command interface |
| 2026-08-10 | Let the scrolling child own the title-to-General inset and cancel the root duplicate before `BeginChild`. | Translating General upward inside the child moved it beneath the child's protected clip padding, so the visible margin covered the header. | Settings layout |
| 2026-08-10 | Standardize managed menu margins on scaled 4/8/16-pixel Tight, Regular, and Section tokens. | One 4-pixel base gives the requested 1x/2x/4x ratio: adjacent drawers, Performance-to-Settings, and footer buttons use Tight; title/body insets use Regular; outer panel and command margins use Section. | Settings layout and command layout |
| 2026-08-10 | Route all three Debug selectors through the existing deferred dropdown transaction. | Raw `ImGui::Combo` calls synchronously applied White-world material rewrites and pass recreation inside the selection frame, bypassing stock-popup dismissal, settle, and idle-frame gates. | Debug and dropdown safety |
| 2026-08-10 | Separate stored slider preferences from effective gated presentation and give authored labels a per-ID fade handoff. | A frozen presentation cache hid renderer-effective values, forced disabled alpha removed gating feedback, and a one-frame side boolean visibly teleported numeric copy through the knob. | Slider presentation |
| 2026-08-10 | Move the title-to-General inset out of the scrolling child and into the Settings root. | The scrollbar and content must begin at General while the carved top margin and rounded body corners remain fixed through scrolling. | Settings layout |
| 2026-08-10 | Collapse each authored skin palette to Primary Accent, Font Color, and Primary Background Color, plus shared semantic accents. | The requested Primary Accent now owns all raised accent surfaces and slider tracks, one font role applies everywhere, and the remaining background role owns the menu body and closed controls. | Interface palette |
| 2026-08-10 | Superseded: keep white raised slider knobs and color their tracks from Primary Accent. | The user later clarified that the knob itself—not the background track—was the requested Primary Accent consumer. | Slider presentation |
| 2026-08-10 | Add a first-row Interface animation action and compact only authored opened combo options. | Motion can be disabled without selecting Ogg, while uniform option rows and rounded highlights do not alter the closed combo or deferred dismissal transaction. | Interface and dropdowns |
| 2026-08-10 | Supersede the white-knob/accent-track decision: Primary Accent now colors the raised knob and Primary Background colors the track. | The user explicitly distinguished the requested knob from the mistakenly recolored background. | Slider presentation |
| 2026-08-10 | Replace the moving slider label with a fixed exact-input lane inside the existing width. | A stable lane cannot overlap the knob, restores direct numeric entry without a modifier, and does not lengthen the control. | Slider presentation |
| 2026-08-10 | Use one popup-scoped rounded highlight that interpolates between Selectable rows. | One background-channel surface moves smoothly without changing stock selection, dismissal, or the deferred renderer-mutation barrier. | Dropdown presentation |
| 2026-08-10 | Use Window Rounding for root bodies and a separately clipped fixed inset shadow. | Frame Rounding made the exposed left arcs look square beside the 8-pixel scrollbar; an interior shadow must remain fixed while the child scrolls without reviving exterior blur. | Settings geometry |
| 2026-08-10 | Give Performance a compact collapsed endpoint that retains only its live summary line. | The title remains independently collapsible while the per-window status never disappears; selector and table remain expanded content. | Performance panel |
| 2026-08-10 | Separate immediate semantic gating from a 280-millisecond disabled-presentation fade. | Input must block immediately while grayscale and alpha transition smoothly, reverse continuously, and snap when animations are disabled or Ogg is active. | Gated controls |
| 2026-08-10 | Draw the Settings inner silhouette and shadow continuation on the scrolling child's final layer. | Parent draw data is emitted below child content, so only a late child-layer outline can keep four fixed inner fillets and visibly shade General without changing child layout or Neo transmission. | Settings geometry |
| 2026-08-10 | Split authored sliders into a track bubble and a fixed toggle-width value bubble separated by 2 pixels. | Independent all-corner surfaces expose the requested four inner fillets, preserve total control width, and retain one merged ImGui ID for direct and Ctrl-click exact input. | Slider presentation |
| 2026-08-10 | Restore a narrow 180-millisecond combo roll layer after the UI and slider patches. | The user explicitly restored roll-down and roll-up; exact popup transition queries now hold the existing 250 ms and whole-idle-frame renderer-mutation barrier until the retained close presentation completes. | Dropdown presentation and safety |
| 2026-08-11 | Draw an alpha-one inset ring as the actual area between independently rounded outer and inner silhouettes on the final child/root layer. | A thick stroke or transparent corner wedge repeats the rejected geometry; the filled ring masks content while preserving the transmitting center. The same contract applies to Performance. | Settings and Performance geometry |
| 2026-08-11 | Give authored sliders a value bubble exactly twice the authored toggle width and format only the inactive copy to four numeric glyphs. | The total slider width stays fixed, exact input retains native precision, signs and decimal points do not consume the four-digit budget, and unsupported fixed-width values fail closed as `----` without introducing letters. | Slider presentation |
| 2026-08-11 | Remove Neo and Noir and restore Amp's source-proven historical semantic blue endpoints. | These skins were uncommitted experiments; Amp now uses translucent `#4296FA4F` Secondary and the white-track-compensated `#1E3757FF` Tertiary source, while Ogg stays stock. | Interface palette |
| 2026-08-11 | Use one 4-pixel authored corner radius and a one-pixel Settings scrollbar inset. | The prior 8-pixel root/body radius exceeded ordinary controls and the stock three-pixel grab channel left a visible gap beside the inset outline. | Panel geometry |
| 2026-08-11 | Submit Settings chrome on the last visible child and rebuild General through the ordinary drawer helper. | ImGui renders expanded drawer child windows after their Settings parent, so parent-only chrome disappeared behind General even though General itself had no paint override. | Settings layering and General |
| 2026-08-11 | Superseded: give the Performance view selector the full content width and remove its redundant reset lane. | Later feedback restored the ordinary long-control inset; the reset lane remains unnecessary. | Performance panel |
| 2026-08-11 | Target 23.44 font heights for the authored panel stack and a 12-pixel Settings scrollbar channel. | The stack becomes exactly 20 percent narrower while the content floor protects labels; a one-pixel grab inset leaves the minimum 10-pixel visible scrollbar that preserves the unchanged 4-pixel radius. | Panel geometry |
| 2026-08-11 | Treat the Interface picker as a managed stack surface bounded by Settings. | The popup must transform with the stack, remain above its bottom edge, close on actual Settings scrolling, and keep its cursor in the final popup layer. | Color picker |
| 2026-08-11 | Give only the scoped Interface picker an opaque-strengthened Primary Background surface. | Its perceived color otherwise differs from the blurred panel body; caller-supplied RGBA preserves customization without changing generic popup presentation. | Color picker |
| 2026-08-11 | Lock outward Settings wheel input to the current endpoint before anchor correction. | BeginChild already consumes the wheel target; applying a second anchor delta at a hard bound caused the visible lurch. | Settings scrolling |
| 2026-08-11 | Use one Tight gap between Settings and the command interface. | The larger Section clearance visually disconnected the two managed surfaces; outer viewport margins remain Section-sized. | Command interface layout |
| 2026-08-11 | Use the compact Performance body's opaque RGB surface for the fully visible CLI. | The CLI previously used the translucent panel body and read as an unrelated scheme; its whole-window appearance transform still owns motion opacity. | Command interface |
| 2026-08-11 | Restore the ordinary long-control inset for the Performance selector and remove Hardware end to end. | Edge-to-edge width was rejected, and removing only the visible option would leave an unused startup query, backend, and test target. | Performance and build |
| 2026-08-11 | Make the scoped Amp picker a hue wheel with retained rounded hue and transparency bars. | The user wants wheel selection without losing either vertical control; rounded meshes and markers preserve the authored control language. | Color picker |
| 2026-08-11 | Route footer Reset and `/reset all` through one complete factory-reset transaction. | Renderer-only reset left every new Interface and Performance setting stale; one transaction keeps both entry points consistent while preserving camera, scene, adapter, and shell state. | Reset behavior |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-10 | `/root` | Active | Plan and screenshot hash recorded | Read-only preflight | Await audits |
| 2026-08-10 | Audit team | Complete | Motion, depth, translucency, Noir, and popup boundaries settled | Read-only source audits | Coordinator integrated contracts |
| 2026-08-10 | `/root` | Active | Consolidated ImGui UI override plus independent slider layer; production source and docs updated | Patch applies exactly to pristine ImGui; stock `BeginComboPopup` and `Selectable` match upstream | Finish focused tests, then build |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `7641889AA179BE1BC38B627ADC511AD1CA35B0A126ACFFEEC690EA615CDEF554` | Release build, shader bundle, 41/41 CTest, Title Case, README counts, and independent rendering review pass; process is responsive at High priority | Windows capture failed twice with `node_repl exec context not found`; user review of the open exact candidate remains |
| 2026-08-10 | `/root` | Rejected | User screenshot SHA-256 `89CC710A197EE5CFBE6DC85C75F9D5AA179128B002885C30106907590C9BD4C8` | Visual review | Repair transparency support, curves, top band, Performance motion, and Interface accents |
| 2026-08-10 | `/root` | Active | Production contracts and dependency patches integrated | ImGui and Donut patches apply to pristine sources; Title Case passes | Integrate focused tests, build, and launch replacement |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `677463E0C698A7AFAB2CD17AA087E1DFCB72E6D0A6F8E0A28C4CC2ED783BD82D`; fresh runtime capture SHA-256 `27AA43075C5B1DE91EB48272D580B3F2E57BECC4C04BB9A68CB05A815F39C20C` | Release build, 41/41 CTest, focused 6/6, `git diff --check`, Title Case 0 violations, README counts current, independent review, responsive High-priority runtime | User visual review of the open exact candidate remains |
| 2026-08-10 | `/root` | Rejected | User screenshot SHA-256 `A8FD66C3E708C53F23F36D6229E7E5EACE8D5E819FF6552566CDCEAF8F62F19A` | Visual review | Repair standard spacing, symmetric top margin, ghost blur, popup placement, drawer order, and advanced alpha palette |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `7044111CEF829FF7130FD34AD674F2E18E4BFB641698D528EECAAA4DB312E501`; Neo runtime capture SHA-256 `895AA1F0669C1C83FCD11BB9D5911218068F7DD0998F1155916E5BB0478720AC` | Release build, focused 9/9, full 41/41 CTest, README counts, Title Case, diff hygiene, responsive High-priority runtime, and independent review pass | User review of the open exact candidate remains |
| 2026-08-10 | `/root` | Rejected | User screenshot SHA-256 `ECCB98EB502FEE97C30D8BD32E4AF284F80142555374EF24789A77BD06780DB1` | Visual review | Restore title inset and deferred-popup lifecycle, normalize root radius, remove View, and add two-axis CLI entrance |
| 2026-08-10 | `/root` | Active | Fourth-candidate production and documentation edits | CMake configure passes, including ImGui override application | Integrate focused tests, build, verify, and launch replacement |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `9BCE7FDA747381176D0D2320B0180EB1459678F2B88E7D8C6C6496239CB97858` | Release build, focused 4/4, full 41/41 CTest, README counts, Title Case, diff hygiene, responsive High-priority runtime, and independent review pass | Exact candidate is open; automated screenshot capture failed with `node_repl exec context not found`, so user visual review remains |
| 2026-08-10 | `/root` | Rejected | User screenshot SHA-256 `0E98FA3CB276E8068C77928656D4AE3ECB692CD270D0672ECAF387C0F81299C0` | Visual review | Move the retained inset inside the scrolling child's clip instead of translating General beneath it |
| 2026-08-10 | `/root` | Active | Root-padding cancellation and child-owned inset contract | Source review | Rebuild, rerun verification, and launch replacement |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `A530564E32F1953D2061255E834B627EC39B514A921266584A51042086CFFC9A` | Focused 4/4, full 41/41 CTest, README counts, Title Case, diff hygiene, responsive High-priority runtime, and independent geometry review pass | Exact corrected candidate is open for user review |
| 2026-08-10 | `/root` | Rejected | User screenshot SHA-256 `11907C137A16BEA963811EE11908526BBC048DA49CFC4F561894F8A336EA758B` | Visual and interaction review | Add a real title-to-General margin, standardize 1x/2x/4x spacing, reduce footer gaps, repair Debug selection, and restore slider gated/depth/value-label motion |
| 2026-08-10 | `/root` | Active | Spacing, dropdown, and slider audits dispatched | Read-only source and visual inspection | Settle contracts, implement serially, rebuild, and launch replacement |
| 2026-08-10 | `/root` | Active | 4/8/16 spacing tokens, explicit title inset, compact footer gaps, deferred Debug selectors, effective gated sliders, raised-gradient authored knobs, and per-ID label cross-fade integrated | Ordered ImGui patches reconstruct the intended five dependency sources exactly in an isolated copy | Integrate focused tests and reviews, then configure, build, verify, and launch |
| 2026-08-10 | `/root` | Active | Root-owned persistent Settings inset, consolidated palette, animation action, Primary Accent slider tracks, and compact rounded authored popup rows integrated | Ordered ImGui UI and slider patches apply cleanly and reconstruct their desired sources byte-for-byte | Finish focused tests and independent review, then configure, build, verify, and launch |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `4F0D2204A7FE26FE2C9A41FCA5B872AE7A701B155D4B64DB5FDBE31B8025FBC3` | Release build, focused 4/4, full 41/41 CTest, README counts, Title Case, diff hygiene, independent review, and responsive High-priority runtime | Obtain user visual and interaction review of the open exact replacement |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `CF75C5718095C2C26F7E34F2C42A428C53BDF77864FEAB35DD21CD436FE4786C` | Full Release build and all 306 shader tasks passed; focused 5/5 and full 41/41 CTest passed; README counts, 1,386-heading Title Case scan, diff hygiene, two independent production reviews, and responsive High-priority runtime passed | Exact candidate is open; automated capture again failed with `node_repl exec context not found`, so user visual and interaction review remains |
| 2026-08-10 | `/root` | Rejected | User screenshot SHA-256 `A42D3E73E5D9D2C0C9B378C17DFDBF030460D62DA6310F396E9A196457D907FB` | Visual and interaction review | Repair the Settings body fillets and fixed-inset shadow, add moving popup selection, restore dedicated slider numeric entry, recolor knobs rather than tracks, compact closed combos, change the animation action to a toggle, retain the collapsed Performance summary, and lengthen gated-state fading |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `ACE7A2B1B575F385D2C39F9C3EF28AF5B391E418FAD5AD34F0BE1825C5D0DB2C` | Release build and all 306 shader tasks passed; focused 6/6 and full 41/41 CTest passed; README counts, 1,386-heading Title Case scan, diff hygiene, exact ordered patch reconstruction, independent review, and responsive High-priority runtime passed | Exact replacement is open for user visual and slider-interaction review |
| 2026-08-10 | `/root` | Rejected | User screenshots SHA-256 `4D5FF005C73BD15FA814DD3B7A55B32FC31294BD137FE59E858DCF8FD9E53943` and `A7D7665DCBAFB9117E0EAA9A06E7418C1F5B1EF9EF3CCD2711C908038E388C28` | Visual and interaction review | Move fillets to the inner Settings silhouette, continue the fixed shadow over General, split sliders into toggle-width twin bubbles, and restore popup roll-down/roll-up |
| 2026-08-10 | `/root` | Active | Late child-layer Settings inset, twin-bubble slider geometry, exact popup-transition source gate, and post-slider combo-roll patch integrated | Ordered ImGui patch reconstruction matches generated desired files exactly | Integrate focused tests and documentation, then build, verify, review, and launch |
| 2026-08-10 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `B301EDC52441E6217551AB23D66AF7FA4C0DC16CD424DAAF183A71419C4E64A3` | Full Release build, 306-permutation production shader contract, focused 3/3 and full 41/41 CTest, README counts, 1,386-heading Title Case scan, diff hygiene, exact ordered patch reconstruction, two independent production reviews, and responsive High-priority runtime pass | Exact candidate is open as PID 42580 for user review of the inner fillets, fixed shadow, slider bubbles, and popup roll |
| 2026-08-11 | `/root` | Rejected | User screenshot SHA-256 `99BB23B8D21B3E4E8943D58D044327AB530D93F4CFC4C8B5B74DD01FF5251109` | Visual and interaction review | Replace stroked inset edges with filled opaque rounded frames over Settings and Performance, widen numeric bubbles, normalize label-free four-digit numeric copy, and reduce authored skins to restored-blue Amp |
| 2026-08-11 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `CF64512095E199A697836746A647F1007B23FB12713633342B77902D0CA855BA`, PID `11736` | Release build; exact ordered ImGui patch reconstruction; focused UI/source tests; 41/41 CTest; README count; Title Case; responsive High-priority launch | User review of the open exact candidate |
| 2026-08-11 | `/root` | Rejected | User text review of the exact candidate above | Visual review | Make scrollbar/frame geometry flush, reduce root radius, widen Performance selector, and rebuild General without special treatment |
| 2026-08-11 | `/root` | Active | General helper, shared topmost-child chrome, unified radius, flush scrollbar, and full-width Performance selector integrated in source | Source review | Integrate focused tests and independent review, then rebuild and launch |
| 2026-08-11 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `5C988FE2EA17AB1DF6B31E7719C2227043F96D343A48C44D6A3156B498DD0258`, PID `22800` | Release build, exact ordered ImGui regeneration, focused UI tests, full 41/41 CTest, README counts, 1,386-heading Title Case scan, diff hygiene, independent review, and responsive High-priority runtime passed | User review of the open exact candidate remains |
| 2026-08-11 | `/root` | Rejected | User text review of the exact candidate above | Visual and interaction review | Narrow the scrollbar and managed stack, balance and opacity-stabilize the collapsed Performance summary, integrate color-picker appearance and bounds, close it on Settings scrolling, and eliminate endpoint lurch |
| 2026-08-11 | `/root` | Active | Compact stack, 12-pixel scrollbar, collapse-local Performance overlay, Tight command gap, Primary Background picker surface, targeted picker lifecycle, and endpoint scroll-lock contracts in integration | Source review and ordered ImGui regeneration in progress | Complete focused tests and independent review, then rebuild, verify, and launch |
| 2026-08-11 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `D1B3EB15F623E56155B956F5CFD731564BFC2F421A71010FEA1BC9044277C7EA`, PID `48664` | Full Release build and all 306 shader tasks passed; focused 4/4 and full 41/41 CTest passed; README counts, 1,386-heading Title Case scan, diff hygiene, exact ordered ImGui reconstruction, independent review, and responsive High-priority runtime passed | User review of the open exact candidate remains |
| 2026-08-11 | `/root` | Rejected | User screenshot and text review of the exact candidate above | Visual and interaction review | Match CLI and compact Performance opacity, rebuild the Interface picker around a wheel with retained rounded bars, restore the Performance selector inset, remove Hardware, and repair factory reset coverage |
| 2026-08-11 | `/root` | Active | CLI/reset/Performance/Hardware source integration complete; scoped picker override regeneration and focused tests in progress | Source review | Finish ordered patch reconstruction, tests, documentation, build, and launch |
| 2026-08-11 | `/root` | Awaiting Visual Review | `uvsr.exe` SHA-256 `A98E94D6E3470EBF6F392E48ECC1B086408ABFE64D17D25EC8260D0D38F4DF18`, PID `50400` | Full Release build and all 306 shader tasks passed; focused contracts and full 40/40 CTest passed; README counts, 1,386-heading Title Case scan, diff hygiene, exact ordered ImGui reconstruction, independent review, and responsive High-priority runtime passed | User review of the open exact candidate remains |

## Risks and Escalation Triggers

- Popup styling is distributed across multiple ordered patches; deleting the
  wrong hunk could alter the closed combo frame or break later patch composition.
- Ultra-bright custom Amp translucency is limited by parent-surface composition,
  so token alpha alone may not produce visible scene transmission.
- Settings collapse and drawer animation use separate state machines and must not
  be conflated while closing the panel seam.
- A single RGBA value cannot reproduce two intentionally different role
  opacities without a documented role-alpha multiplier; preserve exact defaults
  and make alpha semantics explicit.
- Stock ImGui owns the ColorEdit popup; constrained placement may require one
  narrow override hunk while the ordinary combo popup remains stock.

Stop and ask the user if:

- preserving the closed combo presentation proves incompatible with removing
  only the customized submenu; or
- achieving requested ultra-bright Amp transmission requires a broad readability change
  outside headers and their immediate supporting surface.

## Completion

- Final integrated commit: none; the verified source candidate remained local
  and uncommitted.
- Verification summary: the current candidate passed the full Release build,
  the 306-permutation production shader contract, focused UI/source checks and
  full 40/40 CTest, README counts, Title Case, source hygiene, exact artifact
  hashing, independent production review, and a responsive High-priority runtime smoke
  check. User visual and interaction review remains pending.
- Independent review: complete with no actionable P0-P2 finding.
- Coming Soon/documentation update: current documentation is updated; no
  publication is authorized.
- Pushed/PR/merged, or intentionally local: intentionally local.
- Remaining experiments or follow-ups: the complete source and verification
  evidence were handed to `ui-engine-integration.md`; fresh visual acceptance
  belongs to that rebuilt Engine candidate.
- Active ownership released: all worker paths are released.
- Archived to completed/abandoned path:
  `docs/exec-plans/completed/ui-interface-accent-curves.md`.
