[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $LauncherTests,
    [Parameter(Mandatory)] [string] $OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'FeedTools.ps1')
$launcherFeed = & (Join-Path $PSScriptRoot 'verify-launcher-update-feed.ps1') | ConvertFrom-Json
$rendererFeed = & (Join-Path $PSScriptRoot 'verify-renderer-update-feed.ps1') | ConvertFrom-Json
$tests = Assert-RegularFile $LauncherTests 'launcher contract tests'
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'release proof output must be a new directory' }
[IO.Directory]::CreateDirectory($output) | Out-Null

function Get-ReleaseArtifact($Feed, [string] $Tag) {
    # release assets, rather than this checkout's newly built launcher, own the
    # published sequence. draft assets can be proved before feed activation.
    $releaseText = & gh release view $Tag --repo brockliddicoat/uvsr --json tagName,targetCommitish,isDraft,assets
    if ($LASTEXITCODE -ne 0) { throw "release is unavailable: $Tag" }
    $release = $releaseText | ConvertFrom-Json
    if ($release.tagName -cne $Tag -or $release.targetCommitish -cne $Feed.sourceCommit) {
        throw 'release target differs from the signed source commit'
    }
    if (-not $release.isDraft) {
        $refText = & gh api "repos/brockliddicoat/uvsr/git/ref/tags/$Tag"
        if ($LASTEXITCODE -ne 0) { throw 'published release tag is unavailable' }
        $object = ($refText | ConvertFrom-Json).object
        for ($depth = 0; $object.type -eq 'tag' -and $depth -lt 8; ++$depth) {
            $tagText = & gh api "repos/brockliddicoat/uvsr/git/tags/$($object.sha)"
            if ($LASTEXITCODE -ne 0) { throw 'annotated release tag is unavailable' }
            $object = ($tagText | ConvertFrom-Json).object
        }
        if ($object.type -cne 'commit' -or $object.sha -cne $Feed.sourceCommit) {
            throw 'published release tag differs from the signed source commit'
        }
    }
    $assets = @($release.assets | Where-Object name -CEQ $Feed.artifact.name)
    if ($assets.Count -ne 1 -or $assets[0].size -ne $Feed.artifact.size -or
        $assets[0].digest -cne "sha256:$($Feed.artifact.sha256)") {
        throw 'release asset metadata differs from its signed feed'
    }
    & gh release download $Tag --repo brockliddicoat/uvsr --pattern $Feed.artifact.name --dir $output | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'release artifact download failed' }
    $path = Assert-RegularFile (Join-Path $output $Feed.artifact.name) 'downloaded release artifact'
    if ((Get-Item -LiteralPath $path).Length -ne $Feed.artifact.size -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Feed.artifact.sha256) {
        throw 'downloaded artifact bytes differ from their signed feed'
    }
    return $path
}

$launcher = Get-ReleaseArtifact $launcherFeed "uvsr-launcher-v$($launcherFeed.version)"
$archive = Get-ReleaseArtifact $rendererFeed "uvsr-engine-r$($rendererFeed.releaseSequence)"
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($launcher)
if ($metadata.ProductName -cne 'UVSR Launcher' -or
    $metadata.FileVersion -cne "$($launcherFeed.version).0" -or
    $metadata.ProductVersion -cne "$($launcherFeed.version)+$($launcherFeed.sourceCommit)") {
    throw 'downloaded launcher PE metadata differs from its signed identity'
}
& $tests --verify-launcher-health $launcher
if ($LASTEXITCODE -ne 0) { throw 'release launcher health failed' }
& $tests --verify-renderer-archive $archive $rendererFeed.sourceCommit $rendererFeed.settingsHash $rendererFeed.engineVersion $rendererFeed.releaseSequence
if ($LASTEXITCODE -ne 0) { throw 'release archive installation contracts failed' }

$package = Join-Path $output 'renderer'
Expand-Archive -LiteralPath $archive -DestinationPath $package
$manifest = Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.sourceCommit -cne $rendererFeed.sourceCommit -or
    $manifest.releaseSequence -ne $rendererFeed.releaseSequence -or
    $manifest.settingsHash -cne $rendererFeed.settingsHash -or
    $manifest.engineVersion -cne $rendererFeed.engineVersion) {
    throw 'downloaded renderer manifest differs from its signed identity'
}
& $tests --verify-production-services (Join-Path $package 'bin/uvsr-engine.exe') $launcher
if ($LASTEXITCODE -ne 0) { throw 'release engine and launcher service contracts failed' }
Write-Output 'verified signed release bytes, launcher health, renderer installation and recovery, process identity, and COM shortcuts'
