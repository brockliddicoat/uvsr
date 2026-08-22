[CmdletBinding()]
param(
    [string] $OutputDirectory,
    [string] $DotNetPath,
    [string] $PublishedArtifactPath,
    [string] $IdentityBaseCommit,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-NoReparsePathChain {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Description,
        [Parameter(Mandatory)] [string] $TrustedAnchor
    )
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullAnchor = [IO.Path]::GetFullPath($TrustedAnchor).TrimEnd(
        [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $anchorPrefix = $fullAnchor + [IO.Path]::DirectorySeparatorChar
    if ($fullPath -ne $fullAnchor -and
        -not $fullPath.StartsWith($anchorPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escaped its trusted output directory."
    }
    $current = [IO.DirectoryInfo]::new($fullPath)
    while ($null -ne $current) {
        if (Test-Path -LiteralPath $current.FullName) {
            $item = Get-Item -LiteralPath $current.FullName -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description is redirected by a filesystem link: '$($current.FullName)'."
            }
        }
        if ($current.FullName.TrimEnd([IO.Path]::DirectorySeparatorChar,
                [IO.Path]::AltDirectorySeparatorChar) -eq $fullAnchor) {
            return
        }
        $current = $current.Parent
    }
    throw "$Description could not be contained within its trusted output directory."
}

function Assert-NoReparseTree {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Description,
        [Parameter(Mandatory)] [string] $TrustedAnchor
    )
    Assert-NoReparsePathChain -Path $Path -Description $Description `
        -TrustedAnchor $TrustedAnchor
    foreach ($item in Get-ChildItem -LiteralPath $Path -Force) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description contains a filesystem link: '$($item.FullName)'."
        }
        if ($item.PSIsContainer) {
            Assert-NoReparseTree -Path $item.FullName -Description $Description `
                -TrustedAnchor $TrustedAnchor
        }
    }
}

