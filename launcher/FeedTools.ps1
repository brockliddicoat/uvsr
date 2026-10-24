$feedProductId = '0c47a7a8-1ec4-4ffd-b6c4-2f7614181223'
$feedSequenceLimit = 9007199254740991L

function Assert-RegularFile([string] $Path, [string] $Description) {
    $full = [IO.Path]::GetFullPath($Path)
    $item = Get-Item -LiteralPath $full -Force
    if ($item.PSIsContainer -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must be a regular file."
    }
    return $full
}

function Read-CanonicalBase64([string] $Value, [string] $Description) {
    $bytes = [Convert]::FromBase64String($Value)
    if ([Convert]::ToBase64String($bytes) -cne $Value) {
        throw "$Description is not canonical Base64."
    }
    return ,$bytes
}

function Resolve-FeedTrust([bool] $AllowTestKey, [string] $TestKeyId,
    [string] $TestPublicKeySpkiBase64) {
    if ($AllowTestKey) {
        if ($TestKeyId -cnotmatch '^[a-z0-9-]{1,96}$' -or
            [string]::IsNullOrWhiteSpace($TestPublicKeySpkiBase64)) {
            throw 'Test key verification requires a canonical key ID and public key.'
        }
        $keyId = $TestKeyId
        $publicKey = $TestPublicKeySpkiBase64
    }
    else {
        if ($TestKeyId -or $TestPublicKeySpkiBase64) {
            throw 'Test key data requires -AllowTestKey.'
        }
        $keyId = 'uvsr-launcher-update-p256-2026-01'
        $publicKey = 'MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEATbHkDwYIS0nMut5h9Q6m67qfabhuK+VRo6mDW1UlwZQIfeLI7zc1aKblCclkfgd8DDU0LcblFgTFdvoAWgCYg=='
    }
    return @{ KeyId = $keyId; PublicKey = (Read-CanonicalBase64 $publicKey 'Public key') }
}

function Test-FeedVersion([string] $Value, [int] $Components, [long] $Maximum) {
    $parts = $Value.Split('.')
    if ($parts.Count -ne $Components) { return $false }
    foreach ($part in $parts) {
        $number = 0L
        if ($part -cnotmatch '^(0|[1-9][0-9]*)$' -or
            -not [long]::TryParse($part, [ref]$number) -or $number -gt $Maximum) {
            return $false
        }
    }
    return $true
}

function Read-ExactObject([Text.Json.JsonElement] $Element, [string[]] $Names,
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
    if ($result.Count -ne $Names.Count -or
        @($Names | Where-Object { -not $result.ContainsKey($_) }).Count -ne 0) {
        throw "$Description properties are not exact."
    }
    return ,$result
}

function Read-FeedJson([byte[]] $Bytes) {
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xef -and
        $Bytes[1] -eq 0xbb -and $Bytes[2] -eq 0xbf) {
        throw 'Feed JSON contains a byte order mark.'
    }
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($Bytes)
    $options = [Text.Json.JsonDocumentOptions]::new()
    $options.MaxDepth = 8
    return @{ Text = $text; Document = [Text.Json.JsonDocument]::Parse($text, $options) }
}

