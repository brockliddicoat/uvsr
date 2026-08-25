[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $PrivateKeyPemPath,
    [Parameter(Mandatory)] [string] $ArtifactPath,
    [Parameter(Mandatory)] [string] $EnginePath,
    [Parameter(Mandatory)] [string] $RendererPackageValidatorPath,
    [Parameter(Mandatory)] [string] $ShaderInventoryPath,
    [Parameter(Mandatory)] [string] $MediaInventoryPath,
    [Parameter(Mandatory)] [long] $ReleaseSequence,
    [string] $OutputPath,
    [switch] $Force,
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
$maximumArtifactBytes = 32L * 1024 * 1024 * 1024
$maximumSequence = 9007199254740991L

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot 'renderer-update-feed-v1.json'
}
if ($AllowTestKey) {
    if ([string]::IsNullOrWhiteSpace($TestKeyId) -or
        [string]::IsNullOrWhiteSpace($TestPublicKeySpkiBase64)) {
        throw 'Test-key signing requires key ID and public key.'
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

function Assert-RegularFile([string] $Path, [string] $Description) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not [IO.File]::Exists($full)) {
        throw "$Description is missing at '$full'."
    }
    $item = Get-Item -LiteralPath $full -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must be a regular file."
    }
    return $full
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

function Read-Boolean(
    [Text.Json.JsonElement] $Element,
    [string] $Description) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::True -and
        $Element.ValueKind -ne [Text.Json.JsonValueKind]::False) {
        throw "$Description is not a Boolean."
    }
    return $Element.GetBoolean()
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

