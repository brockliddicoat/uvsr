# UVSR Engine Cutdown Archive

## Purpose

This folder keeps each major renderer cutdown as a separate dated record. The
reports explain what changed, what remains, why each surface was removed, and
what must be restored together if a retired feature becomes valuable again.

## Dated Reports

| Record Date | Report | Scope |
| --- | --- | --- |
| 2026-07-30 | [Shader Permutation Cutdown](2026-07-30-shader-permutation-cutdown.md) | The first shader-focused cutdown: Runtime-only Visibility sampling, focused TAA choices, Offline-noise retirement, emissive-transport cleanup, and the factory experiment profile. |
| 2026-08-02 | [Engine Core Cutdown and Restoration Report](2026-08-02-engine-core-cleanup.md) | The verified broader engine-cleanup snapshot plus its later same-day front-end restoration addendum, further cuts, complete ledgers, and restoration guidance. |
| 2026-08-03 | [Front-End Fidelity Restoration](2026-08-03-frontend-fidelity-restoration.md) | Detailed retained Statistics, final preset Custom/reset behavior, expanded Debug and World defaults, resolution-aware Reconstruction disclosure, compact command results, launcher cleanup, and immutable integrated verification over the unchanged backend cutdown. |
| 2026-08-23 | [Stage-Two Renderer and Launcher Cutdown Decisions](2026-08-23-stage-two-cutdown-decisions.md) | Pre-cut evidence, contradictions, recovery boundaries, replacements, risks, and required validation for eight ordered renderer, scene, launcher, Python, identity, and dependency reductions. |
| 2026-08-23 | [ReSTIR Path Tracing Postmortem](2026-08-23-restir-path-tracing.md) | Intended value, exact implementation and burden, evidence gaps and failures, drift, recovery, retirement rationale, and clean-room retry gates for the removed ReSTIR family. |
| 2026-08-23 | [Sample Accumulation Settings Postmortem](2026-08-23-sample-accumulation-settings.md) | Complete policy surface, renderer interactions, measured burden, reset ambiguities, evidence gaps, recovery, and lessons behind the one-accumulator contract. |
| 2026-08-23 | [Temporal AA Recipe Consolidation](2026-08-23-temporal-aa-recipe-consolidation.md) | Purpose, callers, 192-to-16 task replacement, risks, recovery, and required rendered evidence for four retired TAA selectors. |
| 2026-08-23 | [MSAA Per-Sample Visibility Control Retirement](2026-08-23-msaa-per-sample-visibility.md) | Retirement of the persisted shadow-frequency switch in favor of unconditional per-receiver visibility, with exact recovery and rendered-proof gates. |

## Count Comparability

The archive describes historical repository snapshots plus August 23 records
at mixed completion states. Their counts, executable hashes, tests, and
confidence must not be mixed or added together. Features landed between the
July 30 cutdown and the August 2 baseline, and the later reports count integrated
first-party shader manifests differently from the original core-only catalog.

Use the definitions and exact artifacts inside each report when comparing a
historical build. In the August 2 report, use the original sections for what the
engine cleanup removed and for its verified evidence; use the dated addendum
for that restoration candidate's behavior and additional removals. Within the
August 3 report, the original sections own the `CF8D...` artifact and the dated
final addendum owns the accepted `194D...` refinement and integrated
`b4dc241...` verification.
The initial baseline and numbered decision sections of the August 23 report are
pre-cut evidence over exact base `e29a412...`. Later sections explicitly labeled
as completed, provisional, transitional, or final evidence describe subsequent
work at their own stated boundary. Provisional and transitional sections are
non-complete; every open gate remains a requirement, not a completion claim.

## Restoration Use

Start with the dated report that removed the feature. Follow its dependency and
verification notes before copying historical source. Restoring a shader alone
is insufficient when the removed behavior also owned CPU settings, bindings,
resources, UI, packaging, tests, or benchmark identity.

No immutable commit containing the transient transparent shadow-edge overlay is
known. It is absent from the August 2 recovery commit documented by that report.
Treat the addendum contract as a reimplementation guide, not recoverable source.
