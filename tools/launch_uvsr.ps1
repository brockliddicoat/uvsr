[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateNotNullOrEmpty()]
    [string] $BuildDirectory = 'build',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $RendererArguments
)

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $resolvedBuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
}
else {
    $resolvedBuildDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot $BuildDirectory))
}
$executable = Join-Path $resolvedBuildDirectory 'bin\uvsr.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "UVSR is not built at '$resolvedBuildDirectory'. Run: cmake --build `"$resolvedBuildDirectory`" --config Release --target uvsr"
}

$startParameters = @{
    FilePath = $executable
    WorkingDirectory = $repositoryRoot
    PassThru = $true
}
if ($RendererArguments.Count -gt 0) {
    $startParameters.ArgumentList = $RendererArguments
}
$process = Start-Process @startParameters

Write-Output $process
