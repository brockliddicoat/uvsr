# UVSR launcher

`uvsr-launcher.exe` is the Windows 11 x64 installer, updater, repair tool,
launcher, and uninstaller for `uvsr-engine.exe`. end user operation is binary
only. it never downloads renderer source or requires Git, CMake, Python, a
compiler, an SDK, or a build tree.

## endpoints and identity

the configured canonical launcher update target is:

`https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json`

the configured renderer package target is:

`https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/renderer-update-feed-v1.json`

publish the exact tested artifacts before activating their signed feeds. an
HTTP 404 at either feed URL means installation or update metadata is unavailable;
a feed alone cannot supply a missing release artifact. signed feeds use
canonical LF bytes on every checkout.
CI verifies both signatures with the production public key.

the historical v1 feed remains at `/main/launcher/launcher-feed-v1.json`.
the former `/main/installer/launcher-feed-v1.json` alias is retired so launcher
files have one source directory. the public `uvsr-launcher-latest` v1.1.1 client
used that retired URL and requires a manual replacement with the current
launcher. its empty publisher pin already prevented self-update. later published
clients use `launcher/` URLs. the retained v1 file is historical metadata;
current native launchers use only the two signed feeds above.

canonical executable names are `uvsr-launcher.exe` and `uvsr-engine.exe`. the
renderer archive is `uvsr-renderer-windows-11-x64.zip`. versions never appear in
those filenames.

## feed trust

launcher and renderer feeds use pinned P-256 key
`uvsr-launcher-update-p256-2026-01`. a feed has a canonical payload and a 64 byte
IEEE P1363 ECDSA/SHA-256 signature. unknown, duplicate, missing, mis-cased,
noncanonical, or oversized fields fail closed.

the launcher feed binds product, channel, monotonic sequence, version, source
commit, artifact name, size, and SHA-256. the renderer feed also binds settings
hash and the exact engine version. the launcher verifies the feed before using
artifact metadata, downloads over HTTPS with bounded retry and resume, then
verifies exact size and SHA-256.

the renderer ZIP is inspected before extraction. unsafe, duplicate, unexpected,
or missing paths fail. package identity, settings identity, PE version
resources, executable hash, and manifest entries must agree. the independent
renderer package contract is defined in
[Build and Shaders](../docs/build-and-shaders.md).

## transaction and rollback

download and extraction never change the active version. a journal records
download, package, activation, shell activation, and uninstall phases. package
promotion, state replacement, shortcuts, and Apps and Features integration are
one recoverable transaction.

keep the previous validated package until activation and shell integration
commit. an interrupted or failed operation rolls back to the last valid owned
state. cleanup removes only marker owned paths. it preserves running,
unverifiable, foreign, or modified files.

repair revalidates owned state against recorded hashes before replacing it.
uninstall removes only the product state proven to belong to this launcher.
process discovery, shortcuts, and registry ownership use canonical names.

## old name migration

one bounded installed state migration accepts exact historical filename
`UVSR Launcher.exe` only inside an otherwise valid, hash named, launcher owned
package. it verifies the recorded SHA-256 and renames the file once to
`uvsr-launcher.exe`.

after validating the canonical package, that same migration repairs desktop
and Start menu links that still target its exact old filename with no arguments.
it resumes safely if the executable or one shortcut was already migrated.
other shortcut targets and arguments remain unchanged during migration.

installation automatically renames a conflicting shortcut to
`UVSR Launcher (preserved <unique id>).lnk` in the same folder, then creates the
UVSR shortcut and continues. this also preserves malformed shortcut files.
Details and the operation log record the backup path. backups retain their
original bytes and survive rollback, later updates, and uninstall. an unchecked
desktop shortcut option leaves unrelated desktop shortcuts in place. folders,
filesystem links, and unrelated Apps and Features records remain protected.

no normal process, package, launch, update, shortcut, or feed path treats the old
name as an alias. `UVSR-Launcher-Windows-11-x64.exe`, `UVSR Installer.exe`, and
`uvsr.exe` are not current artifact names.

## build and verification

build from a clean exact checkout with Visual Studio 2022 C++ tools, a Windows
SDK, CMake 3.24 or later, and an output directory outside the repository:

```powershell
./launcher/build.ps1 -OutputDirectory C:/uvsr-build/launcher `
  -SourceCommit <full-commit>
```

the standalone CMake build uses Win32/GDI, WinHTTP, Windows CNG cryptography,
COM shell integration, and pinned static zlib 1.3.2 for ZIP inflation. it needs
no renderer build or managed runtime. the executable embeds Noto Sans, UVSR's
license, the font license, and zlib's license for its **Notices** window.

the script builds one native `uvsr-launcher.exe`, verifies feed and transaction
contracts, checks metadata and health, and writes its SHA-256 sidecar and
`build-record.json`. the record binds source inputs, source identity, options,
test status, and the copied artifact. production builds reject dirty source
and submodules. `-DeveloperBuild` permits a recorded dirty identity;
`-NoApplicationLaunch` defers health and child-process checks. registry and COM
tests require production mode or explicit `-SystemServicesTests`. these options
are development boundaries, not production acceptance.

the Windows Launcher workflow can retain the verified executable, its SHA-256,
and build record for one day when `export-launcher-artifact` is enabled on a
manual run. it does not create a release or publish a feed.
`verify-release-artifacts` downloads the exact signed release candidates,
including draft assets, and checks their hashes, launcher health, archive
installation and recovery, engine identity, and COM shortcuts. it uses the
release assets rather than treating a later CI rebuild as the same release.
`stage-renderer-artifact` copies the ZIP from `renderer-artifact-run` to its
existing draft release after checking the run, source commit, size, and signed
SHA-256. only that manual copy job has release write permission.

native tests cover the retained 18 contract responsibilities, plus independent
RFC 6979 signature verification, strict canonical bytes, old schema 11 state,
and interrupted activation. `--pure`, `--system-services`, and `--runtime`
separate file/crypto/offscreen progress checks, registry/COM workflows, and
process lifetime proof.
their existence does not imply they have passed for the current source.
the release gate must also exercise signed feed verification, exact renderer
archive inspection, install, update, repair, rollback, ownership, running
processes, shortcuts, old name migration, and uninstall.

uninstall verifies each file against a durable removal inventory and preserves
settings, logs, partial downloads, and unverified or changed content. signed
feed caches and complete hash-bound downloads can be removed. the running
cleanup helper is preserved until a later external launcher startup can verify
and reclaim it; no interpreter or elevated cleanup task is installed.

## publication authority

feed generation, feed publication, Authenticode signing, release creation,
artifact upload, endpoint migration, and v1 alias retirement are separate
authorized release actions. repository changes perform none of them. the
production private key and signing certificate are never stored here.

a restored historical feed is historical data unless its signature, sequence,
canonical artifact name, size, SHA-256, release artifact, and live endpoint are
all proven for the intended client. a published sequence must never be reused
with different artifact bytes or settings. see
[Recovery](../docs/recovery.md) for source history. historical artifact hashes
do not prove a current launcher or renderer.
