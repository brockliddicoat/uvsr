[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $Launcher,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $LaunchHelper
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$launcherPath = (Resolve-Path -LiteralPath $Launcher).Path
$launchHelperPath = (Resolve-Path -LiteralPath $LaunchHelper).Path
$testRootName = 'uvsr launch menu & (^) ! % ' + [guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) $testRootName
$stubPath = Join-Path $testRoot 'process-stub.exe'
$activeStates = New-Object 'System.Collections.Generic.List[object]'

function Assert-True {
    param(
        [bool] $Condition,
        [string] $Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Read-SharedText {
    param([string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        try {
            return $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-OccurrenceCount {
    param(
        [string] $Text,
        [string] $Needle
    )

    return [regex]::Matches($Text, [regex]::Escape($Needle)).Count
}

function Wait-ForCondition {
    param(
        [scriptblock] $Condition,
        [System.Diagnostics.Process] $Process,
        [string] $FailureMessage,
        [int] $TimeoutMilliseconds = 7000
    )

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
        if (& $Condition) {
            return
        }
        if ($Process.HasExited) {
            throw "$FailureMessage The launcher exited early with code $($Process.ExitCode)."
        }
        Start-Sleep -Milliseconds 25
    }

    throw "$FailureMessage Timed out after $TimeoutMilliseconds ms."
}

function Wait-ForOutputCount {
    param(
        [object] $State,
        [string] $Needle,
        [int] $MinimumCount
    )

    try {
        Wait-ForCondition -Process $State.Process -FailureMessage (
            "Expected launcher output '$Needle' at least $MinimumCount time(s).") -Condition {
            $output = Read-SharedText -Path $State.OutputPath
            (Get-OccurrenceCount -Text $output -Needle $Needle) -ge $MinimumCount
        }
    }
    catch {
        $output = Read-SharedText -Path $State.OutputPath
        $errors = Read-SharedText -Path $State.ErrorPath
        throw "$($_.Exception.Message) Standard output: '$output' Standard error: '$errors'"
    }
}

function Get-LogRecords {
    param([string] $Path)

    $text = Read-SharedText -Path $Path
    if ([string]::IsNullOrWhiteSpace($text)) {
        return @()
    }

    $records = @()
    foreach ($line in ($text -split '\r?\n')) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $payload = [System.Text.Encoding]::UTF8.GetString(
            [System.Convert]::FromBase64String($line))
        $fields = $payload.Split([char] 0)
        $arguments = @()
        if ($fields.Count -gt 1) {
            $arguments = @($fields[1..($fields.Count - 1)])
        }
        $records += [pscustomobject]@{
            WorkingDirectory = $fields[0]
            Arguments = $arguments
        }
    }

    return @($records)
}

function Wait-ForLogCount {
    param(
        [object] $State,
        [int] $MinimumCount
    )

    Wait-ForCondition -Process $State.Process -FailureMessage (
        "Expected process log '$($State.LogPath)' to contain $MinimumCount record(s).") -Condition {
        @(Get-LogRecords -Path $State.LogPath).Count -ge $MinimumCount
    }
}

function New-ScenarioFixture {
    param([string] $Name)

    $root = Join-Path $testRoot $Name
    $toolsDirectory = Join-Path $root 'tools'
    [void] [System.IO.Directory]::CreateDirectory($toolsDirectory)
    Copy-Item -LiteralPath $launcherPath -Destination (Join-Path $root 'LaunchUVSR.cmd')
    Copy-Item -LiteralPath $launchHelperPath -Destination (
        Join-Path $toolsDirectory 'launch_uvsr.ps1')

    return [pscustomobject]@{
        Root = $root
        Launcher = Join-Path $root 'LaunchUVSR.cmd'
        Executable = Join-Path $root 'build\bin\uvsr.exe'
    }
}

function Install-RendererStub {
    param([object] $Fixture)

    $binDirectory = Split-Path -Parent $Fixture.Executable
    [void] [System.IO.Directory]::CreateDirectory($binDirectory)
    Copy-Item -LiteralPath $stubPath -Destination $Fixture.Executable
}

function Install-ExplorerStub {
    param(
        [object] $Fixture,
        [string] $DirectoryName = 'fake-windows'
    )

    $fakeWindows = Join-Path $Fixture.Root $DirectoryName
    [void] [System.IO.Directory]::CreateDirectory($fakeWindows)
    Copy-Item -LiteralPath $stubPath -Destination (Join-Path $fakeWindows 'explorer.exe')
    return $fakeWindows
}

function Start-Menu {
    param(
        [object] $Fixture,
        [string] $SystemRootOverride
    )

    $outputPath = Join-Path $Fixture.Root 'launcher-output.txt'
    $errorPath = Join-Path $Fixture.Root 'launcher-error.txt'
    $logPath = Join-Path $Fixture.Root 'process-log.txt'
    $arguments = '/d /q /c ""{0}" -Menu 1>"{1}" 2>"{2}""' -f `
        $Fixture.Launcher, $outputPath, $errorPath

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $env:ComSpec
    $startInfo.Arguments = $arguments
    $startInfo.WorkingDirectory = $Fixture.Root
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.EnvironmentVariables['UVSR_LAUNCH_MENU_TEST_LOG'] = $logPath
    if (-not [string]::IsNullOrEmpty($SystemRootOverride)) {
        $startInfo.EnvironmentVariables['SystemRoot'] = $SystemRootOverride
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start launcher fixture '$($Fixture.Root)'."
    }

    $state = [pscustomobject]@{
        Process = $process
        OutputPath = $outputPath
        ErrorPath = $errorPath
        LogPath = $logPath
        Fixture = $Fixture
    }
    $activeStates.Add($state)
    return $state
}

function Send-MenuKey {
    param(
        [object] $State,
        [char] $Key
    )

    Assert-True -Condition (-not $State.Process.HasExited) -Message (
        'The launcher exited before input could be sent.')
    $State.Process.StandardInput.Write($Key)
    $State.Process.StandardInput.Flush()
}

function Stop-Menu {
    param([object] $State)

    if (-not $State.Process.HasExited) {
        $State.Process.StandardInput.Close()
        if (-not $State.Process.WaitForExit(3000)) {
            $State.Process.Kill()
            [void] $State.Process.WaitForExit(3000)
        }
    }
}

function Assert-MenuAlive {
    param(
        [object] $State,
        [string] $Context
    )

    Assert-True -Condition (-not $State.Process.HasExited) -Message (
        "The launcher closed after $Context.")
}

[void] [System.IO.Directory]::CreateDirectory($testRoot)

$stubSource = @'
using System;
using System.IO;
using System.Text;

public static class UvsrLaunchMenuProcessStub
{
    public static int Main(string[] args)
    {
        string logPath = Environment.GetEnvironmentVariable("UVSR_LAUNCH_MENU_TEST_LOG");
        if (String.IsNullOrEmpty(logPath))
        {
            return 2;
        }

        string[] fields = new string[args.Length + 1];
        fields[0] = Environment.CurrentDirectory;
        Array.Copy(args, 0, fields, 1, args.Length);
        string payload = String.Join("\0", fields);
        string encoded = Convert.ToBase64String(Encoding.UTF8.GetBytes(payload));
        File.AppendAllText(logPath, encoded + Environment.NewLine);
        return 0;
    }
}
'@

try {
    Add-Type -TypeDefinition $stubSource -Language CSharp -OutputAssembly $stubPath `
        -OutputType ConsoleApplication

    $rendererFixture = New-ScenarioFixture -Name 'renderer-success'
    Install-RendererStub -Fixture $rendererFixture
    $rendererState = Start-Menu -Fixture $rendererFixture
    Wait-ForOutputCount -State $rendererState -Needle 'UVSR is ready.' -MinimumCount 1
    Send-MenuKey -State $rendererState -Key 'x'
    Start-Sleep -Milliseconds 200
    Assert-True -Condition (@(Get-LogRecords -Path $rendererState.LogPath).Count -eq 0) `
        -Message 'An invalid key unexpectedly launched UVSR.'
    Assert-MenuAlive -State $rendererState -Context 'an invalid key'

    Send-MenuKey -State $rendererState -Key '1'
    Wait-ForLogCount -State $rendererState -MinimumCount 1
    Wait-ForOutputCount -State $rendererState `
        -Needle 'SUCCESS: UVSR launch request accepted.' -MinimumCount 1
    Wait-ForOutputCount -State $rendererState -Needle 'UVSR is ready.' -MinimumCount 2
    Assert-MenuAlive -State $rendererState -Context 'the first renderer launch'

    Send-MenuKey -State $rendererState -Key '1'
    Wait-ForLogCount -State $rendererState -MinimumCount 2
    Wait-ForOutputCount -State $rendererState `
        -Needle 'SUCCESS: UVSR launch request accepted.' -MinimumCount 2
    Wait-ForOutputCount -State $rendererState -Needle 'UVSR is ready.' -MinimumCount 3
    Assert-MenuAlive -State $rendererState -Context 'the second renderer launch'

    $rendererRecords = @(Get-LogRecords -Path $rendererState.LogPath)
    foreach ($record in $rendererRecords) {
        Assert-True -Condition ($record.WorkingDirectory -ieq $rendererFixture.Root) `
            -Message 'The renderer did not inherit the repository root as its working directory.'
        Assert-True -Condition ($record.Arguments.Count -eq 0) `
            -Message 'The menu unexpectedly passed renderer arguments.'
    }
    Stop-Menu -State $rendererState
    Write-Output 'PASS: renderer launch success and persistent re-prompt'

    $missingRendererFixture = New-ScenarioFixture -Name 'renderer-missing'
    $missingRendererState = Start-Menu -Fixture $missingRendererFixture
    Wait-ForOutputCount -State $missingRendererState -Needle 'UVSR is ready.' -MinimumCount 1
    Send-MenuKey -State $missingRendererState -Key '1'
    Wait-ForOutputCount -State $missingRendererState `
        -Needle 'FAILURE: uvsr.exe was not found at' -MinimumCount 1
    Wait-ForOutputCount -State $missingRendererState -Needle 'UVSR is ready.' -MinimumCount 2
    Assert-MenuAlive -State $missingRendererState -Context 'a missing renderer failure'
    Stop-Menu -State $missingRendererState
    Write-Output 'PASS: missing renderer failure and persistent re-prompt'

    $rejectedRendererFixture = New-ScenarioFixture -Name 'renderer-rejected'
    Install-RendererStub -Fixture $rejectedRendererFixture
    $fixtureHelper = Join-Path $rejectedRendererFixture.Root 'tools\launch_uvsr.ps1'
    $helperSource = [System.IO.File]::ReadAllText($fixtureHelper)
    $startMarker = '$process = Start-Process @startParameters'
    $startMarkerIndex = $helperSource.IndexOf(
        $startMarker,
        [System.StringComparison]::Ordinal)
    Assert-True -Condition ($startMarkerIndex -ge 0 -and
        $startMarkerIndex -eq
        $helperSource.LastIndexOf($startMarker, [System.StringComparison]::Ordinal)) `
        -Message 'The launch helper did not contain one unambiguous Start-Process marker.'
    $removeBeforeLaunch = 'Remove-Item -LiteralPath $executable -Force' +
        [System.Environment]::NewLine + $startMarker
    [System.IO.File]::WriteAllText(
        $fixtureHelper,
        $helperSource.Replace($startMarker, $removeBeforeLaunch))
    $rejectedRendererState = Start-Menu -Fixture $rejectedRendererFixture
    Wait-ForOutputCount -State $rejectedRendererState -Needle 'UVSR is ready.' -MinimumCount 1
    Send-MenuKey -State $rejectedRendererState -Key '1'
    Wait-ForOutputCount -State $rejectedRendererState `
        -Needle 'FAILURE: Windows could not launch uvsr.exe.' -MinimumCount 1
    Wait-ForOutputCount -State $rejectedRendererState -Needle 'UVSR is ready.' -MinimumCount 2
    Assert-MenuAlive -State $rejectedRendererState -Context 'a rejected renderer launch'
    Stop-Menu -State $rejectedRendererState
    Write-Output 'PASS: rejected renderer request and persistent re-prompt'

    $explorerFixture = New-ScenarioFixture -Name 'explorer-success'
    Install-RendererStub -Fixture $explorerFixture
    $fakeWindows = Install-ExplorerStub -Fixture $explorerFixture
    $explorerState = Start-Menu -Fixture $explorerFixture `
        -SystemRootOverride $fakeWindows
    Wait-ForOutputCount -State $explorerState -Needle 'UVSR is ready.' -MinimumCount 1
    Send-MenuKey -State $explorerState -Key '2'
    Wait-ForLogCount -State $explorerState -MinimumCount 1
    Wait-ForOutputCount -State $explorerState `
        -Needle 'SUCCESS: File-location request accepted.' -MinimumCount 1
    Wait-ForOutputCount -State $explorerState -Needle 'UVSR is ready.' -MinimumCount 2
    Assert-MenuAlive -State $explorerState -Context 'the first file-location request'

    Send-MenuKey -State $explorerState -Key '2'
    Wait-ForLogCount -State $explorerState -MinimumCount 2
    Wait-ForOutputCount -State $explorerState `
        -Needle 'SUCCESS: File-location request accepted.' -MinimumCount 2
    Wait-ForOutputCount -State $explorerState -Needle 'UVSR is ready.' -MinimumCount 3
    Assert-MenuAlive -State $explorerState -Context 'the second file-location request'

    $explorerRecords = @(Get-LogRecords -Path $explorerState.LogPath)
    foreach ($record in $explorerRecords) {
        Assert-True -Condition ($record.Arguments.Count -eq 1) `
            -Message 'Explorer did not receive exactly one selection argument.'
        $expectedArgument = '/select,{0}' -f $explorerFixture.Executable
        Assert-True -Condition ($record.Arguments[0] -ceq $expectedArgument) `
            -Message 'Explorer did not receive the exact /select argument and uvsr.exe path.'
    }
    Stop-Menu -State $explorerState
    Write-Output 'PASS: Explorer selection success and persistent re-prompt'

    $missingTargetFixture = New-ScenarioFixture -Name 'explorer-target-missing'
    $missingTargetWindows = Install-ExplorerStub -Fixture $missingTargetFixture
    $missingTargetState = Start-Menu -Fixture $missingTargetFixture `
        -SystemRootOverride $missingTargetWindows
    Wait-ForOutputCount -State $missingTargetState -Needle 'UVSR is ready.' -MinimumCount 1
    Send-MenuKey -State $missingTargetState -Key '2'
    Wait-ForOutputCount -State $missingTargetState `
        -Needle 'FAILURE: uvsr.exe was not found at' -MinimumCount 1
    Wait-ForOutputCount -State $missingTargetState -Needle 'UVSR is ready.' -MinimumCount 2
    Assert-True -Condition (-not (Test-Path -LiteralPath $missingTargetState.LogPath)) `
        -Message 'Explorer ran even though uvsr.exe was missing.'
    Assert-MenuAlive -State $missingTargetState -Context 'a missing Explorer target'
    Stop-Menu -State $missingTargetState
    Write-Output 'PASS: missing Explorer target failure and persistent re-prompt'

    $missingExplorerFixture = New-ScenarioFixture -Name 'explorer-missing'
    Install-RendererStub -Fixture $missingExplorerFixture
    $emptyWindows = Join-Path $missingExplorerFixture.Root 'empty-windows'
    [void] [System.IO.Directory]::CreateDirectory($emptyWindows)
    $missingExplorerState = Start-Menu -Fixture $missingExplorerFixture `
        -SystemRootOverride $emptyWindows
    Wait-ForOutputCount -State $missingExplorerState -Needle 'UVSR is ready.' -MinimumCount 1
    Send-MenuKey -State $missingExplorerState -Key '2'
    Wait-ForOutputCount -State $missingExplorerState `
        -Needle 'FAILURE: Windows could not open the uvsr.exe file location.' `
        -MinimumCount 1
    Wait-ForOutputCount -State $missingExplorerState -Needle 'UVSR is ready.' -MinimumCount 2
    Assert-MenuAlive -State $missingExplorerState -Context 'a rejected Explorer request'
    Stop-Menu -State $missingExplorerState
    Write-Output 'PASS: rejected Explorer request and persistent re-prompt'

    Write-Output 'All UVSR launch menu contract tests passed.'
}
finally {
    foreach ($state in $activeStates) {
        try {
            Stop-Menu -State $state
        }
        catch {
            Write-Warning "Failed to stop a test-owned launcher: $($_.Exception.Message)"
        }
        finally {
            $state.Process.Dispose()
        }
    }

    $resolvedTempRoot = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    $isOwnedTestRoot = $resolvedTestRoot.StartsWith(
        $resolvedTempRoot,
        [System.StringComparison]::OrdinalIgnoreCase) -and
        ([System.IO.Path]::GetFileName($resolvedTestRoot)).StartsWith(
            'uvsr launch menu & (^) ! % ',
            [System.StringComparison]::Ordinal)
    if (-not $isOwnedTestRoot) {
        throw "Refusing to remove unexpected test path '$resolvedTestRoot'."
    }
    if (Test-Path -LiteralPath $resolvedTestRoot) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