function Assert-UnsignedPeX64 {
    param([Parameter(Mandatory)] [string] $Path)

    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 0x100 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw 'The supplied unsigned launcher artifact is not a Windows PE executable.'
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt $stream.Length - 24) {
            throw 'The supplied unsigned launcher artifact has an invalid PE header offset.'
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550 -or
            $reader.ReadUInt16() -ne 0x8664) {
            throw 'The supplied unsigned launcher artifact is not a Windows x64 executable.'
        }
        $stream.Position = $peOffset + 20
        $optionalHeaderSize = [int]$reader.ReadUInt16()
        $optionalHeaderOffset = [long]$peOffset + 24
        if ($optionalHeaderSize -lt 152 -or
            $optionalHeaderOffset + $optionalHeaderSize -gt $stream.Length) {
            throw 'The supplied unsigned launcher artifact has a truncated or malformed x64 optional header.'
        }
        $stream.Position = $optionalHeaderOffset
        if ($reader.ReadUInt16() -ne 0x020B) {
            throw 'The supplied unsigned launcher artifact does not use the required PE32+ optional header.'
        }
        $stream.Position = $optionalHeaderOffset + 108
        $directoryCount = [long]$reader.ReadUInt32()
        $maximumDirectoryCount = [long](($optionalHeaderSize - 112) / 8)
        if ($directoryCount -lt 5 -or $directoryCount -gt 16 -or
            $directoryCount -gt $maximumDirectoryCount) {
            throw 'The supplied unsigned launcher artifact has an invalid PE32+ data-directory table.'
        }
        $stream.Position = $optionalHeaderOffset + 144
        $certificateTableOffset = $reader.ReadUInt32()
        $certificateTableSize = $reader.ReadUInt32()
        if ($certificateTableOffset -ne 0 -or $certificateTableSize -ne 0) {
            throw 'The supplied unsigned launcher artifact contains an embedded PE certificate table.'
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Resolve-LauncherIdentityBase {
    param([string] $RequestedBase)

    if (-not [string]::IsNullOrWhiteSpace($RequestedBase)) {
        return $RequestedBase.Trim()
    }
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $head = ((& git -C $repositoryRoot rev-parse HEAD) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $head -notmatch '^[0-9a-fA-F]{40}$') {
        throw 'Git could not resolve the launcher build commit.'
    }
    $workingChanges = @(& git -C $repositoryRoot status --porcelain=v1 `
        --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw 'Git could not inspect launcher build changes.'
    }
    if ($workingChanges.Count -gt 0) {
        return $head
    }
    $default = ((& git -C $repositoryRoot rev-parse --verify `
        refs/remotes/origin/main 2>$null) -join '').Trim()
    if ($LASTEXITCODE -eq 0 -and $default -match '^[0-9a-fA-F]{40}$') {
        $mergeBase = ((& git -C $repositoryRoot merge-base $head $default) `
            -join '').Trim()
        if ($LASTEXITCODE -eq 0 -and $mergeBase -match '^[0-9a-fA-F]{40}$' -and
            $mergeBase -ne $head) {
            return $mergeBase
        }
    }
    $parent = ((& git -C $repositoryRoot rev-parse 'HEAD^') -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $parent -notmatch '^[0-9a-fA-F]{40}$') {
        throw 'A launcher identity comparison base is required for this repository history.'
    }
    return $parent
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot 'artifacts'
}
if ([string]::IsNullOrWhiteSpace($DotNetPath)) {
    $DotNetPath = (Get-Command dotnet -ErrorAction Stop).Source
}
$DotNetPath = [IO.Path]::GetFullPath($DotNetPath)
if (-not (Test-Path -LiteralPath $DotNetPath -PathType Leaf)) {
    throw "The .NET host was not found at '$DotNetPath'."
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'UVSR Launcher can be built only on Windows.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'UVSR Launcher requires 64-bit Windows.'
}

& (Join-Path $PSScriptRoot 'verify-launcher-feed-alias.ps1')
& (Join-Path $PSScriptRoot 'tests\launcher-feed-alias-tests.ps1')
& (Join-Path $PSScriptRoot 'tests\launcher-update-feed-tests.ps1')
$updateFeedPath = Join-Path $PSScriptRoot 'launcher-update-feed-v2.json'
$updateFeed = $null
if (Test-Path -LiteralPath $updateFeedPath) {
    if (-not (Test-Path -LiteralPath $updateFeedPath -PathType Leaf)) {
        throw "The optional launcher update feed is not a regular file at '$updateFeedPath'."
    }
    $updateFeedJson = & (Join-Path $PSScriptRoot `
        'verify-launcher-update-feed.ps1') -Path $updateFeedPath
    if ([string]::IsNullOrWhiteSpace(($updateFeedJson -join ''))) {
        throw 'The optional launcher update feed failed strict signature verification.'
    }
    $updateFeed = ($updateFeedJson -join "`n") | ConvertFrom-Json
}

$project = Join-Path $PSScriptRoot 'src\UVSR.Installer\UVSR.Installer.csproj'
$tests = Join-Path $PSScriptRoot 'tests\UVSR.Installer.Tests\UVSR.Installer.Tests.csproj'
$output = [IO.Path]::GetFullPath($OutputDirectory)
$publishId = [guid]::NewGuid().ToString('N')
$publish = Join-Path $output "publish-$publishId"
$publishMarker = Join-Path $publish '.uvsr-launcher-publish-owner'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Assert-NoReparsePathChain -Path $output -Description 'The launcher output directory' `
    -TrustedAnchor $output
$outputPrefix = $output.TrimEnd([IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $publish.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "The launcher publish directory escaped the requested output directory."
}
$locationPushed = $false
try {
    & (Join-Path $PSScriptRoot `
        'tests\renderer-source-bridge-verifier-tests.ps1')
    & (Join-Path $PSScriptRoot 'generate-renderer-source-bridge.ps1') -Check
    $resolvedIdentityBase = Resolve-LauncherIdentityBase $IdentityBaseCommit
    & (Join-Path $PSScriptRoot 'verify-launcher-identity.ps1') `
        -BaseCommit $resolvedIdentityBase
    foreach ($legacyName in @('UVSR-Installer-Windows-11-x64.exe',
            'UVSR-Installer-Windows-11-x64.exe.sha256',
            'UVSR-Launcher-Windows-11-x64.exe',
            'UVSR-Launcher-Windows-11-x64.exe.sha256')) {
        $legacyPath = Join-Path $output $legacyName
        if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
            Remove-Item -LiteralPath $legacyPath -Force
        }
    }
    New-Item -ItemType Directory -Path $publish | Out-Null
    Assert-NoReparsePathChain -Path $publish -Description 'The launcher publish directory' `
        -TrustedAnchor $output
    [IO.File]::WriteAllText($publishMarker, $publishId,
        [Text.UTF8Encoding]::new($false))

    Push-Location $PSScriptRoot
    $locationPushed = $true
$sdk = (& $DotNetPath --version).Trim()
if ($LASTEXITCODE -ne 0 -or $sdk -ne '10.0.400') {
    throw "UVSR Launcher must be built with the pinned .NET SDK 10.0.400; found '$sdk'."
}

