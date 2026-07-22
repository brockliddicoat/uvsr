[CmdletBinding()]
param(
    [ValidateRange(1, 100)]
    [int]$MaximumGpuTemperatureC = 55,

    [ValidateRange(1, 110)]
    [int]$MaximumCpuTemperatureC = 75,

    [ValidateRange(0, 100)]
    [int]$MaximumExternalCpuUtilizationPercent = 20,

    [ValidateRange(0, 100)]
    [int]$MaximumExternalCpuUtilizationBurstPercent = 50,

    [ValidateRange(1, 60)]
    [int]$ExternalCpuUtilizationAverageWindowSamples = 5,

    [ValidateRange(0, 100)]
    [int]$MinimumGpuThermalHeadroomC = 20,

    [ValidateRange(0, 100)]
    [int]$MinimumMeasurementGpuThermalHeadroomC = 5,

    [ValidateRange(-1, 120)]
    [int]$GpuThermalLimitC = -1,

    [ValidateRange(-1, 110)]
    [int]$CpuTemperatureC = -1,

    [ValidateRange(30, 600)]
    [int]$RequiredStableSeconds = 30,

    [ValidateRange(0, 3600)]
    [int]$MeasurementDurationSeconds = 0,

    [ValidateRange(1000, 10000)]
    [int]$SampleIntervalMilliseconds = 1000,

    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 300,

    [ValidateSet("Preflight", "Measurement")]
    [string]$Phase = "Preflight",

    [string]$ExpectedGpuName = "",

    [string]$NvidiaGpuId = "",

    [ValidateRange(0, [int]::MaxValue)]
    [int]$ExpectedRendererProcessId = 0,

    [string]$ExpectedRendererPath = "",

    [ValidateRange(0, [int]::MaxValue)]
    [int]$ExpectedPresentMonProcessId = 0,

    [string]$ExpectedPresentMonPath = "",

    [switch]$RequireStableThermalCounters,

    [switch]$AllowMissingGpuThermalLimiter,

    [switch]$AllowMissingCpuTemperature,

    [switch]$DiagnosticSnapshot,

    [switch]$AllowBatteryPower,

    [string]$LibreHardwareMonitorLibraryPath =
        "work/performance-monitoring/LibreHardwareMonitor-v0.9.6/LibreHardwareMonitorLib.dll",

    [string]$OutputPath = "",

    [string]$TelemetryCsvPath = "",

    [string]$MeasurementReadyPath = ""
)

$ErrorActionPreference = "Stop"
$requireCounterStability = $RequireStableThermalCounters.IsPresent
$missingGpuThermalLimiterAllowed =
    $AllowMissingGpuThermalLimiter.IsPresent
$missingCpuTemperatureAllowed =
    $AllowMissingCpuTemperature.IsPresent
$diagnosticSnapshotRequested = $DiagnosticSnapshot.IsPresent
$allowBatteryPower = $AllowBatteryPower.IsPresent
$invariantCulture = [Globalization.CultureInfo]::InvariantCulture
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$runIdentity =
    (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssfffZ") +
    "-$PID-" + [Guid]::NewGuid().ToString("N").Substring(0, 8)
$effectiveOutputPath = if ([string]::IsNullOrWhiteSpace($OutputPath))
{
    "outputs/svsm-thermal-state-$runIdentity.txt"
}
else
{
    $OutputPath
}
$effectiveTelemetryCsvPath = if ([string]::IsNullOrWhiteSpace(
        $TelemetryCsvPath))
{
    "outputs/svsm-thermal-telemetry-$runIdentity.csv"
}
else
{
    $TelemetryCsvPath
}
$resolvedOutputPath = if ([IO.Path]::IsPathRooted(
        $effectiveOutputPath))
{
    $effectiveOutputPath
}
else
{
    Join-Path $repositoryRoot $effectiveOutputPath
}
$resolvedTelemetryCsvPath = if ([IO.Path]::IsPathRooted(
        $effectiveTelemetryCsvPath))
{
    $effectiveTelemetryCsvPath
}
else
{
    Join-Path $repositoryRoot $effectiveTelemetryCsvPath
}
$resolvedMeasurementReadyPath =
    if ([string]::IsNullOrWhiteSpace($MeasurementReadyPath))
    {
        ""
    }
    elseif ([IO.Path]::IsPathRooted($MeasurementReadyPath))
    {
        $MeasurementReadyPath
    }
    else
    {
        Join-Path $repositoryRoot $MeasurementReadyPath
    }
$resolvedLibreHardwareMonitorLibraryPath =
    if ([IO.Path]::IsPathRooted($LibreHardwareMonitorLibraryPath))
    {
        $LibreHardwareMonitorLibraryPath
    }
    else
    {
        Join-Path $repositoryRoot $LibreHardwareMonitorLibraryPath
    }
$expectedLibreHardwareMonitorHash =
    "6EBC194316536BA61AF5BE24508AD9FCBB2ECC685E716C12E787C79530F66BF0"

if ($Phase -eq "Measurement" -and
    $MeasurementDurationSeconds -eq 0 -and
    -not $diagnosticSnapshotRequested)
{
    throw "Measurement mode requires -MeasurementDurationSeconds."
}
if ($Phase -eq "Preflight" -and
    $MeasurementDurationSeconds -ne 0)
{
    throw "Measurement duration is valid only in Measurement mode."
}
if (-not [string]::IsNullOrWhiteSpace($resolvedMeasurementReadyPath) -and
    ($Phase -ne "Measurement" -or $diagnosticSnapshotRequested))
{
    throw "A measurement-ready marker is valid only for a non-diagnostic Measurement run."
}
if (-not [string]::IsNullOrWhiteSpace($resolvedMeasurementReadyPath) -and
    (Test-Path -LiteralPath $resolvedMeasurementReadyPath))
{
    throw "The measurement-ready marker path already exists; use a unique path so stale readiness cannot be mistaken for this run."
}
if (-not [string]::IsNullOrWhiteSpace($resolvedMeasurementReadyPath) -and
    ($ExpectedRendererProcessId -le 0 -or
        [string]::IsNullOrWhiteSpace($ExpectedRendererPath)))
{
    throw "A measurement-ready marker requires an exact renderer process id and path."
}
if ($MaximumExternalCpuUtilizationBurstPercent -lt
    $MaximumExternalCpuUtilizationPercent)
{
    throw "The external CPU burst limit cannot be below its rolling-average limit."
}

function ConvertTo-NullableDouble
{
    param($Value)

    if ($null -eq $Value)
    {
        return $null
    }

    [double]$parsed = 0.0
    if ([double]::TryParse(
            [string]$Value,
            [Globalization.NumberStyles]::Float,
            $invariantCulture,
            [ref]$parsed))
    {
        return $parsed
    }
    return $null
}

function Format-ReportValue
{
    param($Value)

    if ($null -eq $Value)
    {
        return "unknown"
    }
    if ($Value -is [double] -or $Value -is [single])
    {
        return $Value.ToString("0.###", $invariantCulture)
    }
    return [string]$Value
}

function ConvertTo-UnixMilliseconds
{
    param(
        [Parameter(Mandatory)]
        [datetime]$Value
    )

    $offset = [DateTimeOffset]$Value.ToUniversalTime()
    return $offset.ToUnixTimeMilliseconds()
}

function Write-MeasurementMarker
{
    param(
        [Parameter(Mandatory)]
        [ValidateSet("ready", "complete", "contaminated")]
        [string]$State,

        [Parameter(Mandatory)]
        [datetime]$StartTime,

        [Parameter(Mandatory)]
        [datetime]$Deadline,

        [Nullable[datetime]]$EndTime = $null
    )

    if ([string]::IsNullOrWhiteSpace($resolvedMeasurementReadyPath))
    {
        return
    }

    $markerDirectory = Split-Path -Parent $resolvedMeasurementReadyPath
    if (-not [string]::IsNullOrWhiteSpace($markerDirectory))
    {
        New-Item -ItemType Directory -Force `
            -Path $markerDirectory | Out-Null
    }
    $normalizedRendererPath =
        Resolve-NormalizedPath $ExpectedRendererPath
    $markerLines = [Collections.Generic.List[string]]::new()
    [void]$markerLines.Add("state=$State")
    foreach ($line in @(
        "runIdentity=$runIdentity",
        "measurementStartUtc=$($StartTime.ToUniversalTime().ToString('o'))",
        "measurementDeadlineUtc=$($Deadline.ToUniversalTime().ToString('o'))",
        "monitorProcessId=$PID",
        "rendererProcessId=$ExpectedRendererProcessId",
        "rendererPath=$normalizedRendererPath",
        "measurementStartUnixMs=$(ConvertTo-UnixMilliseconds $StartTime)",
        "measurementDeadlineUnixMs=$(ConvertTo-UnixMilliseconds $Deadline)",
        "thermalGateScope=sensor-process-evidence-only",
        "position1TflopsGate=external-benchmark-procedure"
    ))
    {
        [void]$markerLines.Add($line)
    }
    if ($null -ne $EndTime)
    {
        [void]$markerLines.Add(
            "measurementEndUtc=$($EndTime.ToUniversalTime().ToString('o'))")
        [void]$markerLines.Add(
            "measurementEndUnixMs=$(ConvertTo-UnixMilliseconds $EndTime)")
    }
    $marker = $markerLines -join [Environment]::NewLine
    $temporaryMarkerPath =
        "$resolvedMeasurementReadyPath.$runIdentity.tmp"
    $backupMarkerPath =
        "$resolvedMeasurementReadyPath.$runIdentity.backup"
    try
    {
        Set-Content -LiteralPath $temporaryMarkerPath `
            -Value $marker -Encoding utf8
        if (Test-Path -LiteralPath $resolvedMeasurementReadyPath)
        {
            [IO.File]::Replace(
                $temporaryMarkerPath,
                $resolvedMeasurementReadyPath,
                $backupMarkerPath)
            Remove-Item -LiteralPath $backupMarkerPath -Force
        }
        else
        {
            [IO.File]::Move(
                $temporaryMarkerPath,
                $resolvedMeasurementReadyPath)
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $temporaryMarkerPath)
        {
            Remove-Item -LiteralPath $temporaryMarkerPath -Force
        }
        if (Test-Path -LiteralPath $backupMarkerPath)
        {
            Remove-Item -LiteralPath $backupMarkerPath -Force
        }
    }
}

function Resolve-NormalizedPath
{
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path))
    {
        return ""
    }
    try
    {
        $rootedPath = if ([IO.Path]::IsPathRooted($Path))
        {
            $Path
        }
        else
        {
            Join-Path $repositoryRoot $Path
        }
        return [IO.Path]::GetFullPath($rootedPath).TrimEnd('\')
    }
    catch
    {
        return ""
    }
}

