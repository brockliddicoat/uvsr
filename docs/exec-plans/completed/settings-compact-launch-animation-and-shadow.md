# Settings Compact Launch Animation and Shadow

## Status

- State: complete; accepted for canonical publication
- Coordinator: `/root`
- Worktree: `codex/aa-ui-merged-experiment`
- Base commit: `553e604`
- Started: 2026-07-20
- Publication: canonical `main` publication authorized on 2026-07-22

## Goal and Done Condition

Launch Settings open in the accepted compact all-drawers-collapsed state, apply
the magnifier's opening grow-and-fade to the complete Settings composition, and
match the magnifier's analytic shadow.

Done when:

- [x] Settings launches expanded while every top-level drawer is collapsed.
- [x] The user can still collapse and expand Settings after launch.
- [x] Parent, body, blur bounds, and shadow share the centered 180 ms
      86-percent-to-full appearance.
- [x] The Settings shadow uses 10 px softness, 0.34 opacity, and a 3 px
      downward offset.
- [x] Shadow coverage is discarded inside the rounded Settings silhouette.
- [x] UI contracts, shader rebuild, Release, full tests, documentation checks,
      and live inspection pass.

## Verification Plan

1. Extend UI source contracts for compact launch state, shared appearance
   transforms, shadow constants, and the outside-only shader branch.
2. Rebuild the modified backdrop shader and Release renderer.
3. Run focused and full tests plus document and diff checks.
4. Launch a labeled candidate and inspect the compact endpoint, opening motion,
   and shadow.
5. Keep the result local and uncommitted pending approval.

## Results

- Settings uses a first-use expanded condition, while General, Visibility,
  Buffers, Statistics, Aliasing, Sky, and Lights have no launch-time
  `DefaultOpen` flag. The user can still toggle the outer window and each
  drawer normally after launch.
- Parent and child draw vertices fade and grow around the complete Settings
  center through the shared 180 ms, 86-percent-to-full curve. Backdrop bounds
  use the same center, scale, and opacity.
- A dedicated backdrop permutation renders a rounded, outside-only shadow
  before ImGui. It uses the magnifier's 10 px blur, 0.34 opacity, and 3 px
  downward offset.
- The shader bundle completed, the Release executable linked, all 16 CTest
  targets passed, all 540 document headings passed, and `git diff --check`
  passed.
- The labeled `settingscompact-553e604-0613` Release candidate launched and
  remained responsive. Live inspection confirmed the open compact endpoint,
  all seven collapsed headers, visible footer, and clear right/bottom shadow
  without a dark fill beneath the translucent panel.
- At this task checkpoint, no commit, push, canonical merge, or publication was
  performed.

## Canonical Publication

On 2026-07-22, after subsequent UI iterations and direct user review, this work
was accepted for inclusion in the combined candidate authorized for canonical
`main` publication. The results above remain the evidence for this feature's
original checkpoint.
