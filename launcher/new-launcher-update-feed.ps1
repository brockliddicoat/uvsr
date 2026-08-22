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

$productionKeyId = 'uvsr-launcher-update-p256-2026-01'
$productionPublicKeySpkiBase64 =
    'MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg=='
$productId = '0c47a7a8-1ec4-4ffd-b6c4-2f7614181223'
$artifactName = 'UVSR-Launcher-Windows-11-x64.exe'
$maximumLauncherBytes = 256L * 1024 * 1024
$maximumReleaseSequence = 9007199254740991L

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot 'launcher-update-feed-v2.json'
}
if ($AllowTestKey) {
    if ([string]::IsNullOrWhiteSpace($TestKeyId) -or
        [string]::IsNullOrWhiteSpace($TestPublicKeySpkiBase64)) {
        throw 'Test-key signing requires both -TestKeyId and -TestPublicKeySpkiBase64.'
    }
    $keyId = $TestKeyId
    $publicKeySpkiBase64 = $TestPublicKeySpkiBase64
}
else {
    if (-not [string]::IsNullOrEmpty($TestKeyId) -or
        -not [string]::IsNullOrEmpty($TestPublicKeySpkiBase64)) {
        throw 'A test launcher update key cannot be supplied without -AllowTestKey.'
    }
    $keyId = $productionKeyId
    $publicKeySpkiBase64 = $productionPublicKeySpkiBase64
}

function Test-StableVersion {
    param([Parameter(Mandatory)] [string] $Candidate)
    if ($Candidate -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        return $false
    }
    foreach ($part in $Candidate.Split('.')) {
        $component = 0
        if (-not [int]::TryParse($part,
                [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture,
                [ref]$component)) {
            return $false
        }
    }
    return $true
}

function Read-CanonicalBase64 {
    param(
        [Parameter(Mandatory)] [string] $Value,
        [Parameter(Mandatory)] [string] $Description
    )
    try {
        $bytes = [Convert]::FromBase64String($Value)
    }
    catch [FormatException] {
        throw "$Description is not valid base64."
    }
    if ([Convert]::ToBase64String($bytes) -cne $Value) {
        throw "$Description is not canonical base64."
    }
    return ,$bytes
}

function Assert-RegularFile {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Description
    )
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not [IO.File]::Exists($fullPath)) {
        throw "$Description is missing at '$fullPath'."
    }
    $item = Get-Item -LiteralPath $fullPath -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must be a regular file, not a filesystem link."
    }
    return $fullPath
}

