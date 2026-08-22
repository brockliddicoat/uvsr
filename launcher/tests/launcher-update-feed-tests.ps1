[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$verifier = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\verify-launcher-update-feed.ps1'))
$signer = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\new-launcher-update-feed.ps1'))
foreach ($script in @($verifier, $signer)) {
    if (-not [IO.File]::Exists($script)) {
        throw "A launcher update feed test dependency is missing at '$script'."
    }
}

$temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$temporaryRoot = Join-Path $temporaryParent `
    "uvsr-launcher-update-feed-tests-$([Guid]::NewGuid().ToString('N'))"
$temporaryPrefix = $temporaryParent + [IO.Path]::DirectorySeparatorChar
if (-not $temporaryRoot.StartsWith(
        $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not [IO.Path]::GetFileName($temporaryRoot).StartsWith(
        'uvsr-launcher-update-feed-tests-', [StringComparison]::Ordinal)) {
    throw 'The launcher update feed test root escaped the temporary directory.'
}

$productId = '0c47a7a8-1ec4-4ffd-b6c4-2f7614181223'
$artifactName = 'UVSR-Launcher-Windows-11-x64.exe'
$testKeyId = 'uvsr-launcher-update-p256-test-01'
$utf8 = [Text.UTF8Encoding]::new($false, $true)

function New-TestPayload {
    param(
        [long] $Sequence = 7,
        [string] $Version = '1.2.3',
        [string] $SourceCommit = ('a' * 40),
        [long] $Size = 1234,
        [string] $Sha256 = ('b' * 64),
        [string] $Channel = 'stable'
    )
    return '{"schemaVersion":2,"productId":"' + $productId +
        '","channel":"' + $Channel + '","releaseSequence":' +
        $Sequence.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"version":"' + $Version + '","sourceCommit":"' + $SourceCommit +
        '","artifact":{"name":"' + $artifactName + '","size":' +
        $Size.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"sha256":"' + $Sha256 + '"}}' + "`n"
}

function New-TestEnvelopeBytes {
    param(
        [Parameter(Mandatory)] [Security.Cryptography.ECDsa] $Key,
        [Parameter(Mandatory)] [string] $Payload,
        [string] $KeyId = $testKeyId
    )
    $payloadBytes = $utf8.GetBytes($Payload)
    $signature = $Key.SignData($payloadBytes,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    if ($signature.Length -ne 64) {
        throw 'The test platform did not produce a 64-byte P1363 signature.'
    }
    $envelope = '{"schemaVersion":2,"keyId":"' + $KeyId +
        '","payloadBase64":"' + [Convert]::ToBase64String($payloadBytes) +
        '","signatureBase64":"' + [Convert]::ToBase64String($signature) +
        '"}' + "`n"
    return ,$utf8.GetBytes($envelope)
}

function Write-Fixture {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [byte[]] $Bytes
    )
    $path = Join-Path $temporaryRoot "$Name.json"
    [IO.File]::WriteAllBytes($path, $Bytes)
    return $path
}

