[CmdletBinding()]
param(
    [string] $Path,
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
$artifactName = 'uvsr-launcher.exe'
$maximumFeedBytes = 16 * 1024
$maximumLauncherBytes = 256L * 1024 * 1024
$maximumReleaseSequence = 9007199254740991L

if ([string]::IsNullOrWhiteSpace($Path)) {
    $Path = Join-Path $PSScriptRoot 'launcher-update-feed-v2.json'
}
if ($AllowTestKey) {
    if ([string]::IsNullOrWhiteSpace($TestKeyId) -or
        [string]::IsNullOrWhiteSpace($TestPublicKeySpkiBase64)) {
        throw 'Test-key validation requires both -TestKeyId and -TestPublicKeySpkiBase64.'
    }
    $expectedKeyId = $TestKeyId
    $publicKeySpkiBase64 = $TestPublicKeySpkiBase64
}
else {
    if (-not [string]::IsNullOrEmpty($TestKeyId) -or
        -not [string]::IsNullOrEmpty($TestPublicKeySpkiBase64)) {
        throw 'A test launcher update key cannot be supplied without -AllowTestKey.'
    }
    $expectedKeyId = $productionKeyId
    $publicKeySpkiBase64 = $productionPublicKeySpkiBase64
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

function Read-ExactObject {
    param(
        [Parameter(Mandatory)] [Text.Json.JsonElement] $Element,
        [Parameter(Mandatory)] [string[]] $ExpectedNames,
        [Parameter(Mandatory)] [string] $Description
    )
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Object) {
        throw "$Description must be a JSON object."
    }
    $properties = [Collections.Generic.Dictionary[string, Text.Json.JsonElement]]::new(
        [StringComparer]::Ordinal)
    foreach ($property in $Element.EnumerateObject()) {
        if (-not $properties.TryAdd($property.Name, $property.Value.Clone())) {
            throw "$Description contains duplicate property '$($property.Name)'."
        }
    }
    if ($properties.Count -ne $ExpectedNames.Count) {
        throw "$Description properties are not exact."
    }
    foreach ($name in $ExpectedNames) {
        if (-not $properties.ContainsKey($name)) {
            throw "$Description is missing exact property '$name'."
        }
    }
    return ,$properties
}

function Read-JsonString {
    param(
        [Parameter(Mandatory)] [Text.Json.JsonElement] $Element,
        [Parameter(Mandatory)] [string] $Description
    )
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::String) {
        throw "$Description must be a JSON string."
    }
    $value = $Element.GetString()
    if ($null -eq $value) {
        throw "$Description must not be null."
    }
    return $value
}

function Read-JsonInt64 {
    param(
        [Parameter(Mandatory)] [Text.Json.JsonElement] $Element,
        [Parameter(Mandatory)] [string] $Description
    )
    $value = 0L
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
        -not $Element.TryGetInt64([ref]$value)) {
        throw "$Description must be a JSON integer."
    }
    return $value
}

function Read-StrictSingleLineJson {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Description
    )
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and
        $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        throw "$Description must not contain a UTF-8 byte-order mark."
    }
    try {
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($Bytes)
    }
    catch [Text.DecoderFallbackException] {
        throw "$Description is not strict UTF-8."
    }
    if (-not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.Contains("`r", [StringComparison]::Ordinal) -or
        $text.Substring(0, $text.Length - 1).Contains(
            "`n", [StringComparison]::Ordinal)) {
        throw "$Description must be one canonical LF-terminated JSON line."
    }
    $options = [Text.Json.JsonDocumentOptions]::new()
    $options.AllowTrailingCommas = $false
    $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
    $options.MaxDepth = 8
    try {
        $document = [Text.Json.JsonDocument]::Parse($text, $options)
    }
    catch [Text.Json.JsonException] {
        throw "$Description is not strict JSON: $($_.Exception.Message)"
    }
    return [pscustomobject]@{
        Text = $text
        Document = $document
    }
}

