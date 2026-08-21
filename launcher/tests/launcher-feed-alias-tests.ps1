[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$verifierSource = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\verify-launcher-feed-alias.ps1'))
if (-not [IO.File]::Exists($verifierSource)) {
    throw "The launcher feed alias verifier is missing at '$verifierSource'."
}

$temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$temporaryRoot = Join-Path $temporaryParent `
    "uvsr-launcher-feed-alias-tests-$([Guid]::NewGuid().ToString('N'))"
$temporaryPrefix = $temporaryParent + [IO.Path]::DirectorySeparatorChar
if (-not $temporaryRoot.StartsWith(
        $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not [IO.Path]::GetFileName($temporaryRoot).StartsWith(
        'uvsr-launcher-feed-alias-tests-', [StringComparison]::Ordinal)) {
    throw 'The launcher feed alias test root escaped the temporary directory.'
}

function New-FeedFixture {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [byte[]] $CanonicalBytes,
        [byte[]] $LegacyBytes,
        [switch] $OmitCanonical,
        [switch] $OmitLegacy
    )

    $repository = Join-Path (Join-Path $temporaryRoot $Name) 'repository'
    $launcher = Join-Path $repository 'launcher'
    $installer = Join-Path $repository 'installer'
    [IO.Directory]::CreateDirectory($launcher) | Out-Null
    [IO.Directory]::CreateDirectory($installer) | Out-Null
    [IO.File]::Copy($verifierSource,
        (Join-Path $launcher 'verify-launcher-feed-alias.ps1'), $true)
    if (-not $OmitCanonical) {
        [IO.File]::WriteAllBytes(
            (Join-Path $launcher 'launcher-feed-v1.json'), $CanonicalBytes)
    }
    if (-not $OmitLegacy) {
        [IO.File]::WriteAllBytes(
            (Join-Path $installer 'launcher-feed-v1.json'), $LegacyBytes)
    }
    return $repository
}

function Invoke-FeedVerifier {
    param([Parameter(Mandatory)] [string] $Repository)

    $powerShell = (Get-Process -Id $PID).Path
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $powerShell
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File',
            (Join-Path $Repository 'launcher\verify-launcher-feed-alias.ps1'))) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw 'The launcher feed alias verifier test process did not start.'
        }
        $standardOutput = $process.StandardOutput.ReadToEnd()
        $standardError = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Detail = ($standardOutput + "`n" + $standardError).Trim()
        }
    }
    finally {
        $process.Dispose()
    }
}

function Assert-VerifierResult {
    param(
        [Parameter(Mandatory)] [pscustomobject] $Result,
        [Parameter(Mandatory)] [bool] $ShouldSucceed,
        [string] $ExpectedDetail
    )

    if (($Result.ExitCode -eq 0) -ne $ShouldSucceed) {
        throw "Unexpected launcher feed alias verifier result $($Result.ExitCode) with expected success '$ShouldSucceed' for '$ExpectedDetail': $($Result.Detail)"
    }
    if (-not [string]::IsNullOrEmpty($ExpectedDetail) -and
        -not $Result.Detail.Contains(
            $ExpectedDetail, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Launcher feed alias verifier output did not contain '$ExpectedDetail': $($Result.Detail)"
    }
}

$testError = $null
$junctionPath = $null
try {
    [IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
    $same = [Text.Encoding]::UTF8.GetBytes('{"schemaVersion":1}')
    Assert-VerifierResult -ShouldSucceed $true -ExpectedDetail 'verified' `
        -Result (Invoke-FeedVerifier (New-FeedFixture 'valid' $same $same))

    Assert-VerifierResult -ShouldSucceed $false -ExpectedDetail 'missing' `
        -Result (Invoke-FeedVerifier (New-FeedFixture -Name 'missing' `
            -CanonicalBytes $same -LegacyBytes $same -OmitLegacy))

    $lengthRepository = New-FeedFixture 'length' $same `
        ([Text.Encoding]::UTF8.GetBytes('{}'))
    Assert-VerifierResult -ShouldSucceed $false -ExpectedDetail 'length' `
        -Result (Invoke-FeedVerifier $lengthRepository)

    $hashRepository = New-FeedFixture 'hash' `
        ([Text.Encoding]::UTF8.GetBytes('abc')) `
        ([Text.Encoding]::UTF8.GetBytes('abd'))
    Assert-VerifierResult -ShouldSucceed $false -ExpectedDetail 'SHA-256' `
        -Result (Invoke-FeedVerifier $hashRepository)

    $nonregularRepository = New-FeedFixture -Name 'nonregular' `
        -CanonicalBytes $same -LegacyBytes $same -OmitCanonical
    [IO.Directory]::CreateDirectory(
        (Join-Path $nonregularRepository 'launcher\launcher-feed-v1.json')) |
        Out-Null
    Assert-VerifierResult -ShouldSucceed $false -ExpectedDetail 'regular file' `
        -Result (Invoke-FeedVerifier $nonregularRepository)

    $junctionScenario = Join-Path $temporaryRoot 'reparse'
    $junctionRepository = Join-Path $junctionScenario 'repository'
    $junctionLauncher = Join-Path $junctionRepository 'launcher'
    $junctionTarget = Join-Path $junctionScenario 'external-installer'
    [IO.Directory]::CreateDirectory($junctionLauncher) | Out-Null
    [IO.Directory]::CreateDirectory($junctionTarget) | Out-Null
    [IO.File]::Copy($verifierSource,
        (Join-Path $junctionLauncher 'verify-launcher-feed-alias.ps1'), $true)
    [IO.File]::WriteAllBytes(
        (Join-Path $junctionLauncher 'launcher-feed-v1.json'), $same)
    [IO.File]::WriteAllBytes(
        (Join-Path $junctionTarget 'launcher-feed-v1.json'), $same)
    $sentinel = Join-Path $junctionTarget 'sentinel.user'
    [IO.File]::WriteAllText($sentinel, 'preserve')
    $junctionPath = Join-Path $junctionRepository 'installer'
    New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget |
        Out-Null
    Assert-VerifierResult -ShouldSucceed $false -ExpectedDetail 'filesystem link' `
        -Result (Invoke-FeedVerifier $junctionRepository)
    if ([IO.File]::ReadAllText($sentinel) -ne 'preserve') {
        throw 'The launcher feed alias verifier changed the reparse target.'
    }

    Write-Output 'Launcher feed alias verifier tests passed.'
}
catch {
    $testError = $_
}
finally {
    if ($null -ne $junctionPath -and [IO.Directory]::Exists($junctionPath)) {
        [IO.Directory]::Delete($junctionPath)
    }
    if ([IO.Directory]::Exists($temporaryRoot)) {
        $attributes = [IO.File]::GetAttributes($temporaryRoot)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Refusing to remove a redirected launcher feed alias test root.'
        }
        [IO.Directory]::Delete($temporaryRoot, $true)
    }
}
if ($null -ne $testError) {
    throw $testError
}
