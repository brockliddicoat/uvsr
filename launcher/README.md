# UVSR Launcher for Windows 11

UVSR Launcher is a self-contained Windows 11 x64 application for installing,
launching, updating, and removing UVSR without requiring developer knowledge or
a preconfigured build environment.

## Public User Experience

The public download is `UVSR-Launcher-Windows-11-x64.exe`. It does not require a
preinstalled copy of .NET, Git, CMake, or Python. The main window offers:

- **Install**, which changes to **Installed** after a healthy UVSR Engine is
  present and continues to offer Launch and Reinstall from that state.
- **Update**, which independently checks **UVSR Engine** and **UVSR Launcher**,
  then lets the user choose either or both available updates.
- **Launch**, which opens the verified active UVSR executable and changes to
  **Close** only while an exact launcher-owned renderer process is alive.
- **Uninstall**, which removes launcher-owned programs, caches, shortcuts, and
  registration while preserving renderer settings and history.

The main window uses one fixed, DPI-bounded size. **Details** reveals its log in
the reserved lower region without changing the outer window bounds. Launcher
buttons share one logical size; enabled primary actions use the same blue with
white text, active operations visibly disable mutation actions, determinate and
indeterminate progress use that same blue, and failure text uses the same red
as **Cancel**. Launcher text uses the bundled Noto Sans Regular and Bold faces;
the renderer defaults to Noto Sans Regular, SemiBold, and Bold. Its Interface
drawer can also select the prior Windows Codex appearance or Ogg's original
ProggyClean presentation. The Codex option reads installed Segoe UI faces at
runtime and never copies them; ProggyClean is already embedded by Dear ImGui.
None of these choices changes the established sizes or authored heading
emphasis.

The desktop-shortcut option is selected by default. That shortcut opens UVSR
Launcher, so the same entry point remains useful for launching, repairing, and
updating UVSR. Existing owned UVSR and UVSR Launcher shortcuts are migrated;
unrelated shortcuts are preserved.

Renderer state is refreshed while the window is open. If UVSR exits or crashes,
the action returns to **Launch** automatically. A stale click is discarded
instead of being reinterpreted as the opposite action. Normal shutdown is tried
first; after a timeout, the user can keep waiting, explicitly force-close the
same verified process identities, or cancel. The launcher never force-closes a
renderer automatically.

## First Installation

The first installation performs these actions:

1. Confirms that the computer is an x64 Windows 11 client.
2. Downloads pinned portable Git, CMake, and Python archives over HTTPS and
   verifies their SHA-256 hashes.
3. Downloads and verifies pinned Direct3D Agility SDK, DirectX Headers, and DXC
   archives. CMake receives those local extractions in fully disconnected mode,
   so configuration does not start an unbounded hidden download.
4. Reuses a complete Visual Studio 2022 C++ toolchain and Windows 11 SDK, or
   asks permission to prepare Microsoft's signed Build Tools.
5. Prepares a resumable local Microsoft layout, verifies it with Microsoft
   setup, and performs the elevated installation once from those local files
   with web access disabled for the mutating phase.
6. Fetches one exact commit from the public UVSR `main` branch and its pinned
   submodules. When public `main` is the known pre-contract commit described
   below, the launcher applies its exact embedded compatibility bridge before
   performing a clean DirectX 12 Release build as the original standard user.
7. Packages the exact runtime inventory, records its SHA-256 values, and
   atomically activates it only after the complete build succeeds.

Visual Studio Build Tools can require several gigabytes. Completion time depends
on the connection, CPU, storage, and whether compatible Microsoft components
are already installed; a few minutes cannot be guaranteed on every computer.

## Font Packaging and Compatibility

The current source candidate and launcher use the exact bundled Noto Sans
v2.015 Regular, SemiBold, and Bold files under the SIL Open Font License 1.1.
The launcher exposes that license through **Notices**, and renderer packages
include it under `bin/licenses`. Noto builds neither discover nor copy Windows
system fonts. The optional **Codex (Segoe UI)** runtime presentation reads the
standard installed Windows faces only after launch and never adds their bytes
to build output, staging, a package manifest, or a download.

