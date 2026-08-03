# Engine Core Cleanup Snapshot — Superseded August 2, 2026

## Status

- State: superseded verified cleanup snapshot
- Coordinator: `/root`
- Branch: `codex/engine-core-cleanup`
- Worktree:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\engine-core-cleanup`
- Base and live `origin/main` at start:
  `f7c0c87d8cba6880428fbc34400eb2882fb5182e`
- Started and completed: 2026-08-02
- Publication state: intentionally local; not committed, pushed, merged, or
  released
- Complete cut/modification and restoration inventory:
  [Engine Core Cutdown and Restoration Report](../../postmortem/engine-cutdowns/2026-08-02-engine-core-cleanup.md)
- Earlier dated shader record:
  [Shader Permutation Cutdown](../../postmortem/engine-cutdowns/2026-07-30-shader-permutation-cutdown.md)
- Superseding work:
  [Front-End Restoration Addendum](../../postmortem/engine-cutdowns/2026-08-02-engine-core-cleanup.md#front-end-restoration-addendum--august-2-2026)

Every use of *final*, *current*, *complete*, and *verified* below refers only to
the parent cleanup artifact identified in Verification Evidence. The later
front-end restoration keeps the backend cuts but changes UI behavior, removes
three more paths, rebuilds the executable, and carries independent evidence.

## Goal and Done Condition

The goal was to retain UVSR's supported, user-configurable renderer core while
removing dormant paths, reducing shader and source complexity, and making the
remaining features easier to understand.

- [x] Added Standard White Noise and renamed the existing modes to Hashed White
      Noise and Void Cluster Blue Noise without changing their algorithms.
- [x] Removed visibility-owned temporal accumulation, depth hierarchy,
      recursive indirect diffuse, sample resurrection, redundant AO-only
      routes, planner/profile duplication, and benchmark support.
- [x] Decoupled visibility from PBR and unrelated renderer settings.
- [x] Reorganized debug presentation into an effect-grouped Debug drawer with
      independent world appearance, information filters, and a transparent
      screen-space shadow edge overlay. The superseding restoration later
      removed that overlay.
- [x] Replaced the exclusive AA selector with independent, default-off TAA,
      CMAA2, and MSAA controls. Stationary Bypass is in the main TAA controls;
      the remaining Wicked-derived controls are in a closed Advanced section.
      The superseding restoration later moved Previous-Depth Validation into
      Advanced.
- [x] Removed SVSM and diagnostic CSM from this candidate, retained
      Screen-Space Directional Shadows, and preserved the complete prior shadow
      tree on local branch `codex/svsm-csm-preserved`.
- [x] Removed the forward PBR path, HDR-CMAA2 permutations, DRED command-line
      surface, retired graphics-backend aliases, dormant GPU timing
      normalization, and other definition-only taxonomies and accessors.
- [x] Reconciled task-local stale artifacts only after auditing dirty ownership.
- [x] Completed Release build, shader compilation and packaging, full CTest,
      document and diff validation, exact-artifact runtime smoke, and an
      independent high-risk review.

## Original Cleanup Baseline and Snapshot

| Measure | Baseline | Final | Reduction |
| --- | ---: | ---: | ---: |
| First-party non-blank source lines | 145,256 | 62,503 | 82,753 (56.97%) |
| `src/uvsr.cpp` physical lines | 33,577 | 15,882 | 17,695 (52.70%) |
| First-party production shader permutations | 823 | 315 | 508 (61.73%) |
| First-party developer shader permutations | 3,033 | 315 | 2,718 (89.61%) |
| Integrated production permutations, including unchanged Donut | 899 | 391 | 508 (56.51%) |

The snapshot's first-party shader total is 269 core permutations plus 46
Screen-Space Directional Shadow permutations. Donut's 76 permutations remain
unchanged. The exact runtime package contains 39 selected shader binaries.

The snapshot's tracked cleanup diff spans 124 files with 8,317 inserted and
101,702 deleted physical lines. New untracked execution-plan and dated-archive
documents are not included in those Git numstat totals. The repository counter
is authoritative for the non-blank source-line comparison above because it
excludes documentation, assets, licenses, binaries, and generated build output
consistently at both endpoints.

## Original Cleanup Behavior

- Visibility has one current-frame AO plus one-bounce indirect diffuse route,
  direct noise selection, direct reconstruction controls, and no visibility
  history or recursive-bounce resource chain.
- PBR is deferred-only. Visibility contributes through one explicit binding and
  no visibility UI action changes PBR, the sky, shadows, or other effects.
- Debug controls are grouped by the effect they inspect. World Appearance,
  Visibility isolation, PBR information filters, and Final Image overlays use
  separate state and can compose in this snapshot. The superseding restoration
  replaces Visibility isolation with four views and removes the overlay.
- TAA, CMAA2, and MSAA execute as independent passes in the supported order.
  The MSAA resolve supplies single-sample color, depth, and motion inputs to
  TAA, eliminating the D3D12 multisampled-UAV failure found during smoke.
- Screen-Space Directional Shadows is the only shadow technique in the engine.
  Its isolation view and translucent edge overlay are separate outputs in this
  snapshot; the superseding restoration retains only isolation.
- Shader configuration uses one core manifest plus the retained screen-space
  shadow manifest; production, experiment-default, SVSM, CSM, forward, and
  unused permutation manifests are gone.

## Original Cleanup Verification Evidence

| Check | Result |
| --- | --- |
| Release build | Passed after final source state |
| Core shader compilation | 269 of 269 passed |
| Screen-space shadow shader compilation | 46 of 46 passed |
| Runtime shader package | Exact 39-file contract passed |
| Full CTest | 30 of 30 passed |
| Runtime noise selection | All three final labels opened and selected without error |
| Runtime visibility | Standard White Noise plus visibility enabled without error |
| Runtime AA matrix | TAA, CMAA2, MSAA, TAA+MSAA, all three, and MSAA restart transitions passed |
| Runtime debug composition | World appearance and PBR filter coexisted; this snapshot rendered the edge overlay over Final Lighting |
| Runtime shadow pruning | Final rebuilt artifact exposed only Screen-Space Directional Shadows and enabled it successfully |
| Retired-symbol searches | No production references remained; expected negative contract assertions only |
| Document casing | 864 headings and bold lead-ins checked; zero violations |
| Diff validation | `git diff --check` passed; line-ending conversion warnings only |
| Independent review | No remaining P0-P2 findings |

Exact verified snapshot artifact:

- Path:
  `C:\Users\brock\OneDrive\Documents\uvsr\work\engine-core-cleanup\build-engine-core-cleanup\bin\uvsr.exe`
- Size: 2,475,520 bytes
- SHA-256:
  `536D2A6092927E483573144F858F927E8F3B580C2C2FB2ACE83B9FC7CF6B733C`

## Original Cleanup Feature Confidence

| Remaining Area | Weight | Confidence | Evidence |
| --- | ---: | ---: | --- |
| Core build, package, and launch | 20% | 98% | Full build, exact package contract, exact-artifact launch |
| Visibility | 20% | 95% | Reference tests, shader build, label and enable smoke |
| PBR, lighting, scenes, and lights | 20% | 93% | Source contracts, reference tests, scene assets, runtime scene |
| Anti-aliasing | 15% | 94% | Unit contracts and mixed TAA/CMAA2/MSAA D3D12 matrix |
| Shadows and debug presentation | 15% | 93% | Shadow tests plus final shadow and overlay smoke |
| UI and command surface | 10% | 96% | Command, layout, source, animation, and runtime checks |

The evidence-weighted average confidence in this snapshot's remaining features
is 94.85%.
The residual uncertainty is primarily visual quality across every camera and
scene, long-duration runtime soak, and matched performance benchmarking. No
performance improvement is claimed by this cleanup.

## Reconciliation and Preservation

- Eight pre-existing dirty linked worktrees were audited and preserved without
  modification. The root `main` checkout remains untouched.
- No tracked duplicated media was found, so no media was deleted.
- The task-local stale baseline build and task-local Python bytecode cache were
  moved to the Windows Recycle Bin after their exact paths were validated. They
  remain recoverable.
- The final candidate build is retained at the artifact path above.
- The pre-removal SVSM/CSM implementation remains recoverable on local branch
  `codex/svsm-csm-preserved` at `f7c0c87`.

## Assignment and Review Summary

- Read-only mapping covered visibility ownership, debug and AA composition,
  command surfaces, dead paths, worktrees, plans, builds, and media.
- One coordinator owned all shared hotspots and integration edits.
- The final independent reviewer checked AA ordering and resources, visibility
  bindings, PBR/debug isolation, shadow overlays, shader packaging, retired
  symbols, and definition-only support surfaces.
- Independent review concluded with no P0-P2 findings and released ownership.

## Remaining Work

- Product acceptance remains tied to the exact artifact and settings the user
  chooses to review.
- Committing, pushing, opening a pull request, integrating with `main`, and any
  release remain separate user-authorized actions.
