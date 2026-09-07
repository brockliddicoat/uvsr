[CmdletBinding()]
param(
    [string] $Path,
    [switch] $AllowTestKey,
    [string] $TestKeyId,
    [string] $TestPublicKeySpkiBase64
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'FeedTools.ps1')
if ([string]::IsNullOrWhiteSpace($Path)) {
    $Path = Join-Path $PSScriptRoot 'renderer-update-feed-v1.json'
}
$trust = Resolve-FeedTrust ([bool]$AllowTestKey) $TestKeyId $TestPublicKeySpkiBase64
Read-SignedFeed $Path $true $trust