function Test-StableVersion {
    param([Parameter(Mandatory)] [string] $Version)
    if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        return $false
    }
    foreach ($part in $Version.Split('.')) {
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

function New-CanonicalPayload {
    param(
        [Parameter(Mandatory)] [long] $ReleaseSequence,
        [Parameter(Mandatory)] [string] $Version,
        [Parameter(Mandatory)] [string] $SourceCommit,
        [Parameter(Mandatory)] [long] $ArtifactSize,
        [Parameter(Mandatory)] [string] $ArtifactSha256
    )
    return '{"schemaVersion":2,"productId":"' + $productId +
        '","channel":"stable","releaseSequence":' +
        $ReleaseSequence.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"version":"' + $Version + '","sourceCommit":"' + $SourceCommit +
        '","artifact":{"name":"' + $artifactName + '","size":' +
        $ArtifactSize.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"sha256":"' + $ArtifactSha256 + '"}}' + "`n"
}

function New-CanonicalEnvelope {
    param(
        [Parameter(Mandatory)] [string] $KeyId,
        [Parameter(Mandatory)] [string] $PayloadBase64,
        [Parameter(Mandatory)] [string] $SignatureBase64
    )
    return '{"schemaVersion":2,"keyId":"' + $KeyId +
        '","payloadBase64":"' + $PayloadBase64 +
        '","signatureBase64":"' + $SignatureBase64 + '"}' + "`n"
}

