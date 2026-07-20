[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    # ValidatePattern defaults to case-insensitive matching, and `$` accepts a
    # position before a final newline. Explicit options plus absolute anchors
    # enforce the documented lowercase-letters-only token literally.
    [ValidatePattern('\A[a-z]+\z',
        Options = [System.Text.RegularExpressions.RegexOptions]::None)]
    [string] $Experiment,

    [ValidateNotNullOrEmpty()]
    [string] $BuildDirectory = 'build',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $RendererArguments
)

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repositoryRoot (
    Join-Path $BuildDirectory 'bin\uvsr.exe')
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "UVSR is not built in '$BuildDirectory'."
}

# Pass the validated lowercase one-word description through the child
# environment. The renderer combines it with its build-embedded commit and
# local launch time.
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
