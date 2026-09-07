[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [string] $BuildDirectory,
    [string] $SourceCommit,
    [switch] $DeveloperBuild,
    [switch] $NoApplicationLaunch,
    [switch] $SystemServicesTests,
    [switch] $SkipTests
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output = [IO.Path]::GetFullPath($OutputDirectory)
$prefix = $repository.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ($output -eq $repository -or $output.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'launcher output must be outside the repository.'
}
$head = ((& git -C $repository rev-parse HEAD) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $head -cnotmatch '^[0-9a-f]{40}$') { throw 'could not resolve the source commit.' }
if (-not $SourceCommit) { $SourceCommit = $head }
if ($SourceCommit -cne $head) { throw 'the requested source commit differs from the checkout.' }
$status = ((& git -C $repository status --porcelain=v1 --untracked-files=all --ignore-submodules=none) -join "`n").Trim()
if ($LASTEXITCODE -ne 0) { throw 'could not inspect source status.' }
$submodules = ((& git -C $repository submodule foreach --recursive --quiet 'git status --porcelain=v1 --untracked-files=all') -join "`n").Trim()
if ($LASTEXITCODE -ne 0) { throw 'could not inspect submodule status.' }
if (-not $DeveloperBuild -and ($status -or $submodules)) { throw 'production launcher builds require a clean exact source tree and submodules.' }
if (-not $DeveloperBuild -and ($SkipTests -or $NoApplicationLaunch)) { throw 'production launcher builds require all contract and health checks.' }
$constants = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'native/core.h'))
$version = [regex]::Match($constants, 'LauncherVersion\[\] = "([0-9.]+)"').Groups[1].Value
$sequence = [long][regex]::Match($constants, 'LauncherSequence = ([0-9]+)').Groups[1].Value
if (-not $version -or $sequence -le 0) { throw 'native launcher release identity is missing.' }
function Read-Inputs {
    $files = @()
    foreach ($directory in @('launcher', 'src', 'tools', 'cmake', 'assets/fonts/noto-sans')) {
        $files += Get-ChildItem -LiteralPath (Join-Path $repository $directory) -File -Recurse |
            Where-Object { $_.FullName -notmatch '[\\/](obj|bin)[\\/]' }
    }
    $files += Get-Item -LiteralPath (Join-Path $repository 'LICENSE.md')
    return @($files | Sort-Object FullName -Unique | ForEach-Object {
        [ordered]@{ path = [IO.Path]::GetRelativePath($repository, $_.FullName).Replace('\','/'); bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$inputs = Read-Inputs
$inputText = $inputs | ConvertTo-Json -Depth 4 -Compress
$inputHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($inputText))).ToLowerInvariant()
$identity = if ($DeveloperBuild) { "$head-dirty-launcher-$($inputHash.Substring(0,12))" } else { $head }
New-Item -ItemType Directory -Path $output -Force | Out-Null
if ((Get-Item -LiteralPath $output).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'launcher output cannot be a filesystem link.' }
$marker = Join-Path $output '.uvsr-launcher-build-owner'
if (Test-Path -LiteralPath $marker) {
    if ([IO.File]::ReadAllText($marker) -cne $repository) { throw 'launcher output belongs to another source checkout.' }
} else {
    if (@(Get-ChildItem -LiteralPath $output -Force).Count) { throw 'launcher output must be empty or owned by this build.' }
    [IO.File]::WriteAllText($marker, $repository)
}
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $output 'build' }
$build = [IO.Path]::GetFullPath($BuildDirectory)
$production = if ($DeveloperBuild) { 'OFF' } else { 'ON' }
$binaryDirectory = Join-Path $build "bin/$($inputHash.Substring(0,12))"
& cmake -S $PSScriptRoot -B $build -G 'Visual Studio 17 2022' -A x64 "-DUVSR_LAUNCHER_SOURCE_IDENTITY=$identity" "-DUVSR_LAUNCHER_SOURCE_COMMIT=$head" "-DUVSR_LAUNCHER_PRODUCTION=$production" "-DUVSR_LAUNCHER_BINARY_DIRECTORY=$binaryDirectory" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'native launcher configuration failed.' }
$targets = @('uvsr-launcher')
if (-not $SkipTests -or -not $NoApplicationLaunch) { $targets += 'uvsr_launcher_tests' }
& cmake --build $build --config Release --parallel 2 --target @targets
if ($LASTEXITCODE -ne 0) { throw 'native launcher build failed.' }
$tests = Join-Path $build 'Release/uvsr_launcher_tests.exe'
if (-not $SkipTests) {
    & (Join-Path $PSScriptRoot 'tests/FeedTools.Tests.ps1')
    & $tests --pure
    if ($LASTEXITCODE -ne 0) { throw 'native launcher pure contracts failed.' }
    if (-not $NoApplicationLaunch) {
        & $tests --runtime
        if ($LASTEXITCODE -ne 0) { throw 'native launcher process contracts failed.' }
    }
    if (-not $DeveloperBuild -or $SystemServicesTests) {
        & $tests --system-services
        if ($LASTEXITCODE -ne 0) { throw 'native launcher registry and shell contracts failed.' }
    }
}
$built = Join-Path $binaryDirectory 'Release/uvsr-launcher.exe'
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($built)
if ($metadata.ProductName -cne 'UVSR Launcher' -or $metadata.FileVersion -cne "$version.0" -or $metadata.ProductVersion -cne "$version+$identity") {
    throw 'native launcher metadata does not bind the source input record.'
}
if (-not $NoApplicationLaunch) {
    & $tests --verify-launcher-health $built
    if ($LASTEXITCODE -ne 0) { throw 'native launcher executable health failed.' }
}
$after = Read-Inputs | ConvertTo-Json -Depth 4 -Compress
if ($inputText -cne $after) { throw 'launcher source inputs changed during the build.' }
$artifact = Join-Path $output 'uvsr-launcher.exe'
Copy-Item -LiteralPath $built -Destination $artifact -Force
$hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $output 'uvsr-launcher.exe.sha256'), "$hash  uvsr-launcher.exe`n", [Text.UTF8Encoding]::new($false))
$record = [ordered]@{ schemaVersion = 1; sourceRoot = $repository; sourceCommit = $head; sourceIdentity = $identity
    production = -not [bool]$DeveloperBuild; inputSha256 = $inputHash; inputs = $inputs; buildDirectory = $build
    version = $version; releaseSequence = $sequence; artifact = $artifact; sha256 = $hash
    pureTests = -not [bool]$SkipTests; runtimeTests = -not ([bool]$SkipTests -or [bool]$NoApplicationLaunch)
    systemServicesTests = -not [bool]$SkipTests -and (-not [bool]$DeveloperBuild -or [bool]$SystemServicesTests)
    applicationHealth = -not [bool]$NoApplicationLaunch }
[IO.File]::WriteAllText((Join-Path $output 'build-record.json'), ($record | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
Write-Output "launcher: $artifact"
Write-Output "SHA-256: $hash"
