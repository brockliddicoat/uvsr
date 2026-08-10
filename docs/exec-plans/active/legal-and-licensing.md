# Legal and Licensing: Repository Reorganization

## Status

- State: active; repository work is verified, while hosted CLA enforcement is
  waiting on the governing-law choice, counsel review, and an authorized test
  pull request
- Coordinator: `/root`
- Project/integration branch and worktree: `codex/legal-and-licensing` in `work/legal-and-licensing`
- Base commit: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Started: 2026-08-09
- Last updated: 2026-08-09
- Planned archive: `docs/exec-plans/completed/legal-and-licensing.md`

## Goal and Done Condition

Goal: reorganize repository legal material, document substantial external source use, apply a clear noncommercial source-available license with attribution and a commercial contact path, and require contributor licensing checks on pull requests.

Done when:

- [x] The top-level `third_party` material is reorganized under `legal/` with clear license, code-sample, and source-documentation sections.
- [x] Every substantial identified source has a concise record classifying conceptual influence, implementation reference, adaptation, or incorporated code/assets and identifying governing terms.
- [x] UVSR's first-party license clearly permits credited noncommercial use and reserves commercial use for a separate written agreement without overriding third-party terms.
- [x] The README contains a short accurate licensing summary and commercial contact link.
- [ ] A pull-request contribution agreement check is configured with contributor-facing instructions.
- [x] Links, heading casing, source records, build packaging, and relevant repository checks pass.
- [ ] The final branch is committed and pushed to GitHub only under the user's stated GitHub authorization; no pull request or merge is implied.

## Scope

In scope:

- Repository license and contributor terms.
- `README.md`, top-level `LICENSE`, `legal/`, affected build/reference paths, and `.github/` contribution automation.
- Historical provenance research sufficient to create one durable record per substantial source.

Non-goals:

- Relicensing third-party works or dependencies.
- Claiming Open Source Initiative approval.
- Resolving every future commercial sublicensing dependency.
- Runtime rendering or performance changes.

Affected subsystems and paths:

- `README.md`, `LICENSE`, `legal/**`, `src/third_party/**`, `.github/**`, and references to `third_party/**`.

Shared hotspots reserved for the coordinator:

- `README.md`, repository license terms, contributor agreement text, workflow configuration, path moves, and this execution plan.

## Baseline

- Canonical repository/remote: `https://github.com/brockliddicoat/uvsr.git`, live `main` at `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`.
- Local versus remote state: isolated branch equals live remote `main`; the original checkout's `main` is independently ahead by two and behind by 28 with an unrelated untracked file.
- Verified source commit/build: documentation and workflow task; the active lineage's last verified executable will be linked without rebuilding unless a source/build path change requires a build.
- Known pre-existing failures: none established for this isolated worktree.

## Dependencies and Interfaces

| Dependency/task | Required revision or decision | Status | Consumer |
| --- | --- | --- | --- |
| License research | Accurate first-party terms and third-party boundary | Complete | Coordinator |
| Provenance audit | Substantial-source inventory and evidence | Complete | Coordinator |
| CLA automation research | Maintainable pull-request enforcement design | Complete | Coordinator |

Public contracts:

- Repository license notices must distinguish first-party UVSR material from separately licensed third-party code, assets, and dependencies.
- Source records use a consistent evidence classification and do not overstate code reuse.
- Contribution automation must bind accepted pull-request contributions to the repository's licensing needs while preserving contributor copyright.

## Assignment Summary

| Task ID | Owner | Branch/worktree | Base | Write scope | Dependencies | Status |
| --- | --- | --- | --- | --- | --- | --- |
| LIC-1 | Research agent | Shared read-only | `7202ff9` | None | None | Complete |
| PROV-1 | Repository audit agent | Shared read-only | `7202ff9` | None | None | Complete |
| CLA-1 | Workflow research agent | Shared read-only | `7202ff9` | None | None | Complete |
| INT-1 | `/root` | `codex/legal-and-licensing` | `7202ff9` | All task-owned paths | LIC-1, PROV-1, CLA-1 | Active |

## Assignment Contracts

### LIC-1: License Model Research

- Owner/thread: research agent
- Base commit/state: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Read scope: published license texts and official guidance.
- Write scope: none.
- Deliverable: options, exact constraints, recommended model, required notices, and primary-source links.
- Done when: the recommendation matches credited noncommercial use, separate commercial licensing, and mixed third-party licensing.
- Allowed Git and external actions: read-only; no repository or GitHub writes.
- Stop and report if: custom terms would require representing them as an unmodified named license.

### PROV-1: Substantial Source Provenance Audit

- Owner/thread: repository audit agent
- Base commit/state: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Read scope: repository history, docs, source comments, dependency metadata, and existing attribution records.
- Write scope: none.
- Deliverable: deduplicated substantial-source inventory with evidence paths, likely classification, license, and uncertainties.
- Done when: major architectural, implementation-reference, adapted, incorporated-code, and asset sources are covered.
- Allowed Git and external actions: read-only; no repository or GitHub writes.
- Stop and report if: provenance cannot be supported by repository evidence.

### CLA-1: Contributor Licensing Automation Research

- Owner/thread: workflow research agent
- Base commit/state: `7202ff958d2ed5ffa5a54f7374c1d15c772307a5`
- Read scope: current `.github/` state and official bot/action documentation.
- Write scope: none.
- Deliverable: recommended functional setup, permissions/secrets requirements, contributor experience, and risks.
- Done when: the design can block unaccepted pull requests and is maintainable for a public repository.
- Allowed Git and external actions: read-only; no installations or GitHub writes.
- Stop and report if: setup requires credentials or an external account action unavailable from repository files alone.

