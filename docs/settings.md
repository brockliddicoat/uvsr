# settings and snapshots

this document defines the maintained settings contract. the typed catalog,
startup transaction, migrations, diagnostics, and engine identity implement
it. package proof remains part of the checkpoint gate in
[validation](validation.md).

## canonical schema

one typed C++ schema must own every represented setting:

- canonical name, value type, domain, and default;
- UI section, visible binding, availability, and reset group;
- persistence and snapshot membership;
- selector and mutable value dependencies;
- rollback storage and snapshot read policies;
- typed mutation, effects, and readback;
- diagnostic serialization;
- schema fingerprint and derived engine identity; and
- an explicit migration from each supported older schema.

UI, startup, and diagnostics consume this schema. they do not copy defaults,
path branches, or domains. boundary tests, CI, package validators, and this
document pin the released fingerprint as a known answer. presets resolve to
concrete schema values. actions are not values and do not affect settings
identity.

the current registry uses schema `0018`, 112 values, and four actions. 107
values persist in snapshots and five are session only. its registered hash is
`6f635547a320c10c526bf23e8745a962`, with derived Windows version
`28515.21831.41760.49420`. supported migrations are `0007` through `0017`.
schema `0018` makes Cap the only interface and removes seven skin, font, and
palette values. migration requires and validates those old values, including
palette availability under the saved skin, before discarding them. retained
values are unchanged. current payloads reject the retired names.
schema `0017` changes the default firefly threshold to 50. saved values are preserved.
schema `0016` adds Cap and four Pathing values. legacy snapshots retain their
old transport: maximum 3 scattering bounces, minimum 1, and firefly filtering
off. newly introduced names or the Cap token in an older payload reject.
new defaults are documented in the [user guide](user-guide.md#path-tracing).
min/max bounce transactions validate the pair before mutation and order changes
so apply and rollback preserve the interval. the `ray-marching` persisted token
now displays as Ray Tracing; stored identity is unchanged.
schema `0015` removes 18 screen-space diffuse values and its debug view.
each `0007` through `0014` payload must contain all 19 fields with values in their
original domains. migration validates and discards them before applying retained
values. current payloads reject those names as unknown. tonemapper and FXAA now
share Postprocess; their persisted names and values are unchanged.
schema `0014` adds a session-only frame-limit switch, defaulting off, with a
stored positive limit of 240 FPS. VSync bounds its active maximum to the panel
refresh rate. shadow samples and hard-shadow override persist, defaulting to
four samples and off for new and migrated snapshots. the override preserves
stored samples and light emitter sizes.
schema `0013` adds the tonemapper enable switch, defaulting off for new and
migrated snapshots. existing tone values and LUT selections remain stored.
schema `0012` removes the session-only variable-refresh setting and restores
seven tone controls plus film LUT selection. older snapshots acquire neutral
tone controls and no LUT. Vertical Sync and Frame Rate Limit remain session only.
schema `0011` moves Allow Ray Traversal into Developer without changing values.
schema `0010` removes denoising and hit-distance fields. `0007` through `000f`
snapshots validate and discard those retired fields.
schema `000f` introduced shared session presentation controls. the retained
Vertical Sync and the original 0..960 FPS limit govern scene and test presentation without
changing loaded snapshot values. schema `000e` removed `gpu.adaptive-sync`;
`0007` through `000d` validate and discard that retired choice. all other retained
values survive. `0007` through `000b` also validate and discard
retired AA values. `0007` through `000a` also discard
the retired animation field; `0007` through `0009` discard the accent aliases;
`0007` and `0008` discard the acceleration structure policy fields; `0007`
also discards the old MSAA quality alias. every other older schema is rejected. any
later schema change requires a new registered fingerprint, regenerated engine
identity, and updated known answers.

the registry is
[`settings_snapshot_schema_versions.def`](../src/settings_snapshot_schema_versions.def).
version `0001` is reserved. published rows are immutable. never reuse a version
or fingerprint, allocate a number from branch order, or hand edit a hash.
compose the final schema first, then let the schema probe calculate its
identity. replace an unreleased provisional row before proof. append only when
allocating a new version.

## persistence classes

each represented value has one class:

- **snapshot** values serialize into copied snapshot payloads and participate
  in schema identity.
- **session only** values have a canonical default and participate in identity,
  but do not enter copied snapshots. panel collapse and Material visibility are
  session state.
- **none** applies only to actions. actions do not serialize or participate in
  identity.

dynamic selectors such as adapter, scene, selected light, and selected material
use stable identifiers. the 16 `light.selected.flashlight.*` values serialize
as `<unavailable>` unless the selected light is the flashlight. once a material is
selected, all material fields serialize their latent values even when their
domain, model, or texture hides the control. `debug.pbr.filter` also serializes
its latent value while inactive. loading must not reinterpret a missing
selector or bind an ambiguous display name.

factory reset restores represented renderer and interface defaults. it does not
change camera pose, scene, adapter, or shell navigation unless that owner
explicitly says otherwise. it restores every global flashlight default even
when an ordinary light is selected, while preserving the selected light,
selected material, and authored selected object values. a local reset restores
the smallest coherent preset or control group.

## code and catalog format

a snapshot code is exactly 32 lowercase hexadecimal characters. its first four
characters identify the registered schema. the remaining 28 fingerprint one
canonical value payload. the code is not the payload. decoding requires the
matching local catalog entry.

payload entries are sorted by canonical setting name. each line is
`name=escaped-value` followed by a newline. floats use enough precision for an
exact round trip. copying a code writes one framed code and payload entry to the
versioned catalog under the current user's writable Local App Data. package
routing may move that writable root but may not change payload identity.

the decoder rejects malformed codes, unknown versions, missing catalog entries,
duplicate names, unknown names, invalid domains, collisions, and payload hash
mismatches. `uvsr_settings_snapshot_decoder` is the only maintained standalone
decoder. no interpreter or fallback decoder belongs in source or packages.

the `0007` payload hash known answers are:

| canonical payload | code |
| --- | --- |
| empty | `0007cbf29ce4842223256c62272e07bb` |
| `a=b\n` | `0007ec8b8c82c37596fba90fe6756c5c` |
| `ui.skin=amp\n` | `0007582ac8a06042865d4c6f64bb61a4` |

these answers apply only while `0007` remains a registered migration source.
they do not authorize accepting an unregistered payload.

## startup snapshot transaction

`--settings-snapshot <code>` is the retained load interface. the product has no
runtime text load command. startup performs one planned atomic transaction:

1. decode and migrate the complete payload. validate version, fingerprint,
   membership, domains, duplicates, selector identities, sentinel agreement,
   dependency cycles, and requested final invariants before mutation.
2. resolve `gpu.adapter` as a startup precondition. if it is unavailable or
   differs from the active adapter, fail with zero mutation and direct the user
   to start with the matching `-adapter`. snapshot apply never drives an
   adapter or restart.
3. capture source visible values and raw latent rollback values. drive scene,
   light, and material selectors parent first, then capture the target object's
   raw state and validate target context.
4. apply mutable value prerequisites in deterministic dependency order. spot
   angles use the order required by their requested final pair. apply remaining
   values through ordinary typed setters and read back through snapshot policy.
5. on failure, restore target object mutations, then restore the source scene,
   light, and material selectors in that order. restore and verify source
   visible and raw state. hidden dependents are restored before their
   prerequisites. report the failed phase and rollback result. on success,
   discard both baselines and continue startup.

raw rollback access is transaction internal. refresh and copy always use the
snapshot read policy, including released `<unavailable>` sentinels.

adapter identity is not mutable transaction state. snapshot application has no
adapter journal, resume phase, reverse adapter rollback, or multi restart state.
launcher installation and update rollback remain separate and are owned by the
[launcher guide](../launcher/README.md).

snapshot apply may invalidate or rebuild transient renderer history. it must
not leave a partial scene, material, light, pipeline, or resource state. a
snapshot matching the active adapter must proceed without restart. a later
setter failure must restore every earlier mutable change.

## exact migrations

schema `000c` serializes no TAA, TAA sharpening, or MSAA values. the 17 retired
fields are accepted only in complete supported `0007` through `000b` payloads.
validate their original types, enumerations, and numerical domains, then discard
them before current membership validation. unknown and duplicate names remain
errors. current lookup, save, UI, and diagnostics contain none of these aliases.

schema `0007` requires a canonical old quality value (`low`, `medium`, `high`,
or `ultra`) and direct `anti-aliasing.msaa.samples`. the direct sample value is
validated along with quality. both are discarded because their runtime owner
was removed. the former ordered apply must not restore either setting. do not
keep the old name in normal lookup or save it again.

schemas `0007` and `0008` also require the former BVH preference, BLAS update,
and TLAS update fields with their canonical old domains. validate and discard
them. the world representation now owns one fixed fast trace and refit policy;
only `representation.allow-ray-traversal` remains a user setting.

executable migration is separate. normal install and launch paths recognize
only `uvsr-launcher.exe` and `uvsr-engine.exe`. the launcher may migrate exact
historical filename `UVSR Launcher.exe` once, only inside an otherwise valid,
hash named, launcher owned package after verifying its SHA-256. it must not
treat that name as a general alias. the
[launcher guide](../launcher/README.md) owns this installed state migration.

## verification conditions

maintained checks must prove:

- one typed schema generates or directly supplies storage, defaults, domains,
  UI bindings, persistence, diagnostics, hash, and engine identity;
- the text console, aliases, completion, history, and runtime load path are gone;
- startup snapshot adapter mismatch causes zero mutation and no restart, while
  direct adapter selection restarts once;
- a snapshot matching the active adapter proceeds, and failure rollback and
  readback are fault injected;
- the retired immediate transaction path and redundant selector resume state
  remain absent;
- every supported retired AA migration accepts canonical old values and rejects
  malformed, duplicate, missing, and out-of-domain fields;
- schema probe, snapshot, decoder, transaction, engine identity, launcher, and
  package checks pass; and
- a fresh exact package launches the startup snapshot through ordinary product
  paths.

documentation is a contract, not implementation evidence. bind every proof to
the exact source, build, executable, settings identity, and package it tested.

## schema update procedure

after the composed schema is stable, use the matching external developer build
tree:

```powershell
cmake --build <external-build-root> --config Release --target uvsr_settings_snapshot_schema_probe
<external-build-root>\bin\uvsr_settings_snapshot_schema_probe.exe --check
```

if the composed fingerprint is unregistered, run the probe without `--check`,
replace the current version's unreleased provisional row or append a row for a
new version, rebuild, and rerun `--check`. the integration coordinator owns the
row. then run the focused snapshot, decoder, transaction, catalog, identity,
launcher migration, and package tests listed in [validation](validation.md).