function Test-PathIdentity
{
    param(
        [string]$Actual,
        [string]$Expected
    )

    if ([string]::IsNullOrWhiteSpace($Actual) -or
        [string]::IsNullOrWhiteSpace($Expected))
    {
        return $false
    }
    return [string]::Equals(
        (Resolve-NormalizedPath $Actual),
        (Resolve-NormalizedPath $Expected),
        [StringComparison]::OrdinalIgnoreCase)
}

function Get-CpuPerformanceConstraintSample
{
    try
    {
        $rows = @(
            Get-CimInstance -Namespace root\cimv2 `
                -ClassName Win32_PerfFormattedData_Counters_ProcessorInformation |
                Where-Object { $_.Name -notmatch "_Total$" }
        )
    }
    catch
    {
        $rows = @()
    }

    if ($rows.Count -eq 0)
    {
        return [pscustomobject]@{
            Available = $false
            LogicalProcessorCount = 0
            MaximumPerformanceLimitFlags = -1
            MinimumPercentPerformanceLimit = -1
            MinimumActualFrequencyMHz = -1
            MaximumActualFrequencyMHz = -1
        }
    }

    return [pscustomobject]@{
        Available = $true
        LogicalProcessorCount = $rows.Count
        MaximumPerformanceLimitFlags = [int](
            $rows.PerformanceLimitFlags |
                Measure-Object -Maximum).Maximum
        MinimumPercentPerformanceLimit = [int](
            $rows.PercentPerformanceLimit |
                Measure-Object -Minimum).Minimum
        MinimumActualFrequencyMHz = [int](
            $rows.ActualFrequency |
                Measure-Object -Minimum).Minimum
        MaximumActualFrequencyMHz = [int](
            $rows.ActualFrequency |
                Measure-Object -Maximum).Maximum
    }
}

function Test-CpuPerformanceConstraintSampleReady
{
    param(
        [Parameter(Mandatory)]
        $Sample
    )

    return $Sample.Available -and
        $Sample.MaximumPerformanceLimitFlags -eq 0 -and
        $Sample.MinimumPercentPerformanceLimit -ge 100
}

function Get-CpuUtilizationSample
{
    param([int]$RendererProcessId)

    try
    {
        $totalRow = Get-CimInstance -Namespace root\cimv2 `
            -ClassName Win32_PerfFormattedData_PerfOS_Processor |
            Where-Object { $_.Name -eq "_Total" } |
            Select-Object -First 1
        if ($null -eq $totalRow)
        {
            throw "The total CPU utilization counter is unavailable."
        }

        $processRows = @(
            Get-CimInstance -Namespace root\cimv2 `
                -ClassName Win32_PerfFormattedData_PerfProc_Process |
                Where-Object {
                    [int]$_.IDProcess -gt 0 -and
                    $_.Name -ne "_Total"
                }
        )
        $identityRows = @(
            Get-CimInstance -Namespace root\cimv2 `
                -ClassName Win32_Process
        )
        $identityById = @{}
        foreach ($identity in $identityRows)
        {
            $identityById[[int]$identity.ProcessId] = $identity
        }

        # Treat the complete live ChatGPT/Codex process trees as measurement
        # infrastructure. This includes the current monitor itself when it was
        # launched by Codex, without exempting separately detected builds,
        # captures, or an expected renderer from their process-hygiene rules.
        $assistantProcessIds = New-Object `
            'System.Collections.Generic.HashSet[int]'
        foreach ($identity in $identityRows)
        {
            $processName = [IO.Path]::GetFileNameWithoutExtension(
                [string]$identity.Name)
            if ($processName -match '^(?i:chatgpt|codex)$' -and
                [int]$identity.ProcessId -ne $RendererProcessId)
            {
                [void]$assistantProcessIds.Add(
                    [int]$identity.ProcessId)
            }
        }
        $addedAssistantDescendant = $true
        while ($addedAssistantDescendant)
        {
            $addedAssistantDescendant = $false
            foreach ($identity in $identityRows)
            {
                $processId = [int]$identity.ProcessId
                if ($processId -eq $RendererProcessId -or
                    $assistantProcessIds.Contains($processId))
                {
                    continue
                }
                if ($assistantProcessIds.Contains(
                        [int]$identity.ParentProcessId))
                {
                    [void]$assistantProcessIds.Add($processId)
                    $addedAssistantDescendant = $true
                }
            }
        }

        $logicalProcessorCount = [Math]::Max(
            1, [Environment]::ProcessorCount)
        $totalPercent = [double]$totalRow.PercentProcessorTime
        $rendererPercent = 0.0
        if ($RendererProcessId -gt 0)
        {
            $rendererRow = $processRows |
                Where-Object { $_.IDProcess -eq $RendererProcessId } |
                Select-Object -First 1
            if ($null -ne $rendererRow)
            {
                $rendererPercent = [double](
                    [double]$rendererRow.PercentProcessorTime /
                    $logicalProcessorCount)
            }
        }

        $assistantPercent = 0.0
        $assistantProcesses = @()
        $otherProcesses = @()
        $otherAttributedPercent = 0.0
        foreach ($processRow in $processRows)
        {
            $processId = [int]$processRow.IDProcess
            if ($processId -eq $RendererProcessId)
                { continue }
            $processPercent = [double](
                [double]$processRow.PercentProcessorTime /
                $logicalProcessorCount)
            if ($processPercent -lt 0.0)
                { $processPercent = 0.0 }
            $processName = if ($identityById.ContainsKey($processId))
            {
                [string]$identityById[$processId].Name
            }
            else
            {
                [string]$processRow.Name
            }
            $entry = [pscustomobject]@{
                Name = $processName
                Id = $processId
                Percent = $processPercent
            }
            if ($assistantProcessIds.Contains($processId))
            {
                $assistantPercent += $processPercent
                $assistantProcesses += $entry
            }
            else
            {
                $otherAttributedPercent += $processPercent
                if ($processPercent -gt 0.0)
                    { $otherProcesses += $entry }
            }
        }

        $assistantProcesses = @($assistantProcesses |
            Sort-Object -Property Percent -Descending)
        $otherProcesses = @($otherProcesses |
            Sort-Object -Property Percent -Descending |
            Select-Object -First 12)
        $preAssistantExternalPercent = [Math]::Max(
            0.0, $totalPercent - $rendererPercent)
        $gateExternalPercent = [Math]::Max(
            0.0,
            $preAssistantExternalPercent - $assistantPercent)

        return [pscustomobject]@{
            Available = $true
            TotalPercent = $totalPercent
            RendererPercent = $rendererPercent
            AssistantPercent = $assistantPercent
            PreAssistantExternalPercent =
                $preAssistantExternalPercent
            ExternalPercent = $gateExternalPercent
            OtherAttributedPercent = $otherAttributedPercent
            OtherUnattributedPercent = [Math]::Max(
                0.0,
                $gateExternalPercent - $otherAttributedPercent)
            AssistantProcesses = $assistantProcesses
            OtherProcesses = $otherProcesses
            Error = ""
        }
    }
    catch
    {
        return [pscustomobject]@{
            Available = $false
            TotalPercent = $null
            RendererPercent = $null
            AssistantPercent = $null
            PreAssistantExternalPercent = $null
            ExternalPercent = $null
            OtherAttributedPercent = $null
            OtherUnattributedPercent = $null
            AssistantProcesses = @()
            OtherProcesses = @()
            Error = $_.Exception.Message
        }
    }
}

function Get-SystemMemorySample
{
    try
    {
        $row = Get-CimInstance -Namespace root\cimv2 `
            -ClassName Win32_PerfFormattedData_PerfOS_Memory |
            Select-Object -First 1
        if ($null -eq $row)
        {
            throw "The system memory counters are unavailable."
        }
        return [pscustomobject]@{
            Available = $true
            CommittedBytes = [uint64]$row.CommittedBytes
            CommitLimitBytes = [uint64]$row.CommitLimit
            AvailableBytes = [uint64]$row.AvailableBytes
            PagedPoolBytes = [uint64]$row.PoolPagedBytes
            NonpagedPoolBytes = [uint64]$row.PoolNonpagedBytes
            Error = ""
        }
    }
    catch
    {
        return [pscustomobject]@{
            Available = $false
            CommittedBytes = $null
            CommitLimitBytes = $null
            AvailableBytes = $null
            PagedPoolBytes = $null
            NonpagedPoolBytes = $null
            Error = $_.Exception.Message
        }
    }
}

$script:hardwareComputer = $null
$script:hardwareInitializationError = ""

function Initialize-HardwareMonitor
{
    if (-not (Test-Path -LiteralPath `
            $resolvedLibreHardwareMonitorLibraryPath))
    {
        $script:hardwareInitializationError =
            "LibreHardwareMonitor library is missing."
        return
    }

    $actualHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $resolvedLibreHardwareMonitorLibraryPath).Hash
    if ($actualHash -ne $expectedLibreHardwareMonitorHash)
    {
        $script:hardwareInitializationError =
            "LibreHardwareMonitor library identity is invalid."
        return
    }

    try
    {
        [void][Reflection.Assembly]::LoadFrom(
            (Resolve-Path -LiteralPath `
                $resolvedLibreHardwareMonitorLibraryPath).Path)
        $computer = New-Object LibreHardwareMonitor.Hardware.Computer
        $computer.IsCpuEnabled = $true
        $computer.IsGpuEnabled = $true
        $computer.Open()
        $script:hardwareComputer = $computer
    }
    catch
    {
        $script:hardwareInitializationError = $_.Exception.Message
        $script:hardwareComputer = $null
    }
}