## Integration Order

1. Settle the license model and contribution grant.
2. Settle the folder taxonomy and provenance classification.
3. Move existing material and add source records.
4. Add workflow automation and contributor guidance.
5. Update README and all affected references.
6. Run independent review and verification, then archive this plan.

## Verification Plan

| Acceptance criterion | Evidence required | Command/experiment | Result/artifact |
| --- | --- | --- | --- |
| Folder and links are coherent | No stale tracked `third_party/` paths; Markdown links resolve | Repository search and link checker | Passed; 61 source-distribution Markdown files and four packaged Markdown files checked |
| Terms match stated intent | Independent legal-language consistency review | Text review against source license terms | Passed with counsel and activation boundaries retained |
| CLA is functional | Valid configuration and documented activation boundary | App installation, metadata validation, and live ruleset inspection | App installed only for UVSR; enforcement pending counsel, a signing Gist, a test pull request, and the source-specific rule |
| Documentation headings conform | Full in-scope heading scan and repository validator | `tools/check_document_title_case.py` | Passed; 1,665 headings and bold lead-ins |
| Build and packaging remain coherent | Release build, shader bundle, notices, and tests | CMake Release build, package hashes, and CTest | Passed after repairing one task-introduced contract expectation; final run passed 40 of 40 tests |
| Changes are scoped | Clean task worktree except task-owned diff | `git status` and diff review | Pending final staged-diff review |

## Decisions

| Date/time | Decision | Reasoning and rejected alternatives | Tasks affected |
| --- | --- | --- | --- |
| 2026-08-09 | Work from live remote `main` in an isolated branch/worktree. | The original checkout diverges from GitHub and contains unrelated work. | All |
| 2026-08-09 | Keep all subagents read-only; coordinator is the sole writer. | License, folder, README, and workflow decisions are tightly coupled shared hotspots. | All |
| 2026-08-09 | Use the unmodified PolyForm Noncommercial License 1.0.0 instead of Sentry's FSL. | PolyForm matches the requested noncommercial boundary. FSL permits broader commercial use and automatically changes each release to MIT or Apache 2.0 after two years. | LIC-1, INT-1 |
| 2026-08-09 | Describe commercial licensing as separately available, not as a present dual license. | No standard commercial agreement exists yet, and third-party rights cannot be sublicensed by UVSR. | LIC-1, INT-1 |
| 2026-08-09 | Install hosted CLA Assistant only for UVSR, but do not collect signatures or enforce its status yet. | The CLA still needs a governing-law choice and qualified review; GitHub cannot safely require the status until a test pull request establishes the exact app-produced check. | CLA-1, INT-1 |

## Progress and Handoffs

| Date/time | Task/owner | Status | Revision/artifact | Checks | Risks/next action |
| --- | --- | --- | --- | --- | --- |
| 2026-08-09 | `/root` | Active | Isolated branch at `7202ff9` | Repository, remote, worktree, and PR preflight complete | Begin research and inventory |
| 2026-08-09 | Research agents | Complete | License, provenance, and CLA handoffs | Primary-source review and independent consistency review complete | Integrate and verify |
| 2026-08-09 | Provenance agent | Complete | `legal/sources/README.md` plus 50 records | Legal inventory, local links, and scoped title validation passed | Ownership released |
| 2026-08-09 | `/root` | Active | Release candidate `build-legal/bin/uvsr.exe` | Release build passed; packaged project license and notices verified; final CTest run passed 40 of 40 tests | Complete staged review and publish branch |
| 2026-08-09 | Independent technical reviewer | Complete | Combined task diff and `build-legal/bin` | No P0, P1, or P2 findings; relocated shader dependency and all 21 packaged notices verified | Ownership released |

## Risks and Escalation Triggers

- A custom noncommercial license may be source-available rather than open source; documentation must say so plainly.
- The FSL name and standard text must not be used inaccurately if the desired restriction differs from an unmodified FSL grant.
- Third-party components remain under their own licenses and may prevent a single commercial sublicense without cleanup or separate permissions.
- Repository-only CLA automation may still require a one-time GitHub App installation or branch-protection rule.
- Collecting binding signatures before the governing-law, contracting,
  electronic-signature, privacy, and retention choices are reviewed would turn
  a technical setup into an avoidable legal risk.

Stop and ask the user if:

- A required choice would materially change whether commercial evaluation, internal business use, or paid services are allowed.
- A workflow requires an external account/app installation that cannot be safely completed under existing authorization.

## Completion

- Final integrated commit: pending
- Verification summary: Release build and final 40-of-40 CTest run passed;
  legal inventory, title casing, local links, package contents, and line counts
  passed
- Independent review: legal-language and technical reviews passed with no
  remaining P0, P1, or P2 findings; counsel/activation boundaries remain
- Coming Soon/documentation update: no roadmap entry planned; this is repository governance work
- Pushed/PR/merged, or intentionally local: pending
- Remaining experiments or follow-ups: choose governing law, obtain qualified
  review, publish the versioned signing Gist, exercise CLA Assistant on an
  authorized test pull request, and then require its app-specific check
- Active ownership released: all delegated paths released; coordinator remains
  active for hosted activation
- Archived to completed/abandoned path: remains active until hosted CLA
  enforcement is safely activated
