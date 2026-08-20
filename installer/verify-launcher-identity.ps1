[CmdletBinding()]
param(
    [string] $BaseCommit
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$constantsRelativePath = 'installer/src/UVSR.Installer/ProductConstants.cs'
$lockPath = Join-Path $PSScriptRoot 'launcher-input-lock-v1.json'
$binaryInputs = @(
    '.gitattributes',
    'installer/src/UVSR.Installer',
    'installer/build.ps1',
    'installer/generate-renderer-source-bridge.ps1',
    'installer/global.json',
    'installer/verify-launcher-identity.ps1',
    'cmake/uvsr-launcher-build-contract-v1.json',
    'LICENSE.md'
)

function Read-LauncherIdentity {
    param([Parameter(Mandatory)] [string] $Text)

    $versionMatch = [regex]::Match($Text,
        'internal const string LauncherVersion = "(?<value>[0-9]+\.[0-9]+\.[0-9]+)";')
    $sequenceMatch = [regex]::Match($Text,
        'internal const long LauncherReleaseSequence = (?<value>[0-9]+);')
    if (-not $versionMatch.Success -or -not $sequenceMatch.Success) {
        throw 'The launcher release identity could not be parsed.'
    }
    return [pscustomobject]@{
        Version = [version]$versionMatch.Groups['value'].Value
        Sequence = [long]::Parse($sequenceMatch.Groups['value'].Value,
            [Globalization.CultureInfo]::InvariantCulture)
    }
}

function Get-LauncherInputHash {
    $files = @(& git -C $repositoryRoot ls-files --cached --others `
        --exclude-standard -- $binaryInputs) | Sort-Object -CaseSensitive
    if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) {
        throw 'Git could not enumerate launcher binary inputs.'
    }
    $records = [Text.StringBuilder]::new()
    foreach ($relativePath in $files) {
        $fullPath = Join-Path $repositoryRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "A launcher binary input is missing: '$relativePath'."
        }
        $file = Get-Item -LiteralPath $fullPath
        $hash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
        [void]$records.Append($relativePath.Replace('\', '/'))
        [void]$records.Append("`0")
        [void]$records.Append($file.Length.ToString(
            [Globalization.CultureInfo]::InvariantCulture))
        [void]$records.Append("`0")
        [void]$records.Append($hash)
        [void]$records.Append("`n")
    }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($records.ToString())
        return ([Convert]::ToHexString(
            $algorithm.ComputeHash($bytes))).ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

$current = Read-LauncherIdentity ([IO.File]::ReadAllText(
    (Join-Path $repositoryRoot $constantsRelativePath)))
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    throw 'The checked-in launcher input lock is missing.'
}
$lock = [IO.File]::ReadAllText($lockPath) | ConvertFrom-Json
$lockProperties = @($lock.PSObject.Properties.Name | Sort-Object)
if (($lockProperties -join ',') -ne
        'inputsSha256,releaseSequence,schemaVersion,version' -or
    [int]$lock.schemaVersion -ne 1 -or
    [string]$lock.inputsSha256 -notmatch '^[0-9a-f]{64}$' -or
    [long]$lock.releaseSequence -ne $current.Sequence -or
    [version]([string]$lock.version) -ne $current.Version) {
    throw 'The checked-in launcher input lock does not match the current release identity.'
}
$inputHash = Get-LauncherInputHash
if ($inputHash -ne [string]$lock.inputsSha256) {
    throw "Launcher binary inputs changed under identity $($current.Version) sequence $($current.Sequence). Bump both values and refresh launcher-input-lock-v1.json with inputsSha256 '$inputHash'."
}
Write-Output "Launcher input lock verified: $($current.Version) sequence $($current.Sequence)."

if ([string]::IsNullOrWhiteSpace($BaseCommit)) {
    return
}
$base = $BaseCommit.Trim()
if ($base -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'The launcher identity comparison base must be a full Git commit ID.'
}
& git -C $repositoryRoot cat-file -e "$base^{commit}"
if ($LASTEXITCODE -ne 0) {
    throw "The launcher identity comparison base '$base' is unavailable."
}
$changedInputs = @(& git -C $repositoryRoot diff --name-only $base -- $binaryInputs)
if ($LASTEXITCODE -ne 0) {
    throw 'Git could not compare launcher binary inputs with the requested base.'
}
if ($changedInputs.Count -eq 0) {
    Write-Output 'Launcher binary inputs are unchanged; the release identity may remain stable.'
    return
}

$baseText = (& git -C $repositoryRoot show "${base}:$constantsRelativePath") -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw 'Git could not read the launcher identity at the comparison base.'
}
$previous = Read-LauncherIdentity $baseText
if ($current.Sequence -le $previous.Sequence -or
    $current.Version -le $previous.Version) {
    throw "Launcher binary inputs changed, so both version and release sequence must advance beyond $($previous.Version) sequence $($previous.Sequence)."
}
Write-Output "Launcher binary inputs changed with a unique identity: $($current.Version) sequence $($current.Sequence)."