function Invoke-ExactPackageValidator(
    [string] $ArchivePath,
    [string] $ValidatorPath,
    [string] $ShaderInventory,
    [string] $MediaInventory) {
    $validator = Assert-RegularFile $ValidatorPath 'Renderer package validator'
    $shaders = Assert-RegularFile $ShaderInventory 'Runtime shader inventory'
    $media = Assert-RegularFile $MediaInventory 'Runtime media inventory'
    $staging = Join-Path ([IO.Path]::GetTempPath()) `
        ('uvsr-renderer-signing-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($staging) | Out-Null
    $stagingPrefix = $staging.TrimEnd(
        [IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    try {
        $seen = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        $expandedBytes = 0L
        $candidate = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
        try {
            foreach ($entry in $candidate.Entries) {
                $name = $entry.FullName
                if ([string]::IsNullOrEmpty($name) -or
                    $name.Length -gt 1024 -or $name.Contains('\') -or
                    $name.StartsWith('/') -or $name.Contains(':') -or
                    -not $seen.Add($name)) {
                    throw "Renderer archive contains an unsafe or duplicate path '$name'."
                }
                $directory = $name.EndsWith('/')
                $trimmed = $name.TrimEnd('/')
                if ([string]::IsNullOrEmpty($trimmed)) {
                    throw 'Renderer archive contains an empty root entry.'
                }
                foreach ($segment in $trimmed.Split('/')) {
                    if ([string]::IsNullOrEmpty($segment) -or
                        $segment -eq '.' -or $segment -eq '..') {
                        throw "Renderer archive contains an unsafe path '$name'."
                    }
                }
                $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
                if ($unixType -eq 0xA000) {
                    throw "Renderer archive contains a symbolic link '$name'."
                }
                $destination = [IO.Path]::GetFullPath((Join-Path $staging `
                    $trimmed.Replace('/', [IO.Path]::DirectorySeparatorChar)))
                if (-not $destination.StartsWith($stagingPrefix,
                        [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Renderer archive path escaped staging: '$name'."
                }
                if ($directory) {
                    [IO.Directory]::CreateDirectory($destination) | Out-Null
                    continue
                }
                $expandedBytes += $entry.Length
                if ($entry.Length -lt 0 -or $expandedBytes -gt 64L * 1024 * 1024 * 1024) {
                    throw 'Renderer archive expanded size is outside its safe range.'
                }
                [IO.Directory]::CreateDirectory(
                    [IO.Path]::GetDirectoryName($destination)) | Out-Null
                $source = $entry.Open()
                $target = [IO.FileStream]::new($destination,
                    [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
                    [IO.FileShare]::None)
                try { $source.CopyTo($target) }
                finally {
                    $target.Dispose()
                    $source.Dispose()
                }
            }
        }
        finally {
            $candidate.Dispose()
        }
        & $validator --check $staging --shader-inventory $shaders `
            --media-inventory $media
        if ($LASTEXITCODE -ne 0) {
            throw 'Renderer archive failed the exact package validator; signing is blocked.'
        }
    }
    finally {
        if ([IO.Directory]::Exists($staging)) {
            Remove-Item -LiteralPath $staging -Recurse -Force
        }
    }
}

if ($ReleaseSequence -lt 1 -or $ReleaseSequence -gt $maximumSequence) {
    throw 'Renderer release sequence is not canonical.'
}
$engine = Assert-RegularFile $EnginePath 'Renderer engine'
if ([IO.Path]::GetFileName($engine) -cne 'uvsr-engine.exe') {
    throw 'Renderer engine must use the canonical filename uvsr-engine.exe.'
}
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $engine
$start.ArgumentList.Add('--identity-json')
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
try {
    if (-not $process.Start()) {
        throw 'Renderer identity query could not start.'
    }
    $identityText = $process.StandardOutput.ReadToEnd()
    $identityError = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -or $identityText.Length -lt 1 -or
        $identityText.Length -gt 16 * 1024) {
        throw "Renderer identity query failed: $identityError"
    }
}
finally {
    $process.Dispose()
}
$identityDocument = [Text.Json.JsonDocument]::Parse($identityText)
try {
    $identity = Read-ExactObject $identityDocument.RootElement @(
        'executable','source_commit','source_identity','source_tree_clean',
        'production','configuration','settings_hash','engine_version',
        'product_version') 'Renderer engine identity'
    $sourceCommit = Read-String $identity['source_commit'] 'Source commit'
    $settingsHash = Read-String $identity['settings_hash'] 'Settings hash'
    $engineVersion = Read-String $identity['engine_version'] 'Engine version'
    if ((Read-String $identity['executable'] 'Executable') -cne
            'uvsr-engine.exe' -or
        $sourceCommit -cnotmatch '^[0-9a-f]{40}$' -or
        (Read-String $identity['source_identity'] 'Source identity') -cne
            $sourceCommit -or
        -not (Read-Boolean $identity['source_tree_clean'] 'Source-tree state') -or
        -not (Read-Boolean $identity['production'] 'Production state') -or
        (Read-String $identity['configuration'] 'Build configuration') -cne
            'Release' -or
        $settingsHash -cnotmatch '^[0-9a-f]{32}$' -or
        -not (Test-EngineVersion $engineVersion) -or
        (Read-String $identity['product_version'] 'Product version') -cne
            "$engineVersion+$settingsHash") {
        throw 'Renderer engine identity is not canonical.'
    }
}
finally {
    $identityDocument.Dispose()
}

$artifact = Assert-RegularFile $ArtifactPath 'Renderer artifact'
if ([IO.Path]::GetFileName($artifact) -cne $artifactName) {
    throw "Renderer artifact must be named '$artifactName'."
}
$artifactSize = (Get-Item -LiteralPath $artifact).Length
if ($artifactSize -lt 1 -or $artifactSize -gt $maximumArtifactBytes) {
    throw 'Renderer artifact size is outside its safe range.'
}
$archive = [IO.Compression.ZipFile]::OpenRead($artifact)
try {
    $manifestEntries = @($archive.Entries |
        Where-Object FullName -CEQ 'package-manifest.json')
    if ($manifestEntries.Count -ne 1 -or
        $manifestEntries[0].Length -lt 1 -or
        $manifestEntries[0].Length -gt 16 * 1024 * 1024) {
        throw 'Renderer package manifest is missing or ambiguous.'
    }
    $reader = [IO.StreamReader]::new(
        $manifestEntries[0].Open(), [Text.UTF8Encoding]::new($false, $true))
    try { $manifestText = $reader.ReadToEnd() }
    finally { $reader.Dispose() }
    $manifestDocument = [Text.Json.JsonDocument]::Parse($manifestText)
    try {
        $manifest = Read-ExactObject $manifestDocument.RootElement @(
            'schemaVersion','productId','production','configuration',
            'releaseSequence','sourceCommit','settingsHash','engineVersion',
            'executableSha256','files') 'Renderer package manifest'
        $manifestExecutableHash = Read-String -Element `
            $manifest['executableSha256'] -Description `
            'Manifest executable SHA-256'
        if ((Read-Int64 $manifest['schemaVersion'] 'Manifest schema') -ne 1 -or
            (Read-String $manifest['productId'] 'Manifest product ID') -cne
                $productId -or
            -not (Read-Boolean $manifest['production'] `
                'Manifest production state') -or
            (Read-String $manifest['configuration'] `
                'Manifest build configuration') -cne 'Release' -or
            (Read-Int64 $manifest['releaseSequence'] 'Manifest sequence') -ne
                $ReleaseSequence -or
            (Read-String $manifest['sourceCommit'] 'Manifest source commit') -cne
                $sourceCommit -or
            (Read-String $manifest['settingsHash'] 'Manifest settings hash') -cne
                $settingsHash -or
            (Read-String $manifest['engineVersion'] 'Manifest engine version') -cne
                $engineVersion -or
            $manifestExecutableHash -cnotmatch '^[0-9a-f]{64}$' -or
            $manifest['files'].ValueKind -ne
                [Text.Json.JsonValueKind]::Array -or
            $manifest['files'].GetArrayLength() -lt 1) {
            throw 'Renderer package manifest does not match the engine identity.'
        }
    }
    finally {
        $manifestDocument.Dispose()
    }
    $engineEntries = @($archive.Entries |
        Where-Object FullName -CEQ 'bin/uvsr-engine.exe')
    if ($engineEntries.Count -ne 1 -or $engineEntries[0].Length -lt 1) {
        throw 'Renderer package engine is missing or ambiguous.'
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $engineStream = $engineEntries[0].Open()
    try {
        $archiveEngineHash = [Convert]::ToHexString(
            $sha256.ComputeHash($engineStream)).ToLowerInvariant()
    }
    finally {
        $engineStream.Dispose()
        $sha256.Dispose()
    }
    $externalEngineHash = (Get-FileHash -LiteralPath $engine -Algorithm SHA256).
        Hash.ToLowerInvariant()
    if ($archiveEngineHash -cne $manifestExecutableHash -or
        $externalEngineHash -cne $manifestExecutableHash) {
        throw 'Renderer package engine does not match the queried engine identity.'
    }
}
finally {
    $archive.Dispose()
}

Invoke-ExactPackageValidator $artifact $RendererPackageValidatorPath `
    $ShaderInventoryPath $MediaInventoryPath

$privateKeyPath = Assert-RegularFile $PrivateKeyPemPath 'Private key'
$expectedPublicKey = Read-CanonicalBase64 $publicKeyBase64 'Pinned public key'
$ecdsa = [Security.Cryptography.ECDsa]::Create()
try {
    $ecdsa.ImportFromPem([IO.File]::ReadAllText($privateKeyPath))
    $actualPublicKey = $ecdsa.ExportSubjectPublicKeyInfo()
    $parameters = $ecdsa.ExportParameters($false)
    if ($ecdsa.KeySize -ne 256 -or
        $parameters.Curve.Oid.Value -ne '1.2.840.10045.3.1.7' -or
        -not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals(
            $actualPublicKey, $expectedPublicKey)) {
        throw 'Private key does not match the pinned P-256 identity.'
    }
    $artifactHash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).
        Hash.ToLowerInvariant()
    $payload = '{"schemaVersion":1,"productId":"' + $productId +
        '","channel":"stable","releaseSequence":' +
        $ReleaseSequence.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"sourceCommit":"' + $sourceCommit +
        '","settingsHash":"' + $settingsHash +
        '","engineVersion":"' + $engineVersion +
        '","artifact":{"name":"' + $artifactName +
        '","size":' +
        $artifactSize.ToString([Globalization.CultureInfo]::InvariantCulture) +
        ',"sha256":"' + $artifactHash + '"}}' + [char]10
    $payloadBytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($payload)
    $signature = $ecdsa.SignData(
        $payloadBytes,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    $envelope = '{"schemaVersion":1,"keyId":"' + $keyId +
        '","payloadBase64":"' + [Convert]::ToBase64String($payloadBytes) +
        '","signatureBase64":"' + [Convert]::ToBase64String($signature) +
        '"}' + [char]10
}
finally {
    $ecdsa.Dispose()
}

$output = [IO.Path]::GetFullPath($OutputPath)
if ([IO.File]::Exists($output) -and -not $Force) {
    throw "Output already exists at '$output'; use -Force to replace it."
}
$parent = [IO.Path]::GetDirectoryName($output)
[IO.Directory]::CreateDirectory($parent) | Out-Null
$temporary = Join-Path $parent ('.' + [IO.Path]::GetFileName($output) +
    '.' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    [IO.File]::WriteAllText($temporary, $envelope,
        [Text.UTF8Encoding]::new($false, $true))
    Move-Item -LiteralPath $temporary -Destination $output -Force:$Force
}
finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}
$verifyArguments = @{ Path = $output }
if ($AllowTestKey) {
    $verifyArguments.AllowTestKey = $true
    $verifyArguments.TestKeyId = $keyId
    $verifyArguments.TestPublicKeySpkiBase64 = $publicKeyBase64
}
& (Join-Path $PSScriptRoot 'verify-renderer-update-feed.ps1') @verifyArguments |
    Out-Null
Write-Output $output
