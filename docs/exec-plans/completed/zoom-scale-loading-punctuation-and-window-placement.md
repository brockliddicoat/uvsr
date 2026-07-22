# Zoom Scale, Loading Punctuation, and Window Placement

## Status

- State: complete; accepted for canonical publication
- Coordinator: `/root`
- Worktree: `codex/aa-ui-merged-experiment`
- Base commit: `553e604`
- Started: 2026-07-20
- Publication: canonical `main` publication authorized on 2026-07-22

## Goal and Done Condition

Add a smooth grow-and-fade transition to the exact-pixel magnification window,
match the requested loading-line punctuation, and balance the renderer's
visible startup margins against the Windows work area.

Done when:

- [x] Zoom grows smoothly from a centered smaller rectangle to its exact
      quarter-resolution endpoint and reverses when closing.
- [x] Every intermediate zoom size still maps source texels to complete integer
      destination groups with no sampling.
- [x] Loading reads `Loading scene: <name>, please wait...` while dots cycle.
- [x] The visible top, taskbar, left, and right startup gaps are equal.
- [x] Release, focused tests, full tests, documentation checks, and live
      inspection pass.
- [x] The Settings width is measured in pixels and as a percentage of the live
      renderer width.

## Verification Plan

1. Run pixel-zoom and UI source-contract tests.
2. Rebuild the shader target and Release executable, then run full CTest.
3. Launch a labeled candidate and inspect loading punctuation, zoom motion, and
   all four visible work-area margins.
4. Measure the Settings window width against the live renderer resolution.
5. Keep the result local and uncommitted pending approval.

## Results

- The zoom uses the shared 180 ms eased visibility to grow from a centered
  86-percent whole-pixel rectangle to its exact quarter-resolution endpoint.
  Focused coverage verifies both endpoints and an intermediate size while the
  integer `Texture2D.Load` mapping remains unchanged.
- The loading format is
  `Loading scene: <name>, please wait...`; its one-, two-, and three-dot cycle
  remains intact and the progress bar remains absent.
- The labeled `zoomscale-553e604-0523` Release candidate launched with a
  1902-by-1085 renderer. Native DWM measurement reported exactly 8 px between
  the visible frame and each work-area edge: left, top, right, and taskbar.
- The Settings ImGui window is 525 px wide. At the live 1902 px renderer width,
  that is 27.60 percent.
- The shader target and Release executable built successfully, and all 16 CTest
  targets passed. Automated keyboard input was stopped when concurrent user
  input was detected; the final live inspection remained read-only.
- At this task checkpoint, no commit, push, canonical merge, or publication was
  performed.

## Canonical Publication

On 2026-07-22, after subsequent UI iterations and direct user review, this work
was accepted for inclusion in the combined candidate authorized for canonical
`main` publication. The results above remain the evidence for this feature's
original checkpoint.
