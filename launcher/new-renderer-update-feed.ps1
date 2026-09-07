[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $PrivateKeyPemPath,
    [Parameter(Mandatory)] [string] $ArtifactPath,
    [Parameter(Mandatory)] [string] $EnginePath,
    [Parameter(Mandatory)] [string] $RendererPackageValidatorPath,
    [Parameter(Mandatory)] [string] $ShaderInventoryPath,
    [Parameter(Mandatory)] [string] $AssetMapPath,
    [Parameter(Mandatory)] [long] $ReleaseSequence,
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
    $OutputPath = Join-Path $PSScriptRoot 'renderer-update-feed-v1.json'
}

function Invoke-ExactPackageValidator(
    [string] $ArchivePath,
    [string] $ValidatorPath,
    [string] $ShaderInventory,
    [string] $AssetMap) {
    $validator = Assert-RegularFile $ValidatorPath 'Renderer package validator'
    $shaders = Assert-RegularFile $ShaderInventory 'Runtime shader inventory'
    $assets = Assert-RegularFile $AssetMap 'Runtime asset map'
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
            --asset-map $assets | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw 'Renderer archive failed the exact package validator; signing is blocked.'
        }
        return Get-Content -LiteralPath (Join-Path $staging 'package-manifest.json') -Raw |
            ConvertFrom-Json -AsHashtable
    }
    finally {
        if ([IO.Directory]::Exists($staging)) {
            $resolved = [IO.Path]::GetFullPath($staging)
            if ($resolved + [IO.Path]::DirectorySeparatorChar -cne $stagingPrefix) {
                throw 'Renderer signing staging ownership is inconsistent.'
            }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}

if ($ReleaseSequence -lt 1 -or $ReleaseSequence -gt $feedSequenceLimit) {
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
    $sourceCommit = $identity['source_commit'].GetString()
    $settingsHash = $identity['settings_hash'].GetString()
    $engineVersion = $identity['engine_version'].GetString()
    if ($identity['executable'].GetString() -cne
            'uvsr-engine.exe' -or
        $sourceCommit -cnotmatch '^[0-9a-f]{40}$' -or
        $identity['source_identity'].GetString() -cne
            $sourceCommit -or
        -not $identity['source_tree_clean'].GetBoolean() -or
        -not $identity['production'].GetBoolean() -or
        $identity['configuration'].GetString() -cne
            'Release' -or
        $settingsHash -cnotmatch '^[0-9a-f]{32}$' -or
        -not (Test-FeedVersion $engineVersion 4 65535) -or
        $identity['product_version'].GetString() -cne
            "$engineVersion+$settingsHash") {
        throw 'Renderer engine identity is not canonical.'
    }
}
finally {
    $identityDocument.Dispose()
}

$artifact = Assert-RegularFile $ArtifactPath 'Renderer artifact'
if ([IO.Path]::GetFileName($artifact) -cne 'uvsr-renderer-windows-11-x64.zip') {
    throw 'Renderer artifact must be named uvsr-renderer-windows-11-x64.zip.'
}
$artifactSize = (Get-Item -LiteralPath $artifact).Length
if ($artifactSize -lt 1 -or $artifactSize -gt (32L * 1024 * 1024 * 1024)) {
    throw 'Renderer artifact size is outside its safe range.'
}
$manifest = Invoke-ExactPackageValidator $artifact $RendererPackageValidatorPath `
    $ShaderInventoryPath $AssetMapPath
$engineHash = (Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()
if ($manifest.releaseSequence -ne $ReleaseSequence -or
    $manifest.sourceCommit -cne $sourceCommit -or
    $manifest.settingsHash -cne $settingsHash -or
    $manifest.engineVersion -cne $engineVersion -or
    $manifest.executableSha256 -cne $engineHash) {
    throw 'Validated renderer package does not match the queried engine identity.'
}

$payload = [ordered]@{ schemaVersion = 1; productId = $feedProductId
    channel = 'stable'; releaseSequence = $ReleaseSequence; sourceCommit = $sourceCommit
    settingsHash = $settingsHash; engineVersion = $engineVersion
    artifact = [ordered]@{ name = 'uvsr-renderer-windows-11-x64.zip'; size = $artifactSize
        sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant() } }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))) | Out-Null
Write-SignedFeed $payload $PrivateKeyPemPath $trust $OutputPath ([bool]$Force)
