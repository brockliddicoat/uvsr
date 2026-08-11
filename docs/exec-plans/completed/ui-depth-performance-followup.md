# UI Depth and Performance Follow-Up

## Status

- State: completed
- Coordinator: `/root`
- Branch/worktree: `codex/ui-material-hardware-skin` in
  `work/ui-material-hardware-skin`
- Base commit: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Input candidate: the complete local diff recorded by
  `docs/exec-plans/completed/ui-material-hardware-white-skin.md`, executable
  SHA-256
  `DEC66C9E26D2AE751008750D88DB33F29F3D75193CF4D5F7322E5F98C6FDAF74`
- Started: 2026-08-09
- Planned archive: `docs/exec-plans/completed/ui-depth-performance-followup.md`

## Goal and Done Condition

Refine the rejected local UI candidate so the Performance hierarchy is direct
and nonredundant, while the White skin again communicates raised light headers
and inset dark controls.

Done when:

- [x] The renderer/status block above the root drawer is removed, and the
      renamed Performance header is the first control under the Settings title.
- [x] White drawer headers are slightly translucent with a restrained gradient,
      pitch-black bold text, and a raised edge treatment.
- [x] Dark dropdowns, toggle tracks, and other framed controls retain a visible
      inset/carved outline under both Amp and White.
- [x] User-facing documentation says Performance where it names the drawer.
- [x] Focused contracts, full Release build/CTest, documentation checks,
      independent review, and an exact-candidate launch smoke pass.

## Scope

In scope:

- Settings/Performance hierarchy and collapsed height.
- Amp/White header gradients, header typography, and raised/inset outline logic.
- Focused UI source/render tests and scoped documentation.

Non-goals:

- Hardware metric semantics, renderer algorithms, shaders, assets, publication,
  commit, push, pull request, merge, or release.

Coordinator-owned hotspots:

- `src/uvsr.cpp`, ImGui override patches, `CMakeLists.txt`, UI tests, README,
  documentation, this plan, build tree, and UVSR process.

## Assignments

| Task | Owner | Scope | Write Authority | Status |
| --- | --- | --- | --- | --- |
| Visual-depth seam audit | `/root/ui_architecture_audit` | Current first-party UI and override patches | None | Completed |
| Contract/test audit | `/root/ui_test_audit` | Current tests and user-facing docs | Focused test paths | Completed |
| Integration and verification | `/root` | All task paths and runtime | Local only | Completed |

## Decisions

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-08-09 | Continue from the exact rejected local candidate rather than reconstructing the prior diff. | The user is giving scoped feedback on that artifact, and its source identity and verification record are preserved. |
| 2026-08-09 | Keep unsupported CPU TFLOPS and all hardware semantics unchanged. | This follow-up is presentation-only and must not broaden into hardware inference. |
| 2026-08-09 | Separate title-only collapsed height from the larger expanded CLI minimum. | The Settings shell can now collapse cleanly without sacrificing usable space while the CLI is open. |
| 2026-08-09 | Give raised and carved controls inverse semantic outline gradients while preserving their original silhouettes. | This restores depth on White and unchanged dark controls without producing a blocky outer halo. |
| 2026-08-09 | Keep internal Statistics identifiers stable while renaming the visible drawer to Performance. | The user-facing hierarchy changes without unnecessary persistence or test-fixture churn. |

## Verification Plan

| Criterion | Evidence |
| --- | --- |
| Direct hierarchy | Source contract plus runtime inspection |
| Raised headers and carved controls | Render-contract test plus Amp/White runtime inspection |
| No regressions | Full Release build and all CTest tests |
| Exact artifact | SHA-256, exact-path launch, one responsive window, clean close |

## Completion

- Final artifact: `build/bin/uvsr.exe`, 2,694,144 bytes, SHA-256
  `F44E20CAF297E2D7ADE6B06430F7640C3C94D6CC37135F1FA0D8FEEB90C42027`.
- Verification: Release build passed with `/m:1 /nodeReuse:false`; all 41 CTest
  tests passed; README counts are current at 89,324 first-party, 387,598
  third-party, and 476,922 total lines; the title-case self-test passed; all
  1,363 Markdown headings and bold lead-ins passed; `git diff --check` passed.
- Runtime smoke: the exact executable launched as one responsive High-priority
  window titled `UVSR Renderer D3D12 (7202ff9)` and closed cleanly, leaving zero
  UVSR processes. Computer Use identified the exact window, but its state-capture
  call failed with `node_repl exec context not found`.
- Independent review: no actionable P0-P2 findings.
- Product visual acceptance: pending because no screenshot/state capture was
  available for the exact candidate; no acceptance is inferred from launch.
- Publication: intentionally local, uncommitted, and unpublished.
- Archive: `docs/exec-plans/completed/ui-depth-performance-followup.md`.
