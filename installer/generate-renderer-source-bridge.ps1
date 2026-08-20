[CmdletBinding()]
param(
    [switch] $Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bridgeOutput = Join-Path $PSScriptRoot `
    'src\UVSR.Installer\RendererBridges\uvsr-public-main-0c807484.patch'
$publicBaseCommit = '0c8074848985152ed83f83b4087aaf10013de590'
$publicBaseTree = 'a9ff0276ad295675239839e37124b75dfc5334d7'
$expectedTree = '736fc012878dc66e5f512dab40d722a56ac8c1f5'
$expectedCommit = 'ac19135e176bd137df050fb3da11297a2460312d'
$expectedSize = 24581L
$expectedSha256 = 'e68f814e2e838ef08bf8561bfa033dbaa0b5a523f776983dca18e8dd83ad799a'
$commitMessage = 'uvsr launcher renderer bridge v1'
$commitIdentityName = 'UVSR Launcher'
$commitIdentityEmail = 'launcher@uvsr.local'
$commitIdentityDate = '2026-08-20T00:00:00Z'
$bridgePaths = @(
    'CMakeLists.txt'
    'cmake/VerifyD3D12Runtime.cmake'
    'cmake/uvsr-launcher-build-contract-v1.json'
    'overrides/nvrhi-stable-directx-headers.patch'
    'src/d3d12_agility_exports.cpp'
    'src/gpu_capabilities.h'
    'src/windows_executable_path.h'
    'src/uvsr.cpp'
    'tests/gpu_capabilities_tests.cpp'
    'tests/windows_executable_path_tests.cpp'
)
$expectedBlobs = [ordered]@{
    'CMakeLists.txt' = 'bf37c7807f7db7b4aaa9cc7e89cd0998690ce027'
    'cmake/VerifyD3D12Runtime.cmake' = 'f48564e44f5d1ceb36beed72ee38f5b482958942'
    'cmake/uvsr-launcher-build-contract-v1.json' = '09e963b654d55d8cc2e7cf4bca8f255faa9dccaa'
    'overrides/nvrhi-stable-directx-headers.patch' = '4feec45339f8bb45aff9c82fe46ebc8b9c089e7b'
    'src/d3d12_agility_exports.cpp' = '7d00c7651fc428d34385e2f34e27c4300c727726'
    'src/gpu_capabilities.h' = 'd2f4aac53709713ba3ee2b8938812b331eebe000'
    'src/uvsr.cpp' = '3c5eb7daf2cf057db1026ec115cdc03c9c618aab'
    'src/windows_executable_path.h' = '105ad08dba865b288d02a9849fe22d88bb352dff'
    'tests/gpu_capabilities_tests.cpp' = '49c454698c816e72605a02e630aee0e568292e79'
    'tests/windows_executable_path_tests.cpp' = '4406d0076e2e4d4b08811352e9dd30fb790f0a6e'
}

function Invoke-GitText {
    param(
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $IndexPath,
        [string] $ObjectDirectory,
        [string] $AlternateObjects,
        [hashtable] $AdditionalEnvironment
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $installedGit = Join-Path $env:LOCALAPPDATA `
        'Programs\UVSR\tools\git\cmd\git.exe'
    $start.FileName = if (Test-Path -LiteralPath $installedGit -PathType Leaf) {
        $installedGit
    }
    else {
        (Get-Command git -ErrorAction Stop).Source
    }
    $start.WorkingDirectory = $repositoryRoot
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
    $start.Environment['GIT_INDEX_FILE'] = $IndexPath
    $start.Environment['GIT_CONFIG_NOSYSTEM'] = '1'
    $start.Environment['GIT_CONFIG_GLOBAL'] = 'NUL'
    $start.Environment['GIT_ATTR_NOSYSTEM'] = '1'
    $start.Environment['GIT_NO_REPLACE_OBJECTS'] = '1'
    $start.Environment['LC_ALL'] = 'C'
    $start.Environment['LANG'] = 'C'
    if (-not [string]::IsNullOrWhiteSpace($ObjectDirectory)) {
        $start.Environment['GIT_OBJECT_DIRECTORY'] = $ObjectDirectory
    }
    if (-not [string]::IsNullOrWhiteSpace($AlternateObjects)) {
        $start.Environment['GIT_ALTERNATE_OBJECT_DIRECTORIES'] = $AlternateObjects
    }
    if ($null -ne $AdditionalEnvironment) {
        foreach ($entry in $AdditionalEnvironment.GetEnumerator()) {
            $start.Environment[[string]$entry.Key] = [string]$entry.Value
        }
    }
    foreach ($argument in @('-c', 'core.autocrlf=true', '-c', 'core.safecrlf=false',
             '-c', 'core.attributesFile=NUL', '-c', 'color.ui=false') +
             $Arguments) {
        $null = $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $output = $process.StandardOutput.ReadToEnd()
        $errorOutput = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw "Git failed with exit code $($process.ExitCode): $errorOutput"
        }
        return $output
    }
    finally {
        $process.Dispose()
    }
}

