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
$productionKey = 'MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg=='
$productId = '0c47a7a8-1ec4-4ffd-b6c4-2f7614181223'
$artifactName = 'uvsr-renderer-windows-11-x64.zip'
$maximumFeedBytes = 16 * 1024
$maximumPayloadBytes = 8 * 1024
$maximumArtifactBytes = 32L * 1024 * 1024 * 1024
$maximumSequence = 9007199254740991L

if ([string]::IsNullOrWhiteSpace($Path)) {
    $Path = Join-Path $PSScriptRoot 'renderer-update-feed-v1.json'
}
if ($AllowTestKey) {
    if ([string]::IsNullOrWhiteSpace($TestKeyId) -or
        [string]::IsNullOrWhiteSpace($TestPublicKeySpkiBase64)) {
        throw 'Test-key verification requires key ID and public key.'
    }
    $keyId = $TestKeyId
    $publicKeyBase64 = $TestPublicKeySpkiBase64
}
else {
    if ($TestKeyId -or $TestPublicKeySpkiBase64) {
        throw 'Test key data requires -AllowTestKey.'
    }
    $keyId = $productionKeyId
    $publicKeyBase64 = $productionKey
}

function Read-CanonicalBase64([string] $Value, [string] $Description) {
    try { $bytes = [Convert]::FromBase64String($Value) }
    catch [FormatException] { throw "$Description is not Base64." }
    if ([Convert]::ToBase64String($bytes) -cne $Value) {
        throw "$Description is not canonical Base64."
    }
    return ,$bytes
}

function Read-ExactObject(
    [Text.Json.JsonElement] $Element,
    [string[]] $Names,
    [string] $Description) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Object) {
        throw "$Description is not an object."
    }
    $result = [Collections.Generic.Dictionary[string,Text.Json.JsonElement]]::new(
        [StringComparer]::Ordinal)
    foreach ($property in $Element.EnumerateObject()) {
        if (-not $result.TryAdd($property.Name, $property.Value.Clone())) {
            throw "$Description contains a duplicate property."
        }
    }
    if ($result.Count -ne $Names.Count) {
        throw "$Description has unexpected properties."
    }
    foreach ($name in $Names) {
        if (-not $result.ContainsKey($name)) {
            throw "$Description is missing '$name'."
        }
    }
    return ,$result
}

function Read-String(
    [Text.Json.JsonElement] $Element,
    [string] $Description) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::String) {
        throw "$Description is not a string."
    }
    return $Element.GetString()
}

function Read-Int64(
    [Text.Json.JsonElement] $Element,
    [string] $Description) {
    $value = 0L
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
        -not $Element.TryGetInt64([ref]$value)) {
        throw "$Description is not an integer."
    }
    return $value
}

function Test-EngineVersion([string] $Value) {
    if ($Value -cnotmatch
        '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        return $false
    }
    foreach ($component in $Value.Split('.')) {
        [uint16] $parsed = 0
        if (-not [uint16]::TryParse($component, [ref]$parsed)) {
            return $false
        }
    }
    return $true
}

function Read-Json([byte[]] $Bytes, [string] $Description) {
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xef -and
        $Bytes[1] -eq 0xbb -and $Bytes[2] -eq 0xbf) {
        throw "$Description contains a byte-order mark."
    }
    try {
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($Bytes)
        $options = [Text.Json.JsonDocumentOptions]::new()
        $options.AllowTrailingCommas = $false
        $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
        $options.MaxDepth = 8
        $document = [Text.Json.JsonDocument]::Parse($text, $options)
    }
    catch {
        throw "$Description is not strict UTF-8 JSON."
    }
    return [pscustomobject]@{ Text = $text; Document = $document }
}

$fullPath = [IO.Path]::GetFullPath($Path)
if (-not [IO.File]::Exists($fullPath)) {
    throw "Renderer update feed is missing at '$fullPath'."
}
$item = Get-Item -LiteralPath $fullPath -Force
if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Renderer update feed must be a regular file.'
}
$feedBytes = [IO.File]::ReadAllBytes($fullPath)
if ($feedBytes.Length -lt 1 -or $feedBytes.Length -gt $maximumFeedBytes) {
    throw 'Renderer update feed size is outside its limit.'
}

