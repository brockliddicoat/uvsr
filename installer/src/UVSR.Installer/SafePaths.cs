using System.IO.Compression;

namespace UvsrInstaller;

internal static class SafePaths
{
    internal static bool IsStrictDescendant(string candidate, string parent)
    {
        string fullCandidate = Path.TrimEndingDirectorySeparator(Path.GetFullPath(candidate));
        string fullParent = Path.TrimEndingDirectorySeparator(Path.GetFullPath(parent));
        if (string.Equals(fullCandidate, fullParent, StringComparison.OrdinalIgnoreCase))
            return false;
        return fullCandidate.StartsWith(fullParent + Path.DirectorySeparatorChar,
            StringComparison.OrdinalIgnoreCase);
    }

    internal static string CombineDescendant(string parent, string relative)
    {
        if (Path.IsPathRooted(relative) || relative.Contains(':'))
            throw new InstallerException("An installer-managed relative path is invalid.");
        string result = Path.GetFullPath(Path.Combine(parent, relative));
        if (!IsStrictDescendant(result, parent))
            throw new InstallerException("An installer-managed path escaped its owned directory.");
        return result;
    }

    internal static void RejectReparsePoint(string path, string description)
    {
        if (!File.Exists(path) && !Directory.Exists(path))
            return;
        FileAttributes attributes = File.GetAttributes(path);
        if ((attributes & FileAttributes.ReparsePoint) != 0)
            throw new InstallerException($"The {description} is redirected by a link. No files were changed.");
    }

    internal static void RejectReparsePathChain(string path, string description)
    {
        string full = Path.GetFullPath(path);
        string root = Path.GetPathRoot(full)
            ?? throw new InstallerException($"The {description} has no filesystem root.");
        string current = root;
        RejectReparsePoint(root, description);
        string remainder = full[root.Length..];
        foreach (string component in remainder.Split(
                     new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar },
                     StringSplitOptions.RemoveEmptyEntries))
        {
            current = Path.Combine(current, component);
            if (!File.Exists(current) && !Directory.Exists(current))
                continue;
            RejectReparsePoint(current, description);
        }
    }

    internal static void RejectReparseTree(string root, string description)
    {
        RejectReparsePathChain(root, description);
        if (!Directory.Exists(root))
            return;
        foreach (string entry in Directory.EnumerateFileSystemEntries(root,
                     "*", SearchOption.TopDirectoryOnly))
        {
            RejectReparsePoint(entry, description);
            if ((File.GetAttributes(entry) & FileAttributes.Directory) != 0)
                RejectReparseTree(entry, description);
        }
    }

    internal static void ExtractVerifiedZip(string zipPath, string destination)
    {
        RejectReparsePathChain(Path.GetDirectoryName(destination)!,
            "tool extraction parent directory");
        Directory.CreateDirectory(destination);
        RejectReparsePathChain(destination, "tool extraction directory");
        using ZipArchive archive = ZipFile.OpenRead(zipPath);
        foreach (ZipArchiveEntry entry in archive.Entries)
        {
            string normalized = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
            if (string.IsNullOrEmpty(normalized))
                continue;
            if (Path.IsPathRooted(normalized) || normalized.Contains(':'))
                throw new InstallerException("A downloaded tool archive contains an unsafe path.");
            string output = CombineDescendant(destination, normalized);
            bool directory = normalized.EndsWith(Path.DirectorySeparatorChar);
            if (directory)
            {
                RejectReparsePathChain(Path.GetDirectoryName(output)!,
                    "tool extraction output directory");
                Directory.CreateDirectory(output);
                RejectReparsePathChain(output, "tool extraction output directory");
                continue;
            }
            RejectReparsePathChain(Path.GetDirectoryName(output)!,
                "tool extraction output directory");
            Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            using Stream input = entry.Open();
            using FileStream target = new(output, FileMode.CreateNew, FileAccess.Write, FileShare.None);
            input.CopyTo(target);
        }
    }

    internal static void CopyDirectory(string source, string destination)
    {
        RejectReparsePathChain(source, "runtime source directory");
        RejectReparsePathChain(Path.GetDirectoryName(destination)!,
            "runtime destination parent directory");
        Directory.CreateDirectory(destination);
        RejectReparsePathChain(destination, "runtime destination directory");
        foreach (string directory in Directory.EnumerateDirectories(source, "*", SearchOption.TopDirectoryOnly))
        {
            RejectReparsePoint(directory, "runtime source directory");
            CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
        }
        foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.TopDirectoryOnly))
        {
            RejectReparsePoint(file, "runtime source file");
            File.Copy(file, Path.Combine(destination, Path.GetFileName(file)), overwrite: false);
        }
    }

    internal static void DeleteOwnedTree(string target, string allowedParent)
    {
        target = Path.GetFullPath(target);
        allowedParent = Path.GetFullPath(allowedParent);
        if (!IsStrictDescendant(target, allowedParent))
            throw new InstallerException("Refusing to remove a directory outside UVSR Launcher ownership.");
        RejectReparsePathChain(allowedParent, "installer-owned parent directory");
        RejectReparsePathChain(target, "installer-owned removal path");
        DeleteTreeCore(target);
    }

    private static void DeleteTreeCore(string target)
    {
        if (!Directory.Exists(target) && !File.Exists(target))
            return;

        FileAttributes attributes = File.GetAttributes(target);
        if ((attributes & FileAttributes.ReparsePoint) != 0)
        {
            if ((attributes & FileAttributes.Directory) != 0)
                Directory.Delete(target, recursive: false);
            else
                File.Delete(target);
            return;
        }

        if ((attributes & FileAttributes.Directory) == 0)
        {
            File.SetAttributes(target, FileAttributes.Normal);
            File.Delete(target);
            return;
        }

        foreach (string child in Directory.EnumerateFileSystemEntries(target))
            DeleteTreeCore(child);
        Directory.Delete(target, recursive: false);
    }
}