function Read-CSharpStringConstant {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Name
    )
    $pattern = 'internal\s+const\s+string\s+' + [regex]::Escape($Name) +
        '\s*=\s*"(?<value>[^"]*)"\s*;'
    $match = [regex]::Match($Text, $pattern,
        [Text.RegularExpressions.RegexOptions]::CultureInvariant)
    if (-not $match.Success) {
        throw "RendererSourceBridge.cs is missing string constant '$Name'."
    }
    return $match.Groups['value'].Value
}

function Read-CSharpLongConstant {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Name
    )
    $pattern = 'internal\s+const\s+long\s+' + [regex]::Escape($Name) +
        '\s*=\s*(?<value>[0-9_]+)\s*;'
    $match = [regex]::Match($Text, $pattern,
        [Text.RegularExpressions.RegexOptions]::CultureInvariant)
    if (-not $match.Success) {
        throw "RendererSourceBridge.cs is missing integer constant '$Name'."
    }
    return [long]::Parse($match.Groups['value'].Value.Replace('_', ''),
        [Globalization.CultureInfo]::InvariantCulture)
}

function New-RendererBridgeTemporaryRoot {
    $path = Join-Path ([IO.Path]::GetTempPath()) `
        "uvsr-renderer-bridge-$([Guid]::NewGuid().ToString('N'))"
    [IO.Directory]::CreateDirectory($path) | Out-Null
    return [IO.Path]::GetFullPath($path)
}

function Assert-RendererBridgeTemporaryRoot {
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
        $leaf -cnotmatch '^uvsr-renderer-bridge-[0-9a-f]{32}$') {
        throw "Refusing to remove unexpected renderer bridge path '$fullPath'."
    }
    return $fullPath
}

function Clear-RendererBridgeTemporaryAttributes {
    param([Parameter(Mandatory)] [string] $Path)

    $rootAttributes = [IO.File]::GetAttributes($Path)
    if (($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to traverse reparse point '$Path' during renderer bridge cleanup."
    }
    $directories = [Collections.Generic.Stack[string]]::new()
    $entries = [Collections.Generic.List[string]]::new()
    $directories.Push($Path)
    while ($directories.Count -gt 0) {
        $directory = $directories.Pop()
        foreach ($entry in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
            $attributes = [IO.File]::GetAttributes($entry)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing to traverse reparse point '$entry' during renderer bridge cleanup."
            }
            $entries.Add($entry)
            if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                $directories.Push($entry)
            }
        }
    }

    $clearMask = [int]([IO.FileAttributes]::ReadOnly -bor
        [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)
    foreach ($entry in ($entries | Sort-Object Length -Descending)) {
        $attributes = [IO.File]::GetAttributes($entry)
        $updated = [IO.FileAttributes]([int]$attributes -band (-bnot $clearMask))
        if ($updated -ne $attributes) {
            [IO.File]::SetAttributes($entry, $updated)
        }
    }
    $rootAttributes = [IO.File]::GetAttributes($Path)
    $updatedRoot = [IO.FileAttributes]([int]$rootAttributes -band (-bnot $clearMask))
    if ($updatedRoot -ne $rootAttributes) {
        [IO.File]::SetAttributes($Path, $updatedRoot)
    }
}

function Remove-RendererBridgeTemporaryRoot {
    param([Parameter(Mandatory)] [string] $Path)

    $ownedPath = Assert-RendererBridgeTemporaryRoot -Path $Path
    $retryDelaysMilliseconds = @(0, 100, 250, 500, 1000, 2000)
    for ($attempt = 0; $attempt -lt $retryDelaysMilliseconds.Count; $attempt++) {
        if (-not [IO.Directory]::Exists($ownedPath)) {
            return
        }
        if ($retryDelaysMilliseconds[$attempt] -gt 0) {
            Start-Sleep -Milliseconds $retryDelaysMilliseconds[$attempt]
        }
        try {
            Clear-RendererBridgeTemporaryAttributes -Path $ownedPath
            [IO.Directory]::Delete($ownedPath, $true)
            return
        }
        catch {
            $cause = $_.Exception.GetBaseException()
            $retryable = $cause -is [IO.IOException] -or
                $cause -is [UnauthorizedAccessException]
            if (-not $retryable -or
                $attempt -eq $retryDelaysMilliseconds.Count - 1) {
                throw
            }
        }
    }
}

function Test-RendererBridgeTemporaryCleanup {
    $probeRoot = New-RendererBridgeTemporaryRoot
    $fanout = Join-Path $probeRoot 'objects\aa'
    [IO.Directory]::CreateDirectory($fanout) | Out-Null
    $looseObject = Join-Path $fanout '0123456789abcdef'
    [IO.File]::WriteAllBytes($looseObject, [byte[]](1, 2, 3))
    [IO.File]::SetAttributes($looseObject,
        [IO.File]::GetAttributes($looseObject) -bor [IO.FileAttributes]::ReadOnly)
    [IO.File]::SetAttributes($fanout,
        [IO.File]::GetAttributes($fanout) -bor [IO.FileAttributes]::ReadOnly)
    [IO.File]::SetAttributes($probeRoot,
        [IO.File]::GetAttributes($probeRoot) -bor [IO.FileAttributes]::ReadOnly)
    Remove-RendererBridgeTemporaryRoot -Path $probeRoot
    if ([IO.Directory]::Exists($probeRoot)) {
        throw "Renderer bridge cleanup regression left '$probeRoot' behind."
    }
}

function Test-RendererBridgeRootReparseRejection {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        return
    }

    $externalRoot = Join-Path ([IO.Path]::GetTempPath()) `
        "uvsr-renderer-bridge-external-$([Guid]::NewGuid().ToString('N'))"
    $linkRoot = New-RendererBridgeTemporaryRoot
    [IO.Directory]::Delete($linkRoot)
    [IO.Directory]::CreateDirectory($externalRoot) | Out-Null
    $sentinel = Join-Path $externalRoot 'sentinel.bin'
    [IO.File]::WriteAllBytes($sentinel, [byte[]](4, 5, 6))
    [IO.File]::SetAttributes($sentinel,
        [IO.File]::GetAttributes($sentinel) -bor [IO.FileAttributes]::ReadOnly)
    try {
        New-Item -ItemType Junction -Path $linkRoot -Target $externalRoot |
            Out-Null
        $rejected = $false
        try {
            Remove-RendererBridgeTemporaryRoot -Path $linkRoot
        }
        catch {
            if ($_.Exception.GetBaseException().Message -notlike
                    'Refusing to traverse reparse point*') {
                throw
            }
            $rejected = $true
        }
        if (-not $rejected -or -not [IO.File]::Exists($sentinel) -or
            ([IO.File]::GetAttributes($sentinel) -band
                [IO.FileAttributes]::ReadOnly) -eq 0) {
            throw 'Renderer bridge cleanup did not safely reject a root reparse point.'
        }
    }
    finally {
        if ([IO.Directory]::Exists($linkRoot)) {
            [IO.Directory]::Delete($linkRoot)
        }
        if ([IO.File]::Exists($sentinel)) {
            [IO.File]::SetAttributes($sentinel, [IO.FileAttributes]::Normal)
        }
        if ([IO.Directory]::Exists($externalRoot)) {
            [IO.Directory]::Delete($externalRoot, $true)
        }
    }
}

