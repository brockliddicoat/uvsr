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
candidate and publish that Noto source before publishing a compatible launcher.
The verified 1.1.12 sequence-13 source candidate may be offered as an explicitly
unsigned manual bootstrap only after its immutable versioned prerelease and
clean Windows validation pass. The live sequence-2 bootstrap cannot consume the
current minimum-sequence-4 renderer contract, so it is not evidence for this
transition and must not be offered for a fresh installation.

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
guard, and an HTTP/1.1 fallback for known HTTP/2 transport failures. A fresh
source cache fetches only the selected public `main` tip with `blob:none`, then
materializes the exact checked revision on demand. The launcher validates the
partial-clone origin, promisor, filter, object-store, and repository format
before trusting that cache. When an existing installation needs ancestry
classification, the launcher still obtains full commit and tree history but
keeps blobs deferred. This makes tip resolution and ancestry checks metadata
only while preserving rewrite detection.

Microsoft Build Tools payloads remain under Microsoft's signed setup and catalog
trust boundary. The launcher can resume the supported layout-acquisition phase
after authoritative connectivity failures, verify or repair that layout, and
then run the elevated offline installation once. It never blindly repeats a
partially completed elevated installation.

A failed check, download, build, or candidate health check leaves the active UVSR
and launcher packages unchanged. Details distinguish a stalled connection from
local storage, access, signature, and permanent HTTP failures.

## Unsigned Launcher Download

The root README links the explicitly unsigned 1.1.13 feed-capable launcher. The
link uses the exact immutable
`uvsr-launcher-v<version>/UVSR-Launcher-Windows-11-x64.exe` prerelease asset and
also exposes its SHA-256 checksum. The executable is labeled unsigned; Windows
may show an unknown-publisher or SmartScreen warning.

The bootstrap can install, update, launch, repair, and remove UVSR Engine.
Version 1.1.12 is not authority for launcher self-update and cannot acquire that
capability retroactively. Users open 1.1.13 manually once; it and newer versions
can then authenticate and install later unsigned launchers through the
version-2 signed-metadata feed.

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

Feed-capable launchers use the public update feed at
`launcher/launcher-update-feed-v2.json` on public `main`:
`https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json`.
It is a strict four-field JSON envelope containing `schemaVersion`, `keyId`,
`payloadBase64`, and `signatureBase64`. The launcher rejects duplicate,
unknown, mixed-case, missing, oversized, or malformed fields before decoding.
The signature is ECDSA P-256 with SHA-256 and a fixed 64-byte IEEE-P1363 value;
the key ID and SubjectPublicKeyInfo are compiled into the launcher.
The production key ID is `uvsr-launcher-update-p256-2026-01`; the compiled SPKI
SHA-256 is
`73e4079b971aa69eec69e87b4e581cde0da10d410299f124d8c3092fc0324ed9`.

The decoded UTF-8 payload is another strict JSON document. It binds schema
version 2, the immutable UVSR product ID, channel `stable`, release sequence,
semantic version, exact 40-character source commit, and artifact name, byte
size, and lowercase SHA-256. The release sequence must increase, and the exact
versioned release URL is derived rather than accepted from downloaded data:

`https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v<version>/UVSR-Launcher-Windows-11-x64.exe`

The downloaded executable must remain Authenticode `NotSigned`; a signed or
certificate-bearing file is not silently accepted under this unsigned-release
policy. The launcher independently verifies the exact size and SHA-256, x64 PE
architecture, ProductVersion `<version>+<sourceCommit>`, FileVersion
`<version>.0`, and launcher health identity before activation. There is no
`uvsr-launcher-latest` fallback. The prior Authenticode-oriented version-1 feed
cannot authorize this unsigned update line.

`launcher/launcher-feed-v1.json` and the former
`installer/launcher-feed-v1.json` address are permanent, frozen compatibility
records for already-released launchers. They remain regular byte-identical
files; the build, contract suite, and GitHub workflow reject a missing or
unequal pair. They are never repurposed as unsigned-update authority.

## Update Check Diagnostics

The launcher normally checks three exact public addresses and records each
complete URL before downloading it:

- Launcher signed-metadata update feed:
  `https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json`
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
the exact launcher feed URL, authenticated key ID, running and published
launcher version and release sequence, source commit, exact renderer-contract
URL, and the specific transport, HTTP, schema, signature, or identity failure.
A generic connection message is not used for a schema, signature, or
release-compatibility problem.

Launchers released before the new signed-metadata protocol report one of the
permanent version-1 compatibility addresses instead. Those frozen files contain
the same historical record, so either logged version-1 address can still be
inspected directly, but it does not authorize current unsigned updates.

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

The build first verifies that the two frozen version-1 feed files are exact
mirrors, verifies any present version-2 feed signature and payload, verifies the
frozen exact bridge in check-only mode, and verifies
`launcher/launcher-input-lock-v1.json`. That lock binds
all launcher binary inputs to one semantic version and release sequence using
checkout-invariant bytes. Any binary-input change requires both identity values
to advance and the lock to be refreshed. CI also compares the change with its
Git base, preventing a lock edit from silently reusing an older identity. Local
builds derive that comparison base from the default branch or first parent;
`-IdentityBaseCommit` supplies an explicit full commit when release automation
has a stronger base.