$fullPath = [IO.Path]::GetFullPath($Path)
if (-not [IO.File]::Exists($fullPath)) {
    throw "The launcher update feed is missing at '$fullPath'."
}
$feedItem = Get-Item -LiteralPath $fullPath -Force
if (($feedItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "The launcher update feed must be a regular file, not a filesystem link."
}
$feedBytes = [IO.File]::ReadAllBytes($fullPath)
if ($feedBytes.Length -lt 1 -or $feedBytes.Length -gt $maximumFeedBytes) {
    throw "The launcher update feed size is outside its strict limit."
}

$envelopeJson = Read-StrictSingleLineJson -Bytes $feedBytes `
    -Description 'The launcher update feed envelope'
try {
    $envelope = Read-ExactObject -Element $envelopeJson.Document.RootElement `
        -ExpectedNames @('schemaVersion', 'keyId', 'payloadBase64',
            'signatureBase64') -Description 'The launcher update feed envelope'
    $envelopeSchema = Read-JsonInt64 $envelope['schemaVersion'] `
        'The launcher update feed envelope schemaVersion'
    if ($envelopeSchema -ne 2) {
        throw 'The launcher update feed envelope schemaVersion must be 2.'
    }
    $keyId = Read-JsonString $envelope['keyId'] `
        'The launcher update feed keyId'
    if ($keyId -cne $expectedKeyId) {
        throw "The launcher update feed keyId '$keyId' is not trusted."
    }
    $payloadBase64 = Read-JsonString $envelope['payloadBase64'] `
        'The launcher update feed payloadBase64'
    $signatureBase64 = Read-JsonString $envelope['signatureBase64'] `
        'The launcher update feed signatureBase64'
    $payloadBytes = Read-CanonicalBase64 $payloadBase64 `
        'The launcher update feed payloadBase64'
    $signatureBytes = Read-CanonicalBase64 $signatureBase64 `
        'The launcher update feed signatureBase64'
    if ($payloadBytes.Length -lt 1 -or $payloadBytes.Length -gt 8 * 1024) {
        throw 'The launcher update feed signed payload size is outside its strict limit.'
    }
    if ($signatureBytes.Length -ne 64) {
        throw 'The launcher update feed signature must be a 64-byte P1363 value.'
    }

    $payloadJson = Read-StrictSingleLineJson -Bytes $payloadBytes `
        -Description 'The launcher update feed signed payload'
    try {
        $payload = Read-ExactObject -Element $payloadJson.Document.RootElement `
            -ExpectedNames @('schemaVersion', 'productId', 'channel',
                'releaseSequence', 'version', 'sourceCommit', 'artifact') `
            -Description 'The launcher update feed signed payload'
        $payloadSchema = Read-JsonInt64 $payload['schemaVersion'] `
            'The launcher update feed payload schemaVersion'
        if ($payloadSchema -ne 2) {
            throw 'The launcher update feed payload schemaVersion must be 2.'
        }
        $payloadProductId = Read-JsonString $payload['productId'] `
            'The launcher update feed productId'
        if ($payloadProductId -cne $productId) {
            throw 'The launcher update feed productId is not canonical.'
        }
        $channel = Read-JsonString $payload['channel'] `
            'The launcher update feed channel'
        if ($channel -cne 'stable') {
            throw 'The launcher update feed channel must be stable.'
        }
        $releaseSequence = Read-JsonInt64 $payload['releaseSequence'] `
            'The launcher update feed releaseSequence'
        if ($releaseSequence -lt 1 -or
            $releaseSequence -gt $maximumReleaseSequence) {
            throw 'The launcher update feed releaseSequence is outside its safe range.'
        }
        $version = Read-JsonString $payload['version'] `
            'The launcher update feed version'
        if (-not (Test-StableVersion $version)) {
            throw 'The launcher update feed version is not canonical X.Y.Z.'
        }
        $sourceCommit = Read-JsonString $payload['sourceCommit'] `
            'The launcher update feed sourceCommit'
        if ($sourceCommit -cnotmatch '^[0-9a-f]{40}$') {
            throw 'The launcher update feed sourceCommit must be 40 lowercase hexadecimal characters.'
        }
        $artifact = Read-ExactObject -Element $payload['artifact'] `
            -ExpectedNames @('name', 'size', 'sha256') `
            -Description 'The launcher update feed artifact'
        $payloadArtifactName = Read-JsonString $artifact['name'] `
            'The launcher update feed artifact name'
        if ($payloadArtifactName -cne $artifactName) {
            throw 'The launcher update feed artifact name is not canonical.'
        }
        $artifactSize = Read-JsonInt64 $artifact['size'] `
            'The launcher update feed artifact size'
        if ($artifactSize -lt 1 -or $artifactSize -gt $maximumLauncherBytes) {
            throw 'The launcher update feed artifact size is outside its safe range.'
        }
        $artifactSha256 = Read-JsonString $artifact['sha256'] `
            'The launcher update feed artifact sha256'
        if ($artifactSha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw 'The launcher update feed artifact sha256 must be 64 lowercase hexadecimal characters.'
        }

        $canonicalPayload = New-CanonicalPayload $releaseSequence $version `
            $sourceCommit $artifactSize $artifactSha256
        if ($payloadJson.Text -cne $canonicalPayload) {
            throw 'The launcher update feed signed payload is not in canonical LF form.'
        }
        $canonicalEnvelope = New-CanonicalEnvelope $keyId $payloadBase64 `
            $signatureBase64
        if ($envelopeJson.Text -cne $canonicalEnvelope) {
            throw 'The launcher update feed envelope is not in canonical LF form.'
        }

        $publicKeyBytes = Read-CanonicalBase64 $publicKeySpkiBase64 `
            'The pinned launcher update public key'
        $ecdsa = [Security.Cryptography.ECDsa]::Create()
        try {
            $bytesRead = 0
            $ecdsa.ImportSubjectPublicKeyInfo($publicKeyBytes, [ref]$bytesRead)
            if ($bytesRead -ne $publicKeyBytes.Length -or $ecdsa.KeySize -ne 256) {
                throw 'The pinned launcher update public key is not one exact P-256 key.'
            }
            $parameters = $ecdsa.ExportParameters($false)
            if ($parameters.Curve.Oid.Value -ne '1.2.840.10045.3.1.7') {
                throw 'The pinned launcher update public key is not on curve P-256.'
            }
            if (-not $ecdsa.VerifyData($payloadBytes, $signatureBytes,
                    [Security.Cryptography.HashAlgorithmName]::SHA256,
                    [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)) {
                throw 'The launcher update feed signature is invalid.'
            }
        }
        catch [Security.Cryptography.CryptographicException] {
            throw "The launcher update feed key or signature is invalid: $($_.Exception.Message)"
        }
        finally {
            $ecdsa.Dispose()
        }

        Write-Output $canonicalPayload.TrimEnd("`n")
    }
    finally {
        $payloadJson.Document.Dispose()
    }
}
finally {
    $envelopeJson.Document.Dispose()
}
