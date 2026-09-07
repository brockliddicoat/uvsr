# postmortem archive

start with the [screen-space diffuse removal](screen-space-diffuse.md) for the
complete method lineage, optimization ledger, failed experiments, and a narrower
future implementation. its supporting records and changed-file preimages are
preserved here rather than in active runtime code or routine agent context.

these are historical records. current source, [architecture](../architecture.md),
and the user's accepted decisions govern new work. old claims that a feature is
retained, required, or ready refer only to their recorded candidate. compilation
and unit tests do not override a negative visual result. historical links may
name deleted files or local artifacts; follow the recorded revision and hashes.

## records

| topic | record |
| --- | --- |
| screen-space AO/GI, estimator lineage, all optimization candidates, and future requirements | [diffuse removal](screen-space-diffuse.md) |
| shader permutations, engine ownership, MSAA visibility, ReSTIR, accumulation, and stage-two decisions | [engine cutdowns](engine-cutdowns/README.md) |
| runtime loops, fixed/generic paths, offline noise, and shader families | [shader-path retirements](shader-path-retirements.md) |
| low-resolution receiver rotation and its negative visual result | [sample rotation](visibility-sample-rotation-v1.md) |
| current rewrite AA removals | [TAA](taa.md), [MSAA](msaa.md) |
| older temporal reconstruction experiment | [native-resolution reconstruction](native-resolution-analytical-reconstructive-temporal-anti-aliasing-v1.md) |
| removed display-sync policies and retained controls | [display sync](display-sync.md) |
| retired Amp and Ogg skins, font choices, and palette code | [compressed recovery source](archive/legacy-ui-skins.zip) |
| removed interface animation | [UI animations](ui-animations.md) |
| separate abandoned denoiser candidate | [denoising replacement](denoising-replacement.md) |
| rejected camera-relative flashlight mount behavior | [flashlight centering](flashlight-camera-centering-v1.md) |
| earlier tonemapper/LUT experiment and restore bundle | [tonemapper](tonemapper-drawer-and-luts-v1.md) |
| retired atmospheric sky and celestial motion | [three-band sky](three-band-time-of-day-sky-v1.md), [design snapshot](three-band-time-of-day-sky-v1-design.md) |

## checked preservation and publication

[publication-manifest.tsv](publication-manifest.tsv) binds every archive member
by SHA-256 and source. when a record intentionally changes, review the difference
and update its hash and provenance. an added file must be represented. raw
archive bytes are checkout-invariant through `.gitattributes`.

the developer build-integrity test checks file presence, hashes, exact membership,
and new postmortem paths in other registered local worktrees. the production
package target and CI also require every member in the exact committed source
candidate. run this from the repository before an authorized publication:

```powershell
cmake -DUVSR_REQUIRE_COMMITTED=ON -P cmake/VerifyPostmortems.cmake
```

the checker never stages, commits, copies, or uploads records. a local pass proves
preservation only. a committed-candidate pass proves inclusion in that revision,
not arrival on GitHub. publication still requires its separate authorized action
and the existing release gates. the renderer runtime package excludes this archive.
