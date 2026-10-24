# C++ Port Notes

These are the highest value lessons from the native launcher trial.

## Freeze Contracts First

Inventory the visible controls, dialogs, commands, filenames, feeds, state
files, registry values, shortcuts, and recovery phases before writing C++.
Treat their serialized shapes as the compatibility boundary. Reuse literal old
state fixtures in native tests.

Reserve a new launcher version and release sequence before producing a native
binary. The installed launcher identity binds sequence, version, and SHA-256.
Reusing the C# sequence with different bytes correctly causes an identity
conflict.

## Expose Native Prerequisites

A Win32 port must explicitly own work previously supplied by the managed host:
the Common Controls v6 manifest dependency, PerMonitorV2 DPI awareness, an STA
COM apartment, control accessibility, font registration, process exit codes,
and uncaught exception boundaries. Keep version data and the manifest generated
from the same CMake identity.

Use Unicode Win32 file handles at archive and process boundaries. Narrow file
APIs inside ZIP libraries silently lose valid Windows paths. Keep path checks
case insensitive, reject reparse points, and create extracted files with
`CREATE_NEW`.

## Preserve Trust Shapes

Windows CNG emits ECDSA signatures as 64 byte P1363 values, which matches the
existing feed. Public keys still arrive as DER SubjectPublicKeyInfo. Test both
conversions with generated P-256 keys instead of weakening the production key
path.

Bind download partials to URL, expected size, and SHA-256. A strong ETag is an
extra guard, not a reason to discard a useful partial when a server omits one.
Always hash the final file. Keep small signed feeds in memory and cache only
verified artifacts.

## Keep the Cut Clean

Split protocol and state validation, WinHTTP transfer, ZIP/package validation,
transactions, and Win32 UI into direct translation units. Small RAII handle
owners are useful. Service registries, compatibility layers, and dependency
injection frameworks are not.

Remove the C# project, .NET SDK pin, managed notices, and managed CI in the same
cut. Retain only the replacement dependency license. Build package validation
into a native test executable so renderer CI exercises the same C++ archive
path that ships.

Keep narrow filesystem and process seams only where rollback tests need fault
injection. Direct production paths are easier to audit everywhere else.

## Test Release Topology Early

Build every shipping configuration before expanding the port. This trial's
clean renderer package exposed a preexisting `4a07af5` link mismatch hidden by
developer builds: Donut referenced NVRHI validation after `BUILD_TESTING=OFF`
removed it. Treat configuration dependent dependencies as contracts. Derive CI
identity from the generated package manifest instead of copying hashes and
versions into workflows.
