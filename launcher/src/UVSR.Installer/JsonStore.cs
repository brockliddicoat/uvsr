using System.Text.Json;
using System.Text.Json.Serialization;

namespace UvsrInstaller;

internal static class JsonStore
{
    internal static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true
    };

    internal static T Read<T>(string path, long maximumBytes = ProductConstants.MaximumStateBytes)
    {
        SafePaths.RejectReparsePathChain(path, "installer record");
        FileInfo info = new(path);
        if (!info.Exists || info.Length <= 0 || info.Length > maximumBytes)
            throw new InstallerException($"The UVSR Launcher record '{Path.GetFileName(path)}' is missing or invalid.");
        try
        {
            byte[] data = File.ReadAllBytes(path);
            return JsonSerializer.Deserialize<T>(data, Options)
                ?? throw new JsonException("The JSON record was empty.");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            throw new InstallerException(
                $"The UVSR Launcher record '{Path.GetFileName(path)}' could not be read safely.", ex);
        }
    }

    internal static void WriteAtomic<T>(string path, T value)
    {
        WriteAtomicBytes(path, JsonSerializer.SerializeToUtf8Bytes(value, Options));
    }

    internal static void WriteAtomicBytes(string path, byte[] data)
    {
        string directory = Path.GetDirectoryName(path)
            ?? throw new InstallerException("The UVSR Launcher record has no parent directory.");
        SafePaths.RejectReparsePathChain(directory, "installer record directory");
        Directory.CreateDirectory(directory);
        SafePaths.RejectReparsePathChain(directory, "installer record directory");
        SafePaths.RejectReparsePathChain(path, "installer record destination");
        string temporary = Path.Combine(directory, $".{Path.GetFileName(path)}.{Guid.NewGuid():N}.tmp");
        try
        {
            using (FileStream stream = new(temporary, FileMode.CreateNew, FileAccess.Write,
                       FileShare.None, 4096, FileOptions.WriteThrough))
            {
                stream.Write(data);
                stream.Flush(flushToDisk: true);
            }
            File.Move(temporary, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary))
                File.Delete(temporary);
        }
    }
}