if (-not $SkipTests) {
    & $DotNetPath run --project $tests -c Release
    if ($LASTEXITCODE -ne 0) {
        throw "Launcher contract tests failed with exit code $LASTEXITCODE."
    }
}

& $DotNetPath publish $project `
    -c Release `
    -r win-x64 `
    --self-contained true `
    --nologo `
    -o $publish `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:PublishTrimmed=false `
    -p:EnableCompressionInSingleFile=true `
    -p:DebugType=embedded
if ($LASTEXITCODE -ne 0) {
    throw "Launcher publish failed with exit code $LASTEXITCODE."
}

$publishedExecutable = Join-Path $publish 'UVSR-Launcher.exe'
if (-not (Test-Path -LiteralPath $publishedExecutable -PathType Leaf)) {
    throw "The single-file launcher was not produced at '$publishedExecutable'."
}
$unexpected = Get-ChildItem -LiteralPath $publish -File |
    Where-Object { $_.Name -notin @('UVSR-Launcher.exe', '.uvsr-launcher-publish-owner') }
if ($unexpected) {
    throw "The publish output is not a single executable: $($unexpected.Name -join ', ')."
}

$hash = (Get-FileHash -LiteralPath $publishedExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
$publishedSize = (Get-Item -LiteralPath $publishedExecutable).Length
$constantsPath = Join-Path $PSScriptRoot 'src\UVSR.Installer\ProductConstants.cs'
$constants = [IO.File]::ReadAllText($constantsPath)
$versionMatch = [regex]::Match($constants,
    'internal const string LauncherVersion = "(?<value>[0-9]+\.[0-9]+\.[0-9]+)";')
$sequenceMatch = [regex]::Match($constants,
    'internal const long LauncherReleaseSequence = (?<value>[0-9]+);')
if (-not $versionMatch.Success -or -not $sequenceMatch.Success) {
    throw 'The launcher release identity could not be read from ProductConstants.cs.'
}
$currentVersion = $versionMatch.Groups['value'].Value
$currentSequence = [long]::Parse($sequenceMatch.Groups['value'].Value,
    [Globalization.CultureInfo]::InvariantCulture)
$feedPath = Join-Path $PSScriptRoot 'launcher-feed-v1.json'
$feed = [IO.File]::ReadAllText($feedPath) | ConvertFrom-Json
$releaseFeeds = @(
    [pscustomobject]@{
        Kind = 'signed-v1'
        Sequence = [long]$feed.releaseSequence
        Version = [version]([string]$feed.version)
        VersionText = [string]$feed.version
        Artifact = $feed.artifact
        SourceCommit = $null
    }
)
if ($null -ne $updateFeed) {
    $releaseFeeds += [pscustomobject]@{
        Kind = 'unsigned-v2'
        Sequence = [long]$updateFeed.releaseSequence
        Version = [version]([string]$updateFeed.version)
        VersionText = [string]$updateFeed.version
        Artifact = $updateFeed.artifact
        SourceCommit = [string]$updateFeed.sourceCommit
    }
}
$releaseFeeds = @($releaseFeeds | Sort-Object Sequence)
for ($index = 1; $index -lt $releaseFeeds.Count; $index++) {
    $previousFeed = $releaseFeeds[$index - 1]
    $nextFeed = $releaseFeeds[$index]
    if ($nextFeed.Sequence -le $previousFeed.Sequence -or
        $nextFeed.Version -le $previousFeed.Version) {
        throw 'Published launcher feed identities must advance version and release sequence together without overlap.'
    }
}
$publishedFeed = $releaseFeeds[-1]
$parsedCurrentVersion = [version]$currentVersion
if ($currentSequence -lt $publishedFeed.Sequence -or
    $parsedCurrentVersion -lt $publishedFeed.Version) {
    throw "Launcher identity $currentVersion sequence $currentSequence is older than published identity $($publishedFeed.Version) sequence $($publishedFeed.Sequence)."
}
if (($currentSequence -eq $publishedFeed.Sequence) -ne
    ($parsedCurrentVersion -eq $publishedFeed.Version)) {
    throw 'Launcher version and release sequence must advance together.'
}
if ($currentSequence -gt $publishedFeed.Sequence -and
    $parsedCurrentVersion -le $publishedFeed.Version) {
    throw 'A new launcher release sequence requires a newer semantic version.'
}
if (-not [string]::IsNullOrWhiteSpace($PublishedArtifactPath)) {
    $matchingFeeds = @($releaseFeeds | Where-Object {
            $_.Sequence -eq $currentSequence -and
            $_.Version -eq $parsedCurrentVersion
        })
    if ($matchingFeeds.Count -ne 1) {
        throw 'Final launcher artifact verification requires the checked-in public feed to record the current release identity.'
    }
    $artifactFeed = $matchingFeeds[0]
    $finalArtifact = [IO.Path]::GetFullPath($PublishedArtifactPath)
    if (-not (Test-Path -LiteralPath $finalArtifact -PathType Leaf)) {
        throw "The final launcher artifact was not found at '$finalArtifact'."
    }
    $finalHash = (Get-FileHash -LiteralPath $finalArtifact -Algorithm SHA256).Hash.ToLowerInvariant()
    $finalSize = (Get-Item -LiteralPath $finalArtifact).Length
    if ($finalSize -ne [long]$artifactFeed.Artifact.size -or
        $finalHash -ne [string]$artifactFeed.Artifact.sha256) {
        throw 'The supplied final launcher artifact does not match the checked-in public feed.'
    }
    $finalMetadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($finalArtifact)
    if ($artifactFeed.Kind -eq 'unsigned-v2') {
        if ($finalMetadata.ProductName -ne 'UVSR Launcher' -or
            $finalMetadata.ProductVersion -ne
                "$currentVersion+$($artifactFeed.SourceCommit)" -or
            $finalMetadata.FileVersion -ne "$currentVersion.0") {
            throw 'The supplied unsigned launcher artifact does not contain its exact source-bound product metadata.'
        }
        Assert-UnsignedPeX64 $finalArtifact
        $authenticode = Get-AuthenticodeSignature -LiteralPath $finalArtifact
        if ([string]$authenticode.Status -ne 'NotSigned' -or
            [string]$authenticode.SignatureType -ne 'None' -or
            $null -ne $authenticode.SignerCertificate -or
            $null -ne $authenticode.TimeStamperCertificate) {
            throw 'The supplied unsigned launcher artifact does not have exact Authenticode NotSigned status.'
        }
    }
    else {
        $versionWithBuild = "$currentVersion+"
        if ($finalMetadata.ProductName -ne 'UVSR Launcher' -or
            [string]::IsNullOrWhiteSpace($finalMetadata.ProductVersion) -or
            ($finalMetadata.ProductVersion -ne $currentVersion -and
             -not $finalMetadata.ProductVersion.StartsWith(
                 $versionWithBuild, [StringComparison]::Ordinal))) {
            throw 'The supplied final launcher artifact does not contain the current launcher product metadata.'
        }
    }
    $health = Start-Process -FilePath $finalArtifact `
        -ArgumentList @('--launcher-health-check', "$currentSequence", $currentVersion) `
        -PassThru -WindowStyle Hidden
    try {
        if (-not $health.WaitForExit(15000)) {
            $health.Kill($true)
            $health.WaitForExit()
            throw 'The supplied final launcher artifact did not finish its identity health check.'
        }
        if ($health.ExitCode -ne 0) {
            throw 'The supplied final launcher artifact rejected the current release identity.'
        }
    }
    finally {
        $health.Dispose()
    }
}

$artifact = Join-Path $output 'UVSR-Launcher-Windows-11-x64.exe'
Copy-Item -LiteralPath $publishedExecutable -Destination $artifact
$checksum = Join-Path $output 'UVSR-Launcher-Windows-11-x64.exe.sha256'
[IO.File]::WriteAllText($checksum, "$hash  UVSR-Launcher-Windows-11-x64.exe`n",
    [Text.UTF8Encoding]::new($false))

Write-Output "Launcher:  $artifact"
Write-Output "SHA-256:  $hash"
Write-Warning 'This local artifact is unsigned. Public distribution requires the pinned signed-feed or signed-release checklist and an immutable exact release.'
}
finally {
    if ($locationPushed) {
        Pop-Location
    }
    if (Test-Path -LiteralPath $publish) {
        Assert-NoReparseTree -Path $publish -Description 'The launcher publish directory' `
            -TrustedAnchor $output
        if (-not (Test-Path -LiteralPath $publishMarker -PathType Leaf) -or
            [IO.File]::ReadAllText($publishMarker) -ne $publishId) {
            throw "The launcher publish directory ownership marker is missing or invalid; it was preserved at '$publish'."
        }
        Remove-Item -LiteralPath $publish -Recurse -Force
    }
}
