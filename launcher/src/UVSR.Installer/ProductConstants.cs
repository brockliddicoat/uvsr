using System.Text.RegularExpressions;

namespace UvsrInstaller;

internal static partial class ProductConstants
{
    internal const int SchemaVersion = 1;
    internal const int LauncherSchemaVersion = 1;
    internal const string ProductId = "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223";
    internal const string RepositoryUrl = "https://github.com/brockliddicoat/uvsr.git";
    internal const string RepositoryMainRef = "refs/heads/main";
    internal const string RepositoryMainApi =
        "https://api.github.com/repos/brockliddicoat/uvsr/git/ref/heads/main";
    internal const string LauncherFeedUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json";
    internal const string LegacyLauncherFeedUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-feed-v1.json";
    internal const int LauncherUpdateFeedSchemaVersion = 2;
    internal const string LauncherUpdateFeedKeyId =
        "uvsr-launcher-update-p256-2026-01";
    internal const string LauncherUpdateFeedPublicKeySpkiBase64 =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg==";
    internal const string RepositoryRawUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr";
    internal const string RendererBuildContractRelativePath =
        "cmake/uvsr-launcher-build-contract-v1.json";
    internal const string RendererBuildContractId =
        "uvsr-windows-dx12-stable-619-v1";
    internal const int RendererBuildContractSchemaVersion = 1;
    internal const string PinnedDxcDate = "2026_02_20";
    internal const string LauncherVersion = "1.1.13";
    internal const long LauncherReleaseSequence = 14;
    internal const int D3D12AgilitySdkVersion = 619;
    internal const string LauncherExecutableName = "UVSR Launcher.exe";
    internal const string LauncherArtifactName = "UVSR-Launcher-Windows-11-x64.exe";
    internal const string LegacyInstalledManagerName = "UVSR Installer.exe";
    internal const string OwnerMarkerName = ".uvsr-installer-owner.json";
    internal const string StateFileName = "state.json";
    internal const string TransactionFileName = "transaction.json";
    internal const long MaximumStateBytes = 64 * 1024;
    internal const long MaximumPackageManifestBytes = 16L * 1024 * 1024;
    internal const long MaximumLauncherFeedBytes = 16L * 1024;
    internal const long MaximumLauncherFeedPayloadBytes = 8L * 1024;
    internal const long MaximumRendererBuildContractBytes = 16L * 1024;
    internal const long MaximumLauncherBytes = 256L * 1024 * 1024;
    internal const long MaximumReleaseSequence = 9_007_199_254_740_991L;
    internal static readonly ToolPackage Git = new(
        "Git for Windows MinGit",
        "2.55.0.4",
        "2.55.0.windows.4",
        new Uri("https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.4/MinGit-2.55.0.4-64-bit.zip"),
        "4e03f94c2ffbf70be337e005cee02661c732dbfc81031a078bda9299b9a7d644",
        80L * 1024 * 1024,
        Path.Combine("cmd", "git.exe"));

    internal static readonly ToolPackage CMake = new(
        "CMake",
        "4.4.2",
        "4.4.2",
        new Uri("https://github.com/Kitware/CMake/releases/download/v4.4.2/cmake-4.4.2-windows-x86_64.zip"),
        "e8139d85b3813bc38833142ae1940472e9a587e9b5d2718ac1804c60f4e57a64",
        100L * 1024 * 1024,
        Path.Combine("cmake-4.4.2-windows-x86_64", "bin", "cmake.exe"));

    internal static readonly ToolPackage Python = new(
        "Python",
        "3.13.15",
        "Python 3.13.15",
        new Uri("https://www.python.org/ftp/python/3.13.15/python-3.13.15-embed-amd64.zip"),
        "d1f04d990aee1253d8569e8e5104e30fa9f5fa830899f14843448872d936a2cf",
        30L * 1024 * 1024,
        "python.exe");

    internal static readonly PinnedArchivePackage AgilitySdk = new(
        "Microsoft Direct3D Agility SDK",
        "1.619.5",
        "agility-sdk-1.619.5",
        new Uri("https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.619.5/microsoft.direct3d.d3d12.1.619.5.nupkg"),
        "0e9bcf32aac9a79343ede9b21e4864950ee54577e3d8e19bfcdf002bb4e9bfd6",
        64L * 1024 * 1024,
        string.Empty,
        new[]
        {
            "build/native/include/d3d12.h",
            "build/native/include/d3d12.idl",
            "build/native/bin/x64/D3D12Core.dll",
            "build/native/bin/x64/d3d12SDKLayers.dll"
        });

    internal static readonly PinnedArchivePackage DirectXHeaders = new(
        "Microsoft DirectX Headers",
        "1.619.5",
        "directx-headers-1.619.5",
        new Uri("https://github.com/microsoft/DirectX-Headers/archive/refs/tags/v1.619.5.zip"),
        "e839554c5c14e2fcce85ca99085ffa255626f054b44c2c10683f1062bc30401b",
        8L * 1024 * 1024,
        "DirectX-Headers-1.619.5",
        new[] { "CMakeLists.txt", "include/directx/d3d12.h", "src/dxguids.cpp", "LICENSE" });

    internal static readonly PinnedArchivePackage Dxc = new(
        "Microsoft DirectX Shader Compiler",
        "1.9.2602",
        "dxc-1.9.2602",
        new Uri("https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip"),
        "a1e89031421cf3c1fca6627766ab3020ca4f962ac7e2caa7fab2b33a8436151e",
        64L * 1024 * 1024,
        string.Empty,
        new[] { "bin/x64/dxc.exe", "bin/x64/dxcompiler.dll", "bin/x64/dxil.dll", "LICENSE-MIT.txt" });

    internal static IReadOnlyList<PinnedArchivePackage> BuildDependencies { get; } =
        new[] { AgilitySdk, DirectXHeaders, Dxc };

    internal static readonly Uri VisualStudioBuildTools =
        new("https://aka.ms/vs/17/release/vs_buildtools.exe");

    internal static readonly Uri VisualCppRedistributable =
        new("https://aka.ms/vs/17/release/vc_redist.x64.exe");

    [GeneratedRegex("^[0-9a-f]{40}$", RegexOptions.CultureInvariant)]
    internal static partial Regex CommitRegex();

    [GeneratedRegex("^[0-9a-f]{64}$", RegexOptions.CultureInvariant)]
    internal static partial Regex HashRegex();

    [GeneratedRegex("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$",
        RegexOptions.CultureInvariant)]
    internal static partial Regex StableVersionRegex();

    [GeneratedRegex("^[0-9a-f]{40}-[0-9]{14}-[0-9a-f]{8}$", RegexOptions.CultureInvariant)]
    internal static partial Regex VersionIdRegex();
}

internal sealed record ToolPackage(
    string DisplayName,
    string Version,
    string ExpectedVersionOutput,
    Uri DownloadUri,
    string Sha256,
    long MaximumBytes,
    string ExecutableRelativePath)
{
    internal string DirectoryName =>
        DisplayName.StartsWith("Git", StringComparison.Ordinal) ? "git" :
        DisplayName == "CMake" ? "cmake" : "python";
}

internal sealed record PinnedArchivePackage(
    string DisplayName,
    string Version,
    string DirectoryName,
    Uri DownloadUri,
    string Sha256,
    long MaximumBytes,
    string ContentRelativeRoot,
    IReadOnlyList<string> RequiredRelativePaths);