$envelopeJson = Read-Json $feedBytes 'Renderer update feed envelope'
try {
    $envelope = Read-ExactObject $envelopeJson.Document.RootElement @(
        'schemaVersion','keyId','payloadBase64','signatureBase64'
    ) 'Renderer update feed envelope'
    if ((Read-Int64 $envelope['schemaVersion'] 'Envelope schema') -ne 1) {
        throw 'Renderer update feed envelope schema must be 1.'
    }
    $actualKeyId = Read-String $envelope['keyId'] 'Envelope keyId'
    if ($actualKeyId -cne $keyId) {
        throw 'Renderer update feed key is not trusted.'
    }
    $payloadBase64 = Read-String $envelope['payloadBase64'] 'Payload'
    $signatureBase64 = Read-String $envelope['signatureBase64'] 'Signature'
    $payloadBytes = Read-CanonicalBase64 $payloadBase64 'Payload'
    $signature = Read-CanonicalBase64 $signatureBase64 'Signature'
    if ($payloadBytes.Length -lt 1 -or
        $payloadBytes.Length -gt $maximumPayloadBytes -or
        $signature.Length -ne 64) {
        throw 'Renderer update feed cryptographic fields are outside their limits.'
    }

    $payloadJson = Read-Json $payloadBytes 'Renderer update feed payload'
    try {
        $payload = Read-ExactObject $payloadJson.Document.RootElement @(
            'schemaVersion','productId','channel','releaseSequence',
            'sourceCommit','settingsHash','engineVersion','artifact'
        ) 'Renderer update feed payload'
        $artifact = Read-ExactObject $payload['artifact'] @(
            'name','size','sha256'
        ) 'Renderer artifact'
        $schema = Read-Int64 $payload['schemaVersion'] 'Payload schema'
        $sequence = Read-Int64 $payload['releaseSequence'] 'Release sequence'
        $sourceCommit = Read-String $payload['sourceCommit'] 'Source commit'
        $settingsHash = Read-String $payload['settingsHash'] 'Settings hash'
        $engineVersion = Read-String $payload['engineVersion'] 'Engine version'
        $size = Read-Int64 $artifact['size'] 'Artifact size'
        $sha256 = Read-String $artifact['sha256'] 'Artifact SHA-256'
        if ($schema -ne 1 -or
            (Read-String $payload['productId'] 'Product ID') -cne $productId -or
            (Read-String $payload['channel'] 'Channel') -cne 'stable' -or
            $sequence -lt 1 -or $sequence -gt $maximumSequence -or
            $sourceCommit -cnotmatch '^[0-9a-f]{40}$' -or
            $settingsHash -cnotmatch '^[0-9a-f]{32}$' -or
            -not (Test-EngineVersion $engineVersion) -or
            (Read-String $artifact['name'] 'Artifact name') -cne $artifactName -or
            $size -lt 1 -or $size -gt $maximumArtifactBytes -or
            $sha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Renderer update feed values are not canonical.'
        }
    }
    finally {
        $payloadJson.Document.Dispose()
    }

    $publicKey = Read-CanonicalBase64 $publicKeyBase64 'Public key'
    $ecdsa = [Security.Cryptography.ECDsa]::Create()
    try {
        $bytesRead = 0
        $ecdsa.ImportSubjectPublicKeyInfo($publicKey, [ref]$bytesRead)
        $parameters = $ecdsa.ExportParameters($false)
        if ($bytesRead -ne $publicKey.Length -or $ecdsa.KeySize -ne 256 -or
            $parameters.Curve.Oid.Value -ne '1.2.840.10045.3.1.7' -or
            -not $ecdsa.VerifyData(
                $payloadBytes, $signature,
                [Security.Cryptography.HashAlgorithmName]::SHA256,
                [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)) {
            throw 'Renderer update feed signature is invalid.'
        }
    }
    finally {
        $ecdsa.Dispose()
    }
    $payloadJson.Text
}
finally {
    $envelopeJson.Document.Dispose()
}
