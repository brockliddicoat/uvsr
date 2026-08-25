# Stage-Two Renderer and Launcher Cutdown Decisions

## Record Identity

- Record date: 2026-08-23.
- Evidence snapshot and pre-cut recovery point:
  `e29a41245dbd0e6fd7a819d2341646419ab76e72`.
- Status: the numbered renderer/launcher sections preserve the exact pre-cut
  observation and decision boundary. Completed dependency-ownership slices are
  marked separately. Integrated after-metrics, package/runtime evidence, and
  final artifact identities remain separate completion gates.
- Count definition: physical lines and tracked bytes at the evidence snapshot.
  Broad reference counts are search results, not ownership counts. Build,
  dependency, and output trees are excluded.

“Observed” below means inspected source, Git history, or durable project
records at the evidence snapshot. “Inference” identifies a proposed boundary,
expected reduction, or conclusion not yet demonstrated by the final build.

## Exact `e29a412` Baseline Ledger

All observations below were rechecked on a clean Windows checkout of the full
base commit with its pinned submodules. Physical-line counts include blank
lines. Documentation words are non-empty whitespace-separated tokens. Windows
checkout bytes include checkout line endings; Git blob bytes are called out
when they differ.

### Source, Test, and Shader Burden

The historical checked-in counter, run as
`tools\update_readme_line_counts.cmd --print`, reported 131,798 first-party,
386,156 third-party, and 517,954 total non-blank source lines. It counted the
final ownership of dependency overrides and excluded documentation, assets,
licenses, binaries, and generated output.

The base had 48 tracked files under `tests/**` and 47,702 physical lines. CMake
declared 47 executable targets: one renderer and 46 test/probe executables. It
also declared 51 CTests; five were script/asset checks rather than another
compiled test executable. Ten first-party `.py` files held 7,235 physical lines
and 273,023 Windows-checkout bytes (267,311 Git blob bytes).

`src/uvsr.cpp` was exactly 26,698 physical lines. `src/shaders.cfg` expanded to
311 UVSR shader tasks; the blend row alone was 192 TAA tasks, and the three
other TAA rows added five. This is task expansion, not the 48 unique runtime
shader blobs that the baseline package contract expected.

Run these PowerShell commands in a clean checkout at the base to reproduce the
physical and declaration counts:

```powershell
$base = 'e29a41245dbd0e6fd7a819d2341646419ab76e72'
$tests = @(git ls-tree -r --name-only $base -- tests)
($tests | ForEach-Object { @(git show "${base}:$_").Count } |
    Measure-Object -Sum).Sum

$cmake = @(git show "${base}:CMakeLists.txt")
@($cmake | Where-Object { $_ -match '^\s*add_executable\s*\(' }).Count
@($cmake | Where-Object { $_ -match '^\s*add_test\s*\(' }).Count
@(git show "${base}:src/uvsr.cpp").Count

$python = @(git ls-tree -r --name-only $base |
    Where-Object { $_ -match '^(tests|tools)/.*\.py$' })
($python | ForEach-Object { @(git show "${base}:$_").Count } |
    Measure-Object -Sum).Sum
($python | ForEach-Object { (Get-Item -LiteralPath $_).Length } |
    Measure-Object -Sum).Sum
```

The shader count is reproducible from the config's Cartesian products:

```powershell
$tasks = 0
foreach ($line in git show "${base}:src/shaders.cfg") {
    if (-not $line.Trim() -or $line.TrimStart().StartsWith('#')) { continue }
    $count = 1
    foreach ($set in [regex]::Matches($line, '\{([^{}]+)\}')) {
        $count *= ($set.Groups[1].Value -split ',').Count
    }
    $tasks += $count
}
$tasks
```

### Documentation and Plan Burden

The base's 123 Markdown files under `docs/**` contained 273,127 words. The
largest active procedures were the UI procedure (9,062), collaboration
procedure (2,676), and launcher README (3,637); the root README held 634.
Under `docs/exec-plans`, 95 active/completed/abandoned records contained
196,967 words: active 2/3,249, completed 88/174,055, and abandoned 5/19,663.
Adding the plan README and template yields the correctly paired total of 97
files and 197,669 words.

The record counts use this whitespace-token command at the base; `Get-Item`
over the same paths produced 23,493 active, 1,296,123 completed, and 144,264
abandoned Windows-checkout bytes:

```powershell
function Measure-Words($paths) {
    ($paths | ForEach-Object {
        (Get-Content -LiteralPath $_ -Raw) -split '\s+' |
            Where-Object { $_ }
    } | Measure-Object).Count
}
Measure-Words @(git ls-files 'docs/**/*.md')
Measure-Words @(git ls-files 'docs/exec-plans/active/*.md')
Measure-Words @(git ls-files 'docs/exec-plans/completed/*.md')
Measure-Words @(git ls-files 'docs/exec-plans/abandoned/*.md')
```

### Asset, Dependency, Override, and Launcher Burden

Section 4 records the exact four-scene, HDR, and noise inventories. This is the
inventory algorithm used there: tracked repo-relative path, byte size, and
lowercase file SHA-256, TAB-separated and path-sorted; rows use LF and the
payload has one mandatory trailing LF. For the protected noise payload, pass
only `assets/noise/manifest.json` and the 12 `assets/noise/*.bin` paths; its
README is maintained documentation rather than a runtime asset.

```powershell
function Measure-Inventory($root) {
    $files = @(git ls-files "$root/**" |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Sort-Object)
    $rows = foreach ($path in $files) {
        "$path`t$((Get-Item $path).Length)`t" +
            (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
    }
    $payload = [Text.Encoding]::UTF8.GetBytes(($rows -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    [Convert]::ToHexString($sha.ComputeHash($payload)).ToLowerInvariant()
}
```

The Donut root and six initialized nested pins occupied 1,222 Git blobs and
22,374,135 blob bytes. Thirteen first-party overrides occupied 408,291 Git blob
bytes and 408,407 Windows-checkout bytes. `launcher/**` held 46 files, 21,376
physical lines, 966,768 Git blob bytes, and 971,779 Windows-checkout bytes. The
footprint command below produced the dependency rows in section 8:

```powershell
function Measure-Git-Tree($root) {
    $rows = @(git -C $root ls-tree -r --long HEAD |
        Where-Object { $_ -match '^\d+\s+blob\s+[0-9a-f]+\s+(\d+)\s+' })
    $bytes = 0L
    foreach ($row in $rows) {
        $null = $row -match '^\d+\s+blob\s+[0-9a-f]+\s+(\d+)\s+'
        $bytes += [int64]$Matches[1]
    }
    [pscustomobject]@{ Files = $rows.Count; Bytes = $bytes }
}
```

The launcher source declared download ceilings of 80, 100, 30, 64, 8, and 64
MiB for Git, CMake, Python, Agility SDK, DirectX Headers, and DXC: 346 MiB in
total. `Select-String launcher/src/UVSR.Installer/ProductConstants.cs -Pattern
'L \* 1024 \* 1024'` exposes those inputs. Visual Studio Build Tools, the VC
redistributable, renderer source, and scenes were uncapped, so 346 MiB is a
lower bound rather than an install measurement.

### Configuration and Timing Recheck

Both baseline trees used Visual Studio 17 2022, x64, Release,
`UVSR_BUILD_APPLICATION=ON`, and `UVSR_WITH_NRD=OFF`. The developer tree set
`BUILD_TESTING=ON`; the production tree set it `OFF`. Nevertheless, the base
unconditionally called `enable_testing()` and declared the same 51 first-party
CTests in both configurations. Thus `BUILD_TESTING=OFF` did not yet provide the
required production/test separation; the production build simply had not built
those declared test targets.

