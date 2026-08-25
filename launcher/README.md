# UVSR Launcher

uvsr-launcher.exe is the per-user Windows 11 x64 installer, updater, repair
tool, launcher, and uninstaller for uvsr-engine.exe.

## Runtime Protocol

The launcher downloads a strict signed renderer-update-feed-v1.json and the
exact uvsr-renderer-windows-11-x64.zip named by it. Launcher and renderer feeds
use pinned P-256 key uvsr-launcher-update-p256-2026-01. The envelope carries
canonical Base64 payload and 64-byte IEEE P1363 ECDSA/SHA-256 signature values.
Unknown, duplicate, missing, mis-cased, noncanonical, or oversized fields fail
closed.

The renderer payload binds product, stable channel, monotonic release sequence,
40-hex source commit, 32-hex canonical settings hash, exact four-part engine
version emitted by the engine's C++ identity authority, and artifact name, byte
size, and SHA-256.

The launcher verifies the feed before using artifact metadata, downloads over
HTTPS with bounded retry and resume behavior, verifies exact size and SHA-256,
then inspects the ZIP before extraction. Compressed input is capped at 32 GiB
and expanded content at 64 GiB.

## Package Contract

The ZIP has no wrapper directory. It contains only `package-manifest.json`,
`bin/uvsr-engine.exe`, `bin/D3D12/D3D12Core.dll`, `bin/shaders/**`,
`bin/licenses/**`, `bin/settings/canonical-settings.json`,
`.hdr` descendants under `media/environments/`, noise `manifest.json` and
`.bin` files directly under `media/uvsr/noise`, retained-scene `.scene.json`,
`.gltf`, `.glb`, `.bin`, and `.png` files under
`media/glTF-Sample-Assets/Models`, and exactly the Regular, SemiBold, and Bold
Noto Sans `.ttf` files under
`media/fonts/NotoSans`.

`cmake/runtime-media-inventory.def` is the sole narrow completeness authority
for exactly 305 retained media files and five required governing notices. It is
not a general package-verification framework.

The manifest marks a clean production build and repeats release sequence,
source commit, settings hash, derived engine version, executable SHA-256, and
every non-manifest file size and SHA-256. The launcher rejects extra roots,
unsafe or duplicate paths, missing required trees, inventory drift, or engine
version-resource mismatch.
uvsr-engine.exe reports FileVersion a.b.c.d and ProductVersion
a.b.c.d+settingsHash. The generated settings contract binds the same identity
to all 176 non-Action setting Values, including explicit persistence and
snapshot membership.

## Transactions and Ownership

Download and extraction do not change the active version. A journal records
download, package, activation, shell activation, and uninstall phases. Package
promotion, state replacement, shortcuts, and Apps & Features integration retain
rollback and interrupted-operation recovery. The previous validated package
remains available until activation and shell integration commit. Cleanup
removes only marker-owned paths and preserves running, unverifiable, foreign,
or modified files.

Normal paths recognize only canonical executable names. A bounded installed
launcher migration accepts exact historical filename `UVSR Launcher.exe`
inside an otherwise exact hash-named owned package, verifies its SHA-256, and
renames it once. No ordinary process, launch, package, or shortcut path treats
an old name as an alias.

## Build and Test

Run `launcher/build.ps1` from a clean exact checkout, with `DotNetPath` pointing
to pinned SDK 10.0.400 and `OutputDirectory` outside the repository. The gate
must run retained feed, exact CMake archive, update, repair, rollback,
ownership, process, and exact-name migration contracts; publish one
`uvsr-launcher.exe`; check metadata and the health command; and write its
SHA-256 sidecar. Exact lifecycle and renderer matrices remain release
requirements until the candidate package earns them. End-user operation never
downloads or builds renderer source and requires no source-control, build,
compiler, SDK, or interpreter toolchain.

Feed creation and publication are separate authorized release actions. The
production private key is never stored here.
Public launcher artifacts must also receive a trusted Authenticode signature;
certificate access, signing, and publication remain separate external gates.
The updater must accept either an unsigned PE with no Certificate Table or a
valid Authenticode-signed PE after the signed feed's exact size and SHA-256 pass.

## Retired Endpoints and Recovery Facts

Legacy feeds and forbidden artifact names remain retired. Their exact recovery
facts and restoration criteria are preserved in the
[stage-two cutdown record](../docs/postmortem/engine-cutdowns/2026-08-23-stage-two-cutdown-decisions.md).
Sequence 16 is unissued local verification metadata. Any canonical feed at that
or a later sequence requires the production private key, local verification,
authorized signing, and separate publication authority.
