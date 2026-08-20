using System.Reflection;
using System.Security.Cryptography;

namespace UvsrInstaller;

internal sealed class RendererSourceBridge
{
    internal const string BridgeId = "uvsr-public-main-0c807484-stable-619-v1";
    internal const string PublicBaseCommit =
        "0c8074848985152ed83f83b4087aaf10013de590";
    internal const string PublicBaseTree =
        "a9ff0276ad295675239839e37124b75dfc5334d7";
    internal const string SourceCommit =
        "ac19135e176bd137df050fb3da11297a2460312d";
    internal const string SourceTree =
        "736fc012878dc66e5f512dab40d722a56ac8c1f5";
    internal const string PatchSha256 =
        "e68f814e2e838ef08bf8561bfa033dbaa0b5a523f776983dca18e8dd83ad799a";
    internal const long PatchSize = 24_581;
    internal const string CommitMessage = "uvsr launcher renderer bridge v1";
    internal const string CommitIdentityName = "UVSR Launcher";
    internal const string CommitIdentityEmail = "launcher@uvsr.local";
    internal const string CommitIdentityDate = "2026-08-20T00:00:00Z";
    internal const string ResourceName =
        "UVSR.Installer.RendererBridges.uvsr-public-main-0c807484.patch";

    private static readonly IReadOnlyDictionary<string, string> ExpectedBlobs =
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["CMakeLists.txt"] = "bf37c7807f7db7b4aaa9cc7e89cd0998690ce027",
            ["cmake/VerifyD3D12Runtime.cmake"] =
                "f48564e44f5d1ceb36beed72ee38f5b482958942",
            ["cmake/uvsr-launcher-build-contract-v1.json"] =
                "09e963b654d55d8cc2e7cf4bca8f255faa9dccaa",
            ["overrides/nvrhi-stable-directx-headers.patch"] =
                "4feec45339f8bb45aff9c82fe46ebc8b9c089e7b",
            ["src/d3d12_agility_exports.cpp"] =
                "7d00c7651fc428d34385e2f34e27c4300c727726",
            ["src/gpu_capabilities.h"] =
                "d2f4aac53709713ba3ee2b8938812b331eebe000",
            ["src/uvsr.cpp"] =
                "3c5eb7daf2cf057db1026ec115cdc03c9c618aab",
            ["src/windows_executable_path.h"] =
                "105ad08dba865b288d02a9849fe22d88bb352dff",
            ["tests/gpu_capabilities_tests.cpp"] =
                "49c454698c816e72605a02e630aee0e568292e79",
            ["tests/windows_executable_path_tests.cpp"] =
                "4406d0076e2e4d4b08811352e9dd30fb790f0a6e"
        };

    private RendererSourceBridge()
    {
    }

    internal static RendererSourceBridge Instance { get; } = new();

    internal RendererBuildContract Contract { get; } = new(
        ProductConstants.RendererBuildContractSchemaVersion,
        ProductConstants.ProductId,
        ProductConstants.RendererBuildContractId,
        4,
        ProductConstants.AgilitySdk.Version,
        ProductConstants.DirectXHeaders.Version,
        ProductConstants.Dxc.Version,
        ProductConstants.PinnedDxcDate);

    internal IReadOnlyDictionary<string, string> StagedBlobs => ExpectedBlobs;

    internal byte[] LoadVerifiedPatch()
    {
        using Stream stream = Assembly.GetExecutingAssembly()
            .GetManifestResourceStream(ResourceName)
            ?? throw new InstallerException(
                "The embedded UVSR renderer source bridge is missing.");
        if (stream.CanSeek && stream.Length != PatchSize)
            throw new InstallerException(
                "The embedded UVSR renderer source bridge has an invalid size.");
        byte[] bytes = new byte[PatchSize];
        try
        {
            stream.ReadExactly(bytes);
        }
        catch (EndOfStreamException ex)
        {
            throw new InstallerException(
                "The embedded UVSR renderer source bridge was truncated.", ex);
        }
        if (stream.ReadByte() != -1)
            throw new InstallerException(
                "The embedded UVSR renderer source bridge was too large.");
        byte[] expected = Convert.FromHexString(PatchSha256);
        byte[] actual = SHA256.HashData(bytes);
        if (!CryptographicOperations.FixedTimeEquals(expected, actual))
            throw new InstallerException(
                "The embedded UVSR renderer source bridge failed its integrity check.");
        return bytes;
    }

    internal void ValidateStagedEntries(string output)
    {
        Dictionary<string, string> actual = new(StringComparer.Ordinal);
        foreach (string line in output.Split(new[] { '\r', '\n' },
                     StringSplitOptions.RemoveEmptyEntries))
        {
            int tab = line.IndexOf('\t');
            string[] identity = tab > 0
                ? line[..tab].Split(' ', StringSplitOptions.RemoveEmptyEntries)
                : Array.Empty<string>();
            string path = tab > 0 ? line[(tab + 1)..] : string.Empty;
            if (identity.Length != 3 || identity[0] != "100644" ||
                identity[2] != "0" || !ProductConstants.CommitRegex().IsMatch(identity[1]) ||
                !actual.TryAdd(path, identity[1]))
                throw new InstallerException(
                    "The renderer source bridge staged an invalid Git entry.");
        }
        if (actual.Count != ExpectedBlobs.Count || ExpectedBlobs.Any(expected =>
                !actual.TryGetValue(expected.Key, out string? blob) ||
                !string.Equals(expected.Value, blob, StringComparison.Ordinal)))
            throw new InstallerException(
                "The renderer source bridge changed outside its exact audited path and blob set.");
    }

    internal void ValidatePreparedIdentityValues(
        string head,
        string tree,
        string parentLine,
        string status)
    {
        if (!string.Equals(head, SourceCommit, StringComparison.Ordinal) ||
            !string.Equals(tree, SourceTree, StringComparison.Ordinal) ||
            !string.Equals(parentLine, $"{SourceCommit} {PublicBaseCommit}",
                StringComparison.Ordinal) ||
            !string.IsNullOrWhiteSpace(status))
            throw new InstallerException(
                "The prepared UVSR renderer source no longer matches its exact bridge identity.");
    }
}

internal static class RendererSourceBridgeRegistry
{
    internal static RendererSourceBridge? FindForPublicBase(string publicCommit)
    {
        if (!ProductConstants.CommitRegex().IsMatch(publicCommit))
            throw new InstallerException("The selected UVSR source revision is invalid.");
        return string.Equals(publicCommit, RendererSourceBridge.PublicBaseCommit,
            StringComparison.Ordinal) ? RendererSourceBridge.Instance : null;
    }

    internal static string MapSourceToPublicBase(string sourceCommit)
    {
        if (!ProductConstants.CommitRegex().IsMatch(sourceCommit))
            throw new InstallerException("The installed UVSR source revision is invalid.");
        return string.Equals(sourceCommit, RendererSourceBridge.SourceCommit,
            StringComparison.Ordinal)
            ? RendererSourceBridge.PublicBaseCommit
            : sourceCommit;
    }
}