function Assert-UnsignedPeX64 {
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
        if ($certificateTableOffset -ne 0 -or $certificateTableSize -ne 0) {
            throw 'The unsigned launcher artifact contains an embedded PE certificate table.'
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

if (-not (Test-StableVersion $Version)) {
    throw 'The launcher update version must be canonical X.Y.Z.'
}
if ($ReleaseSequence -lt 1 -or $ReleaseSequence -gt $maximumReleaseSequence) {
    throw 'The launcher update release sequence is outside its safe range.'
}
if ($SourceCommit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'The launcher update source commit must be 40 lowercase hexadecimal characters.'
}
if ($keyId -cnotmatch '^[a-z0-9-]{1,96}$') {
    throw 'The launcher update key ID is not canonical.'
}

$privateKeyPath = Assert-RegularFile $PrivateKeyPemPath `
    'The launcher update private key'
$expectedPublicKey = Read-CanonicalBase64 $publicKeySpkiBase64 `
    'The pinned launcher update public key'
$ecdsa = [Security.Cryptography.ECDsa]::Create()
try {
    try {
        $ecdsa.ImportFromPem([IO.File]::ReadAllText($privateKeyPath))
        [void]$ecdsa.ExportParameters($true)
    }
    catch [Security.Cryptography.CryptographicException] {
        throw "The launcher update private key is not a valid EC private key: $($_.Exception.Message)"
    }
    $actualPublicKey = $ecdsa.ExportSubjectPublicKeyInfo()
    if (-not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals(
            $actualPublicKey, $expectedPublicKey) -or $ecdsa.KeySize -ne 256) {
        throw 'The launcher update private key does not match the pinned P-256 public identity.'
    }
    $parameters = $ecdsa.ExportParameters($false)
    if ($parameters.Curve.Oid.Value -ne '1.2.840.10045.3.1.7') {
        throw 'The launcher update private key is not on curve P-256.'
    }

    $launcherPath = Assert-RegularFile $ArtifactPath 'The launcher artifact'
    if ([IO.Path]::GetFileName($launcherPath) -cne $artifactName) {
        throw "The launcher artifact must use the exact filename '$artifactName'."
    }
    $artifactSize = (Get-Item -LiteralPath $launcherPath).Length
    if ($artifactSize -lt 1 -or $artifactSize -gt $maximumLauncherBytes) {
        throw 'The launcher artifact size is outside its safe range.'
    }
    Assert-UnsignedPeX64 $launcherPath
    $authenticode = Get-AuthenticodeSignature -LiteralPath $launcherPath
    if ([string]$authenticode.Status -ne 'NotSigned' -or
        [string]$authenticode.SignatureType -ne 'None' -or
        $null -ne $authenticode.SignerCertificate -or
        $null -ne $authenticode.TimeStamperCertificate) {
        throw 'The launcher artifact must have exact Authenticode NotSigned status with no signer or timestamp certificate.'
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
    $payload = '{"schemaVersion":2,"productId":"' + $productId +
        '","channel":"stable","releaseSequence":' +
        $ReleaseSequence.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"version":"' + $Version + '","sourceCommit":"' + $SourceCommit +
        '","artifact":{"name":"' + $artifactName + '","size":' +
        $artifactSize.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"sha256":"' + $artifactHash + '"}}' + "`n"
    $payloadBytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($payload)
    $signature = $ecdsa.SignData($payloadBytes,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    if ($signature.Length -ne 64) {
        throw 'The launcher update signature was not a 64-byte P1363 value.'
    }
    $envelope = '{"schemaVersion":2,"keyId":"' + $keyId +
        '","payloadBase64":"' + [Convert]::ToBase64String($payloadBytes) +
        '","signatureBase64":"' + [Convert]::ToBase64String($signature) +
        '"}' + "`n"
    $envelopeBytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($envelope)

    $fullOutputPath = [IO.Path]::GetFullPath($OutputPath)
    $outputParent = [IO.Path]::GetDirectoryName($fullOutputPath)
    if (-not [IO.Directory]::Exists($outputParent)) {
        throw "The launcher update feed output directory is missing at '$outputParent'."
    }
    if ([IO.File]::Exists($fullOutputPath) -and -not $Force) {
        throw "The launcher update feed already exists at '$fullOutputPath'; use -Force to replace it."
    }
    if ([IO.File]::Exists($fullOutputPath)) {
        $outputItem = Get-Item -LiteralPath $fullOutputPath -Force
        if (($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Refusing to replace a launcher update feed filesystem link.'
        }
    }
    $temporaryPath = Join-Path $outputParent `
        ".$([IO.Path]::GetFileName($fullOutputPath)).$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $stream = [IO.FileStream]::new($temporaryPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $stream.Write($envelopeBytes, 0, $envelopeBytes.Length)
            $stream.Flush($true)
        }
        finally {
            $stream.Dispose()
        }
        $verifier = Join-Path $PSScriptRoot 'verify-launcher-update-feed.ps1'
        $verifyParameters = @{
            Path = $temporaryPath
        }
        if ($AllowTestKey) {
            $verifyParameters.AllowTestKey = $true
            $verifyParameters.TestKeyId = $keyId
            $verifyParameters.TestPublicKeySpkiBase64 = $publicKeySpkiBase64
        }
        $verifiedPayload = & $verifier @verifyParameters
        if (($verifiedPayload -join "`n") -cne $payload.TrimEnd("`n")) {
            throw 'The generated launcher update feed did not round-trip exactly.'
        }
        [IO.File]::Move($temporaryPath, $fullOutputPath, [bool]$Force)
    }
    finally {
        if ([IO.File]::Exists($temporaryPath)) {
            [IO.File]::Delete($temporaryPath)
        }
    }

    Write-Output "Launcher update feed: $fullOutputPath"
    Write-Output "Release identity: $Version sequence $ReleaseSequence"
    Write-Output "Source commit: $SourceCommit"
    Write-Output "Artifact: $artifactSize bytes, SHA-256 $artifactHash"
}
finally {
    $ecdsa.Dispose()
}
