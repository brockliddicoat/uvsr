# UVSR Engine Cutdown Archive

## Purpose

This folder keeps each major renderer cutdown as a separate dated record. The
reports explain what changed, what remains, why each surface was removed, and
what must be restored together if a retired feature becomes valuable again.

## Dated Reports

| Completion Date | Report | Scope |
| --- | --- | --- |
| 2026-07-30 | [Shader Permutation Cutdown](2026-07-30-shader-permutation-cutdown.md) | The first shader-focused cutdown: Runtime-only Visibility sampling, focused TAA choices, Offline-noise retirement, emissive-transport cleanup, and the factory experiment profile. |
| 2026-08-02 | [Engine Core Cutdown and Restoration Report](2026-08-02-engine-core-cleanup.md) | The verified broader engine-cleanup snapshot plus its later same-day front-end restoration addendum, further cuts, complete ledgers, and restoration guidance. |
| 2026-08-03 | [Front-End Fidelity Restoration](2026-08-03-frontend-fidelity-restoration.md) | Detailed retained Statistics, final preset Custom/reset behavior, expanded Debug and World defaults, resolution-aware Reconstruction disclosure, compact command results, launcher cleanup, and Canonical handoff over the unchanged backend cutdown. |

## Count Comparability

The archive describes five repository snapshots: the July 30 shader cutdown,
the verified August 2 engine cleanup, the later August 2 front-end restoration,
the first August 3 frontend-fidelity candidate, and its final August 3 accepted
refinement. Their counts, executable hashes, tests, and confidence must not be
mixed or added together. Features landed between the July 30 cutdown and the
August 2 baseline, and the later reports count integrated first-party shader
manifests differently from the original core-only catalog.

Use the definitions and exact artifacts inside each report when comparing a
historical build. In the August 2 report, use the original sections for what the
engine cleanup removed and for its verified evidence; use the dated addendum
for that restoration candidate's behavior and additional removals. Within the
August 3 report, the original sections own the `CF8D...` artifact and the dated
final addendum owns the accepted `194D...` refinement and publication handoff.

## Restoration Use

Start with the dated report that removed the feature. Follow its dependency and
verification notes before copying historical source. Restoring a shader alone
is insufficient when the removed behavior also owned CPU settings, bindings,
resources, UI, packaging, tests, or benchmark identity.

The exact transient transparent shadow-edge overlay source is not recoverable
from the August 2 base commit or the current candidate. Treat its addendum
contract as a reimplementation guide unless a separate intake patch or real
checkpoint is preserved.