The fresh developer gate passed 50 of 51 tests. The existing
`uvsr_d3d12_portability_source_contract` failure was the only failure. CTest
reported 93.18 seconds; the enclosing stopwatch reported 93.392 seconds. The
fast command excluded the five named documentation/launcher/scene checks and
passed 45 of 46, with the same failure; CTest reported 6.22 seconds and the
stopwatch 6.373. The five excluded checks reported 0.74, 7.24, 12.58, 22.28,
and 40.18 test-seconds, totaling 83.02. These exact results contradict the
older prompt approximations of 46.96, 3.86, and about 43.10 seconds.

```powershell
$buildRoot = '<isolated external baseline developer tree>'
$timer = [Diagnostics.Stopwatch]::StartNew()
ctest --test-dir $buildRoot -C Release --output-on-failure
$timer.Stop(); $timer.Elapsed.TotalSeconds

$exclude = '^(uvsr_readme_line_count_self_test|uvsr_readme_line_count_contract|uvsr_launch_menu_contract|uvsr_scene_asset_contract|uvsr_scene_asset_provenance)$'
$timer = [Diagnostics.Stopwatch]::StartNew()
ctest --test-dir $buildRoot -C Release -E $exclude --output-on-failure
$timer.Stop(); $timer.Elapsed.TotalSeconds
```

Incremental base measurements used timestamp-only invalidation and restored the
original timestamp in `finally`: `src/display_output_ps.hlsl` to target
`uvsr_shaders` took 50.569 seconds; `src/uvsr.cpp` to target `uvsr` took 30.598;
and `assets/scenes/bistro_interior_retextured/README.md` to
`uvsr_stage_scenes` took 7.371. The exact reusable command was:

```powershell
function Measure-TouchBuild($path, $target) {
    $file = Get-Item -LiteralPath $path
    $stamp = $file.LastWriteTimeUtc
    try {
        $file.LastWriteTimeUtc = [DateTime]::UtcNow
        $timer = [Diagnostics.Stopwatch]::StartNew()
        cmake --build $buildRoot --config Release --target $target --parallel
        $timer.Stop(); $timer.Elapsed.TotalSeconds
    } finally { $file.LastWriteTimeUtc = $stamp }
}
```

## 1. Conventional Path Tracing and ReSTIR Retirement

### Observed Evidence and Burden

The path-tracing family has 8,524 physical source lines across
`src/path_tracing_settings.h`, the path-tracing pass and constant buffer, three
path-tracing shader/include families, and stable-plane resolve. Its two
dedicated tests add 2,805 lines. `shaders.cfg` compiles 25 tasks: 18 main
solver/RTXDI/next-event-estimation combinations, six primary-surface
combinations, and one stable-plane resolve task. Runtime construction owns 18
main pipeline variants plus fallback selection.

The persisted surface selects three solvers (RTX Path Tracing, ReSTIR Path
Tracing, and ReSTIR GI), three next-event-estimation policies, two denoiser
modes, 11 debug views, and controls for bounce depth, Russian roulette,
candidates, samples per pixel, shader execution reordering, shared primary
surfaces, RTXDI/direct reservoirs, temporal and spatial reuse,
motion-revalidated proposals, stable planes, resolve strength, and firefly
handling. CPU/GPU state includes direct reservoirs, continuation-seed and GI
checkpoint histories, shared depth/motion/surface data, and stable-plane
resolve buffers.

Origins are
`ae7112f4557365e91ce80169c346c47e0f95a2fc` (standard/ReSTIR transport),
`54a57b08a462ad83979ccc8912570f2c6cc7ea03` (accumulation), and
`0224649055f2218dcf1dbab4af4a1ea8a6b894f9` (resampling controls). Historical
plans report shader compilation, CPU/source-contract tests, complete CTest
runs, bounded 1080p Sponza smokes, and rendered RTX and ReSTIR Path Tracing.
They also explicitly defer matched equal-time, image-error, high-sample ground
truth, and performance comparisons. No raw benchmark series or comparison
captures are tracked. User observations report indistinguishable solver
results, slow convergence, and ineffective motion reuse.

### Decision, Replacement, and Risk

Retire ReSTIR Path Tracing, ReSTIR GI, RTXDI coupling, reuse histories and
controls, stable-plane mode paths, their debug views, and fallback chains.
Keep one direct conventional path tracer with only options proven useful.
Inference: a fixed uniform direct-light proposal with MIS is the smallest
initial policy; validation, not this record, decides whether any other
conventional control survives. The 8,524 source lines include the retained
integrator, so the removable line count is unknown. Estimate: 24 of 25 current
shader tasks should disappear if one fixed main task replaces the matrix.

The final gate must cover Bistro and San Miguel, save/load and defaults,
camera/scene/resize and every image-affecting reset, truthful sample counts,
finite output, retained material/light behavior, exact production packaging,
and representative convergence evidence. The cut must remove settings,
bindings, resources, UI, commands, snapshots, tests, shaders, tasks, and docs
together, with zero active ReSTIR or stable-plane references.

