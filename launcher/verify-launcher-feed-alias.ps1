[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd(
    [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$canonicalPath = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot 'launcher-feed-v1.json'))
$legacyPath = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'installer\launcher-feed-v1.json'))

function Assert-OrdinaryFeedFile {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Description
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPrefix = $repositoryRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description escaped the repository root."
    }

    $relativePath = $fullPath.Substring($rootPrefix.Length).Replace(
        [IO.Path]::AltDirectorySeparatorChar,
        [IO.Path]::DirectorySeparatorChar)
    $currentPath = $repositoryRoot
    $components = $relativePath.Split(
        [IO.Path]::DirectorySeparatorChar,
        [StringSplitOptions]::RemoveEmptyEntries)
    if ($components.Count -eq 0) {
        throw "$Description is not a repository file."
    }

    $pathsToCheck = @($repositoryRoot)
    foreach ($component in $components) {
        $currentPath = Join-Path $currentPath $component
        $pathsToCheck += $currentPath
    }
    for ($index = 0; $index -lt $pathsToCheck.Count; $index++) {
        $candidate = $pathsToCheck[$index]
        if (-not [IO.File]::Exists($candidate) -and
            -not [IO.Directory]::Exists($candidate)) {
            throw "$Description is missing at '$fullPath'."
        }
        $attributes = [IO.File]::GetAttributes($candidate)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description is redirected by a filesystem link at '$candidate'."
        }
        $isLeaf = $index -eq ($pathsToCheck.Count - 1)
        if (-not $isLeaf -and
            ($attributes -band [IO.FileAttributes]::Directory) -eq 0) {
            throw "$Description has a non-directory path component at '$candidate'."
        }
    }

    $leafAttributes = [IO.File]::GetAttributes($fullPath)
    $nonRegularAttributes = [IO.FileAttributes]::Directory -bor
        [IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Device
    if (-not [IO.File]::Exists($fullPath) -or
        ($leafAttributes -band $nonRegularAttributes) -ne 0) {
        throw "$Description is not a regular file at '$fullPath'."
    }
}

Assert-OrdinaryFeedFile -Path $canonicalPath `
    -Description 'The canonical launcher feed'
Assert-OrdinaryFeedFile -Path $legacyPath `
    -Description 'The legacy launcher feed compatibility alias'

$canonicalStream = [IO.FileStream]::new($canonicalPath, [IO.FileMode]::Open,
    [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $legacyStream = [IO.FileStream]::new($legacyPath, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($canonicalStream.Length -ne $legacyStream.Length) {
            throw "The legacy launcher feed compatibility alias has length $($legacyStream.Length), but the canonical launcher feed has length $($canonicalStream.Length)."
        }

        $canonicalHash = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($canonicalStream))
        $legacyHash = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($legacyStream))
        if ($canonicalHash -ne $legacyHash) {
            throw "The legacy launcher feed compatibility alias SHA-256 does not match the canonical launcher feed."
        }

        Write-Output "Launcher feed compatibility alias verified: $($canonicalStream.Length) bytes, SHA-256 $($canonicalHash.ToLowerInvariant())."
    }
    finally {
        $legacyStream.Dispose()
    }
}
finally {
    $canonicalStream.Dispose()
}
