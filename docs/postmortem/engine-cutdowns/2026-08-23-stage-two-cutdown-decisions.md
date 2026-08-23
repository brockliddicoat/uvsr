# Stage-Two Renderer And Launcher Cutdown Decisions

## Record Identity

- Record date: 2026-08-23.
- Evidence snapshot and pre-cut recovery point:
  `e29a41245dbd0e6fd7a819d2341646419ab76e72`.
- Status: pre-cut decision record. No removal described here was counted as
  complete when this record was written. Final source, package, runtime, and
  artifact evidence must replace estimates after integration.
- Count definition: physical lines and tracked bytes at the evidence snapshot.
  Broad reference counts are search results, not ownership counts. Build,
  dependency, and output trees are excluded.

“Observed” below means inspected source, Git history, or durable project
records at the evidence snapshot. “Inference” identifies a proposed boundary,
expected reduction, or conclusion not yet demonstrated by the final build.

## 1. Conventional Path Tracing And ReSTIR Retirement

### Observed Evidence And Burden

The path-tracing family has 8,524 physical source lines across
`src/path_tracing_settings.h`, the path-tracing pass and constant buffer, three
path-tracing shader/include families, and stable-plane resolve. Its two
dedicated tests add 2,805 lines. `shaders.cfg` compiles 25 tasks: 18 main
solver/RTXDI/next-event-estimation combinations, six primary-surface
combinations, and one stable-plane resolve task. Runtime construction owns 18
main pipeline variants plus fallback selection.

The persisted surface selects three solvers (RTX Path Tracing, ReSTIR Path
Tracing, and ReSTIR GI), three next-event-estimation policies, three denoiser
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

### Decision, Replacement, And Risk

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

### Observed Evidence And Burden

The family totals 1,610 source lines:
`src/sample_accumulation_settings.h` (625),
`shaders/sample_accumulation.hlsli` (145), and the generic lighting
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

### Decision, Replacement, And Risk

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

## 3. Heitz Ratio Estimation To Direct Visibility

### Observed Evidence And Burden

Dedicated Heitz/directional-shadow source is 1,892 lines: pass (746), header
(128), constant buffer (34), shader (793), shared ratio-estimator include (99),
and directional settings (92). Two dedicated tests add 1,984 lines. Seven
shader tasks cover one-sample hit-distance and modulation routes plus 2x, 4x,
8x, and 16x modulation. A broad ratio/Heitz search reaches 1,051 lines in 38
files; that broad count includes adjacent settings and is not a deletion count.

The implementation provides stochastic ratio-estimator directional shadows,
correlated RGB modulation, a hard-shadow route, optional hit distance, phase
and history state, bindings, UAVs, and noise/sample-rate controls. Durable
evidence includes deterministic CPU math and source tests, shader compilation,
and bounded Sponza smokes. The follow-up plan records corrupted wall/occlusion
captures and inert origin-safety behavior requiring bias repair; exact failing
camera visual acceptance remained pending. Its approximate 0.295 ms result was
an informal smoke, not a controlled benchmark. Origin:
`ca4bd62bf4c052791103dbd49e0dc48e301fdddf`.

### Decision, Replacement, And Risk

Replace the estimator with direct binary ray-query visibility for each active
MSAA sample. Never replicate one pixel visibility result across 2x, 4x, 8x, or
16x samples. Keep ray-traced sky visibility and flashlight shadows on direct
paths; retain hit distance only if an active denoiser consumes it. Remove the
estimator settings, phase/history resources, variants, bindings, UI, tests,
docs, and legal references as one slice.

Validation must cover opaque and alpha-tested edges, mixed visible/occluded
MSAA samples at all four sample counts, sky plus flashlight combinations,
Bistro and San Miguel, camera cuts, resize/reset, finite output, debug-layer
cleanliness, and exact production packaging. The replacement is not proven by
the historical smoke evidence.

Recovery boundary: use
`e29a41245dbd0e6fd7a819d2341646419ab76e72` for complete recovery and
`ca4bd62` for origin archaeology.

## 4. Classroom And Sponza Scene Retirement

### Observed Evidence And Burden

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

### Decision, Retained Assets, And Risk

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
`assets/noise` is 14 files and 22,983,229 bytes with inventory SHA-256
`6784ff5e0d29f7b127a749cfb2fb27217153fd7e5451173b47674c1efd0c8c41`.
Validation must prove exact retained asset inventories, catalog/default/load
behavior, Bistro and San Miguel representative runtime matrices, fresh-package
contents, and narrowed shared provenance/legal tests.

Recovery boundaries: Classroom origin/current asset commit
`f7c0c87d8cba6880428fbc34400eb2882fb5182e`; Sponza origin
`3ac53b382ee16101a04504811b7feb7a055f1773`, camera
`a7e51b7d3a09e18cc4e5da085b511623a87cc0ac`, and current adjustments
`f7c0c87`; complete pre-cut recovery `e29a41245dbd0e6fd7a819d2341646419ab76e72`.

## 5. Launcher Source Build And V1 Retirement

### Observed Evidence And Burden

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

Two v1 feed files remain: `installer/launcher-feed-v1.json` (321 bytes) and
`launcher/launcher-feed-v1.json`; a signed v2 feed also exists. Historical
source-build work begins at `a9004f518...`, compatibility work at `0c807484...`,
and the bridge at `639fd74f...`. Records show fresh-install failures followed
by repairs, but no evidence that a local compiler remains necessary once a
signed renderer package is authoritative.

### Decision, Replacement, And Risk

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

## 6. Executable Identity Consolidation

### Observed Evidence And Burden

The renderer target and output are currently `uvsr`/`uvsr.exe`. Launcher
constants still name `UVSR Launcher.exe`, `UVSR-Launcher-Windows-11-x64.exe`,
and `UVSR Installer.exe`; the project uses assembly name `UVSR-Launcher` and a
manual `1.1.14` file/product version. Active searches find `uvsr.exe` on 35
lines in ten files and the versioned launcher artifact on 28 lines in 14 files.
The canonical new names appear only sparsely at this snapshot.

### Decision, Replacement, And Risk

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

## 7. Python Removal

### Observed Evidence And Burden

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

### Decision, Replacement, And Risk

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

Recovery boundary: recover every script and its call sites from
`e29a41245dbd0e6fd7a819d2341646419ab76e72`.

## 8. Donut Detachment

### Observed Evidence And Ordered Slices

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

Every slice must have one owner, replace a complete used path, and delete the
old path in the same change. Dual backends, wrappers, and fallbacks would make
the transition permanent. Changes to barriers, lifetimes, or ray dispatch need
PIX/debug-layer evidence plus the protected 2x/4x/8x/16x MSAA, AO/GI, sky,
flashlight, path-tracing, Bistro, San Miguel, HDR, and noise matrix. Final
footprints must be measured after each slice; the observed 22,374,135-byte
gitlink footprint is not a package-size or deletion forecast.

## Final Evidence Required

Completion requires measured integrated diffs, zero active retired references,
the full developer gate, and a fresh production package and runtime smoke. Bind
all claims to final source identity, canonical settings-number hash and derived
engine version, package inventory, representative retained-feature outputs,
and SHA-256 identities for the exact proven `uvsr-launcher.exe` and
`uvsr-engine.exe`. Launching alone is not verification.
