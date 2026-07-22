# Zoom Panel Proportion and Level Transitions

## Status

- State: complete; accepted for canonical publication
- Coordinator: `/root`
- Worktree: `codex/aa-ui-merged-experiment`
- Base commit: `553e604`
- Started: 2026-07-20
- Publication: canonical `main` publication authorized on 2026-07-22

## Goal and Done Condition

Resize the magnification panel to 28 percent of renderer width while preserving
the renderer aspect ratio, retain the accepted opening and closing animation,
and add an exact-pixel transition between enabled magnification levels.

Done when:

- [x] Panel width rounds to 28 percent of renderer width.
- [x] Panel height is the nearest whole-pixel match to the renderer aspect
      ratio.
- [x] Opening and closing keep the accepted 86-percent grow and fade.
- [x] Enabled level changes ease inward, switch exact integer factors at the
      midpoint, and ease outward without crossfading.
- [x] Disabled zoom still submits no capture or composite work.
- [x] Focused tests, Release, full tests, documentation checks, and live
      inspection pass.

## Verification Plan

1. Extend pixel-zoom references for proportional sizing, aspect error bounds,
   symmetric level-transition scale, and exact midpoint switching.
2. Run pixel-zoom and UI source-contract tests.
3. Build the shader target and Release renderer, then run full CTest.
4. Launch a labeled candidate and inspect 2x, 3x, and 4x transitions.
5. Keep the result local and uncommitted pending approval.

## Results

- At the live 1902-by-1085 renderer size, the final panel resolves to
  533-by-304 px. Its width is the nearest whole pixel to 28 percent and its
  height is the nearest whole-pixel match to the renderer aspect ratio.
- Opening and closing retain the accepted 180 ms grow-and-fade. Enabled level
  changes use an independent symmetric 180 ms pulse with a full-size to
  86-percent to full-size path.
- The outgoing 2x, 3x, or 4x factor remains exact through the first half. The
  factor switches at the smallest midpoint without crossfading, then the
  incoming exact factor expands through the second half.
- The shader target and Release executable built successfully, and all 16 CTest
  targets passed. Focused tests cover percentage rounding, aspect error,
  symmetric scale, exact midpoint switching, integer texel groups, and the
  disabled no-work guard.
- The labeled `zoomlevels-553e604-0536` build launched and remained responsive.
  Automated key injection was stopped when concurrent user input was detected,
  so no further input was sent over the user's active mouse interaction.
- At this task checkpoint, no commit, push, canonical merge, or publication was
  performed.

## Canonical Publication

On 2026-07-22, after subsequent UI iterations and direct user review, this work
was accepted for inclusion in the combined candidate authorized for canonical
`main` publication. The results above remain the evidence for this feature's
original checkpoint.