Recovery boundary: recover the complete pre-cut family from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`; use `ae7112f`, `54a57b0`, and
`0224649` only to study feature introduction, not as complete modern files.

## 2. Sample-Accumulation Simplification

### Observed Evidence and Burden

The family totals 1,610 source lines:
`src/sample_accumulation_settings.h` (625),
`src/sample_accumulation.hlsli` (145), and the generic lighting
accumulation pass, header, constant buffer, accumulation shader, and prepare
shader (840). Its dedicated settings test adds 578 lines. The persisted surface
offers three presets, five history presets, four workload presets, cumulative
or exponential averaging, every-pixel or variance-guided scheduling, and
fields for effective history, minimum samples, target relative error, and
minimum update rate.

The feature was intended to repair incomplete/noisy convergence and add
variance-based scheduling. Durable evidence covers CPU arithmetic and source
contracts, shader compilation, bounded Sponza runs, and movement/scene resets.
It does not include matched image-error curves demonstrating that the exposed
policies improve product output. The introduction history is `54a57b08...`,
with later control work in `022464905...`.

### Decision, Replacement, and Risk

Replace the policy surface with one cumulative scene-linear mean and a direct
sample counter. Advance only while image-defining state is stable; reset on
camera, scene, resolution, material, light, exposure, or renderer-setting
changes. Inference: a maximum-sample or continuous mode may survive only if a
real workflow requires it. This cut does not authorize removing the protected
AO/GI temporal histories.

Final validation must prove the reset matrix, displayed/diagnostic sample
truth, finite mean state, stationary convergence on both retained scenes, and
fresh production-package behavior. Final measurement must separate deleted
policy code from the retained accumulator; no removal estimate is claimed.

Recovery boundary: `e29a41245dbd0e6fd7a819d2341646419ab76e72` is the exact
complete recovery point; `54a57b0` and `0224649` preserve introduction context.

## 3. Heitz Ratio Estimation to Direct Visibility

### Observed Evidence and Burden

Dedicated Heitz/directional-shadow source is 1,892 lines: pass (746), header
(128), constant buffer (34), shader (793), shared ratio-estimator include (99),
and directional settings (92). Two dedicated tests add 1,984 lines. Seven
shader tasks cover one-sample hit-distance and modulation routes plus 2x, 4x,
8x, and 16x modulation. A broad ratio/Heitz search reaches 1,051 lines in 38
files; that broad count includes adjacent settings and is not a deletion count.

The initial plan added Heitz to produce soft directional-light shadows while
reducing variance by correlating the visible and unshadowed RGB estimators. It
also intended to establish a reusable TLAS-backed representation for later ray
consumers. That was the recorded product reason; it was not a measured win over
direct visibility.

The implementation provides stochastic ratio-estimator directional shadows,
correlated RGB modulation, a hard-shadow route, optional hit distance, phase
and history state, bindings, UAVs, and noise/sample-rate controls. Durable
evidence includes deterministic CPU math and source tests, shader compilation,
and bounded Sponza smokes. The follow-up plan records corrupted wall/occlusion
captures and inert origin-safety behavior requiring bias repair; exact failing
camera visual acceptance remained pending. Its approximate 0.295 ms result was
an informal smoke, not a controlled benchmark. Origin:
`ca4bd62bf4c052791103dbd49e0dc48e301fdddf`.

At the final baseline the pass could own two full-resolution RGBA16F modulation
textures and one conditional R16F hit-distance texture. It had no private
temporal texture; TAA owned temporal filtering. The TLAS, G-buffer, and noise
inputs were shared with retained ray features and are not deletion savings.
The two implementation/follow-up plans total 618 physical lines. **Estimate —
medium confidence:** the August 4–5 record shows the initial candidate plus at
least four feedback/rebuild candidates involving bias, temporal ownership,
hard/soft routing, and sampling. No hour ledger survives, so iteration count,
not elapsed engineering time, is the defensible development-drift estimate.

### Decision, Replacement, and Risk

Replace the estimator with direct binary ray-query visibility for each active
MSAA sample. Never replicate one pixel visibility result across 2x, 4x, 8x, or
16x samples. Keep ray-traced sky visibility and flashlight shadows on direct
paths; retain hit distance only if an active denoiser consumes it. Remove the
estimator settings, phase/history resources, variants, bindings, UI, tests,
docs, and legal references as one slice.

The exact deleted first-party paths are
`src/heitz_ratio_estimator_shadows.cpp`,
`src/heitz_ratio_estimator_shadows.h`,
`src/heitz_ratio_estimator_shadows_cb.h`,
`src/heitz_ratio_estimator_shadows_cs.hlsl`,
`src/ratio_estimator_shared.h`,
`tests/heitz_ratio_estimator_tests.cpp`,
`tests/heitz_ratio_estimator_source_contract_tests.cpp`,
`docs/ratio-estimation.md`,
`legal/documentation/heitz-ratio-estimator-shadows.md`, and the two
plans `docs/exec-plans/completed/heitz-ratio-estimator-shadows.md` and
`docs/exec-plans/completed/heitz-shadow-follow-up.md`. Shared settings, PBR,
shader catalog, UI, and renderer owners were narrowed in place.

Validation must cover opaque and alpha-tested edges, mixed visible/occluded
MSAA samples at all four sample counts, sky plus flashlight combinations,
Bistro and San Miguel, camera cuts, resize/reset, finite output, debug-layer
cleanliness, and exact production packaging. The replacement is not proven by
the historical smoke evidence.

Actual post-cut source evidence is bounded: direct-DXC compilation passed the
directional-visibility 1x/2x/4x/8x/16x permutations and PBR deferred-MSAA
2x/4x/8x/16x consumers; standalone directional arithmetic/source-contract and
PBR lighting source-contract tests passed. The replacement stores and consumes
one visibility value per covered receiver sample. No rendered MSAA matrix has
passed yet, so opaque/alpha-tested mixed coverage, sky/flashlight combinations,
resets, scenes, and package claims remain pending runtime evidence.

Recovery boundary: use
`e29a41245dbd0e6fd7a819d2341646419ab76e72` for complete recovery and
`ca4bd62` for origin archaeology.

Lesson: a mathematically interesting estimator, arithmetic tests, and a
non-black smoke do not establish a useful soft-shadow product. A future
experiment should start from the direct per-sample reference, choose one fixed
stochastic soft route, define penumbra/error and ray-time budgets before UI,
and compare tracked Bistro/San Miguel frames at mixed MSAA edges. Restore Heitz
only if that narrow experiment wins a controlled equal-time comparison and the
full MSAA/reset/package matrix; do not restore its old settings or use it as a
fallback merely because the commit is recoverable.

## 4. Classroom and Sponza Scene Retirement

### Observed Evidence and Burden

Canonical inventories, computed from sorted path, byte size, and SHA-256 rows,
are:

| Scene | Files | Bytes | Inventory SHA-256 | Git Tree |
| --- | ---: | ---: | --- | --- |
| Bistro | 12 | 423,031,519 | `58224e58edfe6bfa60b236f99da032cac8921389863ff09c35cf1e965ceea090` | `3aaf72c9b1208c9c53a3808267b4cd3b6501825f` |
| San Miguel | 281 | 493,928,565 | `c493d3e9543089d27630143ebff481129d3c831f3d92968b65053ca70b8738b7` | `8c353fa6bf65ec31711cf5ecd75412f289d51677` |
| Classroom | 28 | 40,613,074 | `1c9b878a91c44c4fdb85f1ef052c9d8269ce732557d4bd9abb39509a22211edf` | `7a0f895916a5fb4804858623eec6e88ed29c6408` |
| Sponza | 10 | 418,126,903 | `19a4f19c495807f9db45566d40b1b2faad11b50f25d4cf7a7be35b8039abb9d4` | `6e658c662ebebc4d43575db6cc21dbca278edcb4d` |

Classroom and Sponza therefore account for 38 tracked files and 458,739,977
bytes. Adjacent source includes the 1,523-line Classroom exporter and the
1,281-line Sponza importer, plus 150 camera-source lines and a 192-line camera
test for Sponza.

The dedicated `uvsr_sponza_camera_reference` baseline cost was 0.0211322 CTest
seconds and is removed. The shared scene-asset and provenance checks cost
22.2764 and 40.1762 seconds (62.4526 combined) at the base, but remain narrowed
for Bistro, San Miguel, HDR, and noise; without a post-cut focused timing, none
of that shared 62.4526 seconds is claimed as removed.

Classroom records disclose omitted legacy bump data, approximated linked
colors, opaque glass, removed daylight portal and clock cover, deletion of the
dust bin and 13 paper children, and replacement of a diagnostic checker on
eight sheets. The current asset has 545,830 of the source's 607,484 triangles.
Sponza records disclose roof rebuilding, removal of 12,202 ivy components,
texture resizing, and a simplified rounded camera. Its legal record contains
both CC BY text and an earlier limited-commercial statement that require care
when preserving historical notices.

Durable records call Classroom user verification final and Sponza legal status
confirmed, while the stage-two request identifies both as known-problem scenes.
No explicit unresolved renderer bug for either was found in the durable
records. The observed approximations make asset faults and renderer faults hard
to distinguish; deletion is a scope decision, not evidence that neither scene
ever worked.

### Decision, Retained Assets, and Risk

Remove Classroom and Sponza assets, catalog entries, cameras, import/export
tools, dedicated tests, packaging rules, UI choices, defaults, docs, and legal
entries that apply only to them. Keep Bistro and San Miguel byte-identical and
preserve their `source-provenance.json` records. The default scene must become
an explicitly validated retained scene; sorted fallback would choose Bistro,
but that is an inference, not acceptance evidence. Bistro's user-supplied GLB
chain-of-title ambiguity and opaque-liquid fallback remain known risks.

Every HDR and noise asset is protected. At this snapshot `assets/environments`
is eight files and 26,546,349 bytes with inventory SHA-256
`403e9a7aaf47295e650ec91e882bdbf588bb0a8733e230bb8b5856bf0d9331ea`;
the whole baseline `assets/noise` tree was 14 files and 22,983,229 bytes with
inventory SHA-256
`6784ff5e0d29f7b127a749cfb2fb27217153fd7e5451173b47674c1efd0c8c41`.
That whole-tree value includes the maintained README and is a baseline burden,
not the protected runtime boundary. The frozen manifest plus all 12 binary
payloads are 13 files and 22,982,386 bytes with inventory SHA-256
`292fc5f403cae95d201ac6ce2dd5b0f1f2c5d626882a32a7bbd5f7ebaddc60ea`.
This command proves those exact paths remain byte-identical; the inventory
function above independently binds path, size, and file hash:

```powershell
git diff --exit-code e29a41245dbd0e6fd7a819d2341646419ab76e72 -- assets/noise/manifest.json assets/noise/*.bin
```

Validation must prove exact retained asset inventories, catalog/default/load
behavior, Bistro and San Miguel representative runtime matrices, fresh-package
contents, and narrowed shared provenance/legal tests.

A future canonical scene must first have stable source provenance and license
terms, a hash-bound material/resource inventory, repeatable runtime loading, a
reviewed camera/light baseline, and representative rendered output. A sorted
catalog fallback or a successful parse alone is not acceptance evidence.

Recovery boundaries: Classroom origin/current asset commit
`f7c0c87d8cba6880428fbc34400eb2882fb5182e`; Sponza origin
`3ac53b382ee16101a04504811b7feb7a055f1773`, camera
`a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`, and current adjustments
`f7c0c87`; complete pre-cut recovery `e29a41245dbd0e6fd7a819d2341646419ab76e72`.

## 5. Launcher Source Build and V1 Retirement

### Observed Evidence and Burden

`launcher/**` contains 46 tracked files, 971,779 bytes, and 21,376 physical
lines. Major source-build owners are `SourceManager.cs` (1,322),
`ToolchainManager.cs` (1,040), and `PayloadPackager.cs` (707). The synthetic
public-base bridge adds a 485-line generator, 158-line bridge implementation,
a 631-line embedded patch resource, and related coverage in the 4,521-line
launcher test program. It maps public commit `0c807484...` to a synthetic source
commit and embeds a 24,581-byte patch with SHA-256
`e68f...`; the exact full identities remain in commit
`639fd74f9d180f3ba835d2cb0110c949e705b1a7`.

Declared download caps before source, scenes, Visual Studio Build Tools, and
the VC redistributable total 346 MiB: Git 80, CMake 100, Python 30, Agility SDK
64, DirectX Headers 8, and DXC 64. Visual Studio Build Tools and the
redistributable have no equivalent cap in `ProductConstants.cs`. This is a
lower bound, not a measured install total.

At the baseline, two v1 feed files remained:
`installer/launcher-feed-v1.json` (321 bytes) and
`launcher/launcher-feed-v1.json`; a signed v2 feed also existed. Those baseline
files are now retired and are not current launcher inputs. Historical
source-build work begins at `a9004f518...`, compatibility work at `0c807484...`,
and the bridge at `639fd74f...`. Records show fresh-install failures followed
by repairs, but no evidence that a local compiler remains necessary once a
signed renderer package is authoritative.

The exact retired publication facts are recoverable from `e29a412...`. Both v1
copies were schema 1, sequence 2, version `1.1.1`, and named
`UVSR-Launcher-Windows-11-x64.exe` at 58,370,076 bytes with SHA-256
`2b5f092bdf80dcdabca46034f1334f6be374c712400e7bf8d6ae1e672f7a5b36`.
The signed v2 feed used key `uvsr-launcher-update-p256-2026-01`, sequence 15,
version `1.1.14`, source
`5762bb9f00dd1cd9f62aa19bc56e6f28215f30b4`, and the same forbidden artifact
stem at 59,060,905 bytes with SHA-256
`3d7e00ef62188dfbbec8e86e2cd7217f677d5bbf0bfb862c22dfd2fadf791be9`.
Its 64-byte P1363 signature was
`zjWDyIghiGzNDXD4a5dRu7Ldqt2AOnjWT6WL0d1IHH+Vx2gxZV19/3LzuKqijC4SMbv1hpa93ZGYKMrBeKNeqw==`.
The former public feed URL ended in
`/main/launcher/launcher-update-feed-v2.json`; its immutable release artifact
used tag `uvsr-launcher-v1.1.14`. These facts authorize recovery study only.
No URL, release, or endpoint is changed by this local implementation. Sequence
16 is unissued local verification metadata, not a published release.

### Decision, Replacement, and Risk

The launcher becomes an installer/updater only:
`uvsr-launcher.exe` verifies a signed feed and size/SHA-256-bound renderer
package, then transactionally installs `uvsr-engine.exe`. Remove Git, CMake,
Python, compiler, SDK, source checkout, synthetic bridge, patching, build-tree,
and payload-packaging paths and their tests. Remove v1 feed production and
fallback code. Estimated download reduction is at least the 346 MiB cap sum,
plus uncapped build tools and source; final network and disk measurements must
replace that estimate.

Old raw v1 URLs cannot redirect and may return 404 after deletion. That is an
external compatibility effect: no endpoint change or publication is authorized
by this local cut. A release must first prove feed signature/key/sequence,
artifact hash and size, transaction, update, repair, rollback, ownership,
shortcuts, running-process handling, and uninstall against the exact package.

Recovery boundary: complete source-build and v1 behavior at
`e29a41245dbd0e6fd7a819d2341646419ab76e72`; origins `a9004f5`, `0c80748`, and
`639fd74` are historical aids only.

Lesson: making each end user own source, toolchains, patches, and build state
multiplied failure modes without proving a product need. Restore source-build
installation only for a documented deployment requirement that signed,
hash-bound packages cannot satisfy and only after measuring the alternative;
never restore it as an update fallback or permanent compatibility endpoint.

## 6. Executable Identity Consolidation

### Observed Evidence and Burden

At the baseline, the renderer target and output were `uvsr`/`uvsr.exe`.
Launcher constants named `UVSR Launcher.exe`,
`UVSR-Launcher-Windows-11-x64.exe`, and `UVSR Installer.exe`; the project used
assembly name `UVSR-Launcher` and a manual `1.1.14` file/product version.
Baseline searches found `uvsr.exe` on 35 lines in ten files and the versioned
launcher artifact on 28 lines in 14 files. These are retired observations, not
the current executable contract.

### Decision, Replacement, and Risk

Ship only `uvsr-launcher.exe` and `uvsr-engine.exe`, with no versions in either
filename. Recognize exact known legacy installed names only inside one narrow
migration; do not preserve ordinary aliases. Replace manual engine identity
with one authoritative C++ canonical-settings schema hash covering option
identity, type, enum values, persistence, and defaults. Embed its full hash in
the engine, diagnostics, launcher-visible data, and package metadata; derive
engine and numeric Windows versions deterministically from it.

Validation must prove known-answer hashing, deterministic clean rebuilds, PE
metadata, settings and package identity agreement, install/update/repair/
rollback/uninstall behavior, shortcuts and process discovery, and zero active
old-name references outside the explicit migration and this archive. The
canonical hash and derived version do not yet exist at this pre-cut snapshot.

Recovery boundary: all old naming and manual version behavior is recoverable
from `e29a41245dbd0e6fd7a819d2341646419ab76e72`.

Lesson: filename, process, package, and version identity must derive from one
contract. Restore a known old name only inside evidence-backed one-time
installed-state migration; never restore manual engine versions, versioned
filenames, broad aliases, or old names in normal launch and update paths.

## 7. Python Removal

### Observed Evidence and Burden

Ten first-party Python files total 7,235 physical lines and 273,023 bytes:

| Owner | Lines |
| --- | ---: |
| `tests/settings_snapshot_decoder_tests.py` | 197 |
| `tools/check_document_title_case.py` | 1,285 |
| `tools/check_legal_inventory.py` | 138 |
| `tools/decode_settings_snapshot.py` | 274 |
| `tools/export_blender_classroom.py` | 1,523 |
| `tools/generate_noise_assets.py` | 164 |
| `tools/import_san_miguel.py` | 1,010 |
| `tools/repack_gltf_buffers.py` | 984 |
| `tools/sync_launcher_readme_download.py` | 1,133 |
| `tools/update_readme_line_counts.py` | 527 |

CMake requires Python 3.10 and registers README/title and snapshot-decoder
tests. Workflows use Python for title checks, launcher README synchronization,
legal inventory, and README line counts. The launcher downloads Python 3.13.15.
A broad search finds 159 Python references across 48 files, including historical
documents; it is not an active-code count. Active procedure burden includes a
9,062-word UI procedure, 2,676-word collaboration procedure, and 3,637-word
launcher README. The execution-plan archive has 95 active/completed/abandoned
records and 196,967 words; adding its README and template yields 97 files and
197,669 words. “97 files/196,967 words” mixes those definitions.

### Decision, Replacement, and Risk

Port recurring product contracts to narrow C++: canonical settings decoding
and known-answer tests, legal/retained-asset inventory checks, and launcher
feed/schema/value validation. Delete stateful documentation maintenance and
one-time conversion/generation tools after freezing their required outputs and
hashes. Do not retain Python as a fallback or active installation, build, CI,
test, tool, launcher, PATH, package, or documentation dependency.

The retained San Miguel, HDR, and STBN/noise bytes and their provenance must
remain exact even when their import/generation tools disappear. Historical
postmortems and Git commits may name Python; “zero active references” must not
erase recovery evidence or license bodies. Final validation must prove CMake,
both configurations, CTest, workflows, launcher/package contents, PATH
handling, docs links/instructions, frozen hashes, and absence of interpreters
and `.py` files from first-party source and shipped artifacts.

**Post-cut source audit.** No first-party `.py` file remains. Outside policy,
historical postmortems, and third-party source, six exact old-script filename
occurrences remain intentionally: three byte-protected Bistro/San Miguel
provenance fields and three assertions of those fields in
`tests/scene_asset_provenance.cmake`. That test now also proves each named
first-party path is absent. Package validators retain rejection-only `.py` and
`.pyc` extension literals. These are immutable history and negative policy,
not invocations, dependencies, PATH entries, downloads, or package members;
the result is zero active tooling, not zero literal strings. The provenance
and staged-byte parity gate passed locally; final fresh build, workflow, and
exact-package scans remain part of the integrated gate.

Recovery boundary: recover every script and its call sites from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`.

Lesson: port only recurring product validation, and delete one-time generation
after freezing its outputs and provenance. Restore a script only when repeated
current use is demonstrated, a narrow C++ or ordinary build-rule replacement
is materially worse, and developer/package gates still require no interpreter;
never restore Python as a fallback.

## 8. Donut Detachment

### Observed Evidence and Ordered Slices

The Donut gitlink pins `bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`.
Its root tree contains 257 blobs and 2,758,312 bytes. Six nested gitlinks add
965 blobs and 19,615,823 bytes: ShaderMake `5daebd...` (14/118,731), NVRHI
`8e8c36...` (84/1,806,349), cgltf `fa3b80...` (19/282,918), GLFW `7b6aea...`
(170/4,997,025), ImGui `45acd5...` (250/7,290,772), and stb `2e2bef...`
(428/5,120,028). Combined footprint is 1,222 blobs and 22,374,135 bytes.
Thirteen local overrides add 408,407 bytes: five Donut patches, five ImGui
patches, and three NVRHI files.

Current broad coupling is 201 `donut/` reference lines in 63 files, 275
`donut::` lines in 57, 2,451 `nvrhi::` lines in 54, 1,527 `ImGui::` lines in
five, 195 GLFW lines in five, and 18 ShaderMake lines in six. CMake links Donut
render/app/engine and consumes its patched ImGui, NVRHI, and ShaderMake. These
counts overlap and are not additive. No intended detachment slice is complete
at the snapshot.

Each slice below has evidence boundary `e29a41245dbd0e6fd7a819d2341646419ab76e72`
plus the named dependency pin above, and the same recovery boundary: restore
the complete caller and dependency state from `e29a412...`, not a lone copied
file.

| Slice | Observed Owner | Replacement And Required Evidence |
| ---: | --- | --- |
| 1 | Patched nested ImGui | Pin ImGui directly; prove exact UI behavior, inputs, backends, license, and production package. |
| 2 | Five ImGui patches and private/widget coupling | Rebuild required widgets on public APIs, then delete patches; prove all retained controls and DPI/input behavior. |
| 3 | Nested NVRHI plus three overrides | Pin the same NVRHI revision directly, apply only required owned changes upstream or locally, and prove DX12 barriers, descriptors, ray tracing, resize, and debug-layer cleanliness. |
| 4 | ShaderMake tasks and build integration | Compile with direct DXC ownership; prove every retained permutation, dependency tracking, diagnostics, deterministic staging, and package shader inventory. |
| 5 | Donut CPU/HLSL shared headers | Replace used contracts with narrow first-party definitions; prove layout, enum, offset, and known-answer parity on CPU and GPU. |
| 6 | `donut_app`, GLFW, window/device shell | Own a direct Win32/DX12/ImGui shell; prove adapter/device creation, swapchain, HDR/SDR, resize, fullscreen, input, timing, recovery, and shutdown. |
| 7 | Nested cgltf, stb, and Donut scene/image loading | Pin cgltf and one required image decoder directly with a narrow JSON/scene loader; prove retained Bistro, San Miguel, HDR, and noise inventories and material/animation behavior. |
| 8 | Used Donut render passes | Replace one pass at a time and delete its old path in the same change; prove output, resource lifetime, barriers, and affected protected-feature matrix. |
| 9 | Donut core utilities | Replace only used math/filesystem/logging helpers with the standard library and DirectXMath; prove coordinate, precision, path, Unicode, and diagnostics contracts. |
| 10 | Donut gitlink and overrides | Remove only after zero active Donut, patch, nested-dependency, build, package, doc, and license references; prove fresh configure/build without submodule initialization. |
| 11 | Remaining direct NVRHI pin | Evaluate direct D3D12 ownership only after Donut removal and only if measured simplification exceeds migration risk; no speculative rewrite is authorized now. |

### Completed Ownership Slice: Direct ImGui Pin

**Scope and reason.** The build now owns direct gitlink
`third_party/imgui` at exact revision
`45acd5e0e82f4c954432533ae9985ff0e1aad6d5`, and the active `imgui` target is
declared from that root pin. This completes ordered slice 1 only: ImGui remains
permanent, while removal of the five invasive widget patches is still slice 2.
Direct ownership makes the actual dependency identity visible without asking
Donut's root CMake to create the target.

**Evidence and measured burden.** The baseline nested pin was the same revision
and contained 250 blobs/7,290,772 bytes. While Donut remains initialized, the
direct checkout duplicates that recursive working-copy burden; no repository
or package-size reduction is claimed. The completed slice changes ownership,
not ImGui version or UI behavior. CMake rejects a missing or different pin and
marks the active target with its direct source root.

**Removed and retained contracts.** Active build ownership through
`donut/thirdparty/imgui` and Donut's root target declaration is removed. The
existing ImGui sources, five first-party patches, input/back-end integration,
fonts, DPI behavior, skins, settings, and retained controls remain. Donut's
nested physical copy stays only until the parent submodule is removed.

**Validation and recovery.** Exact-revision/source-root configure checks are
implemented. The final developer UI tests, representative input/DPI runtime
matrix, production package license/inventory, and no-op rebuild remain release
gates; the pin alone is not product verification. Recover the complete former
transitive build from `e29a41245dbd0e6fd7a819d2341646419ab76e72`.

**Lesson and restoration criteria.** Moving an identical known-good revision
first isolates ownership risk from UI-change risk. Restore transitive ImGui only
if the direct target cannot reproduce a documented required behavior and a
minimal direct correction demonstrably fails; convenience is not sufficient.

### Completed Ownership Slice: Direct NVRHI Pin

**Scope and reason.** The active DX12 NVRHI target now comes from direct gitlink
`third_party/nvrhi` at exact revision
`8e8c36e37558acec333204619b95d9d2fcdc4a79`. Donut's nested NVRHI CMake is
skipped. The direct target keeps validation in developer builds and uses only
DX12; this makes barrier, descriptor, and ray-tracing ownership explicit while
preserving the known-good implementation.

**Evidence and measured burden.** The baseline nested revision was identical
and held 84 blobs/1,806,349 bytes. The direct checkout temporarily duplicates
that recursive working-copy burden while Donut remains; no size reduction is
claimed. Three existing first-party ownership files remain: the DirectX-Headers
pin patch, D3D12 portability patch, and diagnostics header. CMake asserts both
the exact revision and that the live NVRHI target's source directory is the
direct root.

**Removed and retained contracts.** Donut-root creation of the NVRHI target and
the nested NVRHI target source are removed from the renderer build. NVRHI's DX12
API, validation layer, Agility/DirectX-Headers alignment, existing portability
changes, debug diagnostics, renderer call sites, and package notice are
retained. The final source sweep found no active nested-NVRHI include or target
owner outside this historical record.

**Validation and recovery.** Revision/source-owner configure checks are
implemented. Integrated Release compilation, focused NVRHI/DX12 tests, D3D12
debug-layer and PIX barrier/lifetime/ray-tracing evidence, retained-feature
runtime coverage, and exact production packaging remain final gates. Recover
the former complete transitive target from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`.

**Lesson and restoration criteria.** Pin migration should not be mixed with a
backend rewrite. Restore the nested owner only for a reproducible regression
that the identical direct revision and narrow local fixes cannot resolve; do
not restore it to avoid correcting a stale include or notice.

### Completed Ownership Slice: Direct DXC and Owned Shader Blobs

**Scope and reason.** Direct pinned DXC v1.9.2602 now compiles UVSR, Donut, and
the retained NRD shader catalogs. The archive SHA-256 is
`a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e`;
`dxc.exe` is version 1.9.2602.17 with SHA-256
`b9cff94181248e080804b385da8964b6319fd07760721baa9053a891cf7a727f`.
First-party NVSP parsing/packing replaces ShaderMake's runtime blob helper.
Per-family DXIL objects have compiler depfiles; content-stable catalogs feed
deterministic family blobs. This removes the global invalidation owner while
preserving one developer/production shader catalog.

**Evidence and measured burden.** The baseline nested ShaderMake pin held 14
blobs/118,731 bytes and appeared on 18 broad reference lines in six files.
Baseline expansion was 311 UVSR tasks, including 192 TAA blend tasks, plus 76
Donut tasks; optional upstream NRD described 159 more. The direct ownership
slice first closed at 125 UVSR tasks (109 after the first renderer cut plus 16
required receiver variants). Four later direct common/readback tasks make 129
UVSR tasks. After the completed direct pixel-readback slice removed its six
duplicate Donut tasks, a fresh interim reconfigure reported 129 UVSR tasks in
32 families, 70 Donut tasks in 40 families, and 34 retained NRD tasks in 29
families: 233 total. A caller sweep then excluded Donut's unused eight-task
`passes/taa_cs.hlsl` family; reconfigure purged its 20 generated files and
reported 62 Donut tasks in 39 families, for 225 then-current tasks and exactly
16 retained first-party TAA tasks. That 129/62/34=225 result remains measured
intermediate history. Direct ownership of the four used light-probe shaders
then changed the catalog to an intermediate 133 UVSR, 57 Donut, and 34 NRD
tasks: 224 total. Direct ownership of the two used geometry families
subsequently replaced six Donut tasks, making the live catalog 137 UVSR tasks
in 38 families, 51 Donut tasks in 31 families, and 34 NRD tasks in 29 families:
222 total. The strict runtime package inventory changed from 46 to 45 shader
blobs. This is a moving detachment measure, not a final catalog.
DirectXShaderCatalog (311 lines), `shader_blob.cpp` (184), `shader_blob.h` (42),
and the builder (677) total 1,214 physical first-party lines at this
measurement; its focused scanner test adds 215 test lines. This is replacement
burden, not a net deletion claim. The include scanner is source-complete but
its strict build/self-test and full clean/no-op/stale-output bundle evidence are
still open. ShaderMake remains physically nested only as part of Donut until
that parent gitlink is removed.

**Removed and retained contracts.** Removed active contracts are Donut's root
CMake ownership, the ShaderMake executable/target, its broad shader stamp,
ordinary deletion of all shader outputs before rebuild, and ShaderMake blob
lookup. Retained contracts are the exact DXC version/profile/options, every
supported permutation, NVSP lookup/diagnostics, dependency-driven rebuilds,
stale-output removal for clean packaging, and exact runtime blob inventory.

**Validation and recovery.** Direct-DXC probes compiled all 20 required
sky/flashlight receiver variants and all eight PBR MSAA variants; integrated
Release C++ compilation also passed. After adding the four common/readback
tasks, an incremental bundle build compiled exactly those four shaders and its
renderer contract passed; the immediate no-op ran zero DXC and zero pack work in
0.993 seconds. A fresh all-family compile, one-leaf/include touch test,
stale-output/package test, complete developer gate, and fresh production
shader/package inventory remain final evidence. Recover the former ShaderMake
owner and blob contract from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`.

**Interim detachment metric.** The first-party source scan after direct
readback, before the latest logging integration, found 77 Donut include lines
across 29 files and Donut names or symbols in 41 files; `src/uvsr.cpp` remained
24,781 physical lines. These are remaining-work measures, not full detachment or
final source-burden claims.

**Lesson and restoration criteria.** A compiler wrapper was valuable only while
UVSR did not own its small catalog/blob contract. Restore ShaderMake only if a
required current shader feature cannot be expressed or verified by direct DXC
and the measured benefit exceeds the added transitive source, invalidation, and
diagnostic cost; never restore it as a fallback.

### Completed Ownership Slice: Direct Pixel Readback

**Scope and reason.** The material-selection readback used one narrow Donut
pass, so ordered render-pass slice 8 now owns that path directly. Before the
cut, `uvsr.cpp` included `donut/render/PixelReadbackPass.h`, held a Donut pass,
constructed it at both Material-ID setup points, submitted `Capture(x, y)`, and
published the `uint4` returned by `ReadUInts`. Donut compiled
`donut/src/render/PixelReadbackPass.cpp`; its shader configuration expanded six
pixel-readback tasks within the 76-task Donut catalog. After the cut, those same
two construction and selection call paths use `uvsr::RendererPixelReadback`,
and one first-party `uvsr/renderer_pixel_readback_cs.hlsl` task owns the GPU
operation. `DirectDonut.cmake` excludes the old source and all six old shader
tasks, reducing the interim Donut catalog from 76 to 70.

**Burden and product effects.** The exact replacement implementation is 383
physical lines: `renderer_pixel_readback.h` 65,
`renderer_pixel_readback.cpp` 209, `renderer_pixel_readback_cb.h` 71, and
`renderer_pixel_readback_cs.hlsl` 38. Shared resource-contract tests,
reflection, shader factory, and common-pass support are not charged to this
slice. The path owns one 16-byte volatile constant buffer, one 16-byte
`RGBA32_UINT` typed-UAV intermediate, one 16-byte CPU-read buffer, exact
`b0`/`t0`/`u0` bindings, and a 1x1 dispatch and copy. The source is constrained
to single-sample `Texture2D` Material IDs. Settings, UI, selection meaning, and
asset inventory are unchanged. Relative to `bin/shaders/`, the package replaced
`framework/dxil/passes/pixel_readback_cs.bin` with
`uvsr/dxil/renderer_pixel_readback_cs.bin`. The six former permutations fed one
Donut family blob, so runtime blob cardinality remains one while its ownership,
family, and manifest path change one for one.

**Removed and retained contracts.** Removed active contracts are the Donut
include, type, implementation source, six shader tasks, old family blob and
package member, and duplicate build ownership. Retained behavior is capture at
the requested Material-ID pixel and publication of four unsigned values after
a successful map. Construction is
fail-closed through `IsValid`; `ReadUInts` returns no value when no capture is
pending or mapping fails, so a failure cannot publish a false selection.

**Validation and recovery.** Strict C++17 `/permissive- /W4 /WX` compilation,
direct-DXC `cs_6_5` compilation, `b0`/`t0`/`u0` plus 1x1 reflection, resource
creation truth-table checks, and successful-map/unmap and failed-map/no-publish
behavior checks passed. The integrated caller compiled before the current
full-tree rebuild. The complete developer gate, material-picking runtime check,
and exact production package remain open integration evidence. Immutable
pre-slice recovery is
`9c42f44b3818c8a1d9904452ead3c8fb96349ece`; no authorized final checkpoint
commit yet records this completed working-tree slice, so the eventual
integration commit must be added to this record.

**Lesson and restoration criteria.** A one-pixel readback does not justify a
framework-owned pass or six dormant permutations. Restore the Donut path only
if direct creation, ABI/reflection, or valid material/instance selection parity
fails and the direct implementation cannot be corrected narrowly. Restoration
must atomically restore caller, source, and all six shader tasks; never retain
both inventories.

### Provisional Ownership Slice: Direct Light-Probe Processing

**Scope and reason.** The retained HDR environment path used Donut's
`LightProbeProcessingPass` only for cubemap blits and mips, specular GGX
prefiltering, and the environment BRDF lookup. It now constructs
`RendererLightProbeProcessing` with the direct shader factory and common passes.
The direct class owns those exact calls and returns failure to the environment
state machine; diffuse irradiance remains the first-party CPU projection and
upload, so the unused Donut diffuse shader has no replacement.

**Burden and product effects.** The retired Donut family comprises 805 physical
dependency lines:
the 140-line header, 404-line implementation, 34-line constant-buffer header,
and 227-line five-entry shader. The direct replacement is 925 lines:
`renderer_light_probe_processing.h` 148,
`renderer_light_probe_processing.cpp` 508,
`renderer_light_probe_contract.h` 39, and
`light_probe_processing.hlsl` 230. The dependency files remain nested until the
Donut gitlink leaves, so neither figure is a physical-deletion or net-reduction
claim. The shader catalog replaces five Donut tasks and blobs with
four first-party tasks and blobs. The strict package inventory is now 45
shaders. Settings, HDR assets, diffuse/specular scales, and environment choices
are unchanged.

**Removed and retained contracts.** The active Donut C++ pass/header, its five
shader tasks, `light_probe.hlsl`, `light_probe_cb.h`, framework blob paths, and
caller dependency are removed from the build. Retained GPU contracts are the
16-byte `sampleCount`/`lodBias`/`roughness`/`inputCubeSize` buffer at `b0`, cube
texture `t0`, sampler `s0`, six-face geometry replication, base blit and mip
generation, 1,024-sample GGX filtering, and 1,024-sample split-sum BRDF. The
direct path fail-closes initialization and publishes an imported environment
only after all required processing succeeds.

**Validation and recovery.** The direct ABI has exact size, alignment, member
offset, standard-layout, and trivial-copy assertions. Focused tests cover every
initialization-resource flag, probe activation, constants, environment
projection, and normalized cube directions; the production bundle contract
requires exactly the four direct entries and rejects `diffuse_probe_ps`.
Reconfiguration at this slice bound the then-current 133/57/34 task counts,
excluded both the old Donut C++ pass and shader family, and bound the 45-blob
package inventory. The later geometry closure makes the live count 137/51/34.
Direct DXC, integrated engine link/runtime, rendered HDR diffuse/specular parity,
failure-path publication, and exact production package evidence remain open;
therefore this is not yet a completed working-renderer checkpoint. Recover the
complete pre-slice implementation from
`9c42f44b3818c8a1d9904452ead3c8fb96349ece`.

**Lesson and restoration criteria.** A four-operation environment prefilter
does not justify a framework pass or an unused fifth shader. Restore the Donut
family only if a retained HDR environment fails a provenance-bound diffuse,
specular, BRDF, lifetime, or package comparison and the direct implementation
cannot be corrected narrowly. Restore caller, C++ pass, all five tasks, and
package paths atomically; never ship both paths.

### Completed Ownership Slice: Direct NRD and MathLib Pins

**Scope and reason.** The renderer now fetches immutable commit archives for
NRD 4.17.3 at `792eff196afdd350fd9c3f862119017ccb438a0e` and MathLib
at `974e1387ba936740c7cdc494792d2641bc127e86`. Their archive SHA-256 values are
`ad148d3653e7e4a149af0d1608ec662eeb522144cf34f6a29f9dfd333933baa8`
and `8250a1a903cb9d69234029a226349abf05d23a61a6cb2f1caf8fded8e5bcdea5`.
Direct ownership makes required denoising independent of Donut's optional
upstream build and limits it to REBLUR_DIFFUSE, RELAX_DIFFUSE, and SIGMA_SHADOW.

**Evidence and measured burden.** The baseline upstream NRD ShaderMake catalog
expanded to 159 tasks. The direct catalog has 29 source-family rows expanding
to 34 direct-DXC tasks, 125 fewer than that baseline. The current first-party
owner is 415 physical lines: `DirectNRD.cmake` 187, retained config 9, catalog
32, and narrow blob patch 187. This is ownership burden, not a dependency-size
reduction.

**Removed and retained contracts.** Removed active contracts are
`UVSR_WITH_NRD` optionality, Donut/upstream CMake target ownership, the broad
method set, upstream ShaderMake compilation, and transitive MathLib discovery.
Retained contracts are the three user-facing NRD methods, required CPU/HLSL
interfaces, owned shader blobs, exact upstream terms, and package copies
`bin/licenses/NRD-LICENSE.txt` and
`bin/licenses/NVIDIA-MathLib-MIT.txt`.

**Validation and recovery.** A fresh `BUILD_TESTING=ON` configure reported 34
NRD tasks/29 families; Release builds of exact target `NRD` and the canonical
`uvsr-engine` target passed. The C++ legal inventory check also passed. Focused
denoiser tests, full developer gate, runtime method matrix, and fresh production
package/license/blob inventory remain final gates; this build slice is not
distribution proof. Recover the former complete optional Donut/ShaderMake
ownership from `e29a41245dbd0e6fd7a819d2341646419ab76e72`.

**Lesson and restoration criteria.** A required dependency should expose its
exact immutable source and only the methods the product promises. Restore an
upstream method or build owner only for a proven retained-product requirement
that the direct pin cannot meet, with measured task/runtime cost and license
review; do not restore optionality, transitive discovery, or ShaderMake as a
fallback.

### Provisional Ownership Slice: Direct STB Implementation

**Scope and burden.** DirectDonut no longer compiles
`donut/src/engine/stb_impl.c` (52 lines at Donut commit
`bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`). The 9-line
`src/renderer_stb_image.cpp` is the sole engine/global-linked STB implementation.
It owns retained HDR decode and temporarily exports image-write symbols for
Donut TextureCache; `diffuse_environment_asset_tests` keeps a deliberately
internal `STB_IMAGE_STATIC` test copy. This is implementation ownership, not
completion of the loader cut.

**Behavior and evidence.** First-party HDR loading still uses `stbi_loadf`.
Transitional TextureCache keeps memory decode plus BMP/PNG/JPG/TGA writes. The
RelWithDebInfo `uvsr_renderer_pass_detachment_tests` link and
`uvsr_renderer_pass_detachment_reference` CTest passed after the single-owner
change. The integrated `uvsr-engine` link, scene/HDR runtime checks, and final
loader removal remain open and must replace this focused evidence.

**Recovery and restoration.** Recover the root build graph from
`9c42f44b3818c8a1d9904452ead3c8fb96349ece` and the former implementation from
the Donut commit above. One header library needs one implementation owner;
restore the Donut TU only if the direct TU cannot preserve a proven retained
decode/write caller, and never compile both. Remove temporary write ownership
with TextureCache; do not preserve it after the direct loader lands.

### Transitional Ownership Slice: Direct Third-Party Pins

**Scope and exact pins.** `cmake/DirectThirdParty.cmake` moves five dependency
targets out of Donut's CMake ownership while Donut callers are retired:

| Dependency | Revision | Archive SHA-256 | Current Caller Boundary |
| --- | --- | --- | --- |
| cgltf | `fa3b80fa762790192c9532b63c441627416ff300` | `89351d82a140337ac876e018b091f26176fcc8c227479796993ce79be33ed8a3` | Donut glTF importer and retained scene-asset validation |
| stb | `2e2bef463a5b53ddf8bb788e25da6b8506314c08` | `f6a4669309a29dd8634c3c2c7a955da72469c2dc61471f68d9c499e517ab823f` | First-party HDR loading and Donut's transitional texture path |
| JsonCpp | `89e2973c754a9c02a49974d839779b151e95afd6` | `02f0804596c1e18c064d890ac9497fa17d585e822fcacf07ff8a8aa0b344a7bd` | Donut core JSON callers only |
| TinyEXR | `58a81c36caad469aed86441cc91080f23b496ffb` | `c745ae7f336760014509f900779187825b12e61e699ae8a49679a546cd5b8147` | Donut texture loading only; no retained EXR caller is proven |
| GLFW | `7b6aead9fb88b3623e3b3725ebb42670cbe4c579` | `699bf0b3d0bd422c0212263f30c4b6fc1ab4f67320824b27854f1e5c6949a2a0` | Donut application shell plus transitional UVSR input/window call sites |

Exact package notices are cgltf MIT (1,066 bytes, SHA-256
`f619925f80ef862497aaf8e8155ef218fa6a2190055129523ca3df9119a9ba95`),
stb terms (2,510,
`bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63`),
JsonCpp terms (2,714,
`cec0db5f6d7ed6b3a72647bd50aed02e13c3377fd44382b96dc2915534c042ad`),
GLFW terms (904,
`149704059b5d0bf551637e50042dd4de9c2cae921021f6636298911e3a5f9462`),
and the consolidated TinyEXR/OpenEXR terms (3,116,
`0d3e165809f0c704b67e8cd860e5e0e148122f70a9d2ac90400d3df9482c67f4`).
Their exact `bin/licenses` destinations are in the linked legal dependency
records and source package mapping.

This is an ownership substep, not completion of ordered shell slice 6 or loader
slice 7. GLFW must leave with the Donut shell. TinyEXR must leave with the Donut
texture loader unless a retained EXR caller is proven. The final loader should
keep one image decoder; current first-party PNG/JPG/HDR use supports stb.
JsonCpp survives only while an actual remaining caller requires it. The Donut
gitlink, DirectDonut owner, and recursive third-party tooling—including nested
Python files—also remain, so this is not a completed dependency endpoint or
complete Donut detachment.

**Measured burden and contracts.** At the first five-pin integration
measurement, the direct owner was 109 physical lines and 5,149 bytes. That is
an interim boundary, not the changing loader/JsonCpp endpoint; remeasure after
those callers settle. At the baseline, nested cgltf, GLFW, and stb contributed
617 blobs and 10,399,971 bytes; vendored JsonCpp and TinyEXR contributed six
files and 765,447 bytes. Those physical copies remain inside Donut, so direct
fetching is temporary duplication and no source or package reduction is
claimed. Removed contracts are Donut's target declarations and opaque vendored
ownership.
Retained contracts are exact upstream revisions and archive hashes, existing
call behavior, and exact package notices. Remaining Donut callers are explicit
in `cmake/DirectDonut.cmake`: `donut_core` consumes JsonCpp, `donut_engine`
consumes JsonCpp/stb/cgltf/TinyEXR, and `donut_app` consumes GLFW.

**Validation, recovery, and lesson.** Source audit confirms immutable 40-hex
archive URLs, SHA-bound downloads, exact license checks, direct target owners,
and the caller map above. A fresh configure, developer build, retained scene/HDR
tests, shell/loader runtime evidence, and exact production notice inventory
remain integration gates; no build was run after this direct-pin edit. Recover
the former complete Donut dependency ownership from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`. Direct pins expose ownership but do
not finish detachment. Restore a removed dependency only for a proven retained
caller and its tested behavior; never keep GLFW, TinyEXR, or a second decoder as
a speculative fallback.

Every slice must have one owner, replace a complete used path, and delete the
old path in the same change. Dual backends, wrappers, and fallbacks would make
the transition permanent. Changes to barriers, lifetimes, or ray dispatch need
PIX/debug-layer evidence plus the protected 2x/4x/8x/16x MSAA, AO/GI, sky,
flashlight, path-tracing, Bistro, San Miguel, HDR, and noise matrix. Final
footprints must be measured after each slice; the observed 22,374,135-byte
gitlink footprint is not a package-size or deletion forecast.

## Execution-Plan Archive Retirement

At the pre-cut snapshot, `docs/exec-plans/completed` held 88 records,
1,296,123 bytes, and 174,055 words; `docs/exec-plans/abandoned` held five
records, 144,264 bytes, and 19,663 words. These 93 records mixed transient task
state, machine incidents, command logs, generated measurements, and repeated
requirements. Exact commit history is the recovery mechanism after deletion.

The durable lessons are: establish one baseline before variants; define visual,
performance, reset, and retirement gates before exposing controls; treat source
and arithmetic tests as complements to rendered evidence; bind benchmarks and
acceptance to exact source, settings, scene, camera, package, and artifact; keep
experiments isolated until they win; replace and remove a failed path together;
and preserve only compact recovery evidence in postmortems. The cut-specific
sections above own the stage-two evidence. Earlier retained-feature decisions
remain in the dated postmortems beside this record.

The two files formerly marked active described legal review and cross-vendor
publication against superseded executable and launcher contracts. Their durable
rule remains: publication is separate authority and requires live-remote
identity, a fresh exact production package, cross-vendor evidence, complete
applicable notices/licenses, and executable/package SHA-256 provenance. This
local cut authorizes none of those external actions.

Recovery boundary: restore any archived plan from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`; do not restore an archive as an
active instruction without revalidating its premise and exact base.

## Assertion Replacement Map

Dependency and build identity use exact staging wrappers, ordered digests,
tamper checks, license-byte verification, and package validators. Settings and
restart behavior use the production coordinator and transaction tests, the
live contract verifier, and packaged canonical JSON. Shader and resource
contracts use direct compilation, DXIL reflection, inventory checks, and known
answer tests. Pass selection and fail-close behavior use render-disposition
tests, a fatal subprocess, and debug-runtime evidence. Resource lifetime uses
direct load and retirement tests plus NVRHI validation and PIX. Rendered output
uses the 1,016-case retained matrix and exact-package smoke/PIX evidence.
Distribution uses staged and extracted package validation plus launcher
lifecycle tests. Source spelling is not evidence for these contracts.

## Final Evidence Required

Completion requires measured integrated diffs, zero active retired references,
the full developer gate, and a fresh production package and runtime smoke. Bind
all claims to final source identity, canonical settings-number hash and derived
engine version, package inventory, representative retained-feature outputs,
and SHA-256 identities for the exact proven `uvsr-launcher.exe` and
`uvsr-engine.exe`. Launching alone is not verification.

The package-contract validator now requires exact
`bin/licenses/Intel-XeGTAO-MIT.txt` bytes (1,081 bytes, SHA-256
`1f3bfd6b628535f0f0e779e70c260cfdc1bb45bc8644d7108e65f83e24a05cba`)
and `bin/licenses/Andrew-Helmer-Stochastic-Generation-MIT.txt` bytes (1,053
bytes, SHA-256
`50a4be869e51722a4ca90819535a78df8f82d68facbad52a2da6ab4dc284ad55`).
Its focused self-test passed positive, missing-XeGTAO, and tampered-Helmer
fixtures. This proves the validator boundary, not their presence in a final
archive; the exact staged and extracted package checks remain required.

The developer retained-runtime diagnostic cannot prove the production engine.
The exact-package rendered gate first binds the manifest, independently
computed engine SHA-256, and `--identity-json`; launches that engine with its
ordinary scene/size CLI; applies settings only through normal production
settings/UI; and retains settings/camera identity, captures, and timings.
Focused external
[`pixtool.exe`](https://devblogs.microsoft.com/pix/pixtool/) capture and
debug-layer replay are additionally required for barrier, lifetime,
ray-tracing, and Donut-detachment changes; other provenance-bound rendered
capture routes remain valid. Settings without a normal CLI require operator
interaction, so capture acquisition remains an explicit local DXR-hardware
gap. No matrix-complete claim is authorized until bound artifacts exist.
