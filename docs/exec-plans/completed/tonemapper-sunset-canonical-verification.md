# Tonemapper Sunset Canonical Verification

This record completes the post-merge verification handoff from
[`tonemapper-sunset-publication.md`](tonemapper-sunset-publication.md). It
identifies the exact renderer checkpoint and artifact that satisfy UVSR's
technical, product, integration, and canonical-verification requirements.

## Status

- State: complete; Canonical verified and product accepted
- Canonical verified commit:
  `0b4279839f9f24399dcecefca139af67fc70404f`
- Accepted runtime:
  `UVSR Renderer D3D12 (canon-0b42798-0239)`
- Accepted executable SHA-256:
  `4b2d22edd58081fa35d09c2d720e563adda89fcc7a43fc11aeba56188c23abc4`
- Accepted executable size: `2075648` bytes
- Product acceptance: the user explicitly verified the exact accepted runtime
  and directed that it be marked as the newest Canonical verified build on
  2026-07-17

## Publication Lineage

- Tonemapper sunset pull request:
  [#18](https://github.com/brockliddicoat/uvsr/pull/18), merged as
  `eb49d61a3a6cbaa41f07fed25a5dcd223e876a80`.
- Cross-platform restoration-patch byte-preservation pull request:
  [#19](https://github.com/brockliddicoat/uvsr/pull/19), merged as the accepted
  checkpoint `0b4279839f9f24399dcecefca139af67fc70404f`.
- The accepted checkpoint is on live canonical `main` first-parent history and
  was reverified from a clean, isolated worktree after both integrations.

## Verification Evidence

| Acceptance Criterion | Result |
| --- | --- |
| Clean canonical source | Detached verification worktree was clean at `0b4279839f9f24399dcecefca139af67fc70404f`, matching live `origin/main` |
| Release build | Fresh configure and Release build of the renderer and every registered target passed |
| Registered tests | CTest passed 11 of 11 tests |
| Documentation policy | Title Case checker self-test passed; all 294 headings and bold lead-ins passed |
| Restoration durability | Restore patch SHA-256 matched `e5e85b69d5cf8a5f6ab5cf3933f63b98d765e113df68d4606b4f72f774e5e93a`; `git apply --check` passed |
| Cross-platform patch bytes | Fresh Windows checkout reproduced the manifest SHA-256 exactly with the committed `-text` attribute |
| Revival contract | Standalone drawer restoration remains forbidden; the required companion is `bilateral-grid-local-tonemapper` |
| Legacy package cleanup | A seeded `build/media/luts/kodak` directory was removed by the final canonical Release build |
| Runtime identity | Process `3272` ran the final-worktree executable with title `canon-0b42798-0239` and was responsive at acceptance |
| Product acceptance | The user explicitly verified `canon-0b42798-0239` and accepted it as Canonical verified |

## Canonical Resolution

For an unqualified request for “latest verified,” “newest version,” “newest
good build,” or equivalent wording, resolve UVSR to
`0b4279839f9f24399dcecefca139af67fc70404f` until a later checkpoint on live
canonical first-parent history independently satisfies all Canonical verified
requirements.

The commit that publishes this documentation-only record is not a newer
verified renderer checkpoint. It changes no renderer, shader, asset, packaging,
test, or runtime setting and must not supersede the accepted
`0b4279839f9f24399dcecefca139af67fc70404f` artifact.

## Remaining Revival Contract

The Tonemapper drawer and LUT feature remains strategically sunset rather than
failed. A future request to bring it back must restore the archived global
drawer and LUT implementation and implement the bilateral-grid local
tonemapper in the same candidate before seeking fresh technical verification
and product acceptance.
