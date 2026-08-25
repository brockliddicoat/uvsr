[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [Parameter(Mandatory)] [string] $DotNetPath,
    [string] $SourceCommit,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$dotnet = [IO.Path]::GetFullPath($DotNetPath)
$repositoryPrefix = $repositoryRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ($output -eq $repositoryRoot -or
    $output.StartsWith($repositoryPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Launcher output must be outside the repository.'
}
if (-not (Test-Path -LiteralPath $dotnet -PathType Leaf)) {
    throw "The .NET host was not found at '$dotnet'."
}
$sdk = (& $dotnet --version).Trim()
if ($LASTEXITCODE -ne 0 -or $sdk -ne '10.0.400') {
    throw "UVSR Launcher requires .NET SDK 10.0.400; found '$sdk'."
}
$head = ((& git -C $repositoryRoot rev-parse HEAD) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $head -notmatch '^[0-9a-f]{40}$') {
    throw 'The launcher build could not determine the exact source commit.'
}
$statusArguments = @('-C', $repositoryRoot, 'status', '--porcelain=v1',
    '--untracked-files=all', '--ignore-submodules=none')
$status = ((& git @statusArguments) -join "`n").Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'The launcher build could not inspect the source tree.'
}
$submoduleArguments = @('-C', $repositoryRoot, 'submodule', 'foreach',
    '--recursive', '--quiet', 'git status --porcelain=v1 --untracked-files=all')
$submoduleStatus = ((& git @submoduleArguments) -join "`n").Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'The launcher build could not inspect submodule source trees.'
}
if ($status.Length -ne 0 -or $submoduleStatus.Length -ne 0) {
    throw 'The launcher release build requires a clean source tree and submodules.'
}
if ([string]::IsNullOrWhiteSpace($SourceCommit)) {
    $SourceCommit = $head
}
if ($SourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'The launcher source commit must be 40 lowercase hexadecimal characters.'
}
if ($SourceCommit -cne $head) {
    throw 'The launcher source commit does not match the checked-out source tree.'
}

$constantsPath = Join-Path $PSScriptRoot 'src\UVSR.Installer\ProductConstants.cs'
$constants = [IO.File]::ReadAllText($constantsPath)
$versionMatch = [regex]::Match($constants,
    'internal const string LauncherVersion = "(?<value>[0-9]+\.[0-9]+\.[0-9]+)";')
$sequenceMatch = [regex]::Match($constants,
    'internal const long LauncherReleaseSequence = (?<value>[0-9]+);')
if (-not $versionMatch.Success -or -not $sequenceMatch.Success) {
    throw 'The launcher release identity could not be read.'
}
$version = $versionMatch.Groups['value'].Value
$sequence = [long]$sequenceMatch.Groups['value'].Value

