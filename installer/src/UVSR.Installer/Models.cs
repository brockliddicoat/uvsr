namespace UvsrInstaller;

internal enum InstallerOperation
{
    Install = 0,
    Update = 1,
    Reinstall = 2,
    Uninstall = 3
}

internal enum UpdateComponent
{
    Uvsr,
    Launcher
}

internal enum ComponentUpdateState
{
    NotInstalled,
    Current,
    UpdateAvailable,
    RepairNeeded,
    CheckFailed
}

internal sealed record OwnerMarker(
    int SchemaVersion,
    string ProductId,
    Guid InstallationId);

internal sealed record InstallState(
    int SchemaVersion,
    Guid InstallationId,
    string ActiveVersionId,
    string Commit,
    string ExecutableSha256,
    bool DesktopShortcut,
    DateTimeOffset InstalledUtc)
{
    internal void Validate(Guid expectedInstallationId)
    {
        if (SchemaVersion != ProductConstants.SchemaVersion ||
            InstallationId != expectedInstallationId ||
            string.IsNullOrWhiteSpace(ActiveVersionId) ||
            string.IsNullOrWhiteSpace(Commit) ||
            string.IsNullOrWhiteSpace(ExecutableSha256) ||
            !ProductConstants.VersionIdRegex().IsMatch(ActiveVersionId) ||
            !ProductConstants.CommitRegex().IsMatch(Commit) ||
            !ProductConstants.HashRegex().IsMatch(ExecutableSha256))
        {
            throw new InstallerException(
                "The installed UVSR record is invalid. No files were changed.");
        }
    }
}

internal sealed record PackageManifest(
    int SchemaVersion,
    Guid InstallationId,
    string VersionId,
    string Commit,
    string ExecutableSha256,
    IReadOnlyList<PackageFile> Files,
    DateTimeOffset BuiltUtc);

internal sealed record PackageFile(string RelativePath, long Size, string Sha256);

internal sealed record LauncherState(
    int SchemaVersion,
    string ProductId,
    Guid InstallationId,
    long ReleaseSequence,
    string Version,
    string ExecutableSha256,
    bool DesktopShortcut,
    DateTimeOffset InstalledUtc)
{
    internal void Validate(Guid expectedInstallationId)
    {
        if (SchemaVersion != ProductConstants.LauncherSchemaVersion ||
            !string.Equals(ProductId, ProductConstants.ProductId,
                StringComparison.OrdinalIgnoreCase) ||
            InstallationId != expectedInstallationId ||
            ReleaseSequence is < 1 or > ProductConstants.MaximumReleaseSequence ||
            string.IsNullOrWhiteSpace(Version) ||
            !ProductConstants.StableVersionRegex().IsMatch(Version) ||
            !Version.Split('.').All(part => int.TryParse(part, out _)) ||
            string.IsNullOrWhiteSpace(ExecutableSha256) ||
            !ProductConstants.HashRegex().IsMatch(ExecutableSha256))
            throw new InstallerException("The installed UVSR Launcher record is invalid.");
    }
}

internal sealed record LauncherPackageManifest(
    int SchemaVersion,
    string ProductId,
    Guid InstallationId,
    long ReleaseSequence,
    string Version,
    string ExecutableSha256,
    long ExecutableSize,
    DateTimeOffset InstalledUtc);

internal sealed record LauncherFeed(
    int SchemaVersion,
    string ProductId,
    string? Channel,
    long ReleaseSequence,
    string Version,
    LauncherFeedArtifact Artifact);

internal sealed record LauncherFeedArtifact(
    string Name,
    long Size,
    string Sha256);

internal sealed record LauncherActivationRecord(
    int SchemaVersion,
    string ProductId,
    Guid InstallationId,
    Guid TransactionId,
    string Phase,
    LauncherState? PreviousState,
    LauncherState CandidateState,
    bool ContinueUvsrUpdate,
    DateTimeOffset StartedUtc);

internal sealed record LauncherActivationInspection(
    bool StateFileExists,
    bool StateRecordMalformed,
    LauncherState? RecordedState,
    LauncherState? ValidState,
    long HighestDefensibleSequence,
    string? Problem);

internal sealed record ComponentUpdateStatus(
    UpdateComponent Component,
    ComponentUpdateState State,
    string? CurrentIdentifier,
    string? AvailableIdentifier,
    string Detail,
    LauncherFeed? LauncherFeed = null,
    string? UvsrCommit = null);

internal sealed record UpdateCheckResult(
    ComponentUpdateStatus Uvsr,
    ComponentUpdateStatus Launcher);

internal sealed record TransactionRecord(
    int SchemaVersion,
    Guid InstallationId,
    Guid TransactionId,
    InstallerOperation Operation,
    string Phase,
    string? CandidateVersionId,
    InstallState? PreviousState,
    DateTimeOffset StartedUtc);

internal sealed record UninstallRecord(
    int SchemaVersion,
    string ProductId,
    Guid InstallationId,
    Guid TransactionId,
    string Phase,
    InstallState? PreviousState,
    DateTimeOffset StartedUtc);

internal sealed record InstallSnapshot(
    bool IsInitialized,
    bool IsInstalled,
    Guid? InstallationId,
    InstallState? State,
    string? ExecutablePath,
    string Summary,
    bool IsDamaged = false);

internal sealed record ToolPaths(
    string Git,
    string CMake,
    string Python,
    string VisualStudioInstance,
    string AgilitySdk,
    string DirectXHeaders,
    string Dxc);

internal sealed record ToolInstallMarker(
    int SchemaVersion,
    string DisplayName,
    string Version,
    string Sha256,
    string ExecutableRelativePath,
    string ExecutableSha256);

internal sealed record ElevatedProcessIdentity(
    int ProcessId,
    long CreationTimeUtcFileTime,
    string ExecutablePath);

internal sealed record VisualStudioOperationRecord(
    string Action,
    string ExecutablePath,
    string ExecutableSha256,
    int? ProcessId,
    long? CreationTimeUtcFileTime,
    DateTimeOffset StartedUtc);

internal sealed record VisualStudioLayoutRecord(
    int SchemaVersion,
    string ProductId,
    string ComponentSet,
    string BootstrapperSha256,
    string ChannelId,
    string LayoutPath,
    string Phase,
    DateTimeOffset StartedUtc,
    VisualStudioOperationRecord? ActiveOperation = null);

internal sealed record OperationResult(
    string Message,
    InstallState? State,
    string? ExecutablePath,
    bool CleanupScheduled = false,
    string? RelaunchLauncherPath = null,
    bool ContinueUvsrUpdate = false,
    Guid? LauncherContinuationId = null);

internal sealed record PromptRequest(string Title, string Message);

internal sealed record InstallerProgress(
    string Phase,
    string Detail,
    int? Percent = null,
    bool CanCancel = true);

internal class InstallerException : Exception
{
    internal InstallerException(string message) : base(message) { }
    internal InstallerException(string message, Exception inner) : base(message, inner) { }
}

internal sealed class ShellRollbackException : InstallerException
{
    internal ShellRollbackException(string message, Exception inner) : base(message, inner) { }
}

internal sealed class RebootRequiredException : InstallerException
{
    internal RebootRequiredException() : base(
        "Microsoft's build prerequisites were installed successfully, but Windows must restart before UVSR can be built. Restart Windows, then open UVSR Launcher again.")
    {
    }
}
