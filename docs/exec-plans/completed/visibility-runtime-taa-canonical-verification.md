# Visibility Runtime and TAA Canonical Verification

This record completes publication of the user-accepted visibility runtime and
temporal anti-aliasing build. It identifies the exact renderer checkpoint,
executable, source tree, and integration evidence that satisfy UVSR's
technical, product, and Canonical-verification requirements.

## Status

- State: complete; Canonical verified and product accepted
- Canonical verified renderer merge:
  `64cd553e49f4b0818c634b6c6ddefa809daf8736`
- Accepted implementation commit:
  `58a44cf2899bed1c867338230be76fd1b5b37042`
- Canonical renderer tree:
  `f3be68f59fa85317fa5ba7d68f7cfe4fe2f4278b`
- Accepted executable SHA-256:
  `350495C5FCBD0F9965AA0D2959F8532277AEBA15E82B6291A7624BDA224120A5`
- Accepted executable size: `2478592` bytes
- Clean integrated rebuild SHA-256:
  `698961EFBCCF4A035018FEAF7301164242774CC92A6918A1795087769858DB78`
- Clean integrated rebuild size: `2478592` bytes
- Product acceptance: the user supplied the accepted executable hash and
  explicitly directed that build to become the main Canonical build on
  2026-07-29.

## Publication Lineage

- Live GitHub `main` started at
  `4104018842371546c59fb09017ff964b82965eba`.
- The accepted source state was captured in
  `58a44cf2899bed1c867338230be76fd1b5b37042`, one commit ahead of that live
  base. Its commit message permanently records the accepted executable hash.
- Ready pull request
  [#22](https://github.com/brockliddicoat/uvsr/pull/22) passed the repository's
  README Line Counts and Document Title Case checks.
- The pull request merged without conflict or history rewriting as
  `64cd553e49f4b0818c634b6c6ddefa809daf8736`.
- The merge commit has base `4104018` and accepted implementation `58a44cf` as
  its two parents. Its tree is byte-identical to the accepted implementation
  tree, so integration did not change renderer, shader, asset, test, runtime
  setting, or documentation content.
- Fresh GitHub API and fetch checks confirmed live `main` at `64cd553`, and
  both post-merge push workflows passed against that exact commit.

## Verification Evidence

| Acceptance Criterion | Result |
| --- | --- |
| Accepted executable identity | SHA-256 `350495C5FCBD0F9965AA0D2959F8532277AEBA15E82B6291A7624BDA224120A5`; `2478592` bytes; independently rehashed before publication |
| Accepted source capture | All 26 audited first-party source, shader, test, and documentation paths were committed in `58a44cf`; the committed worktree was clean |
| Release production build | Release x64 succeeded with Visual Studio 2022 v143, MSVC `14.44.35207`, Windows SDK `10.0.26100`, and production AA overrides off |
| Production shader coverage | All `3119` UVSR shader jobs completed successfully, including the selectable TAA, CMAA2, MSAA, Runtime, Generic, and Fixed visibility variants |
| Accepted build stability | The final production build pass over the audited pre-commit worktree did not rewrite the accepted executable, and its hash remained at the exact authorized SHA-256 |
| Integrated tree identity | Candidate and merged Canonical commits both resolve to tree `f3be68f59fa85317fa5ba7d68f7cfe4fe2f4278b` |
| Clean Canonical rebuild | A fresh detached checkout of merged `main`, including every pinned submodule, configured and completed a clean Release build and a second successful `3119`-shader production pass |
| Registered tests | CTest passed 21 of 21 tests before publication and 21 of 21 again from the clean integrated checkout |
| Documentation policy | README line-count self-test and current-count check passed; Title Case self-test passed; all 595 headings and bold lead-ins passed before renderer publication, and all 602 passed with this record included |
| Source hygiene | `git diff --check` passed; candidate and clean Canonical verification worktrees were clean |
| GitHub checks | Both pull-request checks and both post-merge `main` push checks completed successfully |
| Compact Runtime evidence | On Intel, Runtime 20 measured `1221` static IR instructions versus `7989` for Fixed 20, with `7.32%` higher FPS and `16.08%` lower trace time in the controlled comparison |
| Product acceptance | The user explicitly selected executable `350495C5...` and authorized its Canonical publication |

## Artifact Identity

The accepted artifact remains the executable with SHA-256 `350495C5...`.
The clean post-merge compilation produced a separate executable with SHA-256
`698961EF...`; it verifies that integrated `main` builds successfully but does
not replace or redefine the user-accepted artifact. The accepted executable's
build logs, timestamps, source audit, and preserved bytes establish
provenance-based correspondence with the subsequently committed source state;
they do not create a cryptographic link from that pre-commit binary to Git tree
`f3be68f`. The clean integrated rebuild independently verifies that exact Git
tree. Both executables have the same byte size.

The repository has no established executable-release channel, so this
publication promotes the accepted source state and records the artifact
identity without creating an unrelated GitHub release.

## Known Limitation

The accepted behavior centers an exact `1920 x 1080` outer window in the
monitor work area. On a display whose usable work area is shorter than 1080
pixels, preserving that exact size can overlap reserved desktop space. This
known behavior was not changed during publication because doing so would have
changed the accepted build.

## Canonical Resolution

For an unqualified request for "latest verified," "newest version," "newest
good build," or equivalent wording, resolve UVSR to renderer merge
`64cd553e49f4b0818c634b6c6ddefa809daf8736` and accepted executable SHA-256
`350495C5FCBD0F9965AA0D2959F8532277AEBA15E82B6291A7624BDA224120A5` until a
later renderer checkpoint on live Canonical first-parent history independently
satisfies all Canonical verified requirements.

The commit that publishes this verification record is documentation-only after
`64cd553`. It does not supersede the accepted renderer, shader, asset, test,
runtime-setting, source-tree, or executable identity.