New-Item -ItemType Directory -Path $output -Force | Out-Null
$outputItem = Get-Item -LiteralPath $output -Force
if (($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Launcher output must not be a filesystem link.'
}
$publish = Join-Path $output ("publish-" + [guid]::NewGuid().ToString('N'))
$outputPrefix = $output.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
if (-not $publish.StartsWith($outputPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Launcher publish staging escaped the output directory.'
}
$testArtifacts = Join-Path $output 'test-artifacts'
$launcherArtifacts = Join-Path $output 'launcher-artifacts'
New-Item -ItemType Directory -Path $publish | Out-Null
$publishMarker = Join-Path $publish '.uvsr-launcher-build-owner'
$publishId = [guid]::NewGuid().ToString('N')
[IO.File]::WriteAllText($publishMarker, $publishId,
    [Text.UTF8Encoding]::new($false))
try {
    $launcherFeed = Join-Path $PSScriptRoot 'launcher-update-feed-v2.json'
    if (Test-Path -LiteralPath $launcherFeed -PathType Leaf) {
        & (Join-Path $PSScriptRoot 'verify-launcher-update-feed.ps1') -Path $launcherFeed |
            Out-Null
    }
    $rendererFeed = Join-Path $PSScriptRoot 'renderer-update-feed-v1.json'
    if (Test-Path -LiteralPath $rendererFeed -PathType Leaf) {
        & (Join-Path $PSScriptRoot 'verify-renderer-update-feed.ps1') -Path $rendererFeed |
            Out-Null
    }

    $project = Join-Path $PSScriptRoot 'src\UVSR.Installer\UVSR.Installer.csproj'
    $tests = Join-Path $PSScriptRoot 'tests\UVSR.Installer.Tests\UVSR.Installer.Tests.csproj'
    if (-not $SkipTests) {
        $testArguments = @(
            'run', '--project', $tests, '-c', 'Release', '--nologo',
            '--artifacts-path', $testArtifacts
        )
        & $dotnet @testArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Launcher contract tests failed with exit code $LASTEXITCODE."
        }
    }

    $publishArguments = @(
        'publish', $project, '-c', 'Release', '-r', 'win-x64',
        '--self-contained', 'true', '--nologo', '-o', $publish,
        '--artifacts-path', $launcherArtifacts,
        '-p:PublishSingleFile=true',
        '-p:IncludeNativeLibrariesForSelfExtract=true',
        '-p:PublishTrimmed=false',
        '-p:EnableCompressionInSingleFile=true',
        '-p:DebugType=None',
        '-p:DebugSymbols=false',
        "-p:Version=$version",
        "-p:FileVersion=$version.0",
        '-p:IncludeSourceRevisionInInformationalVersion=false',
        "-p:InformationalVersion=$version+$SourceCommit"
    )
    & $dotnet @publishArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Launcher publish failed with exit code $LASTEXITCODE."
    }

    $published = Join-Path $publish 'uvsr-launcher.exe'
    if (-not (Test-Path -LiteralPath $published -PathType Leaf)) {
        throw "The canonical launcher was not produced at '$published'."
    }
    $unexpected = @(Get-ChildItem -LiteralPath $publish -File |
        Where-Object Name -notin @(
            'uvsr-launcher.exe', '.uvsr-launcher-build-owner'
        ))
    if ($unexpected.Count -ne 0) {
        throw "Launcher publish contained unexpected files: $($unexpected.Name -join ', ')."
    }
    $metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($published)
    if ($metadata.ProductName -ne 'UVSR Launcher' -or
        $metadata.FileVersion -ne "$version.0" -or
        $metadata.ProductVersion -ne "$version+$SourceCommit") {
        throw 'The launcher executable metadata did not match its source identity.'
    }
    $health = Start-Process -FilePath $published -ArgumentList @(
        '--launcher-health-check', "$sequence", $version
    ) -PassThru -WindowStyle Hidden
    try {
        if (-not $health.WaitForExit(15000)) {
            $health.Kill($true)
            $health.WaitForExit()
            throw 'The launcher health check timed out.'
        }
        if ($health.ExitCode -ne 0) {
            throw 'The launcher health check rejected its release identity.'
        }
    }
    finally {
        $health.Dispose()
    }

    $artifact = Join-Path $output 'uvsr-launcher.exe'
    $checksum = Join-Path $output 'uvsr-launcher.exe.sha256'
    if ((Test-Path -LiteralPath $artifact) -or
        (Test-Path -LiteralPath $checksum)) {
        throw 'The requested output already contains a launcher artifact.'
    }
    Copy-Item -LiteralPath $published -Destination $artifact
    $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumLine = "$hash  uvsr-launcher.exe" + [Environment]::NewLine
    [IO.File]::WriteAllText($checksum, $checksumLine,
        [Text.UTF8Encoding]::new($false))
    Write-Output "Launcher: $artifact"
    Write-Output "SHA-256: $hash"
}
finally {
    if (Test-Path -LiteralPath $publish) {
        $resolvedPublish = [IO.Path]::GetFullPath($publish)
        if (-not $resolvedPublish.StartsWith($outputPrefix,
                [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $publishMarker -PathType Leaf) -or
            [IO.File]::ReadAllText($publishMarker) -cne $publishId) {
            throw "Launcher publish staging could not prove ownership: '$publish'."
        }
        Remove-Item -LiteralPath $publish -Recurse -Force
    }
}
