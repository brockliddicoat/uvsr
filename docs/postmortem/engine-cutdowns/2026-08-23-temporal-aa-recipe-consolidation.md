# Temporal AA Recipe Consolidation

## Decision and Exact Surface

This record covers the visible, persisted selectors removed from exact pre-cut
source `e29a41245dbd0e6fd7a819d2341646419ab76e72`:

- `anti-aliasing.taa.motion-source` (`preset`, `center`, `closest-cross`,
  `edge-dilation`);
- `anti-aliasing.taa.current-sample` (`preset`, `direct`, `de-jittered`);
- `anti-aliasing.taa.history-filter` (`preset`, `bilinear`, `bicubic`,
  `five-tap-bicubic`, `nine-tap-bicubic`);
- `anti-aliasing.taa.rectification` (`preset`, `pair-tristimulus`,
  `variance-chroma`).

**Observed.** Their callers were `TemporalAaAlgorithmOverrides`, preset
resolution and shader selection in `temporal_aa_options.h`, blend-pipeline
creation in `temporal_aa.cpp`, the settings catalog and snapshot, command
GET/SET/RESET handlers, four Advanced UI controls, temporal-AA tests, broader
UI/source tests, `src/shaders.cfg`, and maintained TAA documentation. No file
was deleted solely for this cut; these owners were narrowed in place.

The purpose was experimental independent selection of motion ownership,
current reconstruction, history sampling, and clipping color space. That
surface implied 3 x 2 x 4 x 2 = 48 shader recipes, then two optimized-compute
and two fused-output states: 192 blend tasks. The four user selectors also
expanded persistence, reset, command, UI, test, documentation, and settings
identity contracts.

## Replacement and Evidence

The replacement is one recipe per retained Low, Medium, High, and Ultra
quality level:

| Quality | Motion | Current | History | Rectification |
| --- | --- | --- | --- | --- |
| Low | Center | Direct | Bilinear | Pair RGB |
| Medium | Center-first edge dilation | Direct | Bilinear | Pair RGB |
| High | Center-first edge dilation | Direct | One-sample bicubic | Variance YCoCg |
| Ultra | Center-first edge dilation | De-jittered | Five-tap Catmull-Rom | Variance YCoCg |

Each recipe retains the two execution axes, so the blend catalog contains 16
tasks instead of 192. The algorithm enums remain runtime constants because the
four recipes need them; the user override fields, commands, defaults, snapshot
values, UI controls, and unreachable recipes retire together. Other retained
TAA controls, including quality, temporal cost, jitter, history length and
strength, depth validation, storage, weight, trust, rectification clip, blend
domain, and sharpening, are outside this cut.

**Observed.** Direct C++ tests exercise the four exact recipe tuples and their
permutation indices. Direct DXC compilation covers all 16 fixed blend tasks.
The final settings-contract gate must prove that the four retired values are
absent and that quality save/load/reset restores the same recipes. These are
structural and compilation checks, not image-quality evidence.

**Not yet final evidence.** Exact-package rendered Low/Medium/High/Ultra
captures, camera motion/disocclusion coverage, finite-value checks, output
comparisons, and frame-time measurements must be recorded before this cut is
called verified. Until then, the fixed recipes are an intentional replacement,
not a claim that every removed combination was visually equivalent.

## Risk, Burden, Recovery, and Advice

The main risk is a scene or motion case that benefited from a removed
cross-recipe combination. A second risk is silently loading an old snapshot as
if the retired selectors still applied. Strict schema identity and rejection
of stale values own that boundary. The measured build burden is 176 removed
blend tasks; final clean before/after shader time and package-size measurements
belong in the stage-two evidence ledger.

Recover the complete pre-cut surface from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`, including settings, commands,
snapshot, UI, options, shader catalog, tests, and documentation. Do not restore
one command without its shader and persistence contract.

Future work should begin with a captured artifact in a retained quality recipe,
show a repeatable improvement from exactly one alternate tuple, and measure
quality and cost before adding any selector. Restore a user-facing control only
when one fixed recipe cannot satisfy a demonstrated case and the benefit
exceeds its shader, identity, UI, reset, test, and documentation burden.
