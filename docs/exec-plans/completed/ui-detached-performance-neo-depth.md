# Detached Performance and Neo Depth Follow-Up

## Status

- State: complete
- Coordinator: `/root`
- Branch/worktree: `codex/ui-material-hardware-skin` in
  `work/ui-material-hardware-skin`
- Base commit: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Input candidate: the complete local diff recorded by
  `docs/exec-plans/completed/ui-depth-performance-followup.md`, executable
  SHA-256
  `F44E20CAF297E2D7ADE6B06430F7640C3C94D6CC37135F1FA0D8FEEB90C42027`
- Visual feedback:
  `C:/Users/brock/AppData/Local/Temp/codex-clipboard-80a8486b-2432-4d94-9a95-a6b986251da3.png`
- Started: 2026-08-09
- Planned archive:
  `docs/exec-plans/completed/ui-detached-performance-neo-depth.md`

## Goal and Done Condition

Refine the rejected local candidate so Performance is a genuinely separate
top-level panel above Settings, Neo retains subtle scene translucency, and the
depth treatment is smooth rather than bright or pixelated.

Done when:

- [x] Performance is detached from the Settings window and positioned directly
      above it while preserving independent collapse behavior.
- [x] Material is the final Settings drawer.
- [x] Visible skin choices are Amp, Neo, and Ogg without changing persistence
      semantics unnecessarily.
- [x] Neo header surfaces barely reveal the scene, use smooth rounded corners,
      and avoid bright lower cutout edges.
- [x] Focused contracts, a full Release build and CTest run, documentation
      checks, independent review, and exact-candidate runtime evidence pass.

## Scope

In scope:

- Performance and Settings window hierarchy, positioning, sizing, and collapse.
- Settings drawer ordering.
- Authored skin display names and scoped documentation.
- Raised and carved edge rendering plus Neo header opacity.
- Focused UI source/render tests.

Non-goals:

- Hardware metric semantics, renderer algorithms, shaders, assets, publication,
  commit, push, pull request, merge, or release.

Coordinator-owned hotspots:

- `src/uvsr.cpp`, ImGui override patches, `CMakeLists.txt`, README,
  documentation, this plan, build tree, and the UVSR process.

## Assignments

| Task | Owner | Scope | Write Authority | Status |
| --- | --- | --- | --- | --- |
| Detached-panel architecture audit | `/root/ui_architecture_audit` | Settings and Performance layout seams | None | Complete |
| Depth and opacity audit | `/root/hardware_color_research` | Neo surfaces and ImGui outline geometry | None | Complete |
| Focused test integration | `/root/ui_test_audit` | Focused UI and layout test paths only | Local test files | Complete |
| Integration and verification | `/root` | All task paths and runtime | Local only | Complete |

## Decisions

| Date | Decision | Reason |
| --- | --- | --- |
| 2026-08-09 | Continue from the exact rejected local candidate. | The screenshot and feedback refer to that artifact, whose source and executable identities are recorded. |
| 2026-08-09 | Interpret detached as a separate top-level ImGui window directly above Settings. | The current in-between root drawer is the hierarchy error the user identified. |
| 2026-08-09 | Keep enum and persistence identifiers stable unless inspection proves they are user-visible. | The request concerns display naming, not migration of saved settings or commands. |
| 2026-08-10 | Keep Performance collapse native and independent while retaining the Settings-only smooth-collapse path. | This detaches panel ownership without coupling or duplicating the specialized Settings animation state. |
| 2026-08-10 | Give Performance and Settings separate title/body backdrops but one bounded vertical stack and appearance pivot. | Independent rounded silhouettes preserve the visual gap while the combined stack still yields smoothly to the command interface. |
| 2026-08-10 | Preserve ImGui's original vertex alpha while recoloring every rounded gradient primitive. | The overwritten transparent anti-alias fringe was the direct cause of the screenshot's sharp polygonal corners. |
| 2026-08-10 | Use Neo header alpha `0.70/0.72/0.76` and carved outline alpha `0.14/0.055` from top to bottom. | Over the retained translucent panel surface, this leaves a barely visible scene contribution and removes the glowing lower cutout rim. |

## Verification Plan

| Criterion | Evidence |
| --- | --- |
| Detached hierarchy and ordering | Source contracts plus runtime inspection |
| Smooth restrained depth | Headless draw-list contract plus runtime screenshot |
| No regressions | Full Release build and all CTest tests |
| Documentation integrity | Generated-count and Title Case checks |
| Exact artifact | SHA-256, exact-path launch, responsive window, clean close |

## Completion

- Final artifact: `build/bin/uvsr.exe`, SHA-256
  `AF0FC718ADF2F9AED4C6DA9F1905E7B0B1B535DA09C4D51DA23FC55EAF59D0A0`.
- Verification: CMake configure and the complete Release build passed; all nine
  focused UI, hardware, renderer, and camera tests passed; all 41 CTest tests
  passed; README generated counts, document Title Case, and `git diff --check`
  passed.
- Independent review: `/root/ui_architecture_audit` reported no actionable
  P0-P2 findings after reviewing the final layout, backdrop, collapse, skin,
  depth, test, and documentation contracts.
- Runtime: exact-path launches were responsive with title
  `UVSR Renderer D3D12 (7202ff9)`. The fresh Amp launch showed Performance
  collapsed independently above Settings; the Neo command path showed smooth
  rounded light surfaces, black bold headers, restrained carved edges, and
  Material last. Evidence is retained under `build/ui-runtime-*.png`; both
  task-owned processes were closed and zero `uvsr.exe` processes remained.
- Product visual acceptance: pending user review of this exact replacement
  artifact.
- Publication: intentionally local and uncommitted.
- Archive: completed on 2026-08-10.
