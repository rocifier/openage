# Copyright 2026 the openage authors. See copying.md for legal info.
# Use a real System.Diagnostics.Process to obtain an authoritative exit code.
# Windows PowerShell 5.1 Start-Process -PassThru can report a blank ExitCode
# even after its process object has been waited on.
function Invoke-FVDNativeProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string] $Executable,
        [string[]] $Arguments = @(),
        [ValidateRange(1, 600000)][int] $TimeoutMs = 15000,
        [string] $WorkingDirectory = (Get-Location).Path
    )

    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # Windows PowerShell 5.1 targets .NET Framework, which lacks
    # ProcessStartInfo.ArgumentList. Quote paths containing spaces.
    $quoted = @($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + $_.Replace('"', '\"') + '"'
        } else {
            $_
        }
    })
    $info.Arguments = $quoted -join ' '

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) {
            throw "Could not start native process: $Executable"
        }
        # Drain both pipes concurrently so neither can deadlock the child.
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMs)) {
            throw "Native process timed out after $TimeoutMs ms: $Executable"
        }
        $exitCode = [int] $process.ExitCode
        $stdout = $outTask.GetAwaiter().GetResult()
        $stderr = $errTask.GetAwaiter().GetResult()
        return [pscustomobject]@{
            ExitCode = $exitCode
            Output = ($stdout + $stderr)
        }
    }
    finally {
        if ($started -and -not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}