Test-RendererBridgeTemporaryCleanup
Test-RendererBridgeRootReparseRejection
$temporaryRoot = New-RendererBridgeTemporaryRoot
$temporaryIndex = Join-Path $temporaryRoot 'index'
$generationError = $null
try {
    $bridgeSourcePath = Join-Path $PSScriptRoot `
        'src\UVSR.Installer\RendererSourceBridge.cs'
    $bridgeSource = [IO.File]::ReadAllText($bridgeSourcePath)
    $expectedConstants = [ordered]@{
        PublicBaseCommit = $publicBaseCommit
        PublicBaseTree = $publicBaseTree
        SourceCommit = $expectedCommit
        SourceTree = $expectedTree
        PatchSha256 = $expectedSha256
        CommitMessage = $commitMessage
        CommitIdentityName = $commitIdentityName
        CommitIdentityEmail = $commitIdentityEmail
        CommitIdentityDate = $commitIdentityDate
    }
    foreach ($entry in $expectedConstants.GetEnumerator()) {
        $actual = Read-CSharpStringConstant -Text $bridgeSource -Name $entry.Key
        if ($actual -cne $entry.Value) {
            throw "RendererSourceBridge.cs constant '$($entry.Key)' was '$actual'; expected '$($entry.Value)'."
        }
    }
    if ((Read-CSharpLongConstant -Text $bridgeSource -Name 'PatchSize') -ne
        $expectedSize) {
        throw "RendererSourceBridge.cs PatchSize does not match $expectedSize."
    }
    foreach ($entry in $expectedBlobs.GetEnumerator()) {
        $blobPattern = '\["' + [regex]::Escape([string]$entry.Key) +
            '"\]\s*=\s*"' + [regex]::Escape([string]$entry.Value) + '"'
        if (-not [regex]::IsMatch($bridgeSource, $blobPattern,
                [Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
            throw "RendererSourceBridge.cs is missing exact blob '$($entry.Key)' = '$($entry.Value)'."
        }
    }

    $objectsText = (Invoke-GitText -Arguments @('rev-parse', '--git-path', 'objects') `
        -IndexPath $temporaryIndex).Trim()
    $repositoryObjects = if ([IO.Path]::IsPathRooted($objectsText)) {
        [IO.Path]::GetFullPath($objectsText)
    }
    else {
        [IO.Path]::GetFullPath((Join-Path $repositoryRoot $objectsText))
    }
    if (-not (Test-Path -LiteralPath $repositoryObjects -PathType Container)) {
        throw "Git object storage was not found at '$repositoryObjects'."
    }
    $temporaryObjects = Join-Path $temporaryRoot 'objects'
    [IO.Directory]::CreateDirectory($temporaryObjects) | Out-Null
    $gitStorage = @{
        IndexPath = $temporaryIndex
        ObjectDirectory = $temporaryObjects
        AlternateObjects = $repositoryObjects
    }

    $baseTree = (Invoke-GitText -Arguments @('rev-parse', "$publicBaseCommit`^{tree}") `
        @gitStorage).Trim()
    if ($baseTree -ne $publicBaseTree) {
        throw "Renderer bridge base tree was $baseTree; expected $publicBaseTree."
    }

    $null = Invoke-GitText -Arguments @('read-tree', $publicBaseCommit) `
        @gitStorage
    $null = Invoke-GitText -Arguments (@('add', '--') + $bridgePaths) `
        @gitStorage

    $stagedPaths = (Invoke-GitText -Arguments `
        (@('diff', '--cached', '--name-only', '--no-renames', $publicBaseCommit, '--') +
         $bridgePaths) @gitStorage).Split("`n",
            [StringSplitOptions]::RemoveEmptyEntries)
    $expectedStagedPaths = [Collections.Generic.HashSet[string]]::new(
        [string[]]$bridgePaths, [StringComparer]::Ordinal)
    if ($stagedPaths.Count -ne $bridgePaths.Count -or
        -not $expectedStagedPaths.SetEquals($stagedPaths)) {
        throw "Renderer bridge staged an unexpected path set: $($stagedPaths -join ', ')."
    }

    $tree = (Invoke-GitText -Arguments @('write-tree') `
        @gitStorage).Trim()
    if ($tree -ne $expectedTree) {
        throw "Renderer bridge result tree was $tree; expected $expectedTree."
    }

    $commitEnvironment = @{
        GIT_AUTHOR_NAME = $commitIdentityName
        GIT_AUTHOR_EMAIL = $commitIdentityEmail
        GIT_AUTHOR_DATE = $commitIdentityDate
        GIT_COMMITTER_NAME = $commitIdentityName
        GIT_COMMITTER_EMAIL = $commitIdentityEmail
        GIT_COMMITTER_DATE = $commitIdentityDate
    }
    $commit = (Invoke-GitText -Arguments @('commit-tree', $expectedTree, '-p',
            $publicBaseCommit, '-m', $commitMessage) @gitStorage `
        -AdditionalEnvironment $commitEnvironment).Trim()
    if ($commit -ne $expectedCommit) {
        throw "Renderer bridge synthetic commit was $commit; expected $expectedCommit."
    }

    $patchText = Invoke-GitText -Arguments `
        (@('diff', '--cached', '--binary', '--full-index', '--no-renames',
           '--no-ext-diff', '--no-textconv', '--no-color', '--src-prefix=a/',
           '--dst-prefix=b/', $publicBaseCommit, '--') +
         $bridgePaths) `
        @gitStorage
    $patchBytes = [Text.UTF8Encoding]::new($false).GetBytes($patchText)
    $patchHash = [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($patchBytes)).ToLowerInvariant()
    if ($patchBytes.LongLength -ne $expectedSize -or
        $patchHash -ne $expectedSha256) {
        throw "Renderer bridge patch was $($patchBytes.LongLength) bytes SHA-256 " +
              "$patchHash; expected $expectedSize bytes SHA-256 $expectedSha256."
    }

    if ($Check) {
        if (-not [IO.File]::Exists($bridgeOutput)) {
            throw "Embedded renderer bridge is missing at '$bridgeOutput'."
        }
        $existing = [IO.File]::ReadAllBytes($bridgeOutput)
        if (-not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals(
                $patchBytes, $existing)) {
            throw "Embedded renderer bridge is stale. Run installer/generate-renderer-source-bridge.ps1."
        }
        Write-Output "Renderer bridge is current: $expectedSize bytes, SHA-256 $expectedSha256, tree $expectedTree, commit $expectedCommit."
    }
    else {
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($bridgeOutput)) |
            Out-Null
        [IO.File]::WriteAllBytes($bridgeOutput, $patchBytes)
        Write-Output "Generated $bridgeOutput"
        Write-Output "Renderer bridge: $expectedSize bytes, SHA-256 $expectedSha256, tree $expectedTree."
    }
}
catch {
    $generationError = $_
}
finally {
    try {
        Remove-RendererBridgeTemporaryRoot -Path $temporaryRoot
    }
    catch {
        if ($null -eq $generationError) {
            throw
        }
        Write-Error ("Renderer bridge cleanup also failed: " +
            $_.Exception.GetBaseException().Message) -ErrorAction Continue
    }
}
if ($null -ne $generationError) {
    throw $generationError
}
