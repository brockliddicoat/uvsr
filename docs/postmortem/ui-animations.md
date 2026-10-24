# interface animations

on September 4, 2026, the user approved removing interface animations and
preserving their code. this retires panel resizing, disclosure rotation,
hover and disabled fades, slider easing, deferred toggle and dropdown commits,
popup rolling, tooltip and color picker transitions, pixel zoom transitions,
and the launcher progress marquee.
static controls, skins, fonts, zoom, and compact panel summaries remain.

## the development trap

presentation timing had become part of command execution. dropdown choices
waited for a popup to close, the composition to become idle, an additional
frame, and a settling interval before changing renderer state. toggles kept a
pending value until their thumb reached its endpoint. displaying a choice and
applying it therefore required separate state.

resizing then needed measured body heights, scroll anchors, input gates, and
retained drawing. popup and tooltip motion transformed vertices and clip
rectangles after layout. interrupted submission, hidden owners, reversed
transitions, and skin changes each required cleanup paths. fixes to one visual
effect could disturb geometry, focus, or another control's commit timing.

the launcher marquee added a timer, offset, speed, visibility handling, and
disposal state to communicate unknown progress. a static busy indicator and
status text carry that information. determinate percentages and accessible
status remain.

the old ImGui integration harness alone reached 8,427 lines. it included useful
interaction proof, but much of its machinery managed frames, transition
endpoints, and draw geometry. that is evidence of maintenance burden, not a
measurement of GPU cost. no performance improvement is claimed without a
controlled comparison.

the mistake was treating decorative motion as a small independent feature
after it had spread into layout and command ownership. immediate presentation
allows one current control value and native popup ownership. future proposals
should establish a concrete user benefit and a bounded implementation before
adding timing state to otherwise immediate interactions.

## preserved implementation

the recovery archive is outside the maintained source:
`work/astra-cutdown/ui-animation-recovery-c4dbcb31-cc9d6c14eb72.zip`.
its SHA-256 is
`435f79dd8ad7c8d7a0b0d19e908bd22f2aa46c9dab1be97ccc958f35e0e29001`.
all 463 archived files were verified against the embedded manifest.

the archive preserves the initial first party source, all five original ImGui
patches, and the binary Git recovery diff for source identity
`c4dbcb31c2a241f5864cb08ae2f80551ef2615fb-dirty-cc9d6c14eb72`.
it includes unrelated baseline files to make the uncommitted source recoverable.
inspect it separately and recover only an explicitly requested owner. copying
the old UI wholesale would also undo later settings and layout work.

the archive is a local recovery artifact, not a published backup or proof that
the old implementation meets current contracts.
