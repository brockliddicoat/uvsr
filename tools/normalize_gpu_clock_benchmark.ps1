[CmdletBinding(DefaultParameterSetName = "Normalize")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [double]$RawGpuMilliseconds,

    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [double]$ObservedGraphicsClockMHz,

    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [double]$ReferenceGraphicsClockMHz,

    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [ValidateNotNullOrEmpty()]
    [string]$GpuIdentity,

    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [ValidateNotNullOrEmpty()]
    [string]$ReferenceGpuIdentity,

    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [ValidateNotNullOrEmpty()]
    [string]$WorkloadIdentity,

    [Parameter(Mandatory = $true, ParameterSetName = "Normalize")]
    [ValidateNotNullOrEmpty()]
    [string]$ReferenceWorkloadIdentity,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$GpuUtilizationPercent,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$ObservedMemoryClockMHz,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$ReferenceMemoryClockMHz,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$ObservedMemoryBandwidthGBps,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$ReferenceMemoryBandwidthGBps,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$TelemetryAgeMilliseconds,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$TelemetryPollIntervalMilliseconds,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$SampleCoveragePercent,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$BeforeAfterControlDriftPercent,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$MinimumValidatedGraphicsClockMHz,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$MaximumValidatedGraphicsClockMHz,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$GpuTemperatureC,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$GpuThermalHeadroomC,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$GpuPowerWatts,

    [Parameter(ParameterSetName = "Normalize")]
    [Nullable[double]]$ReferenceGpuPowerWatts,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$ThermalLimiterActive,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$PowerLimiterActive,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$ContentionDetected,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$DeviceError,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$OutputIncorrect,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$UnsafeTemperature,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$WarmupIncomplete,

    [Parameter(ParameterSetName = "Normalize")]
    [switch]$BracketControlFailed,

    [Parameter(Mandatory = $true, ParameterSetName = "SelfTest")]
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-FinitePositive
{
    param(
        [double]$Value,
        [string]$Name
    )

    if ([double]::IsNaN($Value) -or
        [double]::IsInfinity($Value) -or
        $Value -le 0.0)
    {
        throw "$Name must be finite and greater than zero."
    }
}

function Assert-OptionalPercentage
{
    param(
        [Nullable[double]]$Value,
        [string]$Name
    )

    if ($null -ne $Value -and
        ([double]::IsNaN([double]$Value) -or
         [double]::IsInfinity([double]$Value) -or
         [double]$Value -lt 0.0 -or
         [double]$Value -gt 100.0))
    {
        throw "$Name must be between zero and 100 when supplied."
    }
}

function Get-GpuClockNormalization
{
    param(
        [double]$RawGpuMilliseconds,
        [double]$ObservedGraphicsClockMHz,
        [double]$ReferenceGraphicsClockMHz,
        [string]$GpuIdentity,
        [string]$ReferenceGpuIdentity,
        [string]$WorkloadIdentity,
        [string]$ReferenceWorkloadIdentity,
        [Nullable[double]]$GpuUtilizationPercent,
        [Nullable[double]]$ObservedMemoryClockMHz,
        [Nullable[double]]$ReferenceMemoryClockMHz,
        [Nullable[double]]$ObservedMemoryBandwidthGBps,
        [Nullable[double]]$ReferenceMemoryBandwidthGBps,
        [Nullable[double]]$TelemetryAgeMilliseconds,
        [Nullable[double]]$TelemetryPollIntervalMilliseconds,
        [Nullable[double]]$SampleCoveragePercent,
        [Nullable[double]]$BeforeAfterControlDriftPercent,
        [Nullable[double]]$MinimumValidatedGraphicsClockMHz,
        [Nullable[double]]$MaximumValidatedGraphicsClockMHz,
        [Nullable[double]]$GpuTemperatureC,
        [Nullable[double]]$GpuThermalHeadroomC,
        [Nullable[double]]$GpuPowerWatts,
        [Nullable[double]]$ReferenceGpuPowerWatts,
        [bool]$ThermalLimiterActive,
        [bool]$PowerLimiterActive,
        [bool]$ContentionDetected,
        [bool]$DeviceError,
        [bool]$OutputIncorrect,
        [bool]$UnsafeTemperature,
        [bool]$WarmupIncomplete,
        [bool]$BracketControlFailed
    )

    Assert-FinitePositive $RawGpuMilliseconds "RawGpuMilliseconds"
    Assert-FinitePositive $ObservedGraphicsClockMHz "ObservedGraphicsClockMHz"
    Assert-FinitePositive $ReferenceGraphicsClockMHz "ReferenceGraphicsClockMHz"
    Assert-OptionalPercentage $GpuUtilizationPercent "GpuUtilizationPercent"
    Assert-OptionalPercentage $SampleCoveragePercent "SampleCoveragePercent"
    Assert-OptionalPercentage `
        $BeforeAfterControlDriftPercent `
        "BeforeAfterControlDriftPercent"

    if (-not [StringComparer]::Ordinal.Equals(
            $GpuIdentity,
            $ReferenceGpuIdentity))
    {
        throw "Cross-GPU normalization is forbidden. The sample and reference physical GPU identities must match exactly."
    }

    if (-not [StringComparer]::Ordinal.Equals(
            $WorkloadIdentity,
            $ReferenceWorkloadIdentity))
    {
        throw "Workload normalization is forbidden. The sample and reference workload identities must match exactly."
    }

    $reasons = [System.Collections.Generic.List[string]]::new()
    $qualityRank = 0
    $hardInvalid = $false

    if ($DeviceError)
    {
        $reasons.Add("A device error invalidates normalization.")
        $hardInvalid = $true
    }

    if ($OutputIncorrect)
    {
        $reasons.Add("Visibly incorrect output invalidates normalization.")
        $hardInvalid = $true
    }

    if ($UnsafeTemperature)
    {
        $reasons.Add("Unsafe temperature invalidates normalization.")
        $hardInvalid = $true
    }

    if ($WarmupIncomplete)
    {
        $reasons.Add("Incomplete warmup is a different workload state.")
        $hardInvalid = $true
    }

    if ($BracketControlFailed)
    {
        $reasons.Add("The before/after control bracket failed; the estimate is directional only.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }

    if ($ThermalLimiterActive)
    {
        $reasons.Add("A thermal limiter was active.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }

    if ($PowerLimiterActive)
    {
        $reasons.Add("A power limiter was active.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }

    if ($ContentionDetected)
    {
        $reasons.Add("Competing work was detected.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }

    if ($null -eq $GpuUtilizationPercent)
    {
        $reasons.Add("GPU utilization was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }
    elseif ([double]$GpuUtilizationPercent -lt 75.0)
    {
        $reasons.Add("GPU utilization was below 75 percent.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }
    elseif ([double]$GpuUtilizationPercent -lt 90.0)
    {
        $reasons.Add("GPU utilization was below 90 percent.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }
    elseif ([double]$GpuUtilizationPercent -lt 95.0)
    {
        $reasons.Add("GPU utilization was below the A-grade 95 percent threshold.")
        $qualityRank = [Math]::Max($qualityRank, 1)
    }

    $memoryClockDriftPercent = $null
    $memoryBandwidthDriftPercent = $null
    $memoryEvidenceSupplied = $false
    $memoryClockPairComplete =
        $null -ne $ObservedMemoryClockMHz -and
        $null -ne $ReferenceMemoryClockMHz
    $memoryClockPairPartial =
        ($null -ne $ObservedMemoryClockMHz) -xor
        ($null -ne $ReferenceMemoryClockMHz)
    $memoryBandwidthPairComplete =
        $null -ne $ObservedMemoryBandwidthGBps -and
        $null -ne $ReferenceMemoryBandwidthGBps
    $memoryBandwidthPairPartial =
        ($null -ne $ObservedMemoryBandwidthGBps) -xor
        ($null -ne $ReferenceMemoryBandwidthGBps)

    if ($memoryClockPairPartial)
    {
        throw "ObservedMemoryClockMHz and ReferenceMemoryClockMHz must be supplied together."
    }
    if ($memoryBandwidthPairPartial)
    {
        throw "ObservedMemoryBandwidthGBps and ReferenceMemoryBandwidthGBps must be supplied together."
    }

    if ($memoryClockPairComplete)
    {
        $memoryEvidenceSupplied = $true
        Assert-FinitePositive `
            ([double]$ObservedMemoryClockMHz) `
            "ObservedMemoryClockMHz"
        Assert-FinitePositive `
            ([double]$ReferenceMemoryClockMHz) `
            "ReferenceMemoryClockMHz"

        $memoryClockDriftPercent =
            [Math]::Abs(
                [double]$ObservedMemoryClockMHz -
                [double]$ReferenceMemoryClockMHz) /
            [double]$ReferenceMemoryClockMHz * 100.0

    }

    if ($memoryBandwidthPairComplete)
    {
        $memoryEvidenceSupplied = $true
        Assert-FinitePositive `
            ([double]$ObservedMemoryBandwidthGBps) `
            "ObservedMemoryBandwidthGBps"
        Assert-FinitePositive `
            ([double]$ReferenceMemoryBandwidthGBps) `
            "ReferenceMemoryBandwidthGBps"

        $memoryBandwidthDriftPercent =
            [Math]::Abs(
                [double]$ObservedMemoryBandwidthGBps -
                [double]$ReferenceMemoryBandwidthGBps) /
            [double]$ReferenceMemoryBandwidthGBps * 100.0
    }

    $maximumMemoryDriftPercent = @(
        $memoryClockDriftPercent,
        $memoryBandwidthDriftPercent
    ) | Where-Object { $null -ne $_ } | Measure-Object -Maximum |
        Select-Object -ExpandProperty Maximum

    if (-not $memoryEvidenceSupplied)
    {
        $reasons.Add("Complete memory-clock or bandwidth evidence was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }
    elseif ($maximumMemoryDriftPercent -gt 15.0)
    {
        $reasons.Add("Memory-clock or bandwidth drift exceeded 15 percent.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }
    elseif ($maximumMemoryDriftPercent -gt 5.0)
    {
        $reasons.Add("Memory-clock or bandwidth drift exceeded five percent.")
        $qualityRank = [Math]::Max($qualityRank, 1)
    }

    if ($null -ne $TelemetryAgeMilliseconds -and
        $null -ne $TelemetryPollIntervalMilliseconds)
    {
        if ([double]$TelemetryAgeMilliseconds -lt 0.0)
        {
            throw "TelemetryAgeMilliseconds cannot be negative."
        }
        Assert-FinitePositive `
            ([double]$TelemetryPollIntervalMilliseconds) `
            "TelemetryPollIntervalMilliseconds"

        if ([double]$TelemetryAgeMilliseconds -gt
            2.0 * [double]$TelemetryPollIntervalMilliseconds)
        {
            $reasons.Add("Clock telemetry was more than two poll intervals old.")
            $qualityRank = [Math]::Max($qualityRank, 3)
        }
        elseif ([double]$TelemetryAgeMilliseconds -gt
                [double]$TelemetryPollIntervalMilliseconds)
        {
            $reasons.Add("Clock telemetry was more than one poll interval old.")
            $qualityRank = [Math]::Max($qualityRank, 1)
        }
    }
    else
    {
        $reasons.Add("Complete telemetry-age evidence was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }

    if ($null -eq $SampleCoveragePercent)
    {
        $reasons.Add("Clock-paired sample coverage was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }
    elseif ([double]$SampleCoveragePercent -lt 95.0)
    {
        $reasons.Add("Clock-paired sample coverage was below 95 percent.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }

    if ($null -eq $BeforeAfterControlDriftPercent)
    {
        $reasons.Add("Before/after control drift was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }
    elseif ([double]$BeforeAfterControlDriftPercent -gt 5.0)
    {
        $reasons.Add("Before/after control drift exceeded five percent.")
        $qualityRank = [Math]::Max($qualityRank, 3)
    }
    elseif ([double]$BeforeAfterControlDriftPercent -gt 2.0)
    {
        $reasons.Add("Before/after control drift exceeded two percent.")
        $qualityRank = [Math]::Max($qualityRank, 1)
    }

    if ($null -ne $MinimumValidatedGraphicsClockMHz -and
        $null -ne $MaximumValidatedGraphicsClockMHz)
    {
        Assert-FinitePositive `
            ([double]$MinimumValidatedGraphicsClockMHz) `
            "MinimumValidatedGraphicsClockMHz"
        Assert-FinitePositive `
            ([double]$MaximumValidatedGraphicsClockMHz) `
            "MaximumValidatedGraphicsClockMHz"

        if ([double]$MinimumValidatedGraphicsClockMHz -gt
            [double]$MaximumValidatedGraphicsClockMHz)
        {
            throw "The minimum validated graphics clock cannot exceed the maximum."
        }

        if ($ReferenceGraphicsClockMHz -lt
                [double]$MinimumValidatedGraphicsClockMHz -or
            $ReferenceGraphicsClockMHz -gt
                [double]$MaximumValidatedGraphicsClockMHz)
        {
            throw "The reference graphics clock is outside its own validated range."
        }

        if ($ObservedGraphicsClockMHz -lt
                [double]$MinimumValidatedGraphicsClockMHz -or
            $ObservedGraphicsClockMHz -gt
                [double]$MaximumValidatedGraphicsClockMHz)
        {
            $reasons.Add("The observed graphics clock was outside the GPU's validated range.")
            $qualityRank = [Math]::Max($qualityRank, 3)
        }
    }
    else
    {
        $reasons.Add("A complete per-GPU validated graphics-clock range was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }

    $powerDriftPercent = $null
    if ($null -ne $GpuPowerWatts -and
        $null -ne $ReferenceGpuPowerWatts)
    {
        Assert-FinitePositive ([double]$GpuPowerWatts) "GpuPowerWatts"
        Assert-FinitePositive `
            ([double]$ReferenceGpuPowerWatts) `
            "ReferenceGpuPowerWatts"

        $powerDriftPercent =
            [Math]::Abs(
                [double]$GpuPowerWatts -
                [double]$ReferenceGpuPowerWatts) /
            [double]$ReferenceGpuPowerWatts * 100.0

        if ($powerDriftPercent -gt 20.0)
        {
            $reasons.Add("GPU-power drift exceeded 20 percent.")
            $qualityRank = [Math]::Max($qualityRank, 3)
        }
        elseif ($powerDriftPercent -gt 10.0)
        {
            $reasons.Add("GPU-power drift exceeded 10 percent.")
            $qualityRank = [Math]::Max($qualityRank, 1)
        }
    }
    else
    {
        $reasons.Add("Complete GPU-power evidence was not supplied.")
        $qualityRank = [Math]::Max($qualityRank, 2)
    }

    if ($hardInvalid)
    {
        $qualityRank = 4
    }

    if ($reasons.Count -eq 0)
    {
        $reasons.Add("All supplied checks meet the A-grade thresholds.")
    }

    $clockScaleFactor =
        $ObservedGraphicsClockMHz / $ReferenceGraphicsClockMHz
    $normalizedGpuMilliseconds = if ($hardInvalid)
    {
        $null
    }
    else
    {
        $RawGpuMilliseconds * $clockScaleFactor
    }
    $qualityNames = @("A", "B", "C", "Directional", "Rejected")
    $canPublishNormalizedSummary =
        -not $hardInvalid -and
        $qualityRank -lt 3 -and
        $null -ne $SampleCoveragePercent -and
        [double]$SampleCoveragePercent -ge 95.0

    [pscustomobject]@{
        SchemaVersion = 1
        AdvisoryOnly = $true
        OfficialScoreField = "RawGpuMilliseconds"
        PhysicalGpuIdentity = $GpuIdentity
        SamePhysicalGpuVerified = $true
        CrossGpuComparisonAllowed = $false
        WorkloadIdentity = $WorkloadIdentity
        SameWorkloadVerified = $true
        RawGpuMilliseconds = [Math]::Round($RawGpuMilliseconds, 6)
        ObservedGraphicsClockMHz =
            [Math]::Round($ObservedGraphicsClockMHz, 6)
        ReferenceGraphicsClockMHz =
            [Math]::Round($ReferenceGraphicsClockMHz, 6)
        ClockScaleFactor = [Math]::Round($clockScaleFactor, 9)
        NormalizedGpuMilliseconds = if ($null -eq $normalizedGpuMilliseconds)
        {
            $null
        }
        else
        {
            [Math]::Round([double]$normalizedGpuMilliseconds, 6)
        }
        ClockWorkIndexMillisecondsMHz =
            [Math]::Round(
                $RawGpuMilliseconds * $ObservedGraphicsClockMHz,
                6)
        Formula =
            "raw_ms * observed_graphics_clock_mhz / reference_graphics_clock_mhz"
        QualityGrade = $qualityNames[$qualityRank]
        CanUseForSameGpuTrend = -not $hardInvalid
        CanPublishNormalizedSummary = $canPublishNormalizedSummary
        Evidence = [pscustomobject]@{
            GpuUtilizationPercent = $GpuUtilizationPercent
            MemoryClockDriftPercent = if ($null -eq $memoryClockDriftPercent)
            {
                $null
            }
            else
            {
                [Math]::Round([double]$memoryClockDriftPercent, 6)
            }
            MemoryBandwidthDriftPercent =
                if ($null -eq $memoryBandwidthDriftPercent)
            {
                $null
            }
            else
            {
                [Math]::Round([double]$memoryBandwidthDriftPercent, 6)
            }
            TelemetryAgeMilliseconds = $TelemetryAgeMilliseconds
            TelemetryPollIntervalMilliseconds =
                $TelemetryPollIntervalMilliseconds
            SampleCoveragePercent = $SampleCoveragePercent
            BeforeAfterControlDriftPercent =
                $BeforeAfterControlDriftPercent
            GpuTemperatureC = $GpuTemperatureC
            GpuThermalHeadroomC = $GpuThermalHeadroomC
            GpuPowerWatts = $GpuPowerWatts
            PowerDriftPercent = if ($null -eq $powerDriftPercent)
            {
                $null
            }
            else
            {
                [Math]::Round([double]$powerDriftPercent, 6)
            }
            ThermalLimiterActive = $ThermalLimiterActive
            PowerLimiterActive = $PowerLimiterActive
            ContentionDetected = $ContentionDetected
        }
        Reasons = [string[]]$reasons
    }
}

function Assert-SelfTest
{
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition)
    {
        throw "Self-test failed: $Message"
    }
}

if ($SelfTest)
{
    $baseArguments = @{
        RawGpuMilliseconds = 1.600
        ObservedGraphicsClockMHz = 1800.0
        ReferenceGraphicsClockMHz = 2200.0
        GpuIdentity = "host-a|gpu-a"
        ReferenceGpuIdentity = "host-a|gpu-a"
        WorkloadIdentity = "matched-workload"
        ReferenceWorkloadIdentity = "matched-workload"
        GpuUtilizationPercent = 97.0
        ObservedMemoryClockMHz = 9000.0
        ReferenceMemoryClockMHz = 9000.0
        ObservedMemoryBandwidthGBps = $null
        ReferenceMemoryBandwidthGBps = $null
        TelemetryAgeMilliseconds = 100.0
        TelemetryPollIntervalMilliseconds = 500.0
        SampleCoveragePercent = 100.0
        BeforeAfterControlDriftPercent = 1.0
        MinimumValidatedGraphicsClockMHz = 1700.0
        MaximumValidatedGraphicsClockMHz = 2300.0
        GpuTemperatureC = 65.0
        GpuThermalHeadroomC = 20.0
        GpuPowerWatts = 100.0
        ReferenceGpuPowerWatts = 100.0
        ThermalLimiterActive = $false
        PowerLimiterActive = $false
        ContentionDetected = $false
        DeviceError = $false
        OutputIncorrect = $false
        UnsafeTemperature = $false
        WarmupIncomplete = $false
        BracketControlFailed = $false
    }

    $sameGpu = Get-GpuClockNormalization @baseArguments
    Assert-SelfTest `
        ([Math]::Abs($sameGpu.NormalizedGpuMilliseconds - 1.309091) -lt
            0.000001) `
        "same-GPU formula"
    Assert-SelfTest `
        ($sameGpu.QualityGrade -eq "A") `
        "complete clean evidence should be A grade"
    Assert-SelfTest `
        (-not $sameGpu.CrossGpuComparisonAllowed) `
        "cross-GPU comparison must remain forbidden"

    $crossGpuRejected = $false
    try
    {
        $crossGpuArguments = $baseArguments.Clone()
        $crossGpuArguments.ReferenceGpuIdentity = "host-b|gpu-b"
        $null = Get-GpuClockNormalization @crossGpuArguments
    }
    catch
    {
        $crossGpuRejected =
            $_.Exception.Message -match "Cross-GPU normalization is forbidden"
    }
    Assert-SelfTest $crossGpuRejected "different physical GPU rejection"

    $workloadRejected = $false
    try
    {
        $workloadArguments = $baseArguments.Clone()
        $workloadArguments.ReferenceWorkloadIdentity = "different-workload"
        $null = Get-GpuClockNormalization @workloadArguments
    }
    catch
    {
        $workloadRejected =
            $_.Exception.Message -match "Workload normalization is forbidden"
    }
    Assert-SelfTest $workloadRejected "different workload rejection"

    $invalidArguments = $baseArguments.Clone()
    $invalidArguments.OutputIncorrect = $true
    $invalidOutput = Get-GpuClockNormalization @invalidArguments
    Assert-SelfTest `
        ($null -eq $invalidOutput.NormalizedGpuMilliseconds -and
         $invalidOutput.QualityGrade -eq "Rejected") `
        "incorrect output must suppress the normalized value"

    $directionalArguments = $baseArguments.Clone()
    $directionalArguments.ObservedMemoryClockMHz = 7000.0
    $directional = Get-GpuClockNormalization @directionalArguments
    Assert-SelfTest `
        ($directional.QualityGrade -eq "Directional") `
        "large memory-clock drift should be directional"

    $bandwidthArguments = $baseArguments.Clone()
    $bandwidthArguments.ObservedMemoryClockMHz = $null
    $bandwidthArguments.ReferenceMemoryClockMHz = $null
    $bandwidthArguments.ObservedMemoryBandwidthGBps = 580.0
    $bandwidthArguments.ReferenceMemoryBandwidthGBps = 580.0
    $bandwidth = Get-GpuClockNormalization @bandwidthArguments
    Assert-SelfTest `
        ($bandwidth.QualityGrade -eq "A") `
        "current-clock bandwidth should support the memory evidence path"

    $failedBracketArguments = $baseArguments.Clone()
    $failedBracketArguments.BracketControlFailed = $true
    $failedBracket = Get-GpuClockNormalization @failedBracketArguments
    Assert-SelfTest `
        ($failedBracket.QualityGrade -eq "Directional" -and
         $null -ne $failedBracket.NormalizedGpuMilliseconds -and
         -not $failedBracket.CanPublishNormalizedSummary) `
        "a healthy failed bracket should retain a directional estimate"

    "GPU clock normalization self-test passed (9 cases)."
    exit 0
}

Get-GpuClockNormalization `
    -RawGpuMilliseconds $RawGpuMilliseconds `
    -ObservedGraphicsClockMHz $ObservedGraphicsClockMHz `
    -ReferenceGraphicsClockMHz $ReferenceGraphicsClockMHz `
    -GpuIdentity $GpuIdentity `
    -ReferenceGpuIdentity $ReferenceGpuIdentity `
    -WorkloadIdentity $WorkloadIdentity `
    -ReferenceWorkloadIdentity $ReferenceWorkloadIdentity `
    -GpuUtilizationPercent $GpuUtilizationPercent `
    -ObservedMemoryClockMHz $ObservedMemoryClockMHz `
    -ReferenceMemoryClockMHz $ReferenceMemoryClockMHz `
    -ObservedMemoryBandwidthGBps $ObservedMemoryBandwidthGBps `
    -ReferenceMemoryBandwidthGBps $ReferenceMemoryBandwidthGBps `
    -TelemetryAgeMilliseconds $TelemetryAgeMilliseconds `
    -TelemetryPollIntervalMilliseconds $TelemetryPollIntervalMilliseconds `
    -SampleCoveragePercent $SampleCoveragePercent `
    -BeforeAfterControlDriftPercent $BeforeAfterControlDriftPercent `
    -MinimumValidatedGraphicsClockMHz `
        $MinimumValidatedGraphicsClockMHz `
    -MaximumValidatedGraphicsClockMHz `
        $MaximumValidatedGraphicsClockMHz `
    -GpuTemperatureC $GpuTemperatureC `
    -GpuThermalHeadroomC $GpuThermalHeadroomC `
    -GpuPowerWatts $GpuPowerWatts `
    -ReferenceGpuPowerWatts $ReferenceGpuPowerWatts `
    -ThermalLimiterActive $ThermalLimiterActive `
    -PowerLimiterActive $PowerLimiterActive `
    -ContentionDetected $ContentionDetected `
    -DeviceError $DeviceError `
    -OutputIncorrect $OutputIncorrect `
    -UnsafeTemperature $UnsafeTemperature `
    -WarmupIncomplete $WarmupIncomplete `
    -BracketControlFailed $BracketControlFailed