The version-2 public feed authenticates the final `NotSigned` executable's exact
size, SHA-256, immutable source commit, version, and release sequence. Release
verification can pass `-PublishedArtifactPath` to check that final artifact
after the signed feed records the same identity. The feed's P-256 signature is
metadata authority; it does not imply an Authenticode publisher signature on
the executable.

The `Windows 11 Launcher` workflow also runs the production source-preparation
path against the exact compatibility bridge and pinned recursive submodules,
then performs the same unsigned CI build. Its 14-day workflow artifact is a test
candidate, not a public self-update release.

## Unsigned Manual-Only Release Checklist

The unsigned 1.1.12 manual-only launcher is a separate public-download contract. It does
not change any launcher feed or make unsigned executables acceptable as launcher
updates without the later signed-metadata protocol.

1. Build the launcher from one exact 40-character commit and pass the complete
   launcher, renderer-source compatibility, package, and clean Windows 11
   install/repair checks.
2. Verify the final executable is x64, reports the exact product version and
   launcher health identity, has `NotSigned` Authenticode status, and matches
   the canonical checksum file.
3. Land the unavailable-state validation workflow first. Protect `main` with
   Launcher README Download as a required pull-request check before advertising
   any executable.
4. Create a draft `uvsr-launcher-v<version>` prerelease targeting the exact
   verified commit. Upload only `UVSR-Launcher-Windows-11-x64.exe` and its
   `.sha256` file, verify their inventory and attestations, then publish the
   immutable prerelease. Never upload the expiring Actions ZIP itself.
5. On a new release-state branch, run
   `python tools/sync_launcher_readme_download.py --set-unsigned-version
   <version>`. Leave all launcher feeds byte-for-byte unchanged.
6. Open a pull request and require Launcher README Download. It verifies the
   immutable release and tag, source identity and input lock, public renderer
   compatibility, two-asset inventory, attestation, bytes and checksum, x64
   metadata, `NotSigned` status, health check, and exact generated disclosure.
7. Merge only after the protected check passes. Then repeat an unauthenticated
   download, checksum, metadata, unsigned-status, and health verification from
   public `main` before announcing the bootstrap.

## Authenticated Unsigned Self-Update Release Checklist

The first feed-capable launcher is a one-time update bootstrap because version
1.1.12 permanently rejects every launcher update. Two newer identities are
required to prove the new path immediately: publish the bootstrap first, then
publish a later update target and advertise it through signed metadata.

1. Commit the pinned update key, strict version-2 verifier, source-install
   repair, and blobless source resolver under one new launcher version and
   release sequence. Run the complete launcher, native, package, source-build,
   bridge, and clean Windows 11 install/repair checks.
2. Build the executable from the exact protected-main merge commit. Require x64,
   exact ProductVersion and FileVersion, the matching health identity,
   Authenticode `NotSigned` with no embedded certificates, and a canonical
   checksum file.
3. Create an immutable `uvsr-launcher-v<version>` prerelease targeting that
   exact 40-character commit. Upload only the executable and checksum, verify
   their GitHub attestations and anonymous downloads, and leave the version-2
   feed unchanged. This release is the one-time manual bootstrap.
4. Advance the launcher version and release sequence again through a protected
   pull request. In the same release-state lineage, create a version-2 feed for
   the first bootstrap so its verification path is exercised before another
   release is advertised. Never modify either frozen version-1 feed.
5. Build the second executable from its exact protected-main merge commit and
   publish another immutable `NotSigned` prerelease with the same two-asset,
   attestation, checksum, architecture, metadata, and health gates.
6. Generate a new signed version-2 payload for the second release with the
   offline P-256 private key by running
   `new-launcher-update-feed.ps1 -PrivateKeyPemPath <PEM> -ArtifactPath <EXE>
   -Version <version> -ReleaseSequence <sequence> -SourceCommit <commit>`.
   Verify it with `verify-launcher-update-feed.ps1`, then run
   `python tools/sync_launcher_readme_download.py --set-from-update-feed` to
   update the generated README block. The private key is never committed,
   logged, uploaded as an artifact, or accepted from the downloaded feed.
7. Open a release-state pull request and require Launcher README Download plus
   the path-triggered Windows 11 Launcher workflow. The checks must resolve the
   immutable tag to an ancestor of the publication base, validate source
   identity and the input lock, verify exact release inventory and attestations,
   download both assets anonymously, require `NotSigned`, and rerun health.
8. Merge only after every required and launcher check passes. Wait for the raw
   version-2 feed and generated README block on public `main` to converge, then
   repeat the anonymous byte, checksum, metadata, unsigned-status, and health
   verification.
9. Manually bootstrap the first feed-capable launcher on a clean Windows 11
   machine, use its Update flow to activate the later identity, and verify that
   a healthy existing UVSR Engine package remains intact. Do not claim launcher
   update support until this public-artifact test passes.

Publishing, signing update metadata, tagging, and changing GitHub Releases are
explicit release actions and are not performed by the local build script.

## Network and Hardware Requirements

Installation needs HTTPS access to GitHub, GitHub release assets, Python.org,
NuGet, and Microsoft's Visual Studio services. Running this UVSR build requires
a hardware DirectX 12 adapter with Shader Model 6.5 or newer and a suitable
current vendor driver. Adapter selection is capability-based across Intel, AMD,
NVIDIA, and UMA hardware; unsupported adapters are rejected before shader
pipeline creation with an actionable message. No universal safe GPU-driver
installer exists, so the launcher reports that hardware requirement rather than
modifying drivers.
