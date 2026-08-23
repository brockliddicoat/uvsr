# UVSR Project Agent Core

Policy `2026-08-23.2`. Hard cap: 1,000 words; this is a ceiling, not a target.
Use the fewest clear words. Add only common, high-value rules; every addition
must replace or shorten existing text. Put scoped procedures in nearby docs or
checks. Store no history, examples, machine notes, generated measurements, or
task state here.

## Product

UVSR is focused and DirectX 12 only. ImGui is permanent. Developer and
production builds use the same renderer feature code. Preserve unless the user
explicitly changes the contract:

- 2x, 4x, 8x, and 16x MSAA with correct ray-traced shadows.
- Every user-facing AO/GI option, combination, quality level, and filter.
- Ray-traced sky visibility, the flashlight, and ray-traced flashlight shadows.
- Bistro, San Miguel, every HDR asset, and every STBN/noise asset.
- One conventional standard path tracer with only proven options.

Apply Occam's razor everywhere: choose the least code, policy, dependency, and
state preserving required behavior. Prefer direct ownership and deletion over
wrappers, registries, fallbacks, compatibility layers, or speculative edge-case
handling. Add complexity only for a recurring proven failure when its value
exceeds its code, build, test, and context cost. Simplify touched and directly
adjacent code when behavior becomes clearer with fewer lines or less state;
avoid code-golf and unrelated refactors.

Donut is transitional: never edit it or add coupling. Replace one owned slice
at a time and delete the old path in the same change. Eliminate Python from
first-party source, build, CI, tests, tools, launcher, PATH handling, active
documentation, and packages; port recurring required behavior to narrow C++
before removing Python, and never retain it as fallback.

## Identity and Distribution

The only shipped executable names are `uvsr-launcher.exe` and
`uvsr-engine.exe`. Use them everywhere; filenames never contain versions.
Recognize only exact known old names for one narrow installed-state migration,
not as normal aliases.

One authoritative C++ implementation hashes the canonical settings schema,
option identities, types, enum values, persistence rules, and defaults. Derive
the engine version and required numeric Windows fields deterministically from
that settings-number hash. Embed the full hash in the executable, diagnostics,
launcher-visible data, and package metadata. Do not maintain a manual engine
version or decoder chain.

Use production (`BUILD_TESTING=OFF`) and developer/CI (`BUILD_TESTING=ON`)
configurations only. Developer builds add tests and diagnostics, not product
features. Ship only the launcher, engine, required runtime DLLs, compiled
shaders, retained assets, settings, notices, and licenses. Public install/update
downloads signed launcher or renderer artifacts, never source, interpreters,
Git, CMake, compilers, SDKs, debug layers, symbols, or benchmark tools.

## Work

Inspect relevant status and diffs; preserve unrelated work. Trace callers before
deletion and remove obsolete behavior end to end, including declarations,
shaders, resources, UI, commands, settings, snapshots, tests, build/package
rules, assets, documentation, and legal references. Require zero active
references. Use one writer per file, the smallest useful team, and one
coordinator for integration.

Diagnosis is read-only. Implementation permits the smallest complete local edit
and checks, not a commit or external action. Commit, push, pull request, merge,
release, and deployment each require clear authority for that named action and
exact candidate. Check the live remote when recency matters. Keep generated
builds, caches, downloads, staging, dependencies, binaries, `work/`, and outputs
out of Git.

Maintained first-party documentation uses the fewest words preserving
requirements, evidence, decisions, recovery information, and required license
bodies. Compress or replace old text before adding text. Root `README.md` has a
1,200-word ceiling. Use Title Case in changed headings; check changed documents
only, and self-test a checker only when it changes.

## Build

Before building, compare changed paths with the last exact build identity. Reuse
an isolated build tree only when lineage, generator, toolchain, configuration,
options, and dependencies match. Never share a build tree between concurrent
tasks. Build affected targets incrementally. Reconfigure only after build-system,
option, toolchain, or dependency changes; clean only for proven contamination.
Rebuild shaders or restage assets only when their inputs change.

## Verify

Use focused checks while iterating. Run the full applicable developer gate once
before code handoff or integration, then build and smoke the exact fresh
production package. Visible, runtime, or package changes need representative
output and the affected retained-feature matrix. Compiling or launching is not
verification. Bind claims to exact source, configuration, package, settings
hash, executable, and SHA-256. Policy-only changes need content, link,
instruction-discovery, and word-count checks, not a renderer rebuild. Only
clean, integrated, reverified work is “latest verified.”

## Handoff

Every final response, including policy or documentation work, must link the
exact proven `uvsr-launcher.exe` and `uvsr-engine.exe` for the active lineage,
give each SHA-256, and say whether each was rebuilt. If unchanged, reuse the
last proven pair and label it unchanged. Also report exact base and final source
identity, changed contracts, checks, risks, external actions, the canonical
settings-number hash, and derived engine version. Use provenance, never
timestamps; do not claim completion until both executable identities are proven.
