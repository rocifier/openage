# Copyright 2026 the openage authors. See copying.md for legal info.
# Regression: a successful process must have exit code 0, not a blank value.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "NativeProcess.ps1")

$cmd = Join-Path $env:WINDIR "System32\cmd.exe"
$ok = Invoke-FVDNativeProcess -Executable $cmd -Arguments @("/d", "/c", "echo FVD_NATIVE_EXIT_OK") -TimeoutMs 10000
if ($null -eq $ok.ExitCode -or $ok.ExitCode -ne 0 -or $ok.Output -notmatch "FVD_NATIVE_EXIT_OK") {
    throw "Failed to capture successful native CLI output/exit: $($ok | Out-String)"
}
$bad = Invoke-FVDNativeProcess -Executable $cmd -Arguments @("/d", "/c", "exit /b 7") -TimeoutMs 10000
if ($null -eq $bad.ExitCode -or $bad.ExitCode -ne 7) {
    throw "Failed to capture non-zero native CLI exit: $($bad | Out-String)"
}
Write-Host "Native process exit-code fixtures passed."
