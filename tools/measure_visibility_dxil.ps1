param(
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "build/shader_evidence/current"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repositoryRoot $BuildDirectory
$outputPath = Join-Path $repositoryRoot $OutputDirectory
$dxc = Join-Path $buildPath "_deps/dxc-src/bin/x64/dxc.exe"
if (-not (Test-Path -LiteralPath $dxc)) {
    throw "DXC was not found at $dxc. Configure the CMake build first."
}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$commonTraceDefines = @(
    "VISIBILITY_ESTIMATOR=1",
    "ENABLE_AO=1",
    "ENABLE_GI=1",
    "ENABLE_BOUNCE_REINJECTION=0",
    "INITIALIZE_BOUNCE_CUMULATIVE=0",
    "ENABLE_BOUNCE_METADATA=0"
)
$variants = @(
    @{
        Name = "Runtime Guarded AO And GI"
        Stem = "runtime_guarded_ao_gi"
        Source = "src/screen_space_visibility_cs.hlsl"
        Defines = $commonTraceDefines
    },
    @{
        Name = "Runtime Trusted Even AO And GI"
        Stem = "runtime_even_ao_gi"
        Source = "src/screen_space_visibility_cs.hlsl"
        Defines = $commonTraceDefines + @("RUNTIME_SAMPLE_PARITY=1")
    },
    @{
        Name = "Runtime Trusted Odd AO And GI"
        Stem = "runtime_odd_ao_gi"
        Source = "src/screen_space_visibility_cs.hlsl"
        Defines = $commonTraceDefines + @("RUNTIME_SAMPLE_PARITY=2")
    },
    @{
        Name = "Runtime Packed Edges AO And GI"
        Stem = "runtime_packed_edges_ao_gi"
        Source = "src/screen_space_visibility_composed_edges_cs.hlsl"
        Defines = $commonTraceDefines
    },
    @{
        Name = "Runtime Guarded Later Bounce"
        Stem = "runtime_guarded_later_bounce"
        Source = "src/screen_space_visibility_cs.hlsl"
        Defines = @(
            "VISIBILITY_ESTIMATOR=1",
            "ENABLE_AO=0",
            "ENABLE_GI=1",
            "ENABLE_BOUNCE_REINJECTION=1",
            "INITIALIZE_BOUNCE_CUMULATIVE=0",
            "ENABLE_BOUNCE_METADATA=0",
            "ENABLE_BOUNCE_CONTINUATION=0"
        )
    },
    @{
        Name = "Guide-Aware Resolve"
        Stem = "guide_aware_resolve"
        Source = "src/screen_space_visibility_filter_cs.hlsl"
        Defines = @("ENABLE_AO=1", "ENABLE_GI=0", "SPATIAL_FILTER=0")
    },
    @{
        Name = "Packed Edge Resolve"
        Stem = "packed_edge_resolve"
        Source = "src/screen_space_visibility_filter_packed_edge_cs.hlsl"
        Defines = @(
            "ENABLE_AO=1",
            "ENABLE_GI=0",
            "PACKED_EDGE_RECONSTRUCTION=1",
            "PACKED_EDGE_CONTROLLED_LEAKAGE=0"
        )
    },
    @{
        Name = "Fused Resolve And Apply"
        Stem = "fused_resolve_apply"
        Source = "src/screen_space_visibility_fused_apply_cs.hlsl"
        Defines = @(
            "FUSED_PACKED_EDGE_RECONSTRUCTION=0",
            "ENABLE_AO_POWER=0"
        )
    }
)

function Invoke-VariantCompile {
    param([hashtable]$Variant)

    $source = Join-Path $repositoryRoot $Variant.Source
    $dxil = Join-Path $outputPath ($Variant.Stem + ".dxil")
    $assembly = Join-Path $outputPath ($Variant.Stem + ".asm")
    $arguments = @(
        "-T", "cs_6_5", "-E", "main", "-O3",
        "-I", (Join-Path $repositoryRoot "donut/include"),
        "-D", "TARGET_D3D12", "-Fo", $dxil, "-Fc", $assembly, $source
    )
    foreach ($define in $Variant.Defines) {
        $arguments += @("-D", $define)
    }
    & $dxc @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "DXC failed for $($Variant.Name)."
    }
}

function Measure-Assembly {
    param([hashtable]$Variant)

    $assembly = Join-Path $outputPath ($Variant.Stem + ".asm")
    $dxil = Join-Path $outputPath ($Variant.Stem + ".dxil")
    $lines = Get-Content -LiteralPath $assembly
    $inMain = $false
    $body = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $lines) {
        if (-not $inMain -and $line -match '^define void @main\(\)') {
            $inMain = $true
            continue
        }
        if ($inMain -and $line -eq '}') {
            break
        }
        if ($inMain) {
            $body.Add($line)
        }
    }
    if ($body.Count -eq 0) {
        throw "Could not locate the optimized main function for $($Variant.Name)."
    }

    $instructionPattern = '^  (?:%[^=]+ = |call |br |ret |switch |unreachable)'
    [pscustomobject]@{
        Variant = $Variant.Name
        DxilBytes = (Get-Item -LiteralPath $dxil).Length
        StaticIrInstructions = @($body | Where-Object {
            $_ -match $instructionPattern }).Count
        TextureLoads = @($body | Where-Object {
            $_ -match '@dx\.op\.textureLoad' }).Count
        TextureStores = @($body | Where-Object {
            $_ -match '@dx\.op\.textureStore' }).Count
        TextureGathers = @($body | Where-Object {
            $_ -match '@dx\.op\.textureGather' }).Count
        CbufferLoads = @($body | Where-Object {
            $_ -match '@dx\.op\.cbufferLoad' }).Count
        Branches = @($body | Where-Object {
            $_ -match '^  br ' }).Count
        Transcendentals = @($body | Where-Object {
            $_ -match '@dx\.op\.unary\.' }).Count
    }
}

$results = foreach ($variant in $variants) {
    Invoke-VariantCompile -Variant $variant
    Measure-Assembly -Variant $variant
}

$csvPath = Join-Path $outputPath "visibility-dxil-report.csv"
$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation
$markdownPath = Join-Path $outputPath "visibility-dxil-report.md"
$markdown = [System.Collections.Generic.List[string]]::new()
$markdown.Add("# Visibility DXIL Comparison")
$markdown.Add("")
$markdown.Add("Generated with bundled DXC, shader model 6.5, and ``-O3``. Static IR counts are compiler evidence, not Intel Xe native ISA instruction or physical-register counts.")
$markdown.Add("")
$markdown.Add("| Variant | DXIL Bytes | Static IR Instructions | Texture Loads | Texture Stores | Texture Gathers | Constant-Buffer Loads | Branches | DXIL Unary Operations |")
$markdown.Add("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
foreach ($result in $results) {
    $markdown.Add("| $($result.Variant) | $($result.DxilBytes) | $($result.StaticIrInstructions) | $($result.TextureLoads) | $($result.TextureStores) | $($result.TextureGathers) | $($result.CbufferLoads) | $($result.Branches) | $($result.Transcendentals) |")
}
$markdown.Add("")
$markdown.Add("## Register And Occupancy Limitation")
$markdown.Add("")
$markdown.Add("DXIL uses SSA virtual values and does not expose the Intel driver's allocated GRF count, spills, SIMD width, or occupancy. Those fields require a target-hardware Intel compiler or GPA capture.")
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8

Write-Host "Wrote $markdownPath"
Write-Host "Wrote $csvPath"
