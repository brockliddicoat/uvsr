# Miniengine Taa Canonical Verification

This record completes the publication of the current-main Miniengine temporal
anti-aliasing contender documented in
[`miniengine-taa-canon-contender.md`](miniengine-taa-canon-contender.md). It
identifies the exact renderer checkpoint and artifact that satisfy UVSR's
technical, product, integration, and canonical-verification requirements.

## Status

- State: complete; Canonical verified and product accepted
- Canonical verified renderer commit:
  `3266edf190665c9f9162bad452477bbcdf42c1a8`
- Canonical publication record before this document:
  `3a4be88d3f410a572b3363427244ee89056b54b9`
- Accepted contender runtime:
  `UVSR Renderer D3D12 (canoncontender-3266edf-0454)`
- Post-publication canonical runtime:
  `UVSR Renderer D3D12 (main-3266edf-0507)`
- Accepted executable SHA-256:
  `3AE1D454E8BFC3FEF9CA71BB2888EA6E33B9D0BA7E6DE6A94C223EE647967C12`
- Accepted executable size: `2088448` bytes
- Product acceptance: the user explicitly stated that the exact contender was
  verified and authorized its direct push to `main` on 2026-07-17.

## Publication Lineage

- Live `origin/main` started at
  `9f2e69e832c81c105b059a83e4e72daa25b15783`.
- The accepted current-main contender was a clean four-commit fast-forward:
  `d275175`, `8e3cbad`, renderer repair `3266edf`, and contender record
  `3a4be88`.
- The fast-forward push moved live `origin/main` from `9f2e69e` to `3a4be88`.
- A fresh fetch and `git ls-remote` independently confirmed live `main` at
  `3a4be88`, with accepted renderer `3266edf` on its first-parent history.
- No pull request, merge commit, conflict repair, rebase, force update, or
  history rewrite changed the accepted renderer snapshot during publication.

## Verification Evidence

| Acceptance Criterion | Result |
| --- | --- |
| Current GitHub composition | Contender was built directly on live `origin/main` `9f2e69e`; pre-push relation was zero behind and four ahead |
| Accepted renderer identity | `3266edf190665c9f9162bad452477bbcdf42c1a8`; renderer tree `2aea4ecc7adf674f9ab8a2635866bc13edf77acb` |
| Accepted executable identity | SHA-256 `3AE1D454E8BFC3FEF9CA71BB2888EA6E33B9D0BA7E6DE6A94C223EE647967C12`; `2088448` bytes |
| Release build | Exact renderer commit received a successful full Release rebuild |
| Registered tests | CTest passed 12 of 12 tests before publication and again during publication preflight |
| Documentation policy | Checker self-test passed; all 317 headings and bold lead-ins passed before the canonical record |
| Source hygiene | `git diff --check` passed and the publication worktree was clean |
| High-risk review | Independent integration re-review found no P0-P2 issue after the visibility-history compatibility guard |
| Tonemapper sunset | Removed drawer and LUT state stayed absent; fixed AgX remains downstream of scene-linear TAA |
| Camera behavior | Canonical launch has no `--benchmark-camera` argument; default mode remains interactive `Freelook`; free location is labeled `Piloted` |
| Runtime smoke | Post-publication process `38436` was responsive as `main-3266edf-0507` using the accepted executable bytes |
| Product acceptance | The user explicitly verified the exact `3266edf` contender artifact and authorized publication to `main` |

## Canonical Resolution

For an unqualified request for "latest verified," "newest version," "newest
good build," or equivalent wording, resolve UVSR to
`3266edf190665c9f9162bad452477bbcdf42c1a8` until a later renderer checkpoint on
live canonical first-parent history independently satisfies all Canonical
verified requirements.

The commits that publish the contender and canonical-verification records are
documentation-only after `3266edf`. They do not supersede its accepted
renderer, shader, asset, test, runtime-setting, or executable identity.

## Follow-Up Evidence

Canonical verification establishes the accepted fast implementation and its
integration safety. The retired TAA postmortem still requires controlled visual
and performance evidence before expanding the algorithm: fixed and moving
camera captures, disocclusion and thin-detail cases, stop-and-settle behavior,
resize and renderer-mode transitions, temporal-difference evidence, and
thermally controlled GPU timings.

No recovery mechanism should be added without a named failing capture.
Visibility Temporal Reconstruction and Adaptive Sparse Sampling remain mutually
exclusive with TAA until those histories receive and validate the same
jitter-delta contract.
