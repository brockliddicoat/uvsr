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
    long ReleaseSequence,
    string Commit,
    string SettingsHash,
    string EngineVersion,
    string ArtifactSha256,
    string ExecutableSha256,
    bool DesktopShortcut,
    DateTimeOffset InstalledUtc)
{
    internal void Validate(Guid expectedInstallationId)
    {
        if (SchemaVersion != ProductConstants.SchemaVersion ||
            InstallationId != expectedInstallationId ||
            string.IsNullOrWhiteSpace(ActiveVersionId) ||
            !ProductConstants.VersionIdRegex().IsMatch(ActiveVersionId) ||
            ReleaseSequence is < 1 or > ProductConstants.MaximumReleaseSequence ||
            string.IsNullOrWhiteSpace(Commit) ||
            !ProductConstants.CommitRegex().IsMatch(Commit) ||
            string.IsNullOrWhiteSpace(SettingsHash) ||
            !ProductConstants.SettingsHashRegex().IsMatch(SettingsHash) ||
            !ProductConstants.IsCanonicalEngineVersion(EngineVersion) ||
            string.IsNullOrWhiteSpace(ArtifactSha256) ||
            !ProductConstants.HashRegex().IsMatch(ArtifactSha256) ||
            string.IsNullOrWhiteSpace(ExecutableSha256) ||
            !ProductConstants.HashRegex().IsMatch(ExecutableSha256))
        {
            throw new InstallerException(
                "The installed UVSR record is invalid. No files were changed.");
        }
    }
}

internal sealed record PackageManifest(
    int SchemaVersion,
    string ProductId,
    bool Production,
    string Configuration,
    long ReleaseSequence,
    string SourceCommit,
    string SettingsHash,
    string EngineVersion,
    string ExecutableSha256,
    IReadOnlyList<PackageFile> Files);

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
                StringComparison.Ordinal) ||
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

internal sealed record SignedFeedEnvelope(
    int SchemaVersion,
    string KeyId,
    string PayloadBase64,
    string SignatureBase64);

internal sealed record LauncherFeed(
    int SchemaVersion,
    string ProductId,
    string Channel,
    long ReleaseSequence,
    string Version,
    string SourceCommit,
    LauncherFeedArtifact Artifact);

internal sealed record LauncherFeedArtifact(
    string Name,
    long Size,
    string Sha256);

internal sealed record RendererFeed(
    int SchemaVersion,
    string ProductId,
    string Channel,
    long ReleaseSequence,
    string SourceCommit,
    string SettingsHash,
    string EngineVersion,
    RendererFeedArtifact Artifact);

internal sealed record RendererFeedArtifact(
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
    bool StateRecordUnverifiable,
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
    RendererFeed? RendererFeed = null);

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

internal sealed record OperationResult(
    string Message,
    InstallState? State,
    string? ExecutablePath,
    bool CleanupScheduled = false,
    string? RelaunchLauncherPath = null,
    bool ContinueUvsrUpdate = false,
    Guid? LauncherContinuationId = null);

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
