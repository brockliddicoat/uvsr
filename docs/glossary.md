# glossary

shared terms have one defining owner. module pages link their concrete types,
consumers, errors and checks.

| term | meaning and owner |
| --- | --- |
| borrowed view | pointer/count without ownership or extended lifetime, [worklists](../src/checked_worklist.md) |
| worklist | caller-owned bounded scratch with explicit order and exhaustion, [worklists](../src/checked_worklist.md) |
| view, jitter, reverse-Z | current camera/clip/pixel conventions, [rendering values](../src/renderer_contracts.md#camera-and-layout) |
| surface, perceptual roughness | material/normal semantics and their real shader encoding, [rendering values](../src/renderer_contracts.md#materials-and-surfaces) |
| motion and previous validity | absent motion resource versus technique-local history validity, [rendering values](../src/renderer_contracts.md#temporal-and-color-meaning) |
| scene-linear and display-linear | color meaning before and after display processing, [rendering values](../src/renderer_contracts.md#temporal-and-color-meaning) |
| attempt, submission, presentation | distinct execution boundaries, [frame owner](../src/renderer_frame.md) |
| epoch and schedule token | history invalidation versus a prepared accumulation transaction, [frame data](../src/renderer_frame.md#data-and-completion) |
| submission token and completion | caller ordering versus actual GPU retirement, [pixel readback](../src/renderer_pixel_readback.md#ownership-and-errors) |
| persistent scene identity | logical asset identity, never a serialized native address, [rendering values](../src/renderer_contracts.md#camera-and-layout) |
| prepared candidate and publication | worker-private result versus a scene safe for ordered GPU consumption, [scene lifetime](../src/renderer_scene_lifetime.md#owners-and-publication) |
| terminal teardown | exclusive owner cleanup after worker join and GPU retirement, [scene lifetime](../src/renderer_scene_lifetime.md#jobs-and-teardown) |

return to the [architecture index](architecture.md).
