# Launcher Unsigned Self-Update Release

## Status

- State: active
- Coordinator: `/root`
- Branch/worktree: `codex/launcher-update-feed-v1.1.14` in
  `work/launcher-reliability`
- Base/live `main`: `5762bb9f00dd1cd9f62aa19bc56e6f28215f30b4`
- Started: 2026-08-21
- Publication authority: the user explicitly authorized committing to GitHub
  `main`, updating launcher download links, and enabling launcher updates.

## Goal and Done Condition

Publish the verified fresh-install repair and an explicitly unsigned launcher
line whose releases can update through the launcher after one unavoidable manual
bootstrap from the already-published 1.1.12 binary.

Done when:

- [x] The fresh-install repair is merged to protected `main` through a green PR.
- [x] Unsigned launcher updates retain strict signed-metadata, hash, identity,
  health, version, sequence, source-commit, URL, and activation validation.
- [x] Launcher 1.1.13/sequence 14 is rebuilt from its exact merged commit and
  published as the one-time feed-capable update bootstrap.
- [ ] A newer exact launcher identity is published and advertised through the
  signed version-2 feed and generated README link while both version-1 feeds
  remain frozen and byte-identical.
- [ ] A 1.1.13 launcher updates to the newer release through its Update flow.
- [ ] Post-merge and public anonymous download/install checks pass.

## Scope

In scope:

- The completed Geist checkout repair and blobless exact-source resolver.
- Explicit unsigned launcher self-update policy and warnings.
- Launcher verification, update UX, tests, documentation, workflow, version,
  input lock, release artifacts, feeds, and generated README links.
- Required commits, feature pushes, protected-main PRs/merges, immutable GitHub
  prereleases, and post-publication monitoring.

Non-goals:

- Claiming Authenticode publisher assurance for unsigned binaries.
- Rewriting or replacing immutable 1.1.12 release bytes.
- Bypassing protected `main` or its required checks.
- Changing renderer/package/state schemas or pinned Donut source.

## Release Constraint

Launcher 1.1.12 was compiled with an empty publisher pin that deliberately
rejects every launcher self-update. No feed or release change can alter that
already-published behavior. A one-time manual installation of the feed-capable
1.1.13 update bootstrap is therefore unavoidable. Self-update begins with
1.1.13 and is proven using a later release.

## Assignment Summary

| Task | Owner | Scope | Status |
| --- | --- | --- | --- |
| UNSIGNED-UPDATE-AUDIT | `/root/bridge_identity_design` | Read-only runtime trust design | Complete |
| UNSIGNED-RELEASE-AUDIT | `/root/bridge_implementer` | Read-only workflow/release design | Complete |
| PUBLISH-INTEGRATION-AUDIT | `/root/bridge_patch_audit` | Read-only integration/publication review | Complete |
| UNSIGNED-UPDATE-WRITE | `/root/bridge_identity_design` | Runtime verifier, activation trust, tests | Complete |
| UNSIGNED-RELEASE-WRITE | `/root/bridge_implementer` | Feed tooling, workflows, generated-link states | Complete |
| IMPLEMENTATION | `/root` | Documentation, identity lock, integration, verification | In progress |
| PUBLICATION | `/root` | Commit, PR, merge, releases, feeds, links, monitoring | In progress |

## Frozen Trust Contract

- Version-1 launcher feeds remain byte-for-byte frozen compatibility records and
  never authorize an unsigned executable.
- The canonical version-2 feed is
  `launcher/launcher-update-feed-v2.json`; it has no mutable latest-release
  fallback and no legacy alias.
- Its strict four-field envelope carries one Base64 UTF-8 payload and one fixed
  64-byte IEEE-P1363 ECDSA P-256/SHA-256 signature.
- The strict payload binds schema 2, the UVSR product, channel `stable`, release
  sequence, semantic version, exact source commit, and artifact name, size, and
  SHA-256.
- The compiled key ID is `uvsr-launcher-update-p256-2026-01`; the public SPKI
  SHA-256 is
  `73e4079b971aa69eec69e87b4e581cde0da10d410299f124d8c3092fc0324ed9`.
- The authoritative private key is restricted outside the repository for local
  release signing. CI receives only the compiled public key and verifies
  signatures; private-key bytes must never enter source, Actions secrets, logs,
  artifacts, or generated metadata.
- A candidate executable must be Authenticode `NotSigned`, contain no embedded
  certificates, and independently match size, hash, x64 architecture, exact
  ProductVersion source commit, FileVersion, and health identity.

## Integration and Publication Order

1. Freeze the explicit unsigned update trust contract and regression matrix.
2. Implement it on the current repair branch, advance/refresh identity if
   required, and rerun full launcher/native/package verification.
3. Commit exact task paths, push the feature branch, open a ready PR, and merge
   only after required checks pass.
4. Rebuild launcher 1.1.13 from the exact merged commit and publish its immutable
   versioned prerelease with executable/checksum/attestations.
5. Advance to a newer launcher identity through a second protected-main PR. In
   that PR, introduce the signed version-2 feed and generated README link for
   1.1.13 while proving both version-1 feeds remain frozen. Rebuild the newer
   identity from the merged commit and publish its immutable prerelease.
6. Advance the signed version-2 feed and generated README link to the newer
   release atomically through a final protected-main PR while again proving both
   frozen version-1 feeds remain unchanged; validate the release before merge.
7. Prove anonymous download, checksum, metadata, health, manual bootstrap, and
   in-app 1.1.13-to-newer update on the public artifacts.

