# validation

use the cheapest evidence that can detect the changed failure. compilation,
semantic tests, reflection, lifetime tests, fault injection, package checks,
rendered output, visual review, and performance measurements answer different
questions. do not substitute one for another.

the retained runtime diagnostic defines exactly 34 named cases below. it has no
family multiplication or generic Cartesian expansion. the document defines the
contract; bound runtime results are the evidence.

## evidence identity

every runtime or package record identifies:

- source commit and relevant diff;
- build configuration, generator, toolchain, options, and dependency identity;
- settings schema, complete hash, and loaded snapshot when used;
- executable path and independently calculated SHA-256;
- adapter, driver, scene, camera, viewport, and raster sample count;
- HDR environment, warmup, sample or frame window, and action sequence; and
- package manifest and launcher identity for production evidence.

record exact measurements as exact. label estimates and calculated resource
sizes. a source spelling check is not behavior evidence. replace each such
assertion only after another check detects its distinct failure.

## focused checks

build only affected targets while iterating, then run matching CTest names. the
main focused groups are:

- settings: `uvsr_settings_commands_reference`, `uvsr_settings_snapshot_reference`,
  `uvsr_settings_snapshot_decoder_reference`,
  `uvsr_settings_snapshot_transaction_reference`, and
  `uvsr_settings_snapshot_values_reference`; run the schema probe with `--check`;
- retained display, FXAA, and exposure: `uvsr_display_controls`;
- lighting and resources: `uvsr_pbr_reference`, `uvsr_renderer_resources`,
  `uvsr_flashlight_reference`, and `uvsr_path_tracing_semantic_reference`;
- assets and package: `uvsr_scene_asset_contract`, `uvsr_build_integrity`,
  `uvsr_distribution_contract`, `uvsr_shader_bundle_contract`, and
  `uvsr_shader_reflection`; and
- runtime matrix structure: `uvsr_retained_runtime_diagnostic_tests`.

the standalone native launcher build has `launcher_pure`. explicitly enabled
`launcher_system_services` and `launcher_runtime` cover registry/COM workflows
and child-process ownership. the full build script also checks feed tooling,
native executable health, and source identity. see the [launcher guide](../launcher/README.md).

fault inject allocation, shader creation, dispatch, readback, selector
resolution, settings apply, settings readback, rollback, scene retirement,
package extraction, hash, and signature failures where that boundary changes.

## exact 34 case contract

the suite has seven isolated global-noise cases and 27 interactions:

```text
7 noise + 2 all signal + 6 HDR + 2 flashlight causal + 15 path tracing + 2 recovery = 34
```

each case has a literal stable name, declared baseline, prerequisites, one
audited delta or interaction, expected producers and consumers, action, scene,
sample count, and output checks. prerequisites make the changed value active and
are not additional audited deltas. they reset before the next case. do not
expand a case across scenes, noise combinations, or snapshot states. both
scenes are covered by named cases below; rasterization is always single-sample.

all output must be finite and have the expected extent. each case verifies exact
settings readback, dispatch and resource state, scene identity, sample count,
and the relevant output or history relation. require an image difference only
for causal changes where the expected relation is deterministic. preserve the
linear output hash, luminance summary, edge count, timings, and action evidence
for review.

### isolated noise cases

these cases use raster lighting and ray traced directional visibility, with
global spatiotemporal-blue 128x128 noise, animation on, and accumulation off.
each changes its named setting, alternating Bistro and San Miguel in listed order.

| case names | expected outcome |
| --- | --- |
| `noise-pattern-spatial-white`, `noise-pattern-spatial-blue` | selected texture binds |
| `noise-resolution-64x64`, `noise-resolution-256x256`, `noise-resolution-512x512` | exact texture extent binds |
| `noise-animate-off` | animated phase stops |
| `noise-accumulate-on` | the cumulative lighting mean commits |

all seven require directional visibility and exact accumulation state. no
screen-space diffuse setting or diagnostic case remains. runtime JSON schema
4 removes its obsolete dispatch field.

### all signal interactions

these two cases enable directional visibility, sky visibility,
flashlight lighting and visibility. each
performs a startup snapshot round trip, nudges the camera, switches to the other
retained scene, and resizes to 704x400.
histories and timing snapshots must settle after each action:

- `all-signal-bistro-to-san-miguel`
- `all-signal-san-miguel-to-bistro`

both use one raster sample per pixel. the expected directional, sky, and
flashlight producers must publish their corresponding single-sample outputs.

