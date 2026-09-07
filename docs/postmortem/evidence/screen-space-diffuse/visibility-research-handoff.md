# visibility research build handoff

## purpose

this checkout combines the experimental bent normal output, new cosine weighting estimators, and the cheaper mask free horizon estimator in one local research build. it is preserved for research and possible selective porting. it is not committed or published.

## checkout and lineage

- checkout: `C:\Users\brock\OneDrive\Documents\uvsr\work\visibility-research`
- detached base: `bed36f951407e23d9e6b4a1a9462a96abc059e8a`
- current source state: 38 modified tracked files and four untracked source files
- added source files:
  - `src/bent_ao_application.hlsli`
  - `src/bent_ao_application_shared.h`
  - `src/visibility_horizon_cpu.h`
  - `src/visibility_horizon_shared.h`
- `git diff --check` passes. line ending conversion warnings are present but no whitespace errors were reported.
- preserve the dirty checkout. do not reset, clean, commit, switch branches, or merge without explicit authority.

the worktree was created from the bent normal lineage, then the mask free horizon implementation and cosine estimator controls were composed locally. the source worktrees used as evidence were not modified during composition.

## implemented behavior

the estimator menu exposes six compiled options:

1. `Horizon Approximation`
2. `Bitmask Approximation`
3. `Bitmask Directional Visibility`
4. `Bitmask Cosine Visibility`
5. `Bitmask Cosine First Hit`
6. `Bitmask Cosine Final Visibility`

the horizon path keeps one rising horizon per side and compiles out bitmask sectors, sector phase, interval construction, and bitmask helpers. `Thickness Approximation` is an optional horizon control and defaults off.

bent normal output works with every estimator. the horizon variant starts from the analytic fully open first moment and subtracts moments for newly claimed skyline wedges. AO, GI, later bounce reinjection, packed edges, temporal reconstruction, and bent normal consumers remain supported.

shader configuration includes estimator values `0..5`. bent normal permutations are lazy enabled variants. the combined build retains the current experiment's AO, GI, and reconstruction controls.

## cosine estimator meaning

- `Bitmask Directional Visibility` uses equal solid angle sectors and applies receiver cosine later during lighting.
- `Bitmask Cosine Visibility` warps sectors with the complete receiver cosine cumulative distribution. this is the strongest mathematical reference, but individual stochastic slices need reconstruction because their mass can be below or above one.
- `Bitmask Cosine First Hit` keeps the directional mask and assigns cosine mass when a sector is first claimed. overlap ownership depends on traversal order.
- `Bitmask Cosine Final Visibility` keeps the directional mask, then integrates receiver cosine over the completed visible runs. scalar AO is independent of hit order. GI still uses the directional estimator's first claim radiance ownership.

the user visually preferred First Hit because it looked richer, but questioned whether that was extra darkening rather than better accuracy. the current evidence supports that concern.

across 15 deterministic fixtures averaged over sector phases:

| comparison against dense physical cosine AO | signed bias | root mean square error |
|---|---:|---:|
| First Hit | `-0.054892` | `0.084815` |
| Final Visibility | `-0.020839` | `0.037989` |
| Directional Visibility | `-0.041236` | `0.080780` |

First Hit was darker than the dense cosine reference in 13 of 15 fixtures and darker than Final Visibility in 12 of 15. it was also slightly less accurate than Directional Visibility in aggregate. this does not prove its image is undesirable. it means its extra richness is partly an order dependent darkening effect, not demonstrated physical accuracy.

the earlier recommendation was:

- use `Bitmask Cosine Visibility` as the physical comparison oracle.
- use `Bitmask Cosine Final Visibility` as the safer stable realtime approximation.
- keep First Hit as research or an artistic candidate until it is compared at matched average visibility and under reversed or randomized traversal.

the user suggested renaming Final Visibility to `Last Hit`. no rename was made. `Last Hit` would be misleading because the algorithm does not select a last hit. clearer unimplemented names proposed in the chat were `Bitmask Cosine First Claim` and `Bitmask Cosine Final Mask`.

## verification evidence

verification completed on 2026-08-25:

- Release renderer build passed.
- all 722 production shader tasks compiled.
- CTest passed 33 of 33 tests.
- exact runtime and production shader bundle contracts passed.
- a normal launcher smoke produced a responding DX12 window titled `UVSR Renderer D3D12 (visibilityresearch-bed36f9-2256)` and closed cleanly with launcher exit code zero.
- a one measured frame automated visibility benchmark reached scene and shader execution but exited one after timing out while draining GPU timer sets. do not cite that run as a benchmark result or as evidence of an estimator failure.

the preserved artifacts still match these identities on 2026-09-05:

- launcher: `C:\Users\brock\OneDrive\Documents\uvsr\work\visibility-research\build-visibility-research\bin\uvsr-visibility-research-launcher.exe`
  - size: `36,864` bytes
  - SHA-256: `6C431876DAE1CA5551ADE3AABA1A112D0DFE29FA1C4BE1434B65DBB7A58D3C16`
- renderer: `C:\Users\brock\OneDrive\Documents\uvsr\work\visibility-research\build-visibility-research\bin\uvsr.exe`
  - size: `3,384,832` bytes
  - SHA-256: `0FBDC0EA925CED1F573937DB06EA53A4702156AC01F40363ACF918CB3D286449`

## current architecture warning

this experiment predates the protected executable identity contract. current UVSR permits only `uvsr-launcher.exe` and `uvsr-engine.exe`. do not present the old artifact names above as current distribution candidates. if this work is integrated, port the selected behavior into current source and produce a newly verified package under the current names. do not rename these binaries and assume equivalence, because the local launcher expects `uvsr.exe`.

the current canonical checkout has changed substantially since this detached base. do not merge the whole dirty worktree into current source. trace and port only the selected estimator contracts, equations, controls, tests, and documentation.

## suggested next action

first review the handoff and current source without editing. if the user wants further estimator research, make a matched average visibility comparison between First Hit and Final Visibility and add a reversed traversal diagnostic. if the user wants integration, identify the current visibility owner and write a selective port plan before changing files.