The **Ogg (ProggyClean)** presentation uses the existing font data embedded in
Dear ImGui, so no duplicate font binary is staged. New Noto and dual-transition
builds include Tristan Grimmer's exact MIT notice as
`bin/licenses/ProggyClean-MIT.txt`; historical validated packages remain
recoverable without that newly introduced notice, while a present altered
notice is rejected.

During the sequence-9 transition, CMake also writes three historical-path
aliases whose bytes are the exact Noto Sans faces. The renderer never loads
those aliases. Sequence 10 and later verify the complete dual build, remove the
aliases and the compatibility-only Geist notice from the staging directory, and
record a clean Noto-only package. A package created by sequence 9 may retain the
exact dual set and remains recoverable.

The package validator recognizes only a complete historical three-file
inventory, the complete Noto Sans inventory, or that exact hash-matched dual
transition. Partial, arbitrary mixed, substituted, or extra font inventories
are rejected. Historical packages remain valid for recovery, but sequence 10
and later refuse to create a new package from a legacy-only build. The launcher
also verifies every exact Noto source asset before downloading submodules or
configuring the renderer. This keeps existing managed packages launchable
without allowing stale public source or a damaged font set to become a new
installation.

This font transition has a mandatory source-first publication order. Validate
the dual-output Noto source with the retained exact sequence-9 transition
candidate, publish that Noto source, and only then configure the permanent
signer, advance the launcher identity again, rebuild, sign, and publish that
freshly identified launcher artifact and feed. The local 1.1.12 sequence-13
source candidate is unsigned and must not be offered as install-ready.
The live sequence-2 bootstrap cannot consume the current minimum-sequence-4
renderer contract and has no pinned signer, so it is not evidence for this
transition; users must manually open the final signed bootstrap launcher.

## Exact Public-Source Compatibility Bridge

The historical launcher bridge remains bound to public commit
`0c8074848985152ed83f83b4087aaf10013de590`. That revision predates both the
renderer build contract and bundled Noto source, so the current Noto launcher
does not permit its legacy font output to become a new package. Simply ignoring
either mismatch would reproduce the original pipeline-state failure or install
the retired Windows-font build.

For only that exact base commit and tree, the launcher verifies and applies one
embedded 24,581-byte patch with SHA-256
`e68f814e2e838ef08bf8561bfa033dbaa0b5a523f776983dca18e8dd83ad799a`.
It then requires resulting tree
`736fc012878dc66e5f512dab40d722a56ac8c1f5` and deterministic synthetic Git
commit `ac19135e176bd137df050fb3da11297a2460312d` before any configure, build, or
package step. The prepared source is checked again after the build and before
packaging. The bridge is never downloaded, never applied fuzzily, and never
used for another public commit. Once public `main` advances to a compatible
contract-bearing commit, the ordinary exact-public-source path takes over.
The bridge verifier hashes the frozen embedded patch before applying it to the
exact historical base in an isolated Git index and object store, then checks
the full path, mode, blob, tree, and synthetic-commit identities. It does not
derive this historical patch from current renderer files, so ordinary newer
renderer edits cannot redefine or invalidate the retired bridge.

## Connection Tolerance

Ordinary HTTPS downloads use six bounded attempts, jittered backoff, a 45-second
header deadline, a 90-second no-data stall threshold, and a 45-minute overall
safety deadline. A strong ETag permits a partial file to resume with Range and
If-Range. Changed validators, ignored ranges, HTTP 416, truncated segments,
redirect loops, and HTTPS-to-HTTP downgrades are handled explicitly. The final
file is always checked from byte zero before promotion.

Git network operations use four bounded attempts, Git's 90-second low-speed
guard, and an HTTP/1.1 fallback for known HTTP/2 transport failures. The complete
history is fetched when ancestry must be checked, avoiding false history-rewrite
warnings for older valid installations.

Microsoft Build Tools payloads remain under Microsoft's signed setup and catalog
trust boundary. The launcher can resume the supported layout-acquisition phase
after authoritative connectivity failures, verify or repair that layout, and
then run the elevated offline installation once. It never blindly repeats a
partially completed elevated installation.

A failed check, download, build, or candidate health check leaves the active UVSR
and launcher packages unchanged. Details distinguish a stalled connection from
local storage, access, signature, and permanent HTTP failures.