function Read-SignedFeed([string] $Path, [bool] $Renderer, [hashtable] $Trust) {
    $full = Assert-RegularFile $Path 'Update feed'
    $feedBytes = [IO.File]::ReadAllBytes($full)
    $length = $feedBytes.Length
    if ($length -lt 1 -or $length -gt 16384) {
        throw 'Update feed size is outside its limit.'
    }
    $schema = if ($Renderer) { 1 } else { 2 }
    $json = Read-FeedJson $feedBytes
    $payloadJson = $null
    $key = [Security.Cryptography.ECDsa]::Create()
    try {
        $envelope = Read-ExactObject $json.Document.RootElement @(
            'schemaVersion', 'keyId', 'payloadBase64', 'signatureBase64') 'Feed envelope'
        $payloadText = $envelope['payloadBase64'].GetString()
        $signatureText = $envelope['signatureBase64'].GetString()
        $payloadBytes = Read-CanonicalBase64 $payloadText 'Payload'
        $signature = Read-CanonicalBase64 $signatureText 'Signature'
        if ($envelope['schemaVersion'].GetInt64() -ne $schema -or
            $envelope['keyId'].GetString() -cne $Trust.KeyId -or
            $payloadBytes.Length -lt 1 -or $payloadBytes.Length -gt 8192 -or
            $signature.Length -ne 64) {
            throw 'Feed envelope identity or cryptographic fields are invalid.'
        }
        $bytesRead = 0
        $key.ImportSubjectPublicKeyInfo($Trust.PublicKey, [ref]$bytesRead)
        if ($bytesRead -ne $Trust.PublicKey.Length -or $key.KeySize -ne 256 -or
            $key.ExportParameters($false).Curve.Oid.Value -ne '1.2.840.10045.3.1.7' -or
            -not $key.VerifyData($payloadBytes, $signature,
                [Security.Cryptography.HashAlgorithmName]::SHA256,
                [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)) {
            throw 'Feed signature is invalid.'
        }
        $payloadJson = Read-FeedJson $payloadBytes
        $names = @('schemaVersion', 'productId', 'channel', 'releaseSequence',
            'sourceCommit', 'artifact')
        $names += if ($Renderer) { @('settingsHash', 'engineVersion') } else { 'version' }
        $payload = Read-ExactObject $payloadJson.Document.RootElement $names 'Feed payload'
        $artifact = Read-ExactObject $payload['artifact'] @('name','size','sha256') 'Artifact'
        $sequence = $payload['releaseSequence'].GetInt64()
        $commit = $payload['sourceCommit'].GetString()
        $size = $artifact['size'].GetInt64()
        $hash = $artifact['sha256'].GetString()
        $artifactName = if ($Renderer) { 'uvsr-renderer-windows-11-x64.zip' } else { 'uvsr-launcher.exe' }
        $maximumBytes = if ($Renderer) { 32L * 1024 * 1024 * 1024 } else { 256L * 1024 * 1024 }
        if ($payload['schemaVersion'].GetInt64() -ne $schema -or
            $payload['productId'].GetString() -cne $feedProductId -or
            $payload['channel'].GetString() -cne 'stable' -or
            $sequence -lt 1 -or $sequence -gt $feedSequenceLimit -or
            $commit -cnotmatch '^[0-9a-f]{40}$' -or
            $artifact['name'].GetString() -cne $artifactName -or
            $size -lt 1 -or $size -gt $maximumBytes -or $hash -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Feed values are not canonical.'
        }
        if ($Renderer) {
            if ($payload['settingsHash'].GetString() -cnotmatch '^[0-9a-f]{32}$' -or
                -not (Test-FeedVersion $payload['engineVersion'].GetString() 4 65535)) {
                throw 'Renderer identity is not canonical.'
            }
        }
        else {
            $version = $payload['version'].GetString()
            if (-not (Test-FeedVersion $version 3 2147483647)) {
                throw 'Launcher version is not canonical.'
            }
            $canonical = [ordered]@{ schemaVersion = 2; productId = $feedProductId
                channel = 'stable'; releaseSequence = $sequence; version = $version
                sourceCommit = $commit
                artifact = [ordered]@{ name = $artifactName; size = $size; sha256 = $hash } }
            $canonicalEnvelope = [ordered]@{ schemaVersion = 2; keyId = $Trust.KeyId
                payloadBase64 = $payloadText; signatureBase64 = $signatureText }
            if ($payloadJson.Text -cne (($canonical | ConvertTo-Json -Compress) + "`n") -or
                $json.Text -cne (($canonicalEnvelope | ConvertTo-Json -Compress) + "`n")) {
                throw 'Launcher feed is not in canonical LF form.'
            }
        }
        return $payloadJson.Text.TrimEnd("`n")
    }
    finally {
        $key.Dispose()
        $json.Document.Dispose()
        if ($null -ne $payloadJson) { $payloadJson.Document.Dispose() }
    }
}

function Write-SignedFeed([Collections.IDictionary] $Payload, [string] $PrivateKeyPemPath,
    [hashtable] $Trust, [string] $OutputPath, [bool] $Force) {
    $privateKey = Assert-RegularFile $PrivateKeyPemPath 'Private key'
    $key = [Security.Cryptography.ECDsa]::Create()
    try {
        $key.ImportFromPem([IO.File]::ReadAllText($privateKey))
        if ($key.KeySize -ne 256 -or
            $key.ExportParameters($true).Curve.Oid.Value -ne '1.2.840.10045.3.1.7' -or
            -not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals(
                $key.ExportSubjectPublicKeyInfo(), $Trust.PublicKey)) {
            throw 'Private key does not match the pinned P-256 identity.'
        }
        $payloadText = ($Payload | ConvertTo-Json -Compress -Depth 8) + "`n"
        $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($payloadText)
        $signature = $key.SignData($bytes,
            [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
        $envelope = [ordered]@{ schemaVersion = $Payload.schemaVersion; keyId = $Trust.KeyId
            payloadBase64 = [Convert]::ToBase64String($bytes)
            signatureBase64 = [Convert]::ToBase64String($signature) }
    }
    finally { $key.Dispose() }
    $output = [IO.Path]::GetFullPath($OutputPath)
    $parent = [IO.Path]::GetDirectoryName($output)
    if (-not [IO.Directory]::Exists($parent)) { throw 'Feed output directory is missing.' }
    if ([IO.File]::Exists($output)) {
        if (-not $Force) { throw 'Feed output exists; use -Force to replace it.' }
        $null = Assert-RegularFile $output 'Feed output'
    }
    $temporary = Join-Path $parent ('.' + [IO.Path]::GetFileName($output) +
        '.' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes(
            ($envelope | ConvertTo-Json -Compress) + "`n")
        $stream = [IO.FileStream]::new($temporary, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $stream.Write($bytes); $stream.Flush($true) }
        finally { $stream.Dispose() }
        $verified = Read-SignedFeed $temporary ($Payload.schemaVersion -eq 1) $Trust
        if ($verified -cne $payloadText.TrimEnd("`n")) { throw 'Feed did not round trip exactly.' }
        [IO.File]::Move($temporary, $output, $Force)
    }
    finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
    return $output
}
