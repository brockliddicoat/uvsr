# Shadow Renderer Canonical Verification

This record completes publication of the user-approved shadow renderer build.
It identifies the exact renderer checkpoint, executable, source tree, and
integration evidence that satisfy UVSR's technical, product, and Canonical
verification requirements.

## Status

- State: complete; Canonical verified and product accepted
- Canonical verified renderer checkpoint:
  `bcf4f7e4ade98b313f1aa9ec53b8b34bd5d78294`
- Integrated source tip:
  `1f31feb99e5400b63fd3c9a6e17151ff1f7988a0`
- Canonical renderer tree:
  `26adf492a89defba10bee7ab351bcf0762839e97`
- Accepted runtime:
  `UVSR Renderer D3D12 (shadowmerge-bcf4f7e-1920)`
- Accepted executable SHA-256:
  `4228B26E7602CCF6DB79E8C0549A1CF75908EF7616195301EBFD42BCA90BE2E4`
- Accepted executable size: `3106816` bytes
- Product acceptance: the user approved the exact launched build and directed
  it to be uploaded to GitHub `main` and marked as the newest internal
  Canonical checkpoint on 2026-07-29.

## Publication Lineage

- Live GitHub `main` started at
  `ec4c6f93029ae1d8106b80370bc9c24965993f98`.
- The accepted renderer is merge
  `bcf4f7e4ade98b313f1aa9ec53b8b34bd5d78294`, with integration-plan parent
  `c6830230a170ac54435edd745e53e434d3c79d23` and exact source-lineage parent
  `1f31feb99e5400b63fd3c9a6e17151ff1f7988a0`.
- A fresh fetch proved that the accepted merge was a fast-forward descendant
  of live `main`. The exact accepted SHA was then pushed directly to
  `origin/main` under the user's explicit publication authority.
- A post-push fetch confirmed remote `main` at the exact accepted merge and
  confirmed that its remote and local trees both resolve to
  `26adf492a89defba10bee7ab351bcf0762839e97`.

## Verification Evidence

| Acceptance Criterion | Result |
| --- | --- |
| Accepted executable identity | SHA-256 `4228B26E7602CCF6DB79E8C0549A1CF75908EF7616195301EBFD42BCA90BE2E4`; `3106816` bytes; rehashed after publication |
| Runtime identity | PID `18360` ran the accepted executable with title `shadowmerge-bcf4f7e-1920` and remained responsive after publication |
| Exact source lineage | Merge `bcf4f7e` has exact parents `c683023` and source tip `1f31feb` |
| Integrated tree identity | Local accepted merge and fetched `origin/main` both resolve to tree `26adf492a89defba10bee7ab351bcf0762839e97` |
| Release build | Post-publication all-target Release build passed in `build-integration` |
| Registered tests | CTest passed 28 of 28 tests before publication and 28 of 28 again after publication |
| Runtime bundle | The 80-file manifest contained 80 unique staged files, all byte-identical to their build outputs; all eight environment assets were staged byte-identically |
| Documentation policy | README line-count self-test and current-count check passed with `117,816` first-party, `387,622` third-party, and `505,438` total lines; Title Case self-test and the complete 860-heading scan passed with this record |
| Source hygiene | `git diff --check` passed; the tracked worktree and index were clean at the accepted merge |
| Independent review | Renderer, resource binding, lifetime, runtime-bundle, test-coverage, ancestry, staging, and merge-hygiene reviews returned no blocking findings |
| Product acceptance | The user explicitly approved the exact launched build and authorized direct publication to GitHub `main` |

## Artifact Identity

The accepted artifact remains the executable with SHA-256
`4228B26E7602CCF6DB79E8C0549A1CF75908EF7616195301EBFD42BCA90BE2E4`.
The post-publication verification did not change its bytes, source tree, or
runtime label.

The repository has no established executable-release channel, so this
publication promotes the accepted source state and records the artifact
identity without creating a GitHub release.

## Canonical Resolution

For an unqualified request for "latest verified," "newest version," "newest
good build," or equivalent wording, resolve UVSR to renderer checkpoint
`bcf4f7e4ade98b313f1aa9ec53b8b34bd5d78294` and accepted executable SHA-256
`4228B26E7602CCF6DB79E8C0549A1CF75908EF7616195301EBFD42BCA90BE2E4` until a
later renderer checkpoint on live Canonical first-parent history independently
satisfies all Canonical verified requirements.

The commit that publishes this verification record is documentation-only after
`bcf4f7e`. It does not supersede the accepted renderer, shader, asset, test,
runtime-setting, source-tree, or executable identity.