## Launcher Updates and Update Source

Launcher releases use immutable versioned packages under the per-user program
root. Activation records the release sequence, semantic version, exact
executable SHA-256, and package inventory before shortcuts or Apps & Features
are moved to the new version. External launcher copies redirect automatically
to a verified installed package at the same or a newer release sequence. A
strictly newer downloaded copy remains active so it can upgrade the
installation. Invalid or tampered installed pointers are never followed. An
interrupted combined launcher-plus-UVSR update retains its UVSR continuation
until a valid launcher package completes it.

The fixed public feed is
`launcher/launcher-feed-v1.json` on the public `main` branch:
`https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-feed-v1.json`.
It names one
immutable GitHub Release asset and is parsed with strict size, duplicate-field,
unknown-field, version, sequence, filename, and hash validation.

The former address at `installer/launcher-feed-v1.json` is a permanent
compatibility endpoint for released launchers that compiled that exact raw URL.
It is a regular file whose bytes must always equal the canonical `launcher/`
feed. The build, contract suite, and GitHub workflow reject a missing or unequal
mirror. Every feed-only publication must update both paths in the same commit;
the mirror is never a redirect or an independently edited release record.

New feeds use the exact camelCase field names `schemaVersion`, `productId`,
`channel`, `releaseSequence`, `version`, and `artifact`. Launchers also accept
the original v1 feed's exact PascalCase schema as a whole-document compatibility
format. Mixed casing, duplicate aliases, and unknown fields remain invalid.

The launcher reads that feed first, then downloads the launcher package from:

`https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v<version>/UVSR-Launcher-Windows-11-x64.exe`

where `<version>` comes from the same `version` value in the canonical feed (for example,
`uvsr-launcher-v1.1.1`).

If that exact versioned release tag is not available yet, the launcher also
supports `uvsr-launcher-latest` as a compatibility fallback for update recovery.

A release artifact must also have a valid Authenticode chain whose signer public
key matches the SHA-256 SPKI pin compiled into the launcher. Repository control
and a feed hash alone are not accepted as publisher identity. Local preview
builds intentionally leave that pin unset and fail closed for launcher downloads;
they can still install, launch, update, repair, and remove UVSR. The first public
UVSR Launcher release therefore requires the project's permanent code-signing
identity before it can be called distribution-ready.

## Update Check Diagnostics

The launcher normally checks three exact public addresses and records each
complete URL before downloading it:

- Launcher release feed:
  `https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-feed-v1.json`
- Current renderer commit:
  `https://api.github.com/repos/brockliddicoat/uvsr/git/ref/heads/main`
- Renderer build contract for the resolved commit:
  `https://raw.githubusercontent.com/brockliddicoat/uvsr/<commit>/cmake/uvsr-launcher-build-contract-v1.json`

When the resolved renderer commit is the exact compatibility-bridge base above,
the launcher checks only the feed and current-commit addresses. It validates the
embedded patch instead of requesting a contract that is known not to exist at
that revision. The log records the bridge ID, public base, patch SHA-256,
resulting tree, and synthetic source commit, including the fact that no remote
contract request was made.

Details are written under `%LOCALAPPDATA%\UVSR Installer\logs` in files named
`uvsr-launcher-<date>-<time>-<id>.log`. The update dialog and copied details show
the exact launcher feed URL, the running and published launcher version and
release sequence, the exact renderer-contract URL, and the specific transport,
HTTP, schema, or identity failure. A generic connection message is not used for
a schema or release-compatibility problem.

Launchers released before the directory rename report the permanent
`installer/launcher-feed-v1.json` compatibility address instead. It contains
the same checked release record as the canonical URL, so either logged address
can be inspected directly.