function Invoke-TestVerifier {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $PublicKey
    )
    return & $verifier -Path $Path -AllowTestKey -TestKeyId $testKeyId `
        -TestPublicKeySpkiBase64 $PublicKey
}

function Assert-VerifierFailure {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Expected,
        [Parameter(Mandatory)] [string] $PublicKey
    )
    $path = Write-Fixture $Name $Bytes
    try {
        Invoke-TestVerifier $path $PublicKey | Out-Null
    }
    catch {
        if (-not $_.Exception.Message.Contains(
                $Expected, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Fixture '$Name' failed with '$($_.Exception.Message)' instead of '$Expected'."
        }
        return
    }
    throw "Fixture '$Name' unexpectedly passed launcher update feed verification."
}

function New-TestPeFixture {
    param(
        [int] $OptionalHeaderSize = 240,
        [uint16] $OptionalHeaderMagic = 0x020B,
        [uint32] $DirectoryCount = 16,
        [uint32] $CertificateTableOffset = 0,
        [uint32] $CertificateTableSize = 0,
        [int] $Length = 512
    )
    if ($Length -lt 1 -or $Length -gt 512) {
        throw 'The test PE fixture length is outside its safe range.'
    }
    $bytes = [byte[]]::new(512)
    $stream = [IO.MemoryStream]::new($bytes, $true)
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([uint16]0x5A4D)
        $stream.Position = 0x3C
        $writer.Write([int]0x80)
        $stream.Position = 0x80
        $writer.Write([uint32]0x00004550)
        $writer.Write([uint16]0x8664)
        $writer.Write([uint16]1)
        $stream.Position = 0x80 + 20
        $writer.Write([uint16]$OptionalHeaderSize)
        $writer.Write([uint16]0x0022)
        $optionalHeaderOffset = 0x80 + 24
        $stream.Position = $optionalHeaderOffset
        $writer.Write($OptionalHeaderMagic)
        $stream.Position = $optionalHeaderOffset + 108
        $writer.Write($DirectoryCount)
        $stream.Position = $optionalHeaderOffset + 144
        $writer.Write($CertificateTableOffset)
        $writer.Write($CertificateTableSize)
        if ($CertificateTableOffset -gt 0 -and
            $CertificateTableSize -gt 0 -and
            [long]$CertificateTableOffset + $CertificateTableSize -le
                $bytes.Length) {
            $stream.Position = $CertificateTableOffset
            $writer.Write([byte[]](0x30) * [int]$CertificateTableSize)
        }
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
    if ($Length -eq $bytes.Length) {
        return ,$bytes
    }
    return ,([byte[]]$bytes[0..($Length - 1)])
}

function Assert-SignerArtifactFailure {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Expected,
        [Parameter(Mandatory)] [string] $PrivateKeyPath,
        [Parameter(Mandatory)] [string] $PublicKey,
        [Parameter(Mandatory)] [string] $ArtifactPath,
        [Parameter(Mandatory)] [string] $OutputPath
    )
    [IO.File]::WriteAllBytes($ArtifactPath, $Bytes)
    try {
        & $signer -PrivateKeyPemPath $PrivateKeyPath `
            -ArtifactPath $ArtifactPath -Version '1.2.3' -ReleaseSequence 7 `
            -SourceCommit ('a' * 40) -OutputPath $OutputPath `
            -AllowTestKey -TestKeyId $testKeyId `
            -TestPublicKeySpkiBase64 $PublicKey | Out-Null
    }
    catch {
        if (-not $_.Exception.Message.Contains(
                $Expected, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Signer fixture '$Name' failed with '$($_.Exception.Message)' instead of '$Expected'."
        }
        if ([IO.File]::Exists($OutputPath)) {
            throw "Rejected signer fixture '$Name' created an update feed."
        }
        return
    }
    throw "Signer fixture '$Name' unexpectedly passed artifact validation."
}

$testError = $null
$key = $null
$otherKey = $null
try {
    [IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
    $key = [Security.Cryptography.ECDsa]::Create(
        [Security.Cryptography.ECCurve+NamedCurves]::nistP256)
    $publicKey = [Convert]::ToBase64String(
        $key.ExportSubjectPublicKeyInfo())
    $payload = New-TestPayload
    $validBytes = New-TestEnvelopeBytes $key $payload
    $validPath = Write-Fixture 'valid' $validBytes
    $verified = (Invoke-TestVerifier $validPath $publicKey) -join "`n"
    if ($verified -cne $payload.TrimEnd("`n")) {
        throw 'The valid launcher update feed did not emit its exact signed payload JSON.'
    }

    $productionRejected = $false
    try {
        & $verifier -Path $validPath | Out-Null
    }
    catch {
        if (-not $_.Exception.Message.Contains(
                'not trusted', [StringComparison]::OrdinalIgnoreCase)) {
            throw
        }
        $productionRejected = $true
    }
    if (-not $productionRejected) {
        throw 'The production verifier accepted an ephemeral launcher update key.'
    }

    $validText = $utf8.GetString($validBytes)
    Assert-VerifierFailure 'unknown-envelope-property' `
        $utf8.GetBytes($validText.Replace(
            '{"schemaVersion":2,', '{"schemaVersion":2,"unknown":0,')) `
        'properties are not exact' $publicKey
    Assert-VerifierFailure 'duplicate-envelope-property' `
        $utf8.GetBytes($validText.Replace(
            '{"schemaVersion":2,', '{"schemaVersion":2,"schemaVersion":2,')) `
        'duplicate property' $publicKey
    Assert-VerifierFailure 'wrong-envelope-casing' `
        $utf8.GetBytes($validText.Replace('"keyId"', '"KeyId"')) `
        'missing exact property' $publicKey
    Assert-VerifierFailure 'noncanonical-envelope-spacing' `
        $utf8.GetBytes($validText.Replace('{"schemaVersion"', '{ "schemaVersion"')) `
        'canonical LF form' $publicKey
    Assert-VerifierFailure 'crlf-envelope' `
        $utf8.GetBytes($validText.Replace("`n", "`r`n")) `
        'canonical LF-terminated' $publicKey
    Assert-VerifierFailure 'oversized-envelope' `
        ([byte[]](0x7B) + [byte[]](0x20) * (16 * 1024) + [byte[]](0x7D)) `
        'size is outside' $publicKey

    $invalidBase64 = $validText.Replace(
        '"signatureBase64":"', '"signatureBase64":"*')
    Assert-VerifierFailure 'invalid-base64' $utf8.GetBytes($invalidBase64) `
        'not valid base64' $publicKey
    $shortSignature = [Convert]::ToBase64String([byte[]]::new(63))
    $shortSignatureText = [regex]::Replace($validText,
        '(?<="signatureBase64":")[^"]+', $shortSignature)
    Assert-VerifierFailure 'short-signature' $utf8.GetBytes($shortSignatureText) `
        '64-byte P1363' $publicKey

    $mutatedPayload = $payload.Replace('"version":"1.2.3"',
        '"version":"1.2.4"')
    $payloadBase64 = [Convert]::ToBase64String($utf8.GetBytes($payload))
    $mutatedBase64 = [Convert]::ToBase64String($utf8.GetBytes($mutatedPayload))
    $staleSignature = $validText.Replace($payloadBase64, $mutatedBase64)
    Assert-VerifierFailure 'stale-signature' $utf8.GetBytes($staleSignature) `
        'signature is invalid' $publicKey

    foreach ($case in @(
            [pscustomobject]@{
                Name = 'wrong-channel'
                Payload = New-TestPayload -Channel 'unsigned'
                Error = 'channel must be stable'
            },
            [pscustomobject]@{
                Name = 'invalid-source-commit'
                Payload = New-TestPayload -SourceCommit ('A' * 40)
                Error = '40 lowercase hexadecimal'
            },
            [pscustomobject]@{
                Name = 'invalid-artifact-size'
                Payload = New-TestPayload -Size 0
                Error = 'size is outside'
            },
            [pscustomobject]@{
                Name = 'invalid-artifact-hash'
                Payload = New-TestPayload -Sha256 ('B' * 64)
                Error = '64 lowercase hexadecimal'
            })) {
        Assert-VerifierFailure $case.Name `
            (New-TestEnvelopeBytes $key $case.Payload) $case.Error $publicKey
    }

    $unknownPayload = $payload.Replace(
        '{"schemaVersion":2,', '{"schemaVersion":2,"unknown":0,')
    Assert-VerifierFailure 'unknown-payload-property' `
        (New-TestEnvelopeBytes $key $unknownPayload) 'properties are not exact' `
        $publicKey
    $wrongCasePayload = $payload.Replace('"sourceCommit"', '"SourceCommit"')
    Assert-VerifierFailure 'wrong-payload-casing' `
        (New-TestEnvelopeBytes $key $wrongCasePayload) 'missing exact property' `
        $publicKey

    $otherKey = [Security.Cryptography.ECDsa]::Create(
        [Security.Cryptography.ECCurve+NamedCurves]::nistP256)
    $otherPublicKey = [Convert]::ToBase64String(
        $otherKey.ExportSubjectPublicKeyInfo())
    Assert-VerifierFailure 'wrong-public-key' $validBytes 'signature is invalid' `
        $otherPublicKey

    $privateKeyPath = Join-Path $temporaryRoot 'ephemeral-private.pem'
    [IO.File]::WriteAllText($privateKeyPath, $key.ExportPkcs8PrivateKeyPem(),
        [Text.UTF8Encoding]::new($false))
    $dummyArtifact = Join-Path $temporaryRoot $artifactName
    [IO.File]::WriteAllBytes($dummyArtifact, [byte[]](0))
    $unexpectedOutput = Join-Path $temporaryRoot 'must-not-exist.json'
    try {
        & $signer -PrivateKeyPemPath $privateKeyPath `
            -ArtifactPath $dummyArtifact -Version '1.2.3' -ReleaseSequence 7 `
            -SourceCommit ('a' * 40) -OutputPath $unexpectedOutput | Out-Null
        throw 'The production signer accepted an ephemeral private key.'
    }
    catch {
        if (-not $_.Exception.Message.Contains(
                'does not match the pinned', [StringComparison]::OrdinalIgnoreCase)) {
            throw
        }
    }
    if ([IO.File]::Exists($unexpectedOutput)) {
        throw 'The rejected ephemeral signer created a launcher update feed.'
    }
    try {
        & $signer -PrivateKeyPemPath $privateKeyPath `
            -ArtifactPath $dummyArtifact -Version '1.2.3' -ReleaseSequence 7 `
            -SourceCommit ('a' * 40) -OutputPath $unexpectedOutput `
            -AllowTestKey -TestKeyId $testKeyId `
            -TestPublicKeySpkiBase64 $publicKey | Out-Null
        throw 'The signer accepted a non-PE launcher test artifact.'
    }
    catch {
        if (-not $_.Exception.Message.Contains(
                'Windows PE', [StringComparison]::OrdinalIgnoreCase)) {
            throw
        }
    }

    foreach ($fixture in @(
            [pscustomobject]@{
                Name = 'truncated-optional-header'
                Bytes = New-TestPeFixture -Length 300
                Error = 'truncated or malformed'
            },
            [pscustomobject]@{
                Name = 'short-optional-header'
                Bytes = New-TestPeFixture -OptionalHeaderSize 144
                Error = 'truncated or malformed'
            },
            [pscustomobject]@{
                Name = 'unsupported-optional-header'
                Bytes = New-TestPeFixture -OptionalHeaderMagic 0x010B
                Error = 'PE32+'
            },
            [pscustomobject]@{
                Name = 'missing-security-directory'
                Bytes = New-TestPeFixture -DirectoryCount 4
                Error = 'data-directory'
            },
            [pscustomobject]@{
                Name = 'unsupported-directory-count'
                Bytes = New-TestPeFixture -OptionalHeaderSize 248 `
                    -DirectoryCount 17
                Error = 'data-directory'
            },
            [pscustomobject]@{
                Name = 'embedded-certificate-table'
                Bytes = New-TestPeFixture -CertificateTableOffset 416 `
                    -CertificateTableSize 8
                Error = 'certificate table'
            },
            [pscustomobject]@{
                Name = 'certificate-offset-only'
                Bytes = New-TestPeFixture -CertificateTableOffset 416
                Error = 'certificate table'
            },
            [pscustomobject]@{
                Name = 'certificate-size-only'
                Bytes = New-TestPeFixture -CertificateTableSize 8
                Error = 'certificate table'
            })) {
        Assert-SignerArtifactFailure -Name $fixture.Name -Bytes $fixture.Bytes `
            -Expected $fixture.Error -PrivateKeyPath $privateKeyPath `
            -PublicKey $publicKey -ArtifactPath $dummyArtifact `
            -OutputPath $unexpectedOutput
    }

    $buildSource = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\build.ps1'))
    $workflowSource = [IO.File]::ReadAllText((Join-Path $PSScriptRoot `
        '..\..\.github\workflows\launcher-readme-download.yml'))
    foreach ($contract in @(
            [pscustomobject]@{
                Name = 'launcher build'
                Text = $buildSource
                Required = 'Assert-UnsignedPeX64 $finalArtifact'
            },
            [pscustomobject]@{
                Name = 'launcher release workflow'
                Text = $workflowSource
                Required = '$certificateTableOffset -ne 0'
            })) {
        if (-not $contract.Text.Contains(
                $contract.Required, [StringComparison]::Ordinal) -or
            -not $contract.Text.Contains(
                '$certificateTableSize -ne 0', [StringComparison]::Ordinal) -or
            -not $contract.Text.Contains(
                'embedded PE certificate table', [StringComparison]::Ordinal) -or
            -not $contract.Text.Contains('0x020B', [StringComparison]::Ordinal)) {
            throw "The $($contract.Name) lost the unsigned PE certificate-table gate."
        }
    }

    Write-Output 'Launcher update feed verifier tests passed.'
}
catch {
    $testError = $_
}
finally {
    if ($null -ne $otherKey) {
        $otherKey.Dispose()
    }
    if ($null -ne $key) {
        $key.Dispose()
    }
    if ([IO.Directory]::Exists($temporaryRoot)) {
        $item = Get-Item -LiteralPath $temporaryRoot -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Refusing to remove a redirected launcher update feed test root.'
        }
        [IO.Directory]::Delete($temporaryRoot, $true)
    }
}
if ($null -ne $testError) {
    throw $testError
}
