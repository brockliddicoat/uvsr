# UVSR documentation

each maintained fact has one owner. source and machine checked catalogs remain
authoritative for exact names, values, paths, and bytes.

## user documents

- [user guide](user-guide.md) owns startup, interface controls, rendering
  outcomes, assets, timing, and troubleshooting.
- [settings](settings.md) owns schema, defaults, persistence, snapshots,
  transactions, identity, and migrations.
- [scene catalog](../assets/scenes/README.md),
  [environment catalog](../assets/environments/README.md), and
  [noise catalog](../assets/noise/README.md) own retained asset identities and
  provenance.
- [launcher guide](../launcher/README.md) owns install, update, repair,
  rollback, uninstall, executable names, feeds, and publication trust.
- [legal guide](../legal/README.md) owns licenses, notices, attribution, and
  clearance limits.

UI changes start with the [UI contract](ui.md); load only the relevant procedure.

## developer documents

| owner | canonical document |
| --- | --- |
| current renderer ownership, frame order, state, and retained Donut boundary | [architecture](architecture.md) |
| build identity, commands, dependencies, shaders, inventories, package contents, and release sequence | [build and shaders](build-and-shaders.md) |
| focused checks, proof classes, exact 30 runtime cases, acceptance gates, and package smoke evidence | [validation](validation.md) |
| performance identity, comparison method, tool state, and decision records | [performance](performance.md) |
| concurrent writers, integration, shared processes, and handoff | [agent collaboration](agent-collaboration.md) |
| durable decisions, recovery commits, unresolved risks, and selective recovery | [recovery](recovery.md) |
| preserved experiments, failures, and checked publication inventory | [postmortems](postmortem/README.md) |
| contribution scope, CLA, outside material, and review | [contribution guide](../CONTRIBUTING.md) |

historical reports are not current design or proof. use the exact identities in
[recovery](recovery.md) to inspect an old owner, then implement only the needed
behavior against current contracts.
