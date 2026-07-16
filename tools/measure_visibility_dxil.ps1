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

$variants = @(
    @{
        Name = "Reference Generic AO"
        Stem = "reference_generic_ao"
        Source = "src/screen_space_visibility_cs.hlsl"
        Defines = @(
            "VISIBILITY_ESTIMATOR=1", "ENABLE_AO=1", "ENABLE_GI=0",
            "ENABLE_BOUNCE_REINJECTION=0", "INITIALIZE_BOUNCE_CUMULATIVE=0",
            "ENABLE_BOUNCE_METADATA=0", "ENABLE_ADAPTIVE_SPARSE_SAMPLING=0")
    },
    @{
        Name = "Exact Fixed8 AO"
        Stem = "exact_fixed8_ao"
        Source = "src/screen_space_visibility_fixed_cs.hlsl"
        Defines = @(
            "VISIBILITY_ESTIMATOR=1", "ENABLE_AO=1", "ENABLE_GI=0",
            "ENABLE_BOUNCE_REINJECTION=0", "INITIALIZE_BOUNCE_CUMULATIVE=0",
            "ENABLE_BOUNCE_METADATA=0", "FIXED_SAMPLE_COUNT=8",
            "FIXED_DIRECT_DEPTH=1", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Exact Fixed12 AO"
        Stem = "exact_fixed12_ao"
        Source = "src/screen_space_visibility_fixed_cs.hlsl"
        Defines = @(
            "VISIBILITY_ESTIMATOR=1", "ENABLE_AO=1", "ENABLE_GI=0",
            "ENABLE_BOUNCE_REINJECTION=0", "INITIALIZE_BOUNCE_CUMULATIVE=0",
            "ENABLE_BOUNCE_METADATA=0", "FIXED_SAMPLE_COUNT=12",
            "FIXED_DIRECT_DEPTH=1", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Exact Fixed16 AO"
        Stem = "exact_fixed16_ao"
        Source = "src/screen_space_visibility_fixed_cs.hlsl"
        Defines = @(
            "VISIBILITY_ESTIMATOR=1", "ENABLE_AO=1", "ENABLE_GI=0",
            "ENABLE_BOUNCE_REINJECTION=0", "INITIALIZE_BOUNCE_CUMULATIVE=0",
            "ENABLE_BOUNCE_METADATA=0", "FIXED_SAMPLE_COUNT=16",
            "FIXED_DIRECT_DEPTH=1", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Exact Fixed20 AO"
        Stem = "exact_fixed20_ao"
        Source = "src/screen_space_visibility_fixed_cs.hlsl"
        Defines = @(
            "VISIBILITY_ESTIMATOR=1", "ENABLE_AO=1", "ENABLE_GI=0",
            "ENABLE_BOUNCE_REINJECTION=0", "INITIALIZE_BOUNCE_CUMULATIVE=0",
            "ENABLE_BOUNCE_METADATA=0", "FIXED_SAMPLE_COUNT=20",
            "FIXED_DIRECT_DEPTH=1", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Exact Packed FAST AO"
        Stem = "exact_packed_fast_ao"
        Source = "src/screen_space_visibility_packed_fast_cs.hlsl"
        Defines = @(
            "ENABLE_AO=1", "ENABLE_GI=0", "ENABLE_BOUNCE_METADATA=0",
            "FIXED_DIRECT_DEPTH=1")
    },
    @{
        Name = "Diagnostic Constant Trace"
        Stem = "diagnostic_constant_trace"
        Source = "src/screen_space_visibility_diagnostic_cs.hlsl"
        Defines = @("TRACE_DIAGNOSTIC=1", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Diagnostic Depth Trace"
        Stem = "diagnostic_depth_trace"
        Source = "src/screen_space_visibility_diagnostic_cs.hlsl"
        Defines = @("TRACE_DIAGNOSTIC=2", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Diagnostic Bitmask Trace"
        Stem = "diagnostic_bitmask_trace"
        Source = "src/screen_space_visibility_diagnostic_cs.hlsl"
        Defines = @("TRACE_DIAGNOSTIC=3", "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "Activision Schedule Bitmask"
        Stem = "activision_schedule_bitmask"
        Source = "src/screen_space_visibility_algorithmic_cs.hlsl"
        Defines = @(
            "VISIBILITY_ALGORITHM=1", "FIXED_DIRECT_DEPTH=1",
            "SCHEDULER_SPECIALIZATION=0")
    },
    @{
        Name = "Eight-Sample Horizon Control"
        Stem = "eight_sample_horizon_control"
        Source = "src/screen_space_visibility_algorithmic_cs.hlsl"
        Defines = @(
            "VISIBILITY_ALGORITHM=2", "FIXED_DIRECT_DEPTH=1",
            "SCHEDULER_SPECIALIZATION=2")
    },
    @{
        Name = "PS4 Schedule Horizon Control"
        Stem = "ps4_schedule_horizon_control"
        Source = "src/screen_space_visibility_algorithmic_cs.hlsl"
        Defines = @(
            "VISIBILITY_ALGORITHM=3", "FIXED_DIRECT_DEPTH=1",
            "SCHEDULER_SPECIALIZATION=0")
    },
    @{
        Name = "Reference Compact Resolve"
        Stem = "reference_compact_resolve"
        Source = "src/screen_space_visibility_filter_cs.hlsl"
        Defines = @("ENABLE_AO=1", "ENABLE_GI=0", "SPATIAL_FILTER=0")
    },
    @{
        Name = "Exact Fast Compact Resolve"
        Stem = "exact_fast_compact_resolve"
        Source = "src/screen_space_visibility_filter_exact_cs.hlsl"
        Defines = @("SPATIAL_FILTER=0")
    },
    @{
        Name = "Reference Gaussian Resolve"
        Stem = "reference_gaussian_resolve"
        Source = "src/screen_space_visibility_filter_cs.hlsl"
        Defines = @("ENABLE_AO=1", "ENABLE_GI=0", "SPATIAL_FILTER=1")
    },
    @{
        Name = "Packed Edge 4x4 Resolve"
        Stem = "packed_edge_4x4_resolve"
        Source = "src/screen_space_visibility_filter_packed_edge_cs.hlsl"
        Defines = @("PACKED_EDGE_RECONSTRUCTION=2")
    },
    @{
        Name = "Exact Fused Resolve And Apply"
        Stem = "exact_fused_resolve_apply"
        Source = "src/screen_space_visibility_fused_apply_cs.hlsl"
        Defines = @("FUSED_PACKED_EDGE_RECONSTRUCTION=0")
    },
    @{
        Name = "Reference Composition"
        Stem = "reference_composition"
        Source = "src/screen_space_indirect_composite_cs.hlsl"
        Defines = @()
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
        "-D", "TARGET_D3D12", "-Fo", $dxil, "-Fc", $assembly, $source)
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
$markdown.Add("Generated with bundled DXC, shader model 6.5, and `-O3`. Static IR counts are compiler-level evidence, not Intel Xe native ISA instruction or physical-register counts.")
$markdown.Add("")
$markdown.Add("| Variant | DXIL Bytes | Static IR Instructions | Texture Loads | Texture Stores | Texture Gathers | Constant-Buffer Loads | Branches | DXIL Unary Operations |")
$markdown.Add("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
foreach ($result in $results) {
    $markdown.Add("| $($result.Variant) | $($result.DxilBytes) | $($result.StaticIrInstructions) | $($result.TextureLoads) | $($result.TextureStores) | $($result.TextureGathers) | $($result.CbufferLoads) | $($result.Branches) | $($result.Transcendentals) |")
}
$markdown.Add("")
$markdown.Add("## Register And Occupancy Limitation")
$markdown.Add("")
$markdown.Add("DXIL uses SSA virtual values and does not expose the Intel driver's allocated GRF count, spills, SIMD width, or occupancy. Those fields require a target-hardware Intel compiler/GPA capture; this report intentionally leaves them unclaimed.")
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8

Write-Host "Wrote $markdownPath"
Write-Host "Wrote $csvPath"
