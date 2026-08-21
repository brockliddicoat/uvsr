[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$generatorPath = Join-Path $repositoryRoot `
    'launcher\generate-renderer-source-bridge.ps1'
$bridgeSourcePath = Join-Path $repositoryRoot `
    'launcher\src\UVSR.Installer\RendererSourceBridge.cs'
$bridgePatchPath = Join-Path $repositoryRoot `
    'launcher\src\UVSR.Installer\RendererBridges\uvsr-public-main-0c807484.patch'
$expectedIdentity =
    '24581 bytes, SHA-256 e68f814e2e838ef08bf8561bfa033dbaa0b5a523f776983dca18e8dd83ad799a, ' +
    'tree 736fc012878dc66e5f512dab40d722a56ac8c1f5, ' +
    'commit ac19135e176bd137df050fb3da11297a2460312d'

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [Parameter(Mandatory)] [string] $Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $WorkingDirectory
    )

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($key in @($start.Environment.Keys)) {
        if ($key.StartsWith('GIT_', [StringComparison]::OrdinalIgnoreCase)) {
            $null = $start.Environment.Remove($key)
        }
    }
    foreach ($argument in $Arguments) {
        $null = $start.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::Start($start)
    try {
        $standardOutput = $process.StandardOutput.ReadToEnd()
        $standardError = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StandardOutput = $standardOutput
            StandardError = $standardError
        }
    }
    finally {
        $process.Dispose()
    }
}

function Remove-OwnedTestRoot {
    param([Parameter(Mandatory)] [string] $Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $separators = [char[]]@(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar)
    $temporaryParent = [IO.Path]::GetFullPath(
        [IO.Path]::GetTempPath()).TrimEnd($separators)
    $actualParent = [IO.Path]::GetFullPath(
        [IO.Path]::GetDirectoryName($fullPath)).TrimEnd($separators)
    $leaf = [IO.Path]::GetFileName($fullPath)
    if (-not [string]::Equals($actualParent, $temporaryParent,
            [StringComparison]::OrdinalIgnoreCase) -or
        $leaf -cnotmatch '^uvsr-renderer-bridge-verifier-[0-9a-f]{32}$') {
        throw "Refusing to remove unexpected verifier test path '$fullPath'."
    }
    if ([IO.Directory]::Exists($fullPath)) {
        [IO.Directory]::Delete($fullPath, $true)
    }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    "uvsr-renderer-bridge-verifier-$([Guid]::NewGuid().ToString('N'))"
$sandboxRoot = Join-Path $testRoot 'repository'
$testError = $null
try {
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null
    $git = (Get-Command git -ErrorAction Stop).Source
    $clone = Invoke-CapturedProcess -FilePath $git -WorkingDirectory $testRoot `
        -Arguments @('clone', '--quiet', '--shared', '--no-checkout',
            $repositoryRoot, $sandboxRoot)
    Assert-True ($clone.ExitCode -eq 0) `
        "Verifier test repository creation failed: $($clone.StandardError)"

    $sandboxLauncher = Join-Path $sandboxRoot 'launcher'
    $sandboxBridgeDirectory = Join-Path $sandboxLauncher `
        'src\UVSR.Installer\RendererBridges'
    [IO.Directory]::CreateDirectory($sandboxBridgeDirectory) | Out-Null
    [IO.File]::Copy($generatorPath,
        (Join-Path $sandboxLauncher 'generate-renderer-source-bridge.ps1'))
    [IO.File]::Copy($bridgeSourcePath,
        (Join-Path $sandboxLauncher 'src\UVSR.Installer\RendererSourceBridge.cs'))
    $sandboxPatch = Join-Path $sandboxBridgeDirectory `
        'uvsr-public-main-0c807484.patch'
    [IO.File]::Copy($bridgePatchPath, $sandboxPatch)

    [IO.Directory]::CreateDirectory((Join-Path $sandboxRoot 'src')) | Out-Null
    $cmakeSentinel = 'live CMake input must not define the frozen bridge'
    $sourceSentinel = 'live renderer input must not define the frozen bridge'
    [IO.File]::WriteAllText(
        (Join-Path $sandboxRoot 'CMakeLists.txt'), $cmakeSentinel)
    [IO.File]::WriteAllText(
        (Join-Path $sandboxRoot 'src\uvsr.cpp'), $sourceSentinel)

    $powerShell = (Get-Process -Id $PID).Path
    $sandboxGenerator = Join-Path $sandboxLauncher `
        'generate-renderer-source-bridge.ps1'
    $verified = Invoke-CapturedProcess -FilePath $powerShell `
        -WorkingDirectory $sandboxRoot -Arguments @('-NoLogo', '-NoProfile',
            '-NonInteractive', '-File', $sandboxGenerator, '-Check')
    Assert-True ($verified.ExitCode -eq 0) `
        "Frozen bridge verification failed: $($verified.StandardError)"
    Assert-True ($verified.StandardOutput.Contains(
            "Renderer bridge is current: $expectedIdentity.")) `
        "Frozen bridge verifier did not report its exact identity: $($verified.StandardOutput)"
    Assert-True ([IO.File]::ReadAllText(
            (Join-Path $sandboxRoot 'CMakeLists.txt')) -ceq $cmakeSentinel) `
        'Frozen bridge verification changed the live CMake input.'
    Assert-True ([IO.File]::ReadAllText(
            (Join-Path $sandboxRoot 'src\uvsr.cpp')) -ceq $sourceSentinel) `
        'Frozen bridge verification changed the live renderer input.'

    $tampered = [IO.File]::ReadAllBytes($sandboxPatch)
    $tampered[0] = $tampered[0] -bxor 1
    [IO.File]::WriteAllBytes($sandboxPatch, $tampered)
    $rejected = Invoke-CapturedProcess -FilePath $powerShell `
        -WorkingDirectory $sandboxRoot -Arguments @('-NoLogo', '-NoProfile',
            '-NonInteractive', '-File', $sandboxGenerator, '-Check')
    $rejectionText = $rejected.StandardOutput + $rejected.StandardError
    Assert-True ($rejected.ExitCode -ne 0) `
        'Frozen bridge verifier accepted a modified embedded patch.'
    Assert-True ($rejectionText.Contains('Embedded renderer bridge was') -and
            $rejectionText.Contains('SHA-256')) `
        "Frozen bridge verifier reported the wrong rejection: $rejectionText"

    Write-Output 'Renderer source bridge verifier tests passed.'
}
catch {
    $testError = $_
}
finally {
    try {
        Remove-OwnedTestRoot -Path $testRoot
    }
    catch {
        if ($null -eq $testError) {
            throw
        }
        Write-Error ("Verifier test cleanup also failed: " +
            $_.Exception.GetBaseException().Message) -ErrorAction Continue
    }
}
if ($null -ne $testError) {
    throw $testError
}
