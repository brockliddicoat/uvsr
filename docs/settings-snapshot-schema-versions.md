# Settings Snapshot Schema Versions

The first four hexadecimal characters of a UVSR settings snapshot identify its
serialization schema. A version belongs to one complete schema fingerprint, not
to an agent, task, branch, worktree, or commit. The fingerprint covers the
serialization policy and every represented command descriptor's name, kind,
section, supported verbs, dynamic-selection status, and value domain.

The authoritative append-only registry is
`src/settings_snapshot_schema_versions.def`. Version `0001` is permanently
reserved for the pre-registry design. Registered versions begin at `0002` and
advance past the highest recorded version; gaps are never reused.

## Allocation Procedure

Compose all settings-catalog and serialization-policy changes first. Then build
and run the schema probe:

```powershell
cmake --build <build-directory> --config Release --target uvsr_settings_snapshot_schema_probe
<build-directory>\bin\uvsr_settings_snapshot_schema_probe.exe
```

If the fingerprint is already registered, reuse that version. Otherwise append
the exact row printed by the probe, rebuild it, and require:

```powershell
<build-directory>\bin\uvsr_settings_snapshot_schema_probe.exe --check
```

Do not allocate a version for a presentation-only change whose represented
catalog and serialization policy are unchanged. Do not hand-edit a fingerprint,
reuse a deleted gap, or assign a number from task order or agent identity.

## Concurrent Branches

The integration coordinator owns the authoritative registry update. A row on an
isolated branch is provisional until that branch is composed with the current
integration target. Communicating workers report represented catalog or policy
changes in their handoff and let the coordinator allocate after composition.

Two branches may provisionally select the same next number. They cannot silently
integrate with different schemas: duplicate version rows, duplicate fingerprint
rows, and an unregistered composed fingerprint fail the C++ registry check. The
integrator keeps the target's authoritative rows, composes the schema edits,
discards or renumbers conflicting provisional rows, and runs the probe again.
Identical fingerprints reuse one existing version.

## Disconnected or Unseen Work

Uncommitted, unreachable, or otherwise unseen work cannot make a globally
observable reservation. No local allocator can determine that hidden state.
Such a branch therefore carries only a provisional row, if any.

When the work becomes visible, the integrator first refreshes the target
registry, then composes the represented schema changes and runs the same probe.
The combined fingerprint either reuses an identical registered row or receives
the next version after the target registry's maximum. This makes late discovery
a deterministic reconciliation rather than a silent collision.

## Mechanical Enforcement

`settings_snapshot_schema.h` rejects reserved versions, zero fingerprints,
duplicate versions, and duplicate fingerprints at compile time. The application
also refuses to compile when its live collision-resistant schema fingerprint is
not registered. `uvsr_settings_snapshot_schema_probe --check` repeats the live
mapping check as CTest coverage. The Python decoder independently rejects
reserved, malformed, duplicate, and zero-fingerprint registry rows and selects
the catalog named by the code's registered prefix.

The fingerprint is collision-resistant, not a mathematical proof that two
arbitrary schemas can never hash alike. The one-to-one registry, focused tests,
and semantic integration review remain required.