### HDR interactions

these six 1x Ray Tracing cases select the exact retained environment, enable
diffuse and specular IBL, background, sky visibility, and finite output checks:

- `hdr-environment-day`
- `hdr-environment-bright-overcast`
- `hdr-environment-soft-day`
- `hdr-environment-night`
- `hdr-environment-starry-night`
- `hdr-environment-cloudy`

each row changes from the preceding listed environment, wrapping Cloudy to Day,
and requires a different output. alternate Bistro and San Miguel in listed
order. enable automatic exposure on
the second, fourth, and sixth rows, with distinct minimum, middle, and maximum
compensation, movement, and adjustment period fixtures. verify the selected HDR
identity, no stale environment reuse, IBL and background response, sky
visibility, exposure dispatch state, and stable output after reset.

### flashlight causal interactions

- `flashlight-lighting-toggle-bistro-1x` starts with flashlight lighting on and
  shadows off, then disables the flashlight. direct lighting submission stops
  and output must change.
- `flashlight-shadow-toggle-san-miguel` starts with flashlight and shadows
  on, then disables only Cast Shadows. direct flashlight
  lighting remains, visibility stops, and output must change.

### path tracing interactions

all path cases use 1x, disable selective sky visibility, select
path Tracing, reach at least three accepted center pixel samples, and prove a
finite cumulative mean. the literal names and actions are:

| case | action and expected relation |
| --- | --- |
| `path-tracing-bistro` | Bistro baseline and startup snapshot round trip |
| `path-history-camera-reset` | camera nudge restarts history |
| `path-history-resize-reset` | resize to 800x448 restarts history |
| `path-tracing-san-miguel-scene-reset` | Bistro to San Miguel switch restarts history |
| `path-history-environment-reset` | day to Night changes output and restarts history |
| `path-history-exposure-reset` | `-2.75` to `-1.75` eV changes output and restarts history |
| `path-history-global-noise-reset` | spatiotemporal Blue to Spatial Blue restarts history |
| `path-history-material-reset` | a supported material edit restarts history |
| `path-history-light-reset` | an editable light change restarts history |
| `path-history-flashlight-reset` | flashlight toggle restarts history |
| `path-history-lighting-solution-cycle` | ray tracing and Path Tracing cycle restarts path history |
| `path-history-maximum-bounces-reset` | 1 to 8 bounces changes output and restarts history |
| `path-history-minimum-bounces-reset` | minimum 1 to 4 restarts history |
| `path-history-firefly-filter-reset` | off to on at threshold 10 changes output and restarts history |
| `path-history-firefly-threshold-reset` | 10 to 1000000 changes output and restarts history |

the suite must assert 34 literal unique names and the category counts above.
delete family multiplication, rotating implicit fixtures, and assertions that
only count generic combinations. keep separate schema unit tests for complete
domains and persistence. runtime cases prove rendering, not catalog enumeration.

## full developer gate

use the developer configuration in [build and shaders](build-and-shaders.md).
at a named checkpoint or final code handoff, build all configured targets once
and run the complete CTest suite once.

after a broad failure, run one narrow reproducer. repair or report it rather
than rerunning an unchanged full gate. the developer diagnostic does not prove
the production executable.

## exact package gate

use the separate production tree and strict package procedure in
[build and shaders](build-and-shaders.md). the source must be the exact clean
candidate being evaluated.

validate the staged package and extracted exact ZIP against the package
manifest, runtime shader inventory, runtime asset map, engine exports, settings
identity, dependency state, and legal inventory. reject missing, extra,
modified, linked, debug, source, test, toolchain, interpreter, symbol, or
benchmark content.

install or exercise that exact archive through the trusted launcher path on
local DXR hardware. run the applicable 30 cases through normal product controls
and startup snapshots. preserve captures, timings, debug layer results, package
path, manifest, launcher and engine SHA-256, and identity output. barrier,
lifetime, ray tracing, and Donut boundary changes also need a focused PIX
capture and debug layer replay.

a release gate additionally requires explicit publication authority, current
remote identity, trusted signatures or hashes, exact launcher and renderer
artifacts, and complete legal material. the [launcher guide](../launcher/README.md)
owns feed and installation trust.

### prerequisite recovery

these cases disable the prerequisite for three presented frames, assert that its
consumer is inactive, then restore it and require fresh finite output and active
consumers. path tracing must restart history. settings remain selected throughout.

- `path-traversal-cycle` and `ray-marching-traversal-cycle`.
