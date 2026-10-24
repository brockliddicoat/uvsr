[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string] $Experiment,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $RendererArguments
)

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repositoryRoot 'build\bin\uvsr.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "UVSR is not built. Run: cmake --build build --config Release --target uvsr"
}

# Pass the experiment through the child environment. This avoids losing spaces
# when Start-Process reconstructs a native Windows command line from ArgumentList.
$previousExperiment = $env:UVSR_EXPERIMENT
try {
    $env:UVSR_EXPERIMENT = $Experiment
    $startParameters = @{
        FilePath = $executable
        WorkingDirectory = $repositoryRoot
        PassThru = $true
    }
    if ($RendererArguments.Count -gt 0) {
        $startParameters.ArgumentList = $RendererArguments
    }
    $process = Start-Process @startParameters
}
finally {
    if ($null -eq $previousExperiment) {
        Remove-Item Env:UVSR_EXPERIMENT -ErrorAction SilentlyContinue
    }
    else {
        $env:UVSR_EXPERIMENT = $previousExperiment
    }
}

Write-Output $process
