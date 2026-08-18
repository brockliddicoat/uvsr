namespace UvsrInstaller;

internal sealed class OwnershipManager
{
    private readonly InstallerPaths _paths;

    internal OwnershipManager(InstallerPaths paths) => _paths = paths;

    internal OwnerMarker? Inspect()
    {
        bool programExists = Directory.Exists(_paths.ProgramRoot);
        bool stateExists = Directory.Exists(_paths.StateRoot);
        if (!programExists && !stateExists)
            return null;

        if (programExists)
            SafePaths.RejectReparsePathChain(_paths.ProgramRoot, "UVSR program directory");
        if (stateExists)
            SafePaths.RejectReparsePathChain(_paths.StateRoot, "UVSR installer-state directory");

        OwnerMarker? program = programExists ? ReadMarker(_paths.ProgramMarker) : null;
        OwnerMarker? state = stateExists ? ReadMarker(_paths.StateMarker) : null;
        OwnerMarker marker = program ?? state
            ?? throw new InstallerException(
                "An existing UVSR directory is not owned by UVSR Launcher. It was preserved.");
        ValidateMarker(marker);
        if (program is not null && state is not null && program != state)
            throw new InstallerException("UVSR ownership records do not match. No files were changed.");
        return marker;
    }

    internal OwnerMarker EnsureRoots()
    {
        OwnerMarker? existing = Inspect();
        OwnerMarker marker = existing ?? new OwnerMarker(
            ProductConstants.SchemaVersion,
            ProductConstants.ProductId,
            Guid.NewGuid());

        EnsureOneRoot(_paths.ProgramRoot, _paths.ProgramMarker, marker);
        EnsureOneRoot(_paths.StateRoot, _paths.StateMarker, marker);
        Directory.CreateDirectory(_paths.LogsDirectory);
        return marker;
    }

    internal void ValidateBoth(Guid installationId)
    {
        OwnerMarker marker = Inspect()
            ?? throw new InstallerException("UVSR is not initialized for this Windows user.");
        if (marker.InstallationId != installationId ||
            !File.Exists(_paths.ProgramMarker) || !File.Exists(_paths.StateMarker))
            throw new InstallerException("UVSR ownership could not be proven. No files were changed.");
    }

    private static OwnerMarker ReadMarker(string path)
    {
        SafePaths.RejectReparsePoint(path, "UVSR ownership record");
        return JsonStore.Read<OwnerMarker>(path);
    }

    private static void ValidateMarker(OwnerMarker marker)
    {
        if (marker.SchemaVersion != ProductConstants.SchemaVersion ||
            !string.Equals(marker.ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase) ||
            marker.InstallationId == Guid.Empty)
        {
            throw new InstallerException("An existing UVSR directory has an unknown owner. It was preserved.");
        }
    }

    private static void EnsureOneRoot(string root, string markerPath, OwnerMarker marker)
    {
        SafePaths.RejectReparsePathChain(root, "UVSR installer-owned directory");
        if (Directory.Exists(root))
        {
            if (File.Exists(markerPath))
            {
                OwnerMarker current = JsonStore.Read<OwnerMarker>(markerPath);
                ValidateMarker(current);
                if (current != marker)
                    throw new InstallerException("UVSR ownership records do not match. No files were changed.");
                return;
            }
            if (Directory.EnumerateFileSystemEntries(root).Any())
                throw new InstallerException(
                    $"The existing directory '{root}' is not owned by UVSR Launcher. It was preserved.");
        }
        else
        {
            Directory.CreateDirectory(root);
        }
        JsonStore.WriteAtomic(markerPath, marker);
    }
}
