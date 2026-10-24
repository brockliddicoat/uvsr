# UI contract

preserve the established ImGui language, retained controls, authored spacing,
palettes, fonts, and interaction behavior. approved removals remain removed.
do not infer a redesign from an old screenshot or procedure.

add visual treatment only when it improves comprehension or operation. use concrete
labels and evidence for claims. decorative elements must serve a user purpose.
preserve exact UI labels rather than applying conversational lowercase rules.

read only the procedure relevant to the change. current source and approved product
decisions resolve stale procedural descriptions. controls that affect rendering
must preserve transaction boundaries, resource lifetime, and settings compatibility.
verify visible changes in the exact candidate with representative output; a source
check alone does not prove appearance or interaction.
the current [user guide](user-guide.md#startup-and-interface) owns retained interface
behavior. [settings](settings.md#startup-snapshot-transaction) owns transactions.
[validation](validation.md) owns proof. consult the [animation postmortem](postmortem/ui-animations.md)
only for animation recovery or historical comparison, never to restore removed UI.