function Get-HardwareTreeSensors
{
    param(
        [Parameter(Mandatory)]
        $Hardware
    )

    $Hardware.Update()
    foreach ($sensor in $Hardware.Sensors)
    {
        [pscustomobject]@{
            HardwareName = $Hardware.Name
            HardwareType = $Hardware.HardwareType.ToString()
            SensorName = $sensor.Name
            SensorType = $sensor.SensorType.ToString()
            Value = ConvertTo-NullableDouble $sensor.Value
        }
    }
    foreach ($subHardware in $Hardware.SubHardware)
    {
        Get-HardwareTreeSensors -Hardware $subHardware
    }
}

function Get-CrossVendorHardwareSample
{
    if ($null -eq $script:hardwareComputer)
    {
        return [pscustomobject]@{
            Available = $false
            CpuTemperatureC = $null
            CpuTemperatureLive = $false
            CpuTemperatureSensor = ""
            GpuMappingReady = $false
            GpuName = ""
            GpuHardwareType = ""
            GpuTemperatureC = $null
            GpuCoreTemperatureC = $null
            GpuMaximumTemperatureC = $null
            GpuMaximumTemperatureSensor = ""
            GpuLoadPercent = $null
            GpuCoreClockMHz = $null
            GpuPowerW = $null
            Error = $script:hardwareInitializationError
        }
    }

    try
    {
        $hardware = @($script:hardwareComputer.Hardware)
        $sensors = @(
            foreach ($item in $hardware)
            {
                Get-HardwareTreeSensors -Hardware $item
            }
        )

        $cpuTemperatures = @(
            $sensors | Where-Object {
                $_.HardwareType -eq "Cpu" -and
                $_.SensorType -eq "Temperature" -and
                $null -ne $_.Value -and
                $_.SensorName -notmatch "Distance to TjMax"
            }
        )
        $cpuTemperature = if ($cpuTemperatures.Count -gt 0)
        {
            [double](
                $cpuTemperatures.Value |
                    Measure-Object -Maximum).Maximum
        }
        else
        {
            $null
        }
        $cpuSensor = if ($cpuTemperatures.Count -gt 0)
        {
            ($cpuTemperatures |
                Sort-Object Value -Descending |
                Select-Object -First 1).SensorName
        }
        else
        {
            ""
        }

        $gpuHardware = @(
            $hardware | Where-Object {
                $_.HardwareType.ToString() -match "^Gpu"
            }
        )
        $gpuMatches = if ([string]::IsNullOrWhiteSpace(
                $ExpectedGpuName))
        {
            $gpuHardware
        }
        else
        {
            @($gpuHardware | Where-Object {
                [string]::Equals(
                    $_.Name,
                    $ExpectedGpuName,
                    [StringComparison]::OrdinalIgnoreCase)
            })
        }
        $gpuMappingReady =
            -not [string]::IsNullOrWhiteSpace($ExpectedGpuName) -and
            $gpuMatches.Count -eq 1
        $gpu = if ($gpuMappingReady) { $gpuMatches[0] } else { $null }
        $gpuSensors = if ($null -ne $gpu)
        {
            @($sensors | Where-Object {
                $_.HardwareName -eq $gpu.Name -and
                $_.HardwareType -eq $gpu.HardwareType.ToString()
            })
        }
        else
        {
            @()
        }
        $gpuTemperatures = @($gpuSensors | Where-Object {
            $_.SensorType -eq "Temperature" -and
            $null -ne $_.Value
        })
        $gpuCoreTemperatureSensor = @(
            $gpuTemperatures | Where-Object {
                $_.SensorName -match "^(GPU )?(Core|Temperature)$"
            }
        ) | Select-Object -First 1
        $gpuCoreTemperature = if ($null -ne $gpuCoreTemperatureSensor)
        {
            [double]$gpuCoreTemperatureSensor.Value
        }
        elseif ($gpuTemperatures.Count -gt 0)
        {
            [double](
                $gpuTemperatures.Value |
                    Measure-Object -Minimum).Minimum
        }
        else
        {
            $null
        }
        $gpuMaximumTemperature = if ($gpuTemperatures.Count -gt 0)
        {
            [double](
                $gpuTemperatures.Value |
                    Measure-Object -Maximum).Maximum
        }
        else
        {
            $null
        }
        $gpuMaximumTemperatureSensor = if ($gpuTemperatures.Count -gt 0)
        {
            ($gpuTemperatures |
                Sort-Object Value -Descending |
                Select-Object -First 1).SensorName
        }
        else
        {
            ""
        }
        $gpuLoads = @($gpuSensors | Where-Object {
            $_.SensorType -eq "Load" -and
            $null -ne $_.Value -and
            $_.SensorName -match "GPU Core|D3D 3D|GPU Total"
        })
        $gpuLoad = if ($gpuLoads.Count -gt 0)
        {
            [double]($gpuLoads.Value | Measure-Object -Maximum).Maximum
        }
        else
        {
            $null
        }
        $gpuClocks = @($gpuSensors | Where-Object {
            $_.SensorType -eq "Clock" -and
            $null -ne $_.Value -and
            $_.SensorName -match "GPU Core"
        })
        $gpuClock = if ($gpuClocks.Count -gt 0)
        {
            [double]($gpuClocks.Value | Measure-Object -Maximum).Maximum
        }
        else
        {
            $null
        }
        $gpuPowers = @($gpuSensors | Where-Object {
            $_.SensorType -eq "Power" -and
            $null -ne $_.Value -and
            $_.SensorName -match "GPU (Package|Power|Core)"
        })
        $gpuPower = if ($gpuPowers.Count -gt 0)
        {
            [double]($gpuPowers.Value | Measure-Object -Maximum).Maximum
        }
        else
        {
            $null
        }

        return [pscustomobject]@{
            Available = $true
            CpuTemperatureC = $cpuTemperature
            CpuTemperatureLive = $null -ne $cpuTemperature
            CpuTemperatureSensor = $cpuSensor
            GpuMappingReady = $gpuMappingReady
            GpuName = if ($null -ne $gpu) { $gpu.Name } else { "" }
            GpuHardwareType = if ($null -ne $gpu) {
                $gpu.HardwareType.ToString()
            } else { "" }
            GpuTemperatureC = $gpuMaximumTemperature
            GpuCoreTemperatureC = $gpuCoreTemperature
            GpuMaximumTemperatureC = $gpuMaximumTemperature
            GpuMaximumTemperatureSensor =
                $gpuMaximumTemperatureSensor
            GpuLoadPercent = $gpuLoad
            GpuCoreClockMHz = $gpuClock
            GpuPowerW = $gpuPower
            Error = ""
        }
    }
    catch
    {
        return [pscustomobject]@{
            Available = $false
            CpuTemperatureC = $null
            CpuTemperatureLive = $false
            CpuTemperatureSensor = ""
            GpuMappingReady = $false
            GpuName = ""
            GpuHardwareType = ""
            GpuTemperatureC = $null
            GpuCoreTemperatureC = $null
            GpuMaximumTemperatureC = $null
            GpuMaximumTemperatureSensor = ""
            GpuLoadPercent = $null
            GpuCoreClockMHz = $null
            GpuPowerW = $null
            Error = $_.Exception.Message
        }
    }
}