| Visible Result | Meaning And Recovery |
| --- | --- |
| Running launcher is newer than the published feed | The check succeeded and no launcher update is needed. The displayed versions and sequences identify both sides. |
| Feed schema or identity failure | The feed was reached but its exact fields or release identity were invalid. Inspect the recorded feed URL and reason; reinstalling the same launcher does not repair the feed. |
| Exact pre-contract public base | The immutable bridge identity remains verifiable, but the current Noto launcher refuses to stage its legacy-font output as a new package. Publish matching Noto source; any existing validated installation is preserved. |
| Launcher and renderer source are not a compatible release pair | Public `main` is neither the exact supported bridge base nor a contract-bearing revision supported by that launcher. The launcher disables the unsafe renderer update and preserves the installed copy. Use the complete logged source and contract identities to correct the release pairing. |
| Transport or HTTP failure | The complete failing URL and HTTP or connection reason are shown. **Check Again** retries both component checks independently. |

`cmake/uvsr-launcher-build-contract-v1.json` is the versioned interface between
public renderer source and the launcher. It declares only a recognized contract
ID, minimum launcher sequence, and dependency version identifiers. Download
locations and SHA-256 values remain compiled into the launcher and are never
trusted from downloaded source. When a renderer contract raises the minimum
launcher sequence, publish and validate that launcher artifact and canonical feed
while public renderer source is still compatible with the prior launcher. Allow
the launcher release to propagate before publishing the contract-requiring
renderer source. Reversing that order strands prior launchers on source they
cannot build. The exact bridge above is a bounded historical transition for one
already published legacy commit, not a fallback for missing or unknown
contracts and not a source of new Noto packages.

## Ownership and Recovery

Programs, versioned launcher packages, tool downloads, clean source/build trees,
and any resumable Microsoft layout live below `%LOCALAPPDATA%\Programs\UVSR`.
An empty ownership directory left by a crash during first-run setup is recovered
automatically; a nonempty unmarked or foreign directory is still preserved and
rejected.
Interrupted launcher activation is recovered from whichever cryptographically
valid owned package remains. If package files are only temporarily inaccessible,
the recovery journal is retained for a later retry instead of being discarded.
Legacy compatibility state remains available so already-created installations stay
recoverable. The hidden name is a compatibility
identifier, not visible product branding.

The separate compatibility journal serializes
self-uninstall after the running launcher closes. UVSR renderer settings,
snapshots, and scene history live separately in `%LOCALAPPDATA%\UVSR` and are
never recursively deleted by the launcher.

Visual Studio Build Tools, the Windows SDK, and the Visual C++ runtime are shared
machine components and remain installed after UVSR is removed.

Windows-known Desktop and Start menu roots may be redirected by Windows or an
administrator. The known-folder result is treated as the trusted boundary, while
UVSR-specific descendants, existing shortcut collisions, and foreign files are
still checked before any shell change.

## Pinned Build Inputs

