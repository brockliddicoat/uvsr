[CmdletBinding()]
param(
    [string]$DestinationRoot = "work/performance-monitoring",

    [switch]$VerifyOptionalHWiNFO
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$DestinationRoot = if ([IO.Path]::IsPathRooted($DestinationRoot))
{
    [IO.Path]::GetFullPath($DestinationRoot)
}
else
{
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $DestinationRoot))
}

$windowsPlatform =
    [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [Runtime.InteropServices.OSPlatform]::Windows)
$osArchitecture =
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
if (-not $windowsPlatform)
{
    throw "This bootstrap supports UVSR's Windows/DX12 hosts only."
}
if ($osArchitecture -ne [Runtime.InteropServices.Architecture]::X64)
{
    throw "The pinned PresentMon release asset supports Windows x64 only."
}

$downloadRoot = Join-Path $DestinationRoot "downloads"
$licenseRoot = Join-Path $DestinationRoot "licenses"
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null

$headers = @{
    "User-Agent" = "UVSR-Performance-Tool-Bootstrap"
}

function Get-VerifiedAsset
{
    param(
        [Parameter(Mandatory)]
        [string]$Uri,

        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [uint64]$ExpectedBytes,

        [Parameter(Mandatory)]
        [string]$ExpectedSha256
    )

    $isExpectedAsset = {
        param([string]$CandidatePath)

        if (-not (Test-Path -LiteralPath $CandidatePath `
                -PathType Leaf))
        {
            return $false
        }
        $candidate = Get-Item -LiteralPath $CandidatePath
        if ([uint64]$candidate.Length -ne $ExpectedBytes)
        {
            return $false
        }
        return (Get-FileHash -Algorithm SHA256 `
            -LiteralPath $CandidatePath).Hash -eq $ExpectedSha256
    }

    if (& $isExpectedAsset $Path)
    {
        return Get-Item -LiteralPath $Path
    }

    if (Test-Path -LiteralPath $Path)
    {
        $quarantinePath = "$Path.invalid-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))-$([Guid]::NewGuid().ToString('N'))"
        Move-Item -LiteralPath $Path -Destination $quarantinePath
        Write-Warning "Preserved an invalid existing asset at $quarantinePath"
    }

    $temporaryPath =
        "$Path.download-$PID-$([Guid]::NewGuid().ToString('N')).tmp"
    try
    {
        Invoke-WebRequest -Headers $headers -Uri $Uri `
            -OutFile $temporaryPath
        if (-not (& $isExpectedAsset $temporaryPath))
        {
            throw "Downloaded asset identity does not match: $Uri"
        }

        if (-not (& $isExpectedAsset $Path))
        {
            try
            {
                Move-Item -LiteralPath $temporaryPath `
                    -Destination $Path
            }
            catch
            {
                if (-not (& $isExpectedAsset $Path))
                {
                    throw
                }
            }
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $temporaryPath)
        {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }

    if (-not (& $isExpectedAsset $Path))
    {
        throw "Verified asset installation failed: $Path"
    }
    return Get-Item -LiteralPath $Path
}

