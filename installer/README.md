# UVSR Launcher for Windows 11

UVSR Launcher is a self-contained Windows 11 x64 application for installing,
launching, updating, and removing UVSR without requiring developer knowledge or
a preconfigured build environment.

## Public User Experience

The public download is `UVSR-Launcher-Windows-11-x64.exe`. It does not require a
preinstalled copy of .NET, Git, CMake, or Python. The main window offers:

- **Install**, which installs UVSR or offers Launch and Reinstall when UVSR is
  already present.
- **Update**, which independently checks UVSR and UVSR Launcher, then lets the
  user choose either or both available updates.
- **Launch**, which opens the verified active UVSR executable.
- **Uninstall**, which removes launcher-owned programs, caches, shortcuts, and
  registration while preserving renderer settings and history.

The desktop-shortcut option is selected by default. That shortcut opens UVSR
Launcher, so the same entry point remains useful for launching, repairing, and
updating UVSR. Existing owned UVSR and UVSR Launcher shortcuts are migrated;
unrelated shortcuts are preserved.

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
   submodules, then performs a clean DirectX 12 Release build as the original
   standard user.
7. Packages the exact runtime inventory, records its SHA-256 values, and
   atomically activates it only after the complete build succeeds.

Visual Studio Build Tools can require several gigabytes. Completion time depends
on the connection, CPU, storage, and whether compatible Microsoft components
are already installed; a few minutes cannot be guaranteed on every computer.

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
are moved to the new version. Older launchers redirect to a verified newer
package, and an interrupted combined launcher-plus-UVSR update retains its UVSR
continuation until the new launcher completes it.

The fixed public feed is
`installer/launcher-feed-v1.json` on the public `main` branch:
`https://raw.githubusercontent.com/brockliddicoat/uvsr/main/installer/launcher-feed-v1.json`.
It names one
immutable GitHub Release asset and is parsed with strict size, duplicate-field,
unknown-field, version, sequence, filename, and hash validation.

The launcher reads that feed first, then downloads the launcher package from:

`https://github.com/brockliddicoat/uvsr/releases/download/uvsr-launcher-v<version>/UVSR-Launcher-Windows-11-x64.exe`

where `<version>` comes from the same `Version` value in the feed (for example,
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

## Ownership and Recovery

Programs, versioned launcher packages, tool downloads, clean source/build trees,
and any resumable Microsoft layout live below `%LOCALAPPDATA%\Programs\UVSR`.
Legacy compatibility state remains available so already-created installations stay
recoverable. The hidden name is a compatibility
identifier, not visible product branding.

The separate compatibility journal serializes
self-uninstall after the running launcher closes. UVSR renderer settings,
snapshots, and scene history live separately in `%LOCALAPPDATA%\UVSR` and are
never recursively deleted by the launcher.

Visual Studio Build Tools, the Windows SDK, and the Visual C++ runtime are shared
machine components and remain installed after UVSR is removed.

## Pinned Build Inputs

- Git for Windows MinGit `2.55.0.windows.4`.
- CMake `4.4.2`.
- Python `3.13.15` x64 embeddable distribution.
- Microsoft Direct3D Agility SDK `1.717.1-preview`.
- Microsoft DirectX Headers `1.717.0-preview`.
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
.\installer\build.ps1
```

The script runs the launcher contract suite, publishes a compressed
self-contained .NET 10 `win-x64` single file, embeds the required license and
notice bundle, and writes:

```text
installer\artifacts\UVSR-Launcher-Windows-11-x64.exe
installer\artifacts\UVSR-Launcher-Windows-11-x64.exe.sha256
```

The `Windows 11 Launcher` workflow performs the same unsigned CI build. Its
14-day workflow artifact is a test candidate, not a public self-update release.

## Public Release Checklist

1. Choose the permanent Authenticode identity and set
   `LauncherPublisherSpkiSha256` to its lowercase SHA-256 SPKI pin.
2. Build and run the complete contract suite at the exact release commit.
3. Exercise fresh install, interrupted and resumed downloads, launcher-only and
   combined updates, repair, UAC cancellation, restart-required setup, Launch,
   and both launcher and Apps & Features uninstall on disposable clean Windows
   11 x64 virtual machines.
4. Sign the final launcher, verify its Authenticode chain and pinned signer, then
   regenerate its size and SHA-256. Never use the pre-signing checksum.
5. Create the immutable GitHub Release tag
   `uvsr-launcher-v<version>` and upload the exact signed executable and its
   checksum.
6. Write `installer/launcher-feed-v1.json` with that final release sequence,
   version, size, and SHA-256. Publish the feed only after the release asset is
   available and independently re-downloadable.
7. Recheck the public feed and asset from a clean Windows 11 VM with the previous
   launcher before advertising the new download.

Publishing, signing, tagging, and changing GitHub Releases are explicit release
actions and are not performed by the local build script.

## Network and Hardware Requirements

Installation needs HTTPS access to GitHub, GitHub release assets, Python.org,
NuGet, and Microsoft's Visual Studio services. Running UVSR requires a DirectX
12-capable GPU and a suitable current vendor driver. No universal safe GPU-driver
installer exists, so the launcher reports that hardware requirement rather than
modifying drivers.