- Git for Windows MinGit `2.55.0.windows.4`.
- CMake `4.4.2`.
- Python `3.13.15` x64 embeddable distribution.
- Microsoft Direct3D Agility SDK `1.619.5` with exported
  `D3D12SDKVersion 619` and app-local path `.\D3D12\`.
- Microsoft DirectX Headers `1.619.5`.
- Microsoft DirectX Shader Compiler `1.9.2602` from `2026_02_20`.
- Visual Studio 2022 Build Tools with x64 C++ tools and Windows 11 SDK
  `10.0.26100`.
- The current signed Microsoft Visual C++ x64 Redistributable when required.

Every portable archive uses a fixed HTTPS URL, maximum size, and checked-in
SHA-256. Microsoft setup and redistributable executables additionally require a
valid Microsoft Authenticode signature.

## Build the Launcher

From PowerShell on x64 Windows with the exact .NET SDK `10.0.400`:

```powershell
.\launcher\build.ps1
```

The script runs the launcher contract suite, publishes a compressed
self-contained .NET 10 `win-x64` single file, embeds the required license and
notice bundle, and writes:

```text
launcher\artifacts\UVSR-Launcher-Windows-11-x64.exe
launcher\artifacts\UVSR-Launcher-Windows-11-x64.exe.sha256
```

The build first verifies that the canonical and legacy feed files are exact
mirrors, verifies the frozen exact bridge in check-only mode, and verifies
`launcher/launcher-input-lock-v1.json`. That lock binds
all launcher binary inputs to one semantic version and release sequence using
checkout-invariant bytes. Any binary-input change requires both identity values
to advance and the lock to be refreshed. CI also compares the change with its
Git base, preventing a lock edit from silently reusing an older identity. Local
builds derive that comparison base from the default branch or first parent;
`-IdentityBaseCommit` supplies an explicit full commit when release automation
has a stronger base.

The public feed is a record of the final signed bytes, not the local unsigned
publish output. Release verification can pass `-PublishedArtifactPath` to check
the size and SHA-256 of the final signed artifact after the feed records that
same release identity. This keeps source-identity validation independent from
Authenticode's expected byte changes.

The `Windows 11 Launcher` workflow also runs the production source-preparation
path against the exact compatibility bridge and pinned recursive submodules,
then performs the same unsigned CI build. Its 14-day workflow artifact is a test
candidate, not a public self-update release.

## Public Release Checklist

For the Noto transition, first prove that the retained exact sequence-9
transition candidate can build and stage the dual-output Noto renderer source,
then publish that source. Do not configure and publish the final pinned launcher
or feed until that source-first gate passes. The current 1.1.12 sequence-13
source candidate is only an unsigned local preview; setting the permanent signer changes
a locked input and requires another unique identity, lock, build, and complete
verification pass. The live sequence-2 bootstrap cannot build the
minimum-sequence-4 source and cannot self-update without a pinned signer; step 8
therefore requires a manual signed-launcher bootstrap. The ordinary
consumer-first order in step 9 applies only to a future source contract that
raises its minimum launcher sequence.

1. Choose the permanent Authenticode identity and set
   `LauncherPublisherSpkiSha256` to its lowercase SHA-256 SPKI pin.
2. Build and run the complete contract suite and exact source-bridge preparation
   smoke at the release commit. For a bridge-bearing transition, also complete
   the isolated source-build and package smoke before signing.
3. Exercise fresh install, interrupted and resumed downloads, launcher-only and
   combined updates, repair, UAC cancellation, restart-required setup, Launch,
   and both launcher and Apps & Features uninstall on disposable clean Windows
   11 x64 virtual machines.
4. Sign the final launcher, verify its Authenticode chain and pinned signer, then
   regenerate its size and SHA-256. Never use the pre-signing checksum.
5. Create the immutable GitHub Release tag
   `uvsr-launcher-v<version>` and upload the exact signed executable and its
   checksum.
6. While public renderer source is still compatible with the prior launcher,
   publish a feed-only canonical camelCase update with the final release sequence,
   version, size, and SHA-256. Update the canonical `launcher/` feed and its
   byte-identical legacy `installer/` mirror in the same commit. Publish them
   only after the immutable release asset is independently re-downloadable.
7. Run `build.ps1 -PublishedArtifactPath <signed-launcher>` after the feed update;
   it rechecks the signed file's bytes, product metadata, and embedded release
   identity health check against that feed.
8. Validate the feed and artifact from a clean Windows 11 VM. The first launcher
   shipped with an empty signer pin cannot self-update by design, so that one-time
   bootstrap requires users to open the newly signed launcher manually. Confirm
   that the new pinned launcher can perform future signed updates, then allow the
   bootstrap release to propagate.
9. For a future renderer source that raises its minimum launcher sequence, only
   after the compatible launcher is available, publish that renderer source.
   Confirm that public `main` contains
   `cmake/uvsr-launcher-build-contract-v1.json`, that the commit-specific raw URL
   works, and that its values match the launcher's compiled contract. The Noto
   transition keeps the existing minimum and must already have passed the
   source-first gate above.
10. Recheck a fresh install and a renderer repair from the new launcher on a clean
    Windows 11 VM before advertising the renderer update.

Publishing, signing, tagging, and changing GitHub Releases are explicit release
actions and are not performed by the local build script.

## Network and Hardware Requirements

Installation needs HTTPS access to GitHub, GitHub release assets, Python.org,
NuGet, and Microsoft's Visual Studio services. Running this UVSR build requires
a hardware DirectX 12 adapter with Shader Model 6.5 or newer and a suitable
current vendor driver. Adapter selection is capability-based across Intel, AMD,
NVIDIA, and UMA hardware; unsupported adapters are rejected before shader
pipeline creation with an actionable message. No universal safe GPU-driver
installer exists, so the launcher reports that hardware requirement rather than
modifying drivers.