function Get-TrustedNvidiaSmiCommand
{
    $trustedCandidates = @(
        (Join-Path $env:SystemRoot "System32\nvidia-smi.exe"),
        (Join-Path $env:ProgramFiles `
            "NVIDIA Corporation\NVSMI\nvidia-smi.exe")
    )
    foreach ($candidate in $trustedCandidates)
    {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf))
        {
            continue
        }
        $item = Get-Item -LiteralPath $candidate
        $signature = Get-AuthenticodeSignature `
            -LiteralPath $candidate
        $trustedSigner =
            $signature.SignerCertificate.Subject -match
                "NVIDIA Corporation|Microsoft Windows Hardware Compatibility Publisher"
        if ($item.VersionInfo.CompanyName -eq "NVIDIA Corporation" -and
            $signature.Status -eq "Valid" -and
            $trustedSigner)
        {
            return [pscustomobject]@{
                Available = $true
                Path = $item.FullName
                Signer = $signature.SignerCertificate.Subject
                Error = ""
            }
        }
    }

    return [pscustomobject]@{
        Available = $false
        Path = ""
        Signer = ""
        Error = "A trusted nvidia-smi executable is unavailable."
    }
}

function Get-NvidiaThermalSample
{
    param(
        [string]$GpuName,
        [string]$GpuId,
        [Parameter(Mandatory)]
        $Command
    )

    if (-not $Command.Available -or
        [string]::IsNullOrWhiteSpace($GpuName))
    {
        return [pscustomobject]@{
            Available = $false
            MappingReady = $false
            CommandPath = $Command.Path
            CommandSigner = $Command.Signer
            Error = if ($Command.Available) {
                "No NVIDIA GPU was selected."
            } else { $Command.Error }
        }
    }

    $fields = @(
        "index",
        "uuid",
        "name",
        "temperature.gpu",
        "temperature.gpu.tlimit",
        "pstate",
        "utilization.gpu",
        "power.draw",
        "clocks.current.graphics",
        "clocks_event_reasons.sw_thermal_slowdown",
        "clocks_event_reasons.hw_thermal_slowdown",
        "clocks_event_reasons.hw_slowdown",
        "clocks_event_reasons_counters.sw_thermal_slowdown",
        "clocks_event_reasons_counters.hw_thermal_slowdown"
    )
    $arguments = @(
        "--query-gpu=$($fields -join ',')",
        "--format=csv,noheader,nounits"
    )
    if (-not [string]::IsNullOrWhiteSpace($GpuId))
    {
        $arguments += "--id=$GpuId"
    }

    try
    {
        $lines = @(& $Command.Path $arguments 2>$null)
        if ($LASTEXITCODE -ne 0)
        {
            throw "nvidia-smi returned exit code $LASTEXITCODE."
        }
        $matches = @()
        foreach ($line in $lines)
        {
            $values = @($line -split "," |
                ForEach-Object { $_.Trim() })
            if ($values.Count -ne $fields.Count)
            {
                continue
            }
            if ([string]::Equals(
                    $values[2],
                    $GpuName,
                    [StringComparison]::OrdinalIgnoreCase))
            {
                $matches += ,$values
            }
        }
        if ($matches.Count -ne 1)
        {
            return [pscustomobject]@{
                Available = $true
                MappingReady = $false
                CommandPath = $Command.Path
                CommandSigner = $Command.Signer
                Error = "The selected NVIDIA GPU did not map uniquely."
            }
        }

        $values = $matches[0]
        return [pscustomobject]@{
            Available = $true
            MappingReady = $true
            CommandPath = $Command.Path
            CommandSigner = $Command.Signer
            Index = $values[0]
            Uuid = $values[1]
            Name = $values[2]
            GpuTemperatureC = ConvertTo-NullableDouble $values[3]
            GpuThermalHeadroomC = ConvertTo-NullableDouble $values[4]
            PState = $values[5]
            GpuUtilizationPercent = ConvertTo-NullableDouble $values[6]
            PowerDrawW = ConvertTo-NullableDouble $values[7]
            GraphicsClockMHz = ConvertTo-NullableDouble $values[8]
            SoftwareThermalSlowdown = $values[9]
            HardwareThermalSlowdown = $values[10]
            HardwareSlowdown = $values[11]
            SoftwareThermalSlowdownMicroseconds =
                ConvertTo-NullableDouble $values[12]
            HardwareThermalSlowdownMicroseconds =
                ConvertTo-NullableDouble $values[13]
            Error = ""
        }
    }
    catch
    {
        return [pscustomobject]@{
            Available = $false
            MappingReady = $false
            CommandPath = $Command.Path
            CommandSigner = $Command.Signer
            Error = $_.Exception.Message
        }
    }
}

function Get-ProcessHygieneSample
{
    $rendererProcesses = @(Get-Process -Name uvsr `
        -ErrorAction SilentlyContinue)
    $presentMonProcesses = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "^PresentMon(?:-|$)" })
    $competingBuildProcesses = @(Get-Process `
        -Name cmake,ninja,MSBuild,cl,dxc,link,clang,clang-cl,lld-link,fxc,glslc,slangc,devenv,make,nmake `
        -ErrorAction SilentlyContinue)
    $captureOrStressNames = @(
        "qrenderdoc",
        "renderdoccmd",
        "PIXWin",
        "WinPixGpuCapturer",
        "Nsight.Systems",
        "nsys",
        "nsys-ui",
        "nsight-sys",
        "NVIDIA FrameView",
        "FrameView",
        "FrameViewService",
        "CapFrameX",
        "obs64",
        "RTSS",
        "RTSSHooksLoader",
        "OCCT",
        "aida64"
    )
    $residentMonitoringNames = @(
        "MSIAfterburner",
        "HWiNFO64",
        "HWMonitor",
        "GPU-Z",
        "AWCC",
        "AWCC.SCSubAgent",
        "AWCC.UCSubAgent"
    )
    $captureOrStressProcesses = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $captureOrStressNames -contains $_.Name })
    $residentMonitoringProcesses = @(
        Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $residentMonitoringNames -contains $_.Name }
    )

    $rendererReady = if ($Phase -eq "Preflight")
    {
        $rendererProcesses.Count -eq 0
    }
    else
    {
        $ExpectedRendererProcessId -gt 0 -and
            -not [string]::IsNullOrWhiteSpace($ExpectedRendererPath) -and
            $rendererProcesses.Count -eq 1 -and
            $rendererProcesses[0].Id -eq $ExpectedRendererProcessId -and
            (Test-PathIdentity `
                $rendererProcesses[0].Path `
                $ExpectedRendererPath)
    }
    $presentMonReady = if ($Phase -eq "Preflight")
    {
        $presentMonProcesses.Count -eq 0
    }
    elseif ($ExpectedPresentMonProcessId -gt 0)
    {
        -not [string]::IsNullOrWhiteSpace($ExpectedPresentMonPath) -and
            $presentMonProcesses.Count -eq 1 -and
            $presentMonProcesses[0].Id -eq
                $ExpectedPresentMonProcessId -and
            (Test-PathIdentity `
                $presentMonProcesses[0].Path `
                $ExpectedPresentMonPath)
    }
    else
    {
        $presentMonProcesses.Count -eq 0
    }
    $rendererPriorityClass = if ($rendererProcesses.Count -eq 1)
    {
        try { $rendererProcesses[0].PriorityClass.ToString() }
        catch { "unknown" }
    }
    else
    {
        "unknown"
    }
    $rendererPriorityReady =
        $Phase -eq "Preflight" -or
        ($rendererReady -and $rendererPriorityClass -eq "High")

    return [pscustomobject]@{
        Ready = $rendererReady -and
            $rendererPriorityReady -and
            $presentMonReady -and
            $competingBuildProcesses.Count -eq 0 -and
            $captureOrStressProcesses.Count -eq 0
        RendererReady = $rendererReady
        RendererPriorityReady = $rendererPriorityReady
        RendererPriorityClass = $rendererPriorityClass
        PresentMonReady = $presentMonReady
        RendererProcesses = $rendererProcesses
        PresentMonProcesses = $presentMonProcesses
        CompetingBuildProcesses = $competingBuildProcesses
        CaptureOrOverlayProcesses = $captureOrStressProcesses
        ResidentMonitoringProcesses = $residentMonitoringProcesses
    }
}

function Get-PowerSourceSample
{
    try
    {
        Add-Type -AssemblyName System.Windows.Forms
        $lineStatus = [Windows.Forms.SystemInformation]::PowerStatus.
            PowerLineStatus.ToString()
        $batteryCount = @(
            Get-CimInstance -ClassName Win32_Battery `
                -ErrorAction Stop).Count
        $available = $lineStatus -ne "Unknown" -or
            $batteryCount -eq 0
        $online = $lineStatus -eq "Online" -or
            ($lineStatus -eq "Unknown" -and $batteryCount -eq 0)
        return [pscustomobject]@{
            Available = $available
            Online = $online
            LineStatus = if ($batteryCount -eq 0 -and
                $lineStatus -eq "Unknown") {
                "NoBattery"
            } else { $lineStatus }
            BatteryCount = $batteryCount
        }
    }
    catch
    {
        return [pscustomobject]@{
            Available = $false
            Online = $false
            LineStatus = "Unknown"
            BatteryCount = -1
        }
    }
}