## Verification Plan

| Area | Required Evidence | Status |
| --- | --- | --- |
| Runtime trust | Unsigned-only candidate, strict feed hash/size/URL/identity/health, rollback rejection | Complete locally; 102/102 launcher tests passed |
| Backward behavior | 1.1.12 documented as manual-bootstrap-only | Complete; generated README now advertises the feed-capable 1.1.13 bootstrap while retaining the 1.1.12 limitation |
| Launcher | Release build, full contract suite, health check | Complete for exact merged and public 1.1.13/sequence 14 and 1.1.14/sequence 15 artifacts |
| Renderer/install | Fresh configure/build/package/native suite from exact source | Complete on exact public `main` through launcher-managed LocalAppData smoke; 50/50 native tests passed |
| GitHub release | Immutable exact-commit tag, two exact assets, checksums, attestations | Complete for 1.1.13 and 1.1.14 |
| Public update | 1.1.13 installs manually and updates in-app to the advertised newer release | Pending |

## Risks and Stop Conditions

- Unsigned self-update authority comes from the pinned P-256 metadata key plus
  exact immutable-release identity and hashes, not repository JSON alone and not
  an Authenticode publisher certificate. UI and docs must state that boundary.
- Do not claim the already-published 1.1.12 can self-update; it cannot.
- Stop publication if the release commit, feed, assets, checksum, metadata,
  health identity, attestation, or README state disagree.
- Stop if protected-main required checks are unavailable or failing.

## Progress and Evidence

| Date | Phase | Evidence | Result |
| --- | --- | --- | --- |
| 2026-08-21 | Local integration | Launcher identity `1.1.13`/sequence `14`, input lock `af5695818977ea2c0fa17754811fce34e5499c664352889a3b38fac04778288b` | Unique advance from `50550da`; verified |
| 2026-08-21 | Launcher verification | Release build SHA-256 `62c966dcd88af37358d636397c2f597a8121bc3c295a157f0ca15e85f421ce44`; x64; `NotSigned`; health `14`/`1.1.13` exit `0` | 102/102 launcher tests passed; local preview only because ProductVersion still names the pre-merge base |
| 2026-08-21 | Protected integration | PR #39; exact merge commit `6837da7ae5a5df7d27425d09a7d272f218f4bbf5` | Six PR checks and five post-merge `main` workflows passed, including both Windows launcher builds |
| 2026-08-21 | Public fresh-install smoke | Launcher-managed checkout/configure/build/package/activation from exact public `main` in LocalAppData | Passed; staged renderer SHA-256 `8fb30d9bcfc1a14cc1bd722b11699cf2c823dba0f844956a9e5ec4c805d57459`; renderer not launched |
| 2026-08-21 | Immutable 1.1.13 release | Tag and target `6837da7a`; EXE 59,060,884 bytes / SHA-256 `3b0e9c5826b03fa78288541b11bc6d2a5d745e450597b110d9d1b4d65be223d5`; checksum 99 bytes / SHA-256 `79deef75cd2ec275146ea93b475ef6a5a36e3958e2089059d30a52aa661de949` | Public immutable prerelease; release and both assets attest; anonymous bytes, x64, `NotSigned`, metadata, and health passed |
| 2026-08-21 | Feed signer repair | Production round trip exposed positional array splatting in the new signer | Replaced with named hashtable splatting and added a regression before creating the first production feed |
| 2026-08-21 | Sequence-15 candidate | Identity `1.1.14`/sequence `15`, input lock `06a69b4eb8dcf0f50983c25d21643e827c822f55e57008ead76e8fe21ec7548d`; local artifact SHA-256 `4ac8688cd1684592dc8ec2f6edf4bb65a1de931bf09324f82495646972c6f88f` | 102/102 launcher tests, x64, `NotSigned`, metadata, and health passed; pre-merge artifact is not releasable because its ProductVersion names base `6837da7a` |
| 2026-08-21 | Sequence-15 integration | PR #40; exact merge commit `5762bb9f00dd1cd9f62aa19bc56e6f28215f30b4` | Six PR checks and five post-merge `main` workflows passed, including both PR Windows builds and the exact-main Windows build |
| 2026-08-21 | Immutable 1.1.14 release | Tag and target `5762bb9f`; EXE 59,060,905 bytes / SHA-256 `3d7e00ef62188dfbbec8e86e2cd7217f677d5bbf0bfb862c22dfd2fadf791be9`; checksum 99 bytes / SHA-256 `accf3acee4ba5bae0002774970e7c0451d358bcd200351ca81ea986812913c6c` | Public immutable prerelease; release and both assets attest; anonymous bytes, x64, `NotSigned`, metadata, and health passed |
| 2026-08-21 | Final feed candidate | Production-signed version-2 feed and generated README advance from 1.1.13/sequence 14 to 1.1.14/sequence 15 | Strict verifier and README synchronization pass; protected feed-only PR pending |
| 2026-08-21 | Renderer verification | Incremental native source-contract rebuild plus complete isolated renderer test tree | 50/50 native tests passed |

## Completion

- Commits/PRs/merges: fresh-install and sequence-15 launcher phases complete; final feed-only PR pending
- Releases/feed/link: immutable 1.1.13 and 1.1.14 complete; signed 1.1.14 feed/README advancement pending protected integration
- Public manual install and in-app update: pending
- Independent final review: pending
- Archive path:
  `docs/exec-plans/completed/launcher-unsigned-self-update-release.md`
