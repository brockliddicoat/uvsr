# Launcher Public Source Transition

## Status

- State: completed
- Coordinator: `/root`
- Branch/worktree: `codex/launcher-reliability` at `C:\Users\brock\OneDrive\Documents\uvsr\work\launcher-reliability`
- Base commit: `0c8074848985152ed83f83b4087aaf10013de590`
- Started: 2026-08-20
- Predecessor: `docs/exec-plans/completed/launcher-source-feed-convergence.md`

## Goal and Done Condition

Goal: turn the correctly diagnosed sequence-4/public-source mismatch into a launcher flow that can install the corrected renderer now, using a single embedded compatibility bridge bound to the exact public base, exact patch bytes, exact resulting tree, and exact deterministic source commit without damaging the existing installation.

Done when:

- [x] The exact installed log and public-source state are reproduced.
- [x] The smallest safe transition is selected with its publication boundary explicit.
- [x] The authorized local implementation and regression coverage pass.
- [x] The exact usable artifact is handed off with an isolated full installation-equivalent build proving that unpublished public source can be bridged safely.

## Scope

In scope:

- Read-only audit of the screenshot, installed launcher log, current public `main`, source contract, and release workflow.
- A narrowly scoped compatibility transition if it remains honest about source provenance and preserves trusted pins.
- An embedded bridge for only public commit `0c8074848985152ed83f83b4087aaf10013de590`, with a permanent synthetic-source-to-public-base ancestry mapping.
- Local tests, rebuilds, documentation, and independent review for any implementation.

Non-goals:

- Pushing, merging, publishing source, changing the public feed, signing, or releasing without explicit user authorization.
- Bundling an opaque prebuilt renderer, accepting unknown bases/contracts, downloading a patch, or falling back to the preview Direct3D runtime.

## Baseline

- The installed sequence-4 launcher correctly resolved public `main` to `0c8074848985152ed83f83b4087aaf10013de590` and preserved the installed renderer when `cmake/uvsr-launcher-build-contract-v1.json` was absent.
- The matching stable-runtime renderer and source-contract changes exist only in this dirty local feature worktree.
- Public installation therefore cannot currently consume the corrected renderer source.

## Assignment Summary

| Task | Owner | Write Scope | Required Result | Status |
| --- | --- | --- | --- | --- |
| Transition architecture | read-only worker | none | Determine whether any safe launcher-only transition exists and compare it with publication | Complete |
| Release readiness | read-only worker | none | Identify the minimum exact GitHub/source/feed/signing actions needed for a real install | Complete |
| Embedded bridge implementation | one coordinator-owned writer | launcher source, bridge generator/resource, tests, and build gate | Apply the exact audited renderer delta under a composite source identity | Complete |
| Integration | `/root` | scoped candidate files, docs, metadata, and this plan | Review, verify, rebuild, and hand off | Complete |

## Verification Plan

| Criterion | Evidence | Status |
| --- | --- | --- |
| Existing install preserved | Installed log and state inspection | Confirmed by log; no candidate activated |
| Corrected source is consumable | Exact base plus verified embedded bridge produces a clean deterministic source commit with the strict contract and renderer fixes | Confirmed by isolated source preparation and full build/package smoke |
| Trust remains closed | Unknown/missing contracts, bases, patches, trees, commits, and unverified artifacts remain rejected | Confirmed by generator, mutation, Git-application, schema, and release-identity tests |
| Candidate quality | Launcher/native/package checks plus independent review | 90/90 launcher tests, 49/49 native tests, package smoke, two independent bridge reviews |

## Risks and Escalation Triggers

- A production fix likely crosses the explicit publication boundary because the launcher intentionally builds public source.
- The bridge must record a real deterministic synthetic Git commit whose sole parent is the public base; it must never record the patched output as unmodified public `main`.
- Do not apply a downloaded, mutable, fuzzy, three-way, rejected, or unknown-base patch, or weaken the contract merely to make the dialog disappear.
- Stop for user authorization before any push, merge, feed mutation, signing, or release.

## Completion

- Selected transition: produce a new local `1.1.4` sequence-`5` launcher with an exact embedded compatibility bridge. The bridge applies only to public base `0c8074848985152ed83f83b4087aaf10013de590`, validates the canonical 24,581-byte single-stream patch SHA-256 `e68f814e2e838ef08bf8561bfa033dbaa0b5a523f776983dca18e8dd83ad799a`, requires resulting tree `736fc012878dc66e5f512dab40d722a56ac8c1f5`, creates and verifies one deterministic synthetic commit, and permanently maps that commit back to the public base for ancestry. Once the corrected source is public, normal commit-local contracts supersede the active bridge.
- Verification: the exact production source-preparation path passed for public
  base `0c8074848985152ed83f83b4087aaf10013de590`, resulting in synthetic commit
  `ac19135e176bd137df050fb3da11297a2460312d` and tree
  `736fc012878dc66e5f512dab40d722a56ac8c1f5`; the complete isolated
  configure/build/runtime-contract/package smoke produced `uvsr.exe` SHA-256
  `a24105505c4396995a6b6f31822ea919678ef2db0301765073bdafdcce21d890`.
- Launcher verification: 90/90 contract tests and the release-identity gate
  passed. Launcher `1.1.4` sequence `5` health check exited zero; the exact
  unsigned local artifact is 58,399,489 bytes with SHA-256
  `40fbd3eb555593c484e8cdb1f1244ed07ffde29829977501fa8c8a2c8d0f0297`.
- Native verification: 49/49 CTest checks passed, including GPU capability,
  Unicode executable-path, runtime shader bundle, and production shader bundle
  contracts.
- Publication state: unchanged unless explicitly authorized
- Archive destination: `docs/exec-plans/completed/launcher-public-source-transition.md`

Sequence `4` cannot become the public signed identity: its unsigned bytes were already installed and recorded under that identity. Signing or adding the permanent signer pin changes the executable hash, and the launcher's anti-equivocation rule correctly rejects different bytes at the same version and sequence. Launcher `1.1.4` sequence `5` was then fast-forwarded to public `main`, where its first clean GitHub build exposed a temporary Git-object cleanup defect before artifact upload. It was not published as a signed feed identity. The replacement public identity must therefore be `1.1.5` sequence `6`.