function Get-RendererTelemetry
{
    param(
        $Processes,
        [bool]$GateSampleReady
    )

    $samples = @()
    foreach ($process in $Processes)
    {
        $privateWorkingSet = $null
        $dedicatedGpuMemory = $null
        $sharedGpuMemory = $null
        try
        {
            $privateWorkingSetRows = @(
                Get-CimInstance -Namespace root\cimv2 `
                    -ClassName Win32_PerfFormattedData_PerfProc_Process `
                    -ErrorAction Stop |
                    Where-Object { $_.IDProcess -eq $process.Id }
            )
            if ($privateWorkingSetRows.Count -eq 1)
            {
                $privateWorkingSet = [uint64](
                    $privateWorkingSetRows[0].WorkingSetPrivate)
            }
        }
        catch
        {
            $privateWorkingSet = $null
        }
        try
        {
            $gpuMemoryRows = @(
                Get-CimInstance -Namespace root\cimv2 `
                    -ClassName `
                        Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory `
                    -ErrorAction Stop |
                    Where-Object { $_.Name -match "pid_$($process.Id)_" }
            )
            if ($gpuMemoryRows.Count -gt 0)
            {
                $dedicatedGpuMemory = [uint64](
                    $gpuMemoryRows.DedicatedUsage |
                        Measure-Object -Sum).Sum
                $sharedGpuMemory = [uint64](
                    $gpuMemoryRows.SharedUsage |
                        Measure-Object -Sum).Sum
            }
        }
        catch
        {
            $dedicatedGpuMemory = $null
        }

        $processStartTime = try
        {
            $process.StartTime.ToUniversalTime().ToString("o")
        }
        catch
        {
            "unknown"
        }
        $priorityClass = try
        {
            $process.PriorityClass.ToString()
        }
        catch
        {
            "unknown"
        }

        $samples += [pscustomobject]@{
            Timestamp = (Get-Date).ToUniversalTime().ToString("o")
            GateSampleReady = $GateSampleReady
            ProcessId = $process.Id
            Path = $process.Path
            StartTime = $processStartTime
            PriorityClass = $priorityClass
            PrivateMemoryBytes = [uint64]$process.PrivateMemorySize64
            WorkingSetBytes = [uint64]$process.WorkingSet64
            PrivateWorkingSetBytes = $privateWorkingSet
            HandleCount = [uint32]$process.HandleCount
            ThreadCount = [uint32]$process.Threads.Count
            DedicatedGpuMemoryBytes = $dedicatedGpuMemory
            SharedGpuMemoryBytes = $sharedGpuMemory
        }
    }
    return $samples
}

function Select-MaximumNullable
{
    param($First, $Second)

    if ($null -eq $First) { return $Second }
    if ($null -eq $Second) { return $First }
    return [Math]::Max([double]$First, [double]$Second)
}

Initialize-HardwareMonitor
$trustedNvidiaSmiCommand = Get-TrustedNvidiaSmiCommand

$stableSince = $null
$stableSeconds = 0
$previousSoftwareCounter = $null
$previousHardwareCounter = $null
$firstSoftwareCounter = $null
$firstHardwareCounter = $null
$rendererTelemetry =
    [Collections.Generic.List[object]]::new()
$stableRendererTelemetry =
    [Collections.Generic.List[object]]::new()
$monitoringTelemetry =
    [Collections.Generic.List[object]]::new()
$externalCpuUtilizationSamples =
    [Collections.Generic.Queue[double]]::new()
$measurementContaminated = $false
$measurementStart = $null
$measurementDeadline = $null
$measurementReadyWritten = $false
$timedOut = $false
$final = $null

if ($requireCounterStability)
{
    $primeHardwareSample = Get-CrossVendorHardwareSample
    if ($primeHardwareSample.GpuHardwareType -eq "GpuNvidia")
    {
        $primeNvidiaSample = Get-NvidiaThermalSample `
            -GpuName $primeHardwareSample.GpuName `
            -GpuId $NvidiaGpuId `
            -Command $trustedNvidiaSmiCommand
        if ($primeNvidiaSample.Available -and
            $primeNvidiaSample.MappingReady)
        {
            $previousSoftwareCounter =
                $primeNvidiaSample.SoftwareThermalSlowdownMicroseconds
            $previousHardwareCounter =
                $primeNvidiaSample.HardwareThermalSlowdownMicroseconds
            $firstSoftwareCounter = $previousSoftwareCounter
            $firstHardwareCounter = $previousHardwareCounter
        }
    }
}

$windowStart = Get-Date
$readinessDeadline = $windowStart.AddSeconds($TimeoutSeconds)
$overallDeadline = $readinessDeadline

try
{
    while ($true)
    {
        $hardwareSample = Get-CrossVendorHardwareSample
        $nvidiaSample = if ($hardwareSample.GpuHardwareType -eq
            "GpuNvidia")
        {
            Get-NvidiaThermalSample `
                -GpuName $hardwareSample.GpuName `
                -GpuId $NvidiaGpuId `
                -Command $trustedNvidiaSmiCommand
        }
        else
        {
            [pscustomobject]@{
                Available = $false
                MappingReady = $false
                CommandPath = ""
                CommandSigner = ""
                Error = "The selected GPU is not NVIDIA."
            }
        }
        $cpuConstraintSample = Get-CpuPerformanceConstraintSample
        $processSample = Get-ProcessHygieneSample
        $cpuUtilizationSample = Get-CpuUtilizationSample `
            -RendererProcessId $(if ($Phase -eq "Measurement") {
                $ExpectedRendererProcessId
            } else { 0 })
        $systemMemorySample = Get-SystemMemorySample
        $powerSourceSample = Get-PowerSourceSample

        $cpuTemperature = if ($hardwareSample.CpuTemperatureLive)
        {
            $hardwareSample.CpuTemperatureC
        }
        elseif ($CpuTemperatureC -ge 0)
        {
            [double]$CpuTemperatureC
        }
        else
        {
            $null
        }
        $cpuTemperatureSource = if ($hardwareSample.CpuTemperatureLive)
        {
            "LibreHardwareMonitor:$($hardwareSample.CpuTemperatureSensor)"
        }
        elseif ($CpuTemperatureC -ge 0)
        {
            "manual-one-shot"
        }
        else
        {
            "unknown"
        }
        $cpuTemperatureStableEvidence =
            $hardwareSample.CpuTemperatureLive
        $cpuTemperatureWithinLimit =
            $null -ne $cpuTemperature -and
            $cpuTemperature -le $MaximumCpuTemperatureC
        $cpuReady =
            ($cpuTemperatureWithinLimit -and
                $cpuTemperatureStableEvidence) -or
            ($missingCpuTemperatureAllowed -and
                $null -eq $cpuTemperature)
        $cpuConstraintReady =
            Test-CpuPerformanceConstraintSampleReady `
                -Sample $cpuConstraintSample
        $cpuExternalUtilizationAverage = $null
        $cpuExternalUtilizationWindowReady = $false
        $cpuExternalLoadReady = $false
        if ($cpuUtilizationSample.Available -and
            $null -ne $cpuUtilizationSample.ExternalPercent)
        {
            $externalCpuUtilizationSamples.Enqueue(
                [double]$cpuUtilizationSample.ExternalPercent)
            while ($externalCpuUtilizationSamples.Count -gt
                $ExternalCpuUtilizationAverageWindowSamples)
            {
                [void]$externalCpuUtilizationSamples.Dequeue()
            }
            $cpuExternalUtilizationAverage = [double](
                $externalCpuUtilizationSamples |
                    Measure-Object -Average).Average
            $cpuExternalUtilizationWindowReady =
                $externalCpuUtilizationSamples.Count -ge
                    $ExternalCpuUtilizationAverageWindowSamples
            $cpuExternalLoadReady =
                $cpuExternalUtilizationWindowReady -and
                $cpuUtilizationSample.ExternalPercent -le
                    $MaximumExternalCpuUtilizationBurstPercent -and
                $cpuExternalUtilizationAverage -le
                    $MaximumExternalCpuUtilizationPercent
        }
        else
        {
            $externalCpuUtilizationSamples.Clear()
        }

        $gpuTemperature = $hardwareSample.GpuTemperatureC
        $gpuLoad = $hardwareSample.GpuLoadPercent
        $gpuClock = $hardwareSample.GpuCoreClockMHz
        $gpuPower = $hardwareSample.GpuPowerW
        $gpuPState = $null
        $gpuThermalHeadroom = if ($GpuThermalLimitC -ge 0 -and
            $null -ne $gpuTemperature)
        {
            [double]$GpuThermalLimitC - [double]$gpuTemperature
        }
        else
        {
            $null
        }
        $thermalLimiterAvailable = $false
        $thermalLimiterInactive = $null
        $softwareCounter = $null
        $hardwareCounter = $null
        if ($nvidiaSample.Available -and $nvidiaSample.MappingReady)
        {
            $gpuTemperature = Select-MaximumNullable `
                $gpuTemperature $nvidiaSample.GpuTemperatureC
            $gpuLoad = Select-MaximumNullable `
                $gpuLoad $nvidiaSample.GpuUtilizationPercent
            if ($null -ne $nvidiaSample.GraphicsClockMHz)
            {
                $gpuClock = $nvidiaSample.GraphicsClockMHz
            }
            if ($null -ne $nvidiaSample.PowerDrawW)
            {
                $gpuPower = $nvidiaSample.PowerDrawW
            }
            $gpuPState = $nvidiaSample.PState
            if ($null -ne $nvidiaSample.GpuThermalHeadroomC)
            {
                $gpuThermalHeadroom =
                    $nvidiaSample.GpuThermalHeadroomC
            }
            $thermalLimiterAvailable = $true
            $thermalLimiterInactive =
                $nvidiaSample.SoftwareThermalSlowdown -eq "Not Active" -and
                $nvidiaSample.HardwareThermalSlowdown -eq "Not Active" -and
                $nvidiaSample.HardwareSlowdown -eq "Not Active"
            $softwareCounter =
                $nvidiaSample.SoftwareThermalSlowdownMicroseconds
            $hardwareCounter =
                $nvidiaSample.HardwareThermalSlowdownMicroseconds
            if ($null -eq $firstSoftwareCounter)
            {
                $firstSoftwareCounter = $softwareCounter
                $firstHardwareCounter = $hardwareCounter
            }
        }

        $counterStabilityReady = -not $requireCounterStability -or
            ($null -ne $previousSoftwareCounter -and
                $null -ne $previousHardwareCounter -and
                $softwareCounter -eq $previousSoftwareCounter -and
                $hardwareCounter -eq $previousHardwareCounter)
        $gpuIdleReady = $Phase -ne "Preflight" -or
            ($null -ne $gpuLoad -and $gpuLoad -le 5.0)
        $gpuThermalLimiterEvidenceReady =
            $thermalLimiterAvailable -or
            ($missingGpuThermalLimiterAllowed -and
                $GpuThermalLimitC -ge 0 -and
                $null -ne $gpuClock)
        $gpuThermalLimiterConditionReady =
            if ($thermalLimiterAvailable)
            {
                $thermalLimiterInactive -eq $true
            }
            else
            {
                $gpuThermalLimiterEvidenceReady
            }
        $gpuThermalLimiterState =
            if (-not $thermalLimiterAvailable)
            {
                "unknown"
            }
            elseif ($thermalLimiterInactive)
            {
                "inactive"
            }
            else
            {
                "active"
            }
        $gpuTemperatureReady = if ($Phase -eq "Preflight")
        {
            $null -ne $gpuTemperature -and
                $gpuTemperature -le $MaximumGpuTemperatureC -and
                $null -ne $gpuThermalHeadroom -and
                $gpuThermalHeadroom -ge $MinimumGpuThermalHeadroomC
        }
        else
        {
            $null -ne $gpuTemperature -and
                $null -ne $gpuThermalHeadroom -and
                $gpuThermalHeadroom -ge
                    $MinimumMeasurementGpuThermalHeadroomC
        }
        $gpuReady =
            $hardwareSample.GpuMappingReady -and
            $gpuTemperatureReady -and
            $gpuIdleReady -and
            $gpuThermalLimiterEvidenceReady -and
            $gpuThermalLimiterConditionReady -and
            $counterStabilityReady
        $powerSourceReady =
            $powerSourceSample.Available -and
            ($powerSourceSample.Online -or $allowBatteryPower)
        $sampleReady = $cpuReady -and $gpuReady -and
            $cpuConstraintReady -and $cpuExternalLoadReady -and
            $processSample.Ready -and $powerSourceReady
        if ($Phase -eq "Measurement" -and
            $cpuExternalUtilizationWindowReady -and
            $null -eq $measurementStart)
        {
            if ((Get-Date) -gt $readinessDeadline)
            {
                $sampleReady = $false
            }
            elseif ($sampleReady)
            {
                $measurementStart = Get-Date
                $measurementDeadline =
                    $measurementStart.AddSeconds(
                        $MeasurementDurationSeconds)
                $overallDeadline = $measurementDeadline
                Write-MeasurementMarker `
                    -State ready `
                    -StartTime $measurementStart `
                    -Deadline $measurementDeadline
                $measurementReadyWritten =
                    -not [string]::IsNullOrWhiteSpace(
                        $resolvedMeasurementReadyPath)
            }
            else
            {
                if ([string]::IsNullOrWhiteSpace(
                        $resolvedMeasurementReadyPath))
                {
                    $measurementContaminated = $true
                }
                else
                {
                    $sampleReady = $false
                }
            }
        }
        elseif ($Phase -eq "Measurement" -and
            $null -ne $measurementStart -and
            -not $sampleReady)
        {
            $measurementContaminated = $true
        }
        if ($measurementContaminated)
        {
            $sampleReady = $false
        }

        if ($sampleReady)
        {
            if ($null -eq $stableSince)
            {
                $stableStartCandidate = Get-Date
                if ($Phase -ne "Preflight" -or
                    $stableStartCandidate -le $readinessDeadline)
                {
                    $stableSince = $stableStartCandidate
                    $stableRendererTelemetry.Clear()
                    if ($Phase -eq "Preflight")
                    {
                        $overallDeadline =
                            $stableSince.AddSeconds(
                                $RequiredStableSeconds)
                    }
                }
            }
        }
        else
        {
            $stableSince = $null
            $stableRendererTelemetry.Clear()
        }

        $currentRendererTelemetry = @(
            Get-RendererTelemetry `
                -Processes $processSample.RendererProcesses `
                -GateSampleReady $sampleReady)
        foreach ($telemetrySample in $currentRendererTelemetry)
        {
            [void]$rendererTelemetry.Add($telemetrySample)
            if ($sampleReady)
            {
                [void]$stableRendererTelemetry.Add(
                    $telemetrySample)
            }
        }
        $currentRenderer = $currentRendererTelemetry |
            Select-Object -First 1
        [void]$monitoringTelemetry.Add([pscustomobject]@{
            Timestamp = (Get-Date).ToUniversalTime().ToString("o")
            Phase = $Phase
            GateSampleReady = $sampleReady
            MeasurementContaminated = $measurementContaminated
            CpuTemperatureC = $cpuTemperature
            CpuTemperatureSource = $cpuTemperatureSource
            CpuTemperatureLive =
                $hardwareSample.CpuTemperatureLive
            CpuTotalUtilizationPercent =
                $cpuUtilizationSample.TotalPercent
            CpuRendererUtilizationPercent =
                $cpuUtilizationSample.RendererPercent
            CpuAssistantUtilizationPercent =
                $cpuUtilizationSample.AssistantPercent
            CpuPreAssistantExternalUtilizationPercent =
                $cpuUtilizationSample.PreAssistantExternalPercent
            CpuExternalUtilizationPercent =
                $cpuUtilizationSample.ExternalPercent
            CpuExternalUtilizationAveragePercent =
                $cpuExternalUtilizationAverage
            CpuExternalUtilizationWindowReady =
                $cpuExternalUtilizationWindowReady
            CpuOtherAttributedUtilizationPercent =
                $cpuUtilizationSample.OtherAttributedPercent
            CpuOtherUnattributedUtilizationPercent =
                $cpuUtilizationSample.OtherUnattributedPercent
            CpuOtherProcessBreakdown =
                (($cpuUtilizationSample.OtherProcesses |
                    ForEach-Object {
                        '{0}:{1}:{2}' -f $_.Name, $_.Id,
                            (Format-ReportValue $_.Percent)
                    }) -join ';')
            CpuPerformanceConstraintReady = $cpuConstraintReady
            GpuName = $hardwareSample.GpuName
            GpuHardwareType = $hardwareSample.GpuHardwareType
            GpuTemperatureC = $gpuTemperature
            GpuCoreTemperatureC =
                $hardwareSample.GpuCoreTemperatureC
            GpuMaximumTemperatureSensor =
                $hardwareSample.GpuMaximumTemperatureSensor
            GpuThermalHeadroomC = $gpuThermalHeadroom
            GpuUtilizationPercent = $gpuLoad
            GpuGraphicsClockMHz = $gpuClock
            GpuPowerW = $gpuPower
            GpuPState = $gpuPState
            GpuThermalLimiterTelemetryAvailable =
                $thermalLimiterAvailable
            GpuThermalLimiterState = $gpuThermalLimiterState
            ProcessHygieneReady = $processSample.Ready
            RendererPriorityReady =
                $processSample.RendererPriorityReady
            RendererProcessCount =
                $processSample.RendererProcesses.Count
            PresentMonProcessCount =
                $processSample.PresentMonProcesses.Count
            CompetingBuildProcessCount =
                $processSample.CompetingBuildProcesses.Count
            CaptureOrOverlayProcessCount =
                $processSample.CaptureOrOverlayProcesses.Count
            ResidentMonitoringProcessCount =
                $processSample.ResidentMonitoringProcesses.Count
            RendererProcessId = $currentRenderer.ProcessId
            RendererPath = $currentRenderer.Path
            RendererStartTime = $currentRenderer.StartTime
            RendererPriorityClass = $currentRenderer.PriorityClass
            RendererPrivateMemoryBytes =
                $currentRenderer.PrivateMemoryBytes
            RendererWorkingSetBytes =
                $currentRenderer.WorkingSetBytes
            RendererPrivateWorkingSetBytes =
                $currentRenderer.PrivateWorkingSetBytes
            RendererHandleCount = $currentRenderer.HandleCount
            RendererThreadCount = $currentRenderer.ThreadCount
            RendererDedicatedGpuMemoryBytes =
                $currentRenderer.DedicatedGpuMemoryBytes
            RendererSharedGpuMemoryBytes =
                $currentRenderer.SharedGpuMemoryBytes
            SystemCommittedBytes =
                $systemMemorySample.CommittedBytes
            SystemCommitLimitBytes =
                $systemMemorySample.CommitLimitBytes
            SystemAvailableBytes =
                $systemMemorySample.AvailableBytes
            SystemPagedPoolBytes =
                $systemMemorySample.PagedPoolBytes
            SystemNonpagedPoolBytes =
                $systemMemorySample.NonpagedPoolBytes
            PowerSourceReady = $powerSourceReady
            PowerLineStatus = $powerSourceSample.LineStatus
        })

        $stableSeconds = if ($null -eq $stableSince)
        {
            0
        }
        else
        {
            [int][Math]::Floor(
                ((Get-Date) - $stableSince).TotalSeconds)
        }
        $stableRequirementMet =
            $stableSeconds -ge $RequiredStableSeconds
        $measurementDurationMet =
            $Phase -eq "Measurement" -and
            $null -ne $measurementDeadline -and
            (Get-Date) -ge $measurementDeadline
        $thermalGateReady =
            if ($diagnosticSnapshotRequested)
            {
                $false
            }
            elseif ($Phase -eq "Preflight")
            {
                $sampleReady -and $stableRequirementMet
            }
            else
            {
                $sampleReady -and
                    -not $measurementContaminated -and
                    $measurementDurationMet
            }
        $sampleTime = Get-Date
        $readinessTimedOut =
            if ($Phase -eq "Preflight")
            {
                $null -eq $stableSince -and
                    $sampleTime -ge $readinessDeadline
            }
            else
            {
                $null -eq $measurementStart -and
                    $sampleTime -ge $readinessDeadline
            }
        $completionTimedOut =
            if ($Phase -eq "Preflight")
            {
                $null -ne $stableSince -and
                    $sampleTime -ge $overallDeadline
            }
            else
            {
                $null -ne $measurementStart -and
                    $sampleTime -ge $overallDeadline
            }
        $timedOut =
            -not $diagnosticSnapshotRequested -and
            -not $thermalGateReady -and
            -not $measurementContaminated -and
            ($readinessTimedOut -or $completionTimedOut)

        $final = [pscustomobject]@{
            Timestamp = (Get-Date).ToUniversalTime().ToString("o")
            MeasurementEnd = $sampleTime
            ThermalGateReady = $thermalGateReady
            TimedOut = $timedOut
            MeasurementContaminated = $measurementContaminated
            MeasurementDurationMet = $measurementDurationMet
            MeasurementStart = $measurementStart
            StableRequirementMet = $stableRequirementMet
            HardwareSample = $hardwareSample
            NvidiaSample = $nvidiaSample
            CpuConstraintSample = $cpuConstraintSample
            CpuUtilizationSample = $cpuUtilizationSample
            SystemMemorySample = $systemMemorySample
            ProcessSample = $processSample
            PowerSourceSample = $powerSourceSample
            PowerSourceReady = $powerSourceReady
            CpuTemperatureC = $cpuTemperature
            CpuTemperatureSource = $cpuTemperatureSource
            CpuTemperatureStableEvidence =
                $cpuTemperatureStableEvidence
            CpuTemperatureWithinLimit =
                $cpuTemperatureWithinLimit
            CpuReady = $cpuReady
            CpuConstraintReady = $cpuConstraintReady
            CpuExternalLoadReady = $cpuExternalLoadReady
            CpuExternalUtilizationAveragePercent =
                $cpuExternalUtilizationAverage
            CpuExternalUtilizationWindowReady =
                $cpuExternalUtilizationWindowReady
            GpuTemperatureC = $gpuTemperature
            GpuLoadPercent = $gpuLoad
            GpuClockMHz = $gpuClock
            GpuPowerW = $gpuPower
            GpuPState = $gpuPState
            GpuThermalHeadroomC = $gpuThermalHeadroom
            GpuReady = $gpuReady
            ThermalLimiterAvailable = $thermalLimiterAvailable
            ThermalLimiterInactive = $thermalLimiterInactive
            ThermalLimiterState = $gpuThermalLimiterState
            ThermalLimiterEvidenceReady =
                $gpuThermalLimiterEvidenceReady
            SoftwareCounter = $softwareCounter
            HardwareCounter = $hardwareCounter
        }

        if ($diagnosticSnapshotRequested -or
            $final.ThermalGateReady -or
            $measurementContaminated -or
            $timedOut)
        {
            break
        }

        $previousSoftwareCounter = $softwareCounter
        $previousHardwareCounter = $hardwareCounter
        Start-Sleep -Milliseconds $SampleIntervalMilliseconds
    }
}
finally
{
    if ($null -ne $script:hardwareComputer)
    {
        $script:hardwareComputer.Close()
    }
}

if ($measurementReadyWritten)
{
    # Bind terminal coverage to the last telemetry sample, not to a later
    # timestamp taken after the hardware monitor has already been closed.
    $measurementEnd = $final.MeasurementEnd
    $terminalState = if ($final.ThermalGateReady -and
        $final.MeasurementDurationMet -and
        -not $final.MeasurementContaminated)
    {
        "complete"
    }
    else
    {
        "contaminated"
    }
    Write-MeasurementMarker `
        -State $terminalState `
        -StartTime $measurementStart `
        -Deadline $measurementDeadline `
        -EndTime $measurementEnd
}
elseif ($Phase -eq "Measurement" -and
    -not [string]::IsNullOrWhiteSpace($resolvedMeasurementReadyPath) -and
    $null -ne $final -and
    $final.TimedOut)
{
    # A renderer launched with a marker waits with both shadow producers off.
    # Publish a terminal state when readiness times out so it can abort instead
    # of remaining open indefinitely after the monitor exits.
    Write-MeasurementMarker `
        -State contaminated `
        -StartTime $windowStart `
        -Deadline $readinessDeadline `
        -EndTime $final.MeasurementEnd
}

$acceptedRendererTelemetry = if ($final.ThermalGateReady)
{
    @($stableRendererTelemetry)
}
else
{
    @()
}
$initialRendererTelemetry = $acceptedRendererTelemetry |
    Select-Object -First 1
$finalAcceptedRendererTelemetry = $acceptedRendererTelemetry |
    Select-Object -Last 1
$finalRendererTelemetry = $rendererTelemetry | Select-Object -Last 1
$rendererPrivateMemoryDelta = if ($null -ne $initialRendererTelemetry -and
    $null -ne $finalAcceptedRendererTelemetry)
{
    [int64]$finalAcceptedRendererTelemetry.PrivateMemoryBytes -
        [int64]$initialRendererTelemetry.PrivateMemoryBytes
}
else
{
    $null
}
$rendererWorkingSetDelta = if ($null -ne $initialRendererTelemetry -and
    $null -ne $finalAcceptedRendererTelemetry)
{
    [int64]$finalAcceptedRendererTelemetry.WorkingSetBytes -
        [int64]$initialRendererTelemetry.WorkingSetBytes
}
else
{
    $null
}
$softwareThermalCounterDelta = if ($null -ne $firstSoftwareCounter -and
    $null -ne $final.SoftwareCounter)
{
    [double]$final.SoftwareCounter - [double]$firstSoftwareCounter
}
else
{
    $null
}
$hardwareThermalCounterDelta = if ($null -ne $firstHardwareCounter -and
    $null -ne $final.HardwareCounter)
{
    [double]$final.HardwareCounter - [double]$firstHardwareCounter
}
else
{
    $null
}
$processSample = $final.ProcessSample
$powerSourceSample = $final.PowerSourceSample
$hardwareSample = $final.HardwareSample
$nvidiaSample = $final.NvidiaSample
$cpuConstraintSample = $final.CpuConstraintSample
$cpuUtilizationSample = $final.CpuUtilizationSample
$systemMemorySample = $final.SystemMemorySample

$report = @(
    "timestamp=$($final.Timestamp)",
    "phase=$Phase",
    "summaryPath=$resolvedOutputPath",
    "telemetryCsvPath=$resolvedTelemetryCsvPath",
    "diagnosticSnapshot=$([int]$diagnosticSnapshotRequested)",
    "thermalGateReady=$([int][bool]$final.ThermalGateReady)",
    "thermalGateScope=sensor-process-evidence-only",
    "position1TflopsGate=external-benchmark-procedure",
    "timedOut=$([int][bool]$final.TimedOut)",
    "timeoutSeconds=$TimeoutSeconds",
    "readinessDeadline=$($readinessDeadline.ToUniversalTime().ToString('o'))",
    "completionDeadline=$($overallDeadline.ToUniversalTime().ToString('o'))",
    "measurementContaminated=$([int][bool]$final.MeasurementContaminated)",
    "measurementDurationSeconds=$MeasurementDurationSeconds",
    "measurementDurationMet=$([int][bool]$final.MeasurementDurationMet)",
    "measurementStart=$(Format-ReportValue $final.MeasurementStart)",
    "measurementReadyPath=$(Format-ReportValue $resolvedMeasurementReadyPath)",
    "measurementReadyWritten=$([int][bool]$measurementReadyWritten)",
    "cpuReady=$([int][bool]$final.CpuReady)",
    "cpuTemperatureWithinLimit=$([int][bool]$final.CpuTemperatureWithinLimit)",
    "gpuReady=$([int][bool]$final.GpuReady)",
    "cpuConstraintReady=$([int][bool]$final.CpuConstraintReady)",
    "cpuExternalLoadReady=$([int][bool]$final.CpuExternalLoadReady)",
    "processHygieneReady=$([int][bool]$processSample.Ready)",
    "powerSourceReady=$([int][bool]$final.PowerSourceReady)",
    "powerLineStatus=$($powerSourceSample.LineStatus)",
    "batteryCount=$($powerSourceSample.BatteryCount)",
    "allowBatteryPower=$([int][bool]$allowBatteryPower)",
    "stableRequirementMet=$([int][bool]$final.StableRequirementMet)",
    "requiredStableSeconds=$RequiredStableSeconds",
    "observedStableSeconds=$stableSeconds",
    "expectedGpuName=$ExpectedGpuName",
    "selectedGpuName=$($hardwareSample.GpuName)",
    "selectedGpuHardwareType=$($hardwareSample.GpuHardwareType)",
    "gpuMappingReady=$([int][bool]$hardwareSample.GpuMappingReady)",
    "gpuTemperatureC=$(Format-ReportValue $final.GpuTemperatureC)",
    "gpuCoreTemperatureC=$(Format-ReportValue $hardwareSample.GpuCoreTemperatureC)",
    "gpuMaximumSensorTemperatureC=$(Format-ReportValue $hardwareSample.GpuMaximumTemperatureC)",
    "gpuMaximumTemperatureSensor=$($hardwareSample.GpuMaximumTemperatureSensor)",
    "gpuThermalHeadroomC=$(Format-ReportValue $final.GpuThermalHeadroomC)",
    "gpuUtilizationPercent=$(Format-ReportValue $final.GpuLoadPercent)",
    "gpuGraphicsClockMHz=$(Format-ReportValue $final.GpuClockMHz)",
    "gpuPowerDrawW=$(Format-ReportValue $final.GpuPowerW)",
    "gpuPState=$(Format-ReportValue $final.GpuPState)",
    "gpuThermalLimiterTelemetryAvailable=$([int][bool]$final.ThermalLimiterAvailable)",
    "gpuThermalLimiterState=$($final.ThermalLimiterState)",
    "gpuThermalLimiterEvidenceReady=$([int][bool]$final.ThermalLimiterEvidenceReady)",
    "allowMissingGpuThermalLimiter=$([int]$missingGpuThermalLimiterAllowed)",
    "softwareThermalSlowdown=$(Format-ReportValue $nvidiaSample.SoftwareThermalSlowdown)",
    "hardwareThermalSlowdown=$(Format-ReportValue $nvidiaSample.HardwareThermalSlowdown)",
    "hardwareSlowdown=$(Format-ReportValue $nvidiaSample.HardwareSlowdown)",
    "softwareThermalSlowdownMicroseconds=$(Format-ReportValue $final.SoftwareCounter)",
    "hardwareThermalSlowdownMicroseconds=$(Format-ReportValue $final.HardwareCounter)",
    "softwareThermalSlowdownDeltaMicroseconds=$(Format-ReportValue $softwareThermalCounterDelta)",
    "hardwareThermalSlowdownDeltaMicroseconds=$(Format-ReportValue $hardwareThermalCounterDelta)",
    "cpuTemperatureC=$(Format-ReportValue $final.CpuTemperatureC)",
    "cpuTemperatureSource=$($final.CpuTemperatureSource)",
    "cpuTemperatureStableEvidence=$([int][bool]$final.CpuTemperatureStableEvidence)",
    "allowMissingCpuTemperature=$([int]$missingCpuTemperatureAllowed)",
    "cpuConstraintTelemetryAvailable=$([int][bool]$cpuConstraintSample.Available)",
    "cpuLogicalProcessorCount=$($cpuConstraintSample.LogicalProcessorCount)",
    "cpuMaximumPerformanceLimitFlags=$($cpuConstraintSample.MaximumPerformanceLimitFlags)",
    "cpuMinimumPercentPerformanceLimit=$($cpuConstraintSample.MinimumPercentPerformanceLimit)",
    "cpuMinimumActualFrequencyMHz=$($cpuConstraintSample.MinimumActualFrequencyMHz)",
    "cpuMaximumActualFrequencyMHz=$($cpuConstraintSample.MaximumActualFrequencyMHz)",
    "cpuUtilizationTelemetryAvailable=$([int][bool]$cpuUtilizationSample.Available)",
    "cpuTotalUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.TotalPercent)",
    "cpuRendererUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.RendererPercent)",
    "cpuAssistantUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.AssistantPercent)",
    "cpuAssistantProcesses=$(($cpuUtilizationSample.AssistantProcesses |
        ForEach-Object { '{0}:{1}:{2}' -f $_.Name, $_.Id,
            (Format-ReportValue $_.Percent) }) -join ';')",
    "cpuPreAssistantExternalUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.PreAssistantExternalPercent)",
    "cpuExternalUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.ExternalPercent)",
    "cpuExternalUtilizationAveragePercent=$(Format-ReportValue $final.CpuExternalUtilizationAveragePercent)",
    "cpuExternalUtilizationWindowReady=$([int][bool]$final.CpuExternalUtilizationWindowReady)",
    "cpuExternalGateMeaning=total-minus-renderer-minus-live-chatgpt-codex-process-trees",
    "cpuOtherAttributedUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.OtherAttributedPercent)",
    "cpuOtherUnattributedUtilizationPercent=$(Format-ReportValue $cpuUtilizationSample.OtherUnattributedPercent)",
    "cpuOtherProcessBreakdown=$(($cpuUtilizationSample.OtherProcesses |
        ForEach-Object { '{0}:{1}:{2}' -f $_.Name, $_.Id,
            (Format-ReportValue $_.Percent) }) -join ';')",
    "cpuUtilizationTelemetryError=$($cpuUtilizationSample.Error)",
    "rendererProcessCount=$($processSample.RendererProcesses.Count)",
    "rendererProcesses=$(($processSample.RendererProcesses |
        ForEach-Object { '{0}:{1}' -f $_.Name, $_.Id }) -join ';')",
    "rendererIdentityReady=$([int][bool]$processSample.RendererReady)",
    "rendererPriorityReady=$([int][bool]$processSample.RendererPriorityReady)",
    "rendererPriorityClass=$($processSample.RendererPriorityClass)",
    "presentMonProcessCount=$($processSample.PresentMonProcesses.Count)",
    "presentMonProcesses=$(($processSample.PresentMonProcesses |
        ForEach-Object { '{0}:{1}' -f $_.Name, $_.Id }) -join ';')",
    "presentMonIdentityReady=$([int][bool]$processSample.PresentMonReady)",
    "competingBuildProcessCount=$($processSample.CompetingBuildProcesses.Count)",
    "competingBuildProcesses=$(($processSample.CompetingBuildProcesses |
        ForEach-Object { '{0}:{1}' -f $_.Name, $_.Id }) -join ';')",
    "captureOrOverlayProcessCount=$($processSample.CaptureOrOverlayProcesses.Count)",
    "captureOrOverlayProcesses=$(($processSample.CaptureOrOverlayProcesses |
        ForEach-Object { '{0}:{1}' -f $_.Name, $_.Id }) -join ';')",
    "residentMonitoringProcessCount=$($processSample.ResidentMonitoringProcesses.Count)",
    "residentMonitoringProcesses=$(($processSample.ResidentMonitoringProcesses |
        ForEach-Object { '{0}:{1}' -f $_.Name, $_.Id }) -join ';')",
    "rendererTelemetrySampleCount=$($rendererTelemetry.Count)",
    "rendererAcceptedWindowTelemetrySampleCount=$($acceptedRendererTelemetry.Count)",
    "rendererProcessId=$(Format-ReportValue $finalRendererTelemetry.ProcessId)",
    "rendererPath=$(Format-ReportValue $finalRendererTelemetry.Path)",
    "rendererStartTime=$(Format-ReportValue $finalRendererTelemetry.StartTime)",
    "rendererTelemetryPriorityClass=$(Format-ReportValue $finalRendererTelemetry.PriorityClass)",
    "rendererPrivateMemoryBytes=$(Format-ReportValue $finalRendererTelemetry.PrivateMemoryBytes)",
    "rendererPrivateMemoryDeltaBytes=$(Format-ReportValue $rendererPrivateMemoryDelta)",
    "rendererWorkingSetBytes=$(Format-ReportValue $finalRendererTelemetry.WorkingSetBytes)",
    "rendererWorkingSetDeltaBytes=$(Format-ReportValue $rendererWorkingSetDelta)",
    "rendererPrivateWorkingSetBytes=$(Format-ReportValue $finalRendererTelemetry.PrivateWorkingSetBytes)",
    "rendererHandleCount=$(Format-ReportValue $finalRendererTelemetry.HandleCount)",
    "rendererThreadCount=$(Format-ReportValue $finalRendererTelemetry.ThreadCount)",
    "rendererDedicatedGpuMemoryBytes=$(Format-ReportValue $finalRendererTelemetry.DedicatedGpuMemoryBytes)",
    "rendererSharedGpuMemoryBytes=$(Format-ReportValue $finalRendererTelemetry.SharedGpuMemoryBytes)",
    "systemMemoryTelemetryAvailable=$([int][bool]$systemMemorySample.Available)",
    "systemCommittedBytes=$(Format-ReportValue $systemMemorySample.CommittedBytes)",
    "systemCommitLimitBytes=$(Format-ReportValue $systemMemorySample.CommitLimitBytes)",
    "systemAvailableBytes=$(Format-ReportValue $systemMemorySample.AvailableBytes)",
    "systemPagedPoolBytes=$(Format-ReportValue $systemMemorySample.PagedPoolBytes)",
    "systemNonpagedPoolBytes=$(Format-ReportValue $systemMemorySample.NonpagedPoolBytes)",
    "systemMemoryTelemetryError=$($systemMemorySample.Error)",
    "maximumCpuTemperatureC=$MaximumCpuTemperatureC",
    "maximumExternalCpuUtilizationPercent=$MaximumExternalCpuUtilizationPercent",
    "maximumExternalCpuUtilizationBurstPercent=$MaximumExternalCpuUtilizationBurstPercent",
    "externalCpuUtilizationAverageWindowSamples=$ExternalCpuUtilizationAverageWindowSamples",
    "maximumGpuTemperatureC=$MaximumGpuTemperatureC",
    "minimumGpuThermalHeadroomC=$MinimumGpuThermalHeadroomC",
    "minimumMeasurementGpuThermalHeadroomC=$MinimumMeasurementGpuThermalHeadroomC",
    "manualGpuThermalLimitC=$(if ($GpuThermalLimitC -ge 0) { $GpuThermalLimitC } else { 'unknown' })",
    "manualGpuThermalLimitMeaning=lowest-applicable-limit-for-all-gated-sensors",
    "requireStableThermalCounters=$([int][bool]$requireCounterStability)",
    "libreHardwareMonitorAvailable=$([int][bool]$hardwareSample.Available)",
    "libreHardwareMonitorError=$($hardwareSample.Error)",
    "nvidiaTelemetryAvailable=$([int][bool]$nvidiaSample.Available)",
    "nvidiaMappingReady=$([int][bool]$nvidiaSample.MappingReady)",
    "nvidiaSmiPath=$($nvidiaSample.CommandPath)",
    "nvidiaSmiSigner=$($nvidiaSample.CommandSigner)",
    "nvidiaTelemetryError=$($nvidiaSample.Error)"
) -join [Environment]::NewLine

$outputDirectory = Split-Path -Parent $resolvedOutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory))
{
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$telemetryDirectory = Split-Path -Parent $resolvedTelemetryCsvPath
if (-not [string]::IsNullOrWhiteSpace($telemetryDirectory))
{
    New-Item -ItemType Directory -Force `
        -Path $telemetryDirectory | Out-Null
}
Set-Content -LiteralPath $resolvedOutputPath `
    -Value $report -Encoding utf8
$monitoringTelemetry | Export-Csv `
    -LiteralPath $resolvedTelemetryCsvPath `
    -NoTypeInformation -Encoding utf8
$report

if (-not $final.ThermalGateReady)
{
    exit 2
}
