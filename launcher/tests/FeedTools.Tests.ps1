$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../FeedTools.ps1')

function Assert-Rejected([scriptblock] $Action) {
    try { & $Action | Out-Null }
    catch { return }
    throw 'An invalid feed was accepted.'
}

$root = Join-Path ([IO.Path]::GetTempPath()) ('uvsr-feed-contracts-' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($root)
$key = [Security.Cryptography.ECDsa]::Create([Security.Cryptography.ECCurve+NamedCurves]::nistP256)
try {
    $pem = Join-Path $root 'test-key.pem'
    [IO.File]::WriteAllText($pem, $key.ExportECPrivateKeyPem())
    $spki = [Convert]::ToBase64String($key.ExportSubjectPublicKeyInfo())
    $trust = Resolve-FeedTrust $true 'test-key' $spki
    foreach ($renderer in @($false, $true)) {
        $schema = if ($renderer) { 1 } else { 2 }
        $component = if ($renderer) { 'renderer' } else { 'launcher' }
        $artifact = if ($renderer) { 'uvsr-renderer-windows-11-x64.zip' } else { 'uvsr-launcher.exe' }
        $payload = [ordered]@{ schemaVersion = $schema; productId = $feedProductId
            channel = 'stable'; releaseSequence = 16 }
        if (-not $renderer) { $payload.version = '1.2.0' }
        $payload.sourceCommit = 'a' * 40
        if ($renderer) {
            $payload.settingsHash = 'b' * 32
            $payload.engineVersion = '1.2.3.4'
        }
        $payload.artifact = [ordered]@{ name = $artifact; size = 1234; sha256 = 'c' * 64 }
        $path = Join-Path $root "$component.json"
        $null = Write-SignedFeed $payload $pem $trust $path $false
        $expected = $payload | ConvertTo-Json -Compress -Depth 8
        $actual = & (Join-Path $PSScriptRoot "../verify-$component-update-feed.ps1") `
            -Path $path -AllowTestKey -TestKeyId 'test-key' -TestPublicKeySpkiBase64 $spki
        if ($actual -cne $expected) { throw 'Signed feed did not round trip.' }
        Assert-Rejected { Read-SignedFeed $path $renderer (Resolve-FeedTrust $false '' '') }
        Assert-Rejected { Write-SignedFeed $payload $pem $trust $path $false }
        $original = [IO.File]::ReadAllText($path)
        foreach ($text in @(
            $original.Replace('"keyId":"test-key"', '"keyId":"wrong-key"'),
            $original.Replace('"schemaVersion":', '"SchemaVersion":'),
            $original.Replace('"keyId":', '"keyId":"test-key","keyId":'),
            $original.Replace('"signatureBase64":"', '"signatureBase64":"A'),
            ([char]0xfeff + $original),
            (' ' * 16385))) {
            [IO.File]::WriteAllText($path, $text)
            Assert-Rejected { Read-SignedFeed $path $renderer $trust }
        }
        [IO.File]::WriteAllText($path, $original)
        foreach ($bad in @(
            { $payload.artifact.name = 'old-name.exe' },
            { $payload.releaseSequence = 9007199254740992L },
            { $payload.sourceCommit = 'A' * 40 },
            { $payload.artifact.size = 0 },
            { $payload.artifact.sha256 = 'C' * 64 })) {
            & $bad
            Assert-Rejected { Write-SignedFeed $payload $pem $trust $path $true }
            if ([IO.File]::ReadAllText($path) -cne $original) {
                throw 'Rejected feed changed the active output.'
            }
            $payload = $expected | ConvertFrom-Json -AsHashtable
        }
        if (-not $renderer) {
            [IO.File]::WriteAllText($path, $original.TrimEnd("`n"))
            Assert-Rejected { Read-SignedFeed $path $renderer $trust }
        }
        Write-Output "PASS $component signing, trust, canonical fields, rejection, and atomic replacement"
    }
}
finally {
    $key.Dispose()
    $resolved = [IO.Path]::GetFullPath($root)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'uvsr-feed-contracts-*') {
        throw 'Feed test cleanup escaped its temporary directory.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
