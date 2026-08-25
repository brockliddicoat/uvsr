# MSAA Per-Sample Visibility Control Retirement

## Decision and Exact Surface

This record covers retirement of persisted setting
`anti-aliasing.msaa.per-sample-shadows` from exact pre-cut source
`e29a41245dbd0e6fd7a819d2341646419ab76e72`. It defaulted on and let users
choose between one ray-traced visibility result per covered MSAA receiver and a
closest-receiver result broadcast across the pixel.

**Observed.** The setting was owned by `MsaaSettings` and its quality/default
logic in `src/temporal_aa_options.h`; application/reset, command GET/SET/RESET,
Advanced UI, render dispatch, and help text in `src/uvsr.cpp`; the canonical
settings snapshot/catalog; retained and narrowed
`tests/temporal_aa_tests.cpp` and
`tests/ui_settings_command_catalog_tests.cpp`; deleted
`tests/renderer_source_contract_tests.cpp` and
`tests/ui_source_contract_tests.cpp`; and active PBR, visibility, TAA, and UI
documentation. The option also affected history invalidation when toggled.

The control was added to expose a cost-versus-edge-correctness policy while the
visibility implementation could trace either frequency. That policy conflicts
with the protected product contract: 2x, 4x, 8x, and 16x MSAA require correct
ray-traced visibility. A selectable closest-surface broadcast can knowingly
produce the wrong result when samples cover different surfaces.

## Replacement and Evidence

The replacement is unconditional receiver-frequency visibility for every
covered sample. Directional, sky, and flashlight producers use 1/2/4/8/16
sample variants and `Texture2DArray` visibility; deferred MSAA lighting reads
the corresponding receiver sample. There is no off state, hidden alias,
snapshot compatibility value, or fallback broadcast contract.

**Observed.** CPU tests cover the fixed 2/4/8/16 sample-count contract. Direct
DXC checks compile the directional, sky, flashlight, and deferred-lighting
receiver variants, and GPU ABI/reflection tests cover the per-sample resource
shape. These prove selectability, layouts, and compilation only.

**Not yet final evidence.** Exact-package rendered evidence must cover all four
MSAA counts on Bistro and San Miguel; directional, sky, and flashlight alone
and together; denoising enabled and disabled; opaque, alpha-tested, and mixed
shadowed/unshadowed sample edges; finite linear outputs; debug-layer
cleanliness; distinct/reference comparisons; reset/resize/scene changes; and
frame-time tolerance. Closest-surface denoiser remapping requires those mixed
surface comparisons in particular. Case labels, source spelling, a nonblack
swapchain, or successful dispatch are not substitutes.

## Risk, Burden, Recovery, and Advice

Retirement removes one persisted Boolean, one UI row, command/domain/default
handling, snapshot identity, conditional dispatch/reset branches, and their
source-spelling assertions. It deliberately retains the cost of correct
per-sample work. Final before/after settings, test, shader, package, and runtime
measurements belong in the stage-two evidence ledger; no performance saving is
claimed here.

The risks are excessive cost at 16x, incorrect sample-to-surface ownership,
and a denoised closest-surface value that does not represent every covered
sample. The response to a proven cost problem is to optimize the unconditional
per-sample algorithm or implement a rendered-equivalent resolve, not restore a
user switch that permits incorrect output.

Recover the complete former control from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`, including settings, snapshot,
commands, UI, dispatch policy, invalidation, tests, and docs. Restore it only if
a direct rendered study proves a cheaper mode equivalent at all protected
mixed edges and sample counts. Future agents should begin with per-sample
ground truth, retain one correctness contract, and reject any broadcast unless
its equivalence is measured rather than inferred.
