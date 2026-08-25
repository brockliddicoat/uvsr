using System.Text.RegularExpressions;

namespace UvsrInstaller;

internal static partial class ProductConstants
{
    internal const int SchemaVersion = 1;
    internal const int LauncherSchemaVersion = 1;
    internal const string ProductId = "0c47a7a8-1ec4-4ffd-b6c4-2f7614181223";
    internal const string LauncherFeedUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/launcher-update-feed-v2.json";
    internal const string RendererFeedUrl =
        "https://raw.githubusercontent.com/brockliddicoat/uvsr/main/launcher/renderer-update-feed-v1.json";
    internal const int LauncherUpdateFeedSchemaVersion = 2;
    internal const int RendererUpdateFeedSchemaVersion = 1;
    internal const string UpdateFeedKeyId =
        "uvsr-launcher-update-p256-2026-01";
    internal const string UpdateFeedPublicKeySpkiBase64 =
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg==";
    internal const string LauncherVersion = "1.2.0";
    internal const long LauncherReleaseSequence = 16;
    internal const string LauncherExecutableName = "uvsr-launcher.exe";
    internal const string EngineExecutableName = "uvsr-engine.exe";
    internal const string LauncherArtifactName = "uvsr-launcher.exe";
    internal const string RendererArtifactName = "uvsr-renderer-windows-11-x64.zip";
    internal const string PackageManifestName = "package-manifest.json";
    internal const string SettingsContractRelativePath =
        "bin/settings/canonical-settings.json";
    internal const int SettingsContractSchemaVersion = 7;
    internal const string MigrationLauncherName = "UVSR Launcher.exe";
    internal const string OwnerMarkerName = ".uvsr-installer-owner.json";
    internal const string StateFileName = "state.json";
    internal const string TransactionFileName = "transaction.json";
    internal const long MaximumStateBytes = 64 * 1024;
    internal const long MaximumPackageManifestBytes = 16L * 1024 * 1024;
    internal const long MaximumUpdateFeedBytes = 16L * 1024;
    internal const long MaximumUpdateFeedPayloadBytes = 8L * 1024;
    internal const long MaximumLauncherBytes = 256L * 1024 * 1024;
    internal const long MaximumRendererPackageBytes = 32L * 1024 * 1024 * 1024;
    internal const long MaximumRendererExpandedBytes = 64L * 1024 * 1024 * 1024;
    internal const long MaximumReleaseSequence = 9_007_199_254_740_991L;

    [GeneratedRegex("^[0-9a-f]{40}$", RegexOptions.CultureInvariant)]
    internal static partial Regex CommitRegex();

    [GeneratedRegex("^[0-9a-f]{64}$", RegexOptions.CultureInvariant)]
    internal static partial Regex HashRegex();

    [GeneratedRegex("^[0-9a-f]{32}$", RegexOptions.CultureInvariant)]
    internal static partial Regex SettingsHashRegex();

    [GeneratedRegex(@"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$",
        RegexOptions.CultureInvariant)]
    internal static partial Regex StableVersionRegex();

    [GeneratedRegex(@"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$",
        RegexOptions.CultureInvariant)]
    internal static partial Regex EngineVersionRegex();

    [GeneratedRegex("^[0-9a-f]{40}-[0-9]{14}-[0-9a-f]{8}$", RegexOptions.CultureInvariant)]
    internal static partial Regex VersionIdRegex();

    internal static bool IsCanonicalEngineVersion(string? value) =>
        !string.IsNullOrWhiteSpace(value) &&
        EngineVersionRegex().IsMatch(value) &&
        value.Split('.').All(part => ushort.TryParse(part, out _));
}
