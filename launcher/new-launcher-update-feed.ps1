[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $PrivateKeyPemPath,
    [Parameter(Mandatory)] [string] $ArtifactPath,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [long] $ReleaseSequence,
    [Parameter(Mandatory)] [string] $SourceCommit,
    [string] $OutputPath,
    [switch] $Force,
    [switch] $AllowTestKey,
    [string] $TestKeyId,
    [string] $TestPublicKeySpkiBase64
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'FeedTools.ps1')
$trust = Resolve-FeedTrust ([bool]$AllowTestKey) $TestKeyId $TestPublicKeySpkiBase64
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot 'launcher-update-feed-v2.json'
}
if (-not (Test-FeedVersion $Version 3 2147483647) -or
    $ReleaseSequence -lt 1 -or $ReleaseSequence -gt $feedSequenceLimit -or
    $SourceCommit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'Launcher release identity is not canonical.'
}

function Read-PeX64CertificateTable {
    param([Parameter(Mandatory)] [string] $Path)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 0x100 -or $reader.ReadUInt16() -ne 0x5A4D) {
            throw 'The launcher artifact is not a Windows PE executable.'
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt $stream.Length - 24) {
            throw 'The launcher artifact has an invalid PE header offset.'
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550 -or
            $reader.ReadUInt16() -ne 0x8664) {
            throw 'The launcher artifact is not a Windows x64 executable.'
        }
        $stream.Position = $peOffset + 20
        $optionalHeaderSize = [int]$reader.ReadUInt16()
        $optionalHeaderOffset = [long]$peOffset + 24
        if ($optionalHeaderSize -lt 152 -or
            $optionalHeaderOffset + $optionalHeaderSize -gt $stream.Length) {
            throw 'The launcher artifact has a truncated or malformed x64 optional header.'
        }
        $stream.Position = $optionalHeaderOffset
        if ($reader.ReadUInt16() -ne 0x020B) {
            throw 'The launcher artifact does not use the required PE32+ optional header.'
        }
        $stream.Position = $optionalHeaderOffset + 108
        $directoryCount = [long]$reader.ReadUInt32()
        $maximumDirectoryCount = [long](($optionalHeaderSize - 112) / 8)
        if ($directoryCount -lt 5 -or $directoryCount -gt 16 -or
            $directoryCount -gt $maximumDirectoryCount) {
            throw 'The launcher artifact has an invalid PE32+ data-directory table.'
        }
        $stream.Position = $optionalHeaderOffset + 144
        $certificateTableOffset = $reader.ReadUInt32()
        $certificateTableSize = $reader.ReadUInt32()
        if (($certificateTableOffset -eq 0) -ne
            ($certificateTableSize -eq 0)) {
            throw 'The launcher PE Certificate Table is incomplete.'
        }
        if ($certificateTableSize -ne 0) {
            if ($certificateTableOffset % 8 -ne 0 -or
                $certificateTableSize -lt 8 -or
                [long]$certificateTableOffset + $certificateTableSize -ne
                    $stream.Length) {
                throw 'The launcher PE Certificate Table is malformed.'
            }
            $stream.Position = $certificateTableOffset
            [long]$remaining = $certificateTableSize
            while ($remaining -gt 0) {
                if ($remaining -lt 8) {
                    throw 'The launcher PE certificate entry is truncated.'
                }
                [long]$certificateLength = $reader.ReadUInt32()
                $certificateRevision = $reader.ReadUInt16()
                $certificateType = $reader.ReadUInt16()
                if ($certificateLength -lt 8 -or
                    $certificateLength -gt $remaining -or
                    $certificateRevision -ne 0x0200 -or
                    $certificateType -ne 0x0002) {
                    throw 'The launcher PE certificate entry is invalid.'
                }
                [long]$paddedLength = ($certificateLength + 7) -band -8
                if ($paddedLength -gt $remaining) {
                    throw 'The launcher PE certificate padding is invalid.'
                }
                $stream.Position += $paddedLength - 8
                $remaining -= $paddedLength
            }
        }
        return [pscustomobject]@{
            Offset = [long]$certificateTableOffset
            Size = [long]$certificateTableSize
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Assert-LauncherHealth {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [long] $Sequence,
        [Parameter(Mandatory)] [string] $ExpectedVersion
    )
    $health = Start-Process -FilePath $Path -WindowStyle Hidden `
        -ArgumentList @('--launcher-health-check', "$Sequence", $ExpectedVersion) `
        -PassThru
    try {
        if (-not $health.WaitForExit(15000)) {
            $health.Kill($true)
            $health.WaitForExit()
            throw 'The launcher artifact health check timed out.'
        }
        if ($health.ExitCode -ne 0) {
            throw "The launcher artifact health check exited with $($health.ExitCode)."
        }
    }
    finally {
        try {
            if (-not $health.HasExited) {
                $health.Kill($true)
                $health.WaitForExit()
            }
        }
        finally {
            $health.Dispose()
        }
    }
}

$launcherPath = Assert-RegularFile $ArtifactPath 'The launcher artifact'
if ([IO.Path]::GetFileName($launcherPath) -cne 'uvsr-launcher.exe') {
    throw "The launcher artifact must use the exact filename ''uvsr-launcher.exe''."
}
$artifactSize = (Get-Item -LiteralPath $launcherPath).Length
if ($artifactSize -lt 1 -or $artifactSize -gt (256L * 1024 * 1024)) {
    throw 'The launcher artifact size is outside its safe range.'
}
$certificateTable = Read-PeX64CertificateTable $launcherPath
$authenticode = Get-AuthenticodeSignature -LiteralPath $launcherPath
if ([string]$authenticode.Status -eq 'Valid') {
    if ($certificateTable.Size -eq 0 -or
        $null -eq $authenticode.SignerCertificate) {
        throw 'The valid launcher signature lacks its PE certificate identity.'
    }
}
elseif ([string]$authenticode.Status -eq 'NotSigned') {
    if ($certificateTable.Size -ne 0 -or
        [string]$authenticode.SignatureType -ne 'None' -or
        $null -ne $authenticode.SignerCertificate -or
        $null -ne $authenticode.TimeStamperCertificate) {
        throw 'The unsigned launcher has conflicting Authenticode state.'
    }
}
else {
    throw "The launcher Authenticode signature is not valid: " +
        [string]$authenticode.Status
}
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($launcherPath)
if ($metadata.ProductName -ne 'UVSR Launcher' -or
    $metadata.ProductVersion -ne "$Version+$SourceCommit" -or
    $metadata.FileVersion -ne "$Version.0") {
    throw 'The launcher artifact product metadata does not match its exact release identity.'
}
Assert-LauncherHealth $launcherPath $ReleaseSequence $Version

$artifactHash = (Get-FileHash -LiteralPath $launcherPath `
    -Algorithm SHA256).Hash.ToLowerInvariant()
$payload = [ordered]@{ schemaVersion = 2; productId = $feedProductId
    channel = 'stable'; releaseSequence = $ReleaseSequence; version = $Version
    sourceCommit = $SourceCommit
    artifact = [ordered]@{ name = 'uvsr-launcher.exe'; size = $artifactSize; sha256 = $artifactHash } }
Write-SignedFeed $payload $PrivateKeyPemPath $trust $OutputPath ([bool]$Force)
