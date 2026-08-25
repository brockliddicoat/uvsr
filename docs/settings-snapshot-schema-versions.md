# Settings Snapshot Schema Versions

## Version 0007 Contract

The first four lowercase hexadecimal characters of a 32-character UVSR
settings code identify its serialization schema. Stage two uses schema `0007`.
The remaining 28 characters fingerprint one canonical sorted value payload; the
local catalog is required to recover every value reversibly.

The authoritative represented surface is the descriptor catalog in
`src/ui_settings_command_catalog.h`. Its 176 sorted `Value` descriptors each
have one canonical default from `src/ui_settings_canonical_defaults.def` and
one explicit persistence class:

- `SnapshotCatalog` values participate in schema membership and copied codes;
- `SessionOnly` values have canonical defaults but are excluded from copied
  snapshots (`ui.settings-collapsed` and `material-editor.visible`); and
- the four `Action` descriptors use `None` and are excluded from settings
  identity and copied snapshots.

The schema fingerprint mixes serialization policy plus each represented
`Value` descriptor's name, kind, section, verbs, dynamic flag, domain,
persistence, and canonical default. Session-only values participate in this
identity even though copied snapshots omit them. Unrelated action changes do
not version settings. Changing a value default or persistence rule is a schema
change even when the command name remains.

The current unreleased schema `0007` row has canonical settings-number hash
`9c50b0f1515e89d856c8ebb627b86984` and derived four-part engine version
`40016.45297.20830.35288`. That identity owns executable diagnostics,
launcher-visible data, numeric Windows metadata, and package metadata; do not
maintain another version. Before release, a composed catalog change replaces
the provisional row and every bound identity together.

## Fixed Known-Answer Tests

The payload hash uses the registered version prefix plus FNV64 and masked FNV48
payload components. These vectors are fixed for schema `0007`:

| Canonical Payload | Expected Code |
| --- | --- |
| Empty | `0007cbf29ce4842223256c62272e07bb` |
| `a=b\n` | `0007ec8b8c82c37596fba90fe6756c5c` |
| `ui.skin=amp\n` | `0007582ac8a06042865d4c6f64bb61a4` |

The engine identity test must pin the final 128-bit settings-number hash and its
derived four-part numeric Windows version after the catalog freezes. A catalog,
default, persistence, policy, KAT, or identity change without the corresponding
registry/test update must fail the developer gate.

## Serialization and Storage

Represented values are sorted by command name. Each line is
`name=escaped-value` followed by a newline; floats use maximum round-trip
precision and unavailable dynamic values serialize as `<unavailable>`. Copying
a code writes a bracketed code/payload entry to the versioned catalog under the
current user's writable Local App Data. Package-local routing may redirect that
root but may not change schema or payload identity.

Build `uvsr_settings_snapshot_decoder` for decoding. It accepts a 32-digit code
and optional catalog paths, verifies version and payload fingerprint, rejects
malformed/duplicate values and collisions, and prints text or JSON. No
interpreter or decoder fallback is part of the product.

## Allocation and Coordination

The append-only registry is `src/settings_snapshot_schema_versions.def`.
Version `0001` remains reserved; registered fingerprints begin at `0002`.
Never reuse a version or fingerprint, hand-edit a hash, or allocate from task or
branch order.

Compose every descriptor, default, and serialization-policy change first. In
the matching external developer build tree, build and run:

```powershell
cmake --build <external-build-root> --config Release --target uvsr_settings_snapshot_schema_probe
<external-build-root>\bin\uvsr_settings_snapshot_schema_probe.exe --check
```

If the live fingerprint is unregistered, run the probe without `--check`, append
its exact row, rebuild, and rerun `--check`. The integration coordinator owns
that row. Isolated branches report schema changes but do not create globally
observable reservations. Identical composed fingerprints reuse an existing row;
different fingerprints receive different versions.

Compile-time validation rejects reserved or zero rows, duplicate versions,
duplicate fingerprints, and an unregistered live schema. Focused snapshot,
catalog, decoder, probe, and engine-identity tests must pass before the full
developer gate and exact production-package verification.
