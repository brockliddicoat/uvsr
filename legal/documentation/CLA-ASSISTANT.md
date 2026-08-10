# CLA Assistant Setup

UVSR's planned enforcement uses the hosted
[CLA Assistant](https://cla-assistant.io) GitHub App so every pull-request
committer is covered before merge. Adults who personally control their work
accept the exact
[UVSR Contributor License Agreement](../licenses/UVSR-CONTRIBUTOR-LICENSE-AGREEMENT.md).
Entity-, employer-, client-, guardian-, or other owner-approved contributions
need a separately reviewed agreement preserved independently, with the covered
GitHub identity administratively recognized by the app. The app
reports the `license/cla` status. Until the signing configuration, test pull
request, and required `main` ruleset are all verified, this check is documented
but not enforced. Repository files alone cannot complete activation.

## Current Activation State

On August 9, 2026, the hosted CLA Assistant GitHub App was installed with
repository access limited to `brockliddicoat/uvsr`. No signing Gist is linked,
no contributor signatures are being collected, and no `main` ruleset requires
the app's status. The installation is therefore non-enforcing until the
remaining steps below are completed.

## Signing Copy

Create a public GitHub Gist whose main file is byte-for-byte identical to the
repository CLA. Add a second Gist file named `metadata` using
[`cla-assistant-metadata.json`](cla-assistant-metadata.json). Record the Gist
revision and SHA-256 digest in the activation log. A changed CLA must use a new
version and signature record; editing only the repository copy does not make
existing signers accept new terms.

## One-Time Activation

1. Have qualified counsel confirm the contributor agreement, contracting party,
   and privacy notice.
2. Install the CLA Assistant GitHub App only for `brockliddicoat/uvsr`.
3. In CLA Assistant, link `brockliddicoat/uvsr` to the public signing Gist and
   provide the repository privacy-notice URL.
4. Open a test fork pull request so the app emits `license/cla`.
5. Create an active `main` ruleset that requires pull requests and the
   `license/cla` check specifically from the CLA Assistant App. Block branch
   deletion and force pushes, and do not add routine bypass actors.
6. Preserve each separately executed owner agreement independently, then
   import or approve its covered GitHub identity in CLA Assistant and record
   the mapping and evidence. Do not use a branch-rule bypass.
7. Review each bot's outbound license and provenance before explicitly
   approving its identity; bots cannot sign and must not receive a blanket
   bypass.
8. Verify unsigned, signed, separately approved, multi-author, newly added
   author, bot, and merge queue behavior. Every committer must be covered.
9. Export signature records periodically and preserve the CLA, Gist revision,
   digest, metadata schema, and privacy notice that applied to each signature.

Do not require the status before the app has emitted it successfully: an
unavailable or misnamed check can lock every pull request. An installed app
without a required ruleset is advisory and does not protect the merge path.

## Contributor Experience

On a pull request, the app identifies uncovered committers and provides an
eligible adult individual a GitHub-authenticated signing link. A separately
approved owner identity or reviewed bot identity must be marked through CLA
Assistant's administrative flow instead of accepting the individual agreement.
The executed owner agreement remains in UVSR's independent legal records. Once
all committers are covered, the app updates `license/cla`. A later commit from a
new author must make the check pending again until that identity is covered.

## Security and Maintenance

The hosted app avoids a repository secret or personal access token and does not
execute pull-request code. Limit installation access to this repository, review
the app's permissions, retain independent signature exports, and periodically
confirm that the expected status is still emitted by the same GitHub App.

Do not replace this setup with the archived CLA Assistant Lite action unless a
separately reviewed migration is necessary. A DCO is also insufficient because
it does not grant the commercial relicensing and sublicensing rights UVSR needs.
