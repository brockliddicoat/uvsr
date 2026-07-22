# Pixel Zoom and Benchmark Controls

## Status

- State: complete; accepted for canonical publication
- Coordinator: `/root`
- Worktree: `codex/aa-ui-merged-experiment`
- Base commit: `553e604`
- Started: 2026-07-20
- Publication: canonical `main` publication authorized on 2026-07-22

## Goal and Done Condition

Add an exact pixel zoom tool without changing the existing AA, visibility, or
optimization behavior. Move roll-left from Z to X, use Z and a UI button to
cycle Off, 2x, 3x, and 4x, show the crosshair only while zoom is active, and
move the AA motion test beneath Statistics' current benchmark action.

Done when:

- [x] Z and the button share the required zoom cycle while X owns roll-left.
- [x] The zoom panel is one quarter of both renderer dimensions, uses the
      Settings margin at the top right, and expands each source texel into an
      exact integer group.
- [x] Rounded corners cut away the zoom image and the one-pixel translucent
      gradient is layered over it without filtering the interior.
- [x] Disabled zoom submits no copy or composite GPU work.
- [x] The crosshair exists only while zoom is active.
- [x] Run Current With Motion appears immediately below Run Current in
      Statistics and no longer appears in Aliasing.
- [x] Shaders, Release, tests, documentation checks, and live behavior pass.

## Verification Plan

1. Run the focused pixel-zoom, camera-control, UI-contract, and TAA tests.
2. Rebuild the complete shader bundle and Release executable.
3. Run full CTest and the document Title Case checker.
4. Launch a labeled build and exercise Z through every zoom state, the footer
   button, X/C roll, resize behavior, crosshair visibility, and Statistics.
5. Keep the result local and uncommitted pending approval.

## Results

- ShaderMake rebuilt 18,337 DXIL tasks successfully, including
  `pixel_zoom_ps.hlsl`.
- The Release executable linked successfully and all 16 CTest targets passed.
- The pixel-zoom reference test proves the full Off, 2x, 3x, 4x cycle, exact
  integer center groups, quarter-resolution layout, and sampler-free texel
  loads. Camera and UI source contracts prove the X/Z remap, conditional
  crosshair, button state machine, and Statistics placement.
- The first labeled Release build launched and remained responsive. Live
  captures at 2x and 3x showed the quarter-size rounded panel, exact block
  magnification, and state changes through Z. The panel was then corrected to
  the requested top-right corner after the user clarified the earlier
  top-left wording; the replacement layout is covered by the executable-level
  reference test.
- The document Title Case self-test and full scan passed with zero violations.
- At this task checkpoint, no commit, push, canonical merge, or publication was
  performed.

## Canonical Publication

On 2026-07-22, after subsequent UI iterations and direct user review, this work
was accepted for inclusion in the combined candidate authorized for canonical
`main` publication. The results above remain the evidence for this feature's
original checkpoint.