function Get-ZipFileManifest
{
    param(
        [Parameter(Mandatory)]
        [string]$ArchivePath
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try
    {
        $manifest = @()
        foreach ($entry in $archive.Entries)
        {
            if ([string]::IsNullOrWhiteSpace($entry.Name))
            {
                continue
            }
            $stream = $entry.Open()
            $sha256 = [Security.Cryptography.SHA256]::Create()
            try
            {
                $hash = [BitConverter]::ToString(
                    $sha256.ComputeHash($stream)).Replace("-", "")
            }
            finally
            {
                $sha256.Dispose()
                $stream.Dispose()
            }
            $manifest += [pscustomobject]@{
                Path = $entry.FullName.Replace("\", "/")
                Bytes = [uint64]$entry.Length
                Sha256 = $hash
            }
        }
        return @($manifest | Sort-Object Path)
    }
    finally
    {
        $archive.Dispose()
    }
}

function Assert-ArchiveExtraction
{
    param(
        [Parameter(Mandatory)]
        [string]$ArchivePath,

        [Parameter(Mandatory)]
        [string]$ExtractionRoot
    )

    if (-not (Test-Path -LiteralPath $ExtractionRoot `
            -PathType Container))
    {
        throw "Archive extraction is missing: $ExtractionRoot"
    }

    $root = (Resolve-Path -LiteralPath $ExtractionRoot).Path.TrimEnd(
        '\', '/')
    $expected = @(Get-ZipFileManifest -ArchivePath $ArchivePath)
    $actualFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File)
    $actual = @{}
    foreach ($file in $actualFiles)
    {
        $relativePath = $file.FullName.Substring($root.Length).TrimStart(
            '\', '/').Replace("\", "/")
        $actual[$relativePath] = $file
    }

    if ($actual.Count -ne $expected.Count)
    {
        throw "Unexpected extracted-file count under $ExtractionRoot."
    }
    foreach ($entry in $expected)
    {
        if (-not $actual.ContainsKey($entry.Path))
        {
            throw "Expected extracted file is missing: $($entry.Path)"
        }
        $file = $actual[$entry.Path]
        if ([uint64]$file.Length -ne $entry.Bytes)
        {
            throw "Unexpected extracted byte count: $($entry.Path)"
        }
        $hash = (Get-FileHash -Algorithm SHA256 `
            -LiteralPath $file.FullName).Hash
        if ($hash -ne $entry.Sha256)
        {
            throw "Unexpected extracted SHA-256: $($entry.Path)"
        }
    }
}

function Expand-VerifiedArchive
{
    param(
        [Parameter(Mandatory)]
        [string]$ArchivePath,

        [Parameter(Mandatory)]
        [string]$ExtractionRoot
    )

    $destinationFullPath = [IO.Path]::GetFullPath($ExtractionRoot)
    $destinationParent = [IO.Path]::GetFullPath(
        (Split-Path -Parent $destinationFullPath)).TrimEnd('\')
    $allowedRoot =
        [IO.Path]::GetFullPath($DestinationRoot).TrimEnd('\') + '\'
    if (-not ($destinationFullPath + '\').StartsWith(
            $allowedRoot,
            [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Extraction target is outside the monitoring-tool root."
    }

    if (Test-Path -LiteralPath $destinationFullPath)
    {
        try
        {
            Assert-ArchiveExtraction `
                -ArchivePath $ArchivePath `
                -ExtractionRoot $destinationFullPath
            return
        }
        catch
        {
            $quarantinePath =
                "$destinationFullPath.invalid-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))-$([Guid]::NewGuid().ToString('N'))"
            Move-Item -LiteralPath $destinationFullPath `
                -Destination $quarantinePath
            Write-Warning "Preserved an invalid extraction at $quarantinePath"
        }
    }

    $temporaryRoot = Join-Path $destinationParent `
        ".$([IO.Path]::GetFileName($destinationFullPath)).extracting-$PID-$([Guid]::NewGuid().ToString('N'))"
    try
    {
        Expand-Archive -LiteralPath $ArchivePath `
            -DestinationPath $temporaryRoot
        Assert-ArchiveExtraction `
            -ArchivePath $ArchivePath `
            -ExtractionRoot $temporaryRoot

        if (-not (Test-Path -LiteralPath $destinationFullPath))
        {
            try
            {
                Move-Item -LiteralPath $temporaryRoot `
                    -Destination $destinationFullPath
            }
            catch
            {
                Assert-ArchiveExtraction `
                    -ArchivePath $ArchivePath `
                    -ExtractionRoot $destinationFullPath
            }
        }
        else
        {
            Assert-ArchiveExtraction `
                -ArchivePath $ArchivePath `
                -ExtractionRoot $destinationFullPath
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $temporaryRoot)
        {
            $temporaryFullPath = [IO.Path]::GetFullPath($temporaryRoot)
            if ((Split-Path -Parent $temporaryFullPath) -ne
                $destinationParent -or
                [IO.Path]::GetFileName($temporaryFullPath) -notmatch
                    '^\..+\.extracting-\d+-[0-9a-f]{32}$')
            {
                throw "Refusing to remove an unexpected temporary path."
            }
            Remove-Item -LiteralPath $temporaryFullPath `
                -Recurse -Force
        }
    }
}

$libreArchive = Get-VerifiedAsset `
    -Uri "https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v0.9.6/LibreHardwareMonitor.zip" `
    -Path (Join-Path $downloadRoot "LibreHardwareMonitor-v0.9.6.zip") `
    -ExpectedBytes 6632626 `
    -ExpectedSha256 "086D9F1B5A99E643EDC2CFAAAC16051685B551E4C5AC0B32A57C58C0E529C001"

$presentMon = Get-VerifiedAsset `
    -Uri "https://github.com/GameTechDev/PresentMon/releases/download/v2.5.1/PresentMon-2.5.1-x64.exe" `
    -Path (Join-Path $downloadRoot "PresentMon-2.5.1-x64.exe") `
    -ExpectedBytes 956768 `
    -ExpectedSha256 "9BEC3083069F58F911E6A512F4806DB51A27BD096103087BC1D05EF54C80A191"

$licenseAssets = @(
    @{
        Uri = "https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v0.9.6/LICENSE"
        Name = "LibreHardwareMonitor-LICENSE"
        Bytes = 16725
        Sha256 = "1F256ECAD192880510E84AD60474EAB7589218784B9A50BC7CEEE34C2B91F1D5"
    },
    @{
        Uri = "https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v0.9.6/THIRD-PARTY-NOTICES.txt"
        Name = "LibreHardwareMonitor-THIRD-PARTY-NOTICES.txt"
        Bytes = 25905
        Sha256 = "A60D5EE62F4D700CAFF38566F42874D554B8B530437C72804FB28B958CFBDA9B"
    },
    @{
        Uri = "https://raw.githubusercontent.com/GameTechDev/PresentMon/v2.5.1/LICENSE.txt"
        Name = "PresentMon-LICENSE.txt"
        Bytes = 1067
        Sha256 = "4C949341B1893C8C6AD82F7FB4EEDF622CD1FD9C22A9AF8F19B2DAC19D1947B6"
    },
    @{
        Uri = "https://raw.githubusercontent.com/GameTechDev/PresentMon/v2.5.1/THIRD_PARTY.txt"
        Name = "PresentMon-THIRD_PARTY.txt"
        Bytes = 6471
        Sha256 = "E039937F1A2FC2EB8F24A25B4229551A5A8056026D2DA878507E20269EB56267"
    }
)

foreach ($asset in $licenseAssets)
{
    Get-VerifiedAsset `
        -Uri $asset.Uri `
        -Path (Join-Path $licenseRoot $asset.Name) `
        -ExpectedBytes $asset.Bytes `
        -ExpectedSha256 $asset.Sha256 | Out-Null
}

$libreRoot = Join-Path $DestinationRoot "LibreHardwareMonitor-v0.9.6"
Expand-VerifiedArchive `
    -ArchivePath $libreArchive.FullName `
    -ExtractionRoot $libreRoot

$libreExecutable = Join-Path $libreRoot "LibreHardwareMonitor.exe"
$libreLibrary = Join-Path $libreRoot "LibreHardwareMonitorLib.dll"
$expectedExtractedFiles = @(
    @{
        Path = $libreExecutable
        Sha256 = "FE216A48A48A6048156A133A39B960437D781A4C9214E5ABC0F26F666F61BA22"
    },
    @{
        Path = $libreLibrary
        Sha256 = "6EBC194316536BA61AF5BE24508AD9FCBB2ECC685E716C12E787C79530F66BF0"
    }
)
foreach ($file in $expectedExtractedFiles)
{
    if (-not (Test-Path -LiteralPath $file.Path))
    {
        throw "Expected extracted file is missing: $($file.Path)"
    }
    $actualSha256 =
        (Get-FileHash -Algorithm SHA256 -LiteralPath $file.Path).Hash
    if ($actualSha256 -ne $file.Sha256)
    {
        throw "Unexpected SHA-256 for extracted file $($file.Path)."
    }
}

$presentMonSignature =
    Get-AuthenticodeSignature -LiteralPath $presentMon.FullName
if ($presentMonSignature.Status -ne "Valid" -or
    $presentMonSignature.SignerCertificate.Subject -notmatch
        "CN=Intel Corporation")
{
    throw "PresentMon's Intel Authenticode signature is not valid."
}

$hwInfoExecutable = $null
if ($VerifyOptionalHWiNFO.IsPresent)
{
    # HWiNFO is deliberately not downloaded by this bootstrap. Its portable
    # build temporarily loads a kernel driver when executed, commercial use is
    # licensed separately, and redistribution requires approval. This opt-in
    # block only verifies and extracts an archive the user already downloaded
    # from https://www.hwinfo.com/download/; it never executes a binary.
    $hwInfoArchivePath = Join-Path $downloadRoot "hwi_850.zip"
    if (-not (Test-Path -LiteralPath $hwInfoArchivePath))
    {
        throw "Optional HWiNFO archive is missing: $hwInfoArchivePath"
    }
    $hwInfoArchive = Get-Item -LiteralPath $hwInfoArchivePath
    if ($hwInfoArchive.Length -ne 20824195)
    {
        throw "Unexpected HWiNFO archive byte count."
    }
    $hwInfoArchiveHash =
        (Get-FileHash -Algorithm SHA256 `
            -LiteralPath $hwInfoArchivePath).Hash
    if ($hwInfoArchiveHash -ne
        "90F4F896B1CEF5D211E3F7721DE8ADA901245159274790840CA7072942271B45")
    {
        throw "Unexpected HWiNFO archive SHA-256."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead(
        $hwInfoArchive.FullName)
    try
    {
        $entryNames = @($archive.Entries.FullName | Sort-Object)
        $expectedEntryNames = @(
            "HWiNFO32.exe",
            "HWiNFO64.exe",
            "HWiNFO_ARM64.exe"
        ) | Sort-Object
        if (($entryNames -join "`n") -ne
            ($expectedEntryNames -join "`n"))
        {
            throw "Unexpected HWiNFO archive contents."
        }
    }
    finally
    {
        $archive.Dispose()
    }

    $hwInfoRoot = Join-Path $DestinationRoot "HWiNFO-v8.50"
    Expand-VerifiedArchive `
        -ArchivePath $hwInfoArchive.FullName `
        -ExtractionRoot $hwInfoRoot
    $hwInfoFiles = @(
        @{
            Name = "HWiNFO32.exe"
            Sha256 = "6B67372D41BD84D0952A0E810DA0E73B918D6A463814B8C5F15ECB1B08A0CD3A"
        },
        @{
            Name = "HWiNFO64.exe"
            Sha256 = "B022C6D2E8C3E816548220ECC1022A2931864129B152BDA740795A0B53721342"
        },
        @{
            Name = "HWiNFO_ARM64.exe"
            Sha256 = "7F71912EED21B88160AEE8E8C63349A702AAA024CC5F24165FCF73E585C2B835"
        }
    )
    foreach ($file in $hwInfoFiles)
    {
        $path = Join-Path $hwInfoRoot $file.Name
        if (-not (Test-Path -LiteralPath $path) -or
            (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash -ne
                $file.Sha256)
        {
            throw "Unexpected HWiNFO executable identity: $path"
        }
        $item = Get-Item -LiteralPath $path
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($item.VersionInfo.ProductVersion -ne "8.50-6020" -or
            $signature.Status -ne "Valid" -or
            $signature.SignerCertificate.Subject -notmatch
                'CN="?REALiX, s\.r\.o\."?')
        {
            throw "HWiNFO version or REALiX signature validation failed: $path"
        }
    }
    $hwInfoExecutable = Join-Path $hwInfoRoot "HWiNFO64.exe"
}

[pscustomobject]@{
    LibreHardwareMonitorVersion = "0.9.6"
    LibreHardwareMonitorPath = $libreExecutable
    PresentMonVersion = "2.5.1"
    PresentMonPath = $presentMon.FullName
    PresentMonSigner =
        $presentMonSignature.SignerCertificate.Subject
    OptionalHWiNFOPath = $hwInfoExecutable
    LicensePath = $licenseRoot
}
